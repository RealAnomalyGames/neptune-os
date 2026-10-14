#include "audio.h"
#include "audio_assets.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_COMMAND        0x04
#define PCI_BAR0           0x10
#define PCI_BAR1           0x14
#define PCI_CLASS_REG      0x08

#define AC97_NAM_RESET     0x00
#define AC97_MASTER_VOL    0x02
#define AC97_PCM_VOL       0x18
#define AC97_EXT_CAPS      0x28
#define AC97_EXT_CTRL      0x2A
#define AC97_FRONT_DAC     0x2C

#define AC97_PO_BDBAR      0x10
#define AC97_PO_CIV        0x14
#define AC97_PO_LVI        0x15
#define AC97_PO_STATUS     0x16
#define AC97_PO_PICB       0x18
#define AC97_PO_CONTROL    0x1B

#define AC97_GLOBAL_CONTROL 0x2C

#define AC97_STATUS_HALTED 0x0001
#define AC97_STATUS_CLEAR  0x001C

#define AC97_CONTROL_RUN   0x01
#define AC97_CONTROL_RESET 0x02

#define AUDIO_RATE         48000u
#define AUDIO_BUFFER_SIZE  32768u
#define AUDIO_BDL_ENTRIES  32u
#define AUDIO_MAX_BYTES    (AUDIO_BUFFER_SIZE * AUDIO_BDL_ENTRIES)
#define AUDIO_MAX_FRAMES   (AUDIO_MAX_BYTES / 4u)

#define AUDIO_WAIT_LIMIT   1000000000u

#define BDL_IOC (1u << 31)
#define BDL_BUP (1u << 30)

#define PCI_ENABLE_IO      (1u << 0)
#define PCI_ENABLE_BUSMASTER (1u << 2)

typedef struct {
    uint32_t address;
    uint32_t control;
} __attribute__((packed)) ac97_bdl_entry_t;

static volatile bool initialized;
static uint16_t mixer_base;
static uint16_t busmaster_base;

static uint64_t kernel_phys_base;
static uint64_t kernel_virt_base;

static ac97_bdl_entry_t bdl[AUDIO_BDL_ENTRIES]
    __attribute__((aligned(4096)));

static uint8_t dma_audio[AUDIO_MAX_BYTES]
    __attribute__((aligned(4096)));

static uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t pci_read32(uint8_t bus, uint8_t device,
                           uint8_t function, uint8_t offset)
{
    uint32_t address =
        0x80000000u |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFCu);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write16(uint8_t bus, uint8_t device,
                        uint8_t function, uint8_t offset,
                        uint16_t value)
{
    uint32_t address =
        0x80000000u |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFCu);

    outl(PCI_CONFIG_ADDRESS, address);

    uint16_t old = inw((uint16_t)(PCI_CONFIG_DATA + (offset & 2u)));
    uint16_t merged;

    if ((offset & 2u) == 0) {
        merged = value;
    } else {
        merged = old;
    }

    outw((uint16_t)(PCI_CONFIG_DATA + (offset & 2u)), merged);
}

static bool find_ac97(void)
{
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint32_t dev = 0; dev < 32; ++dev) {
            uint32_t id = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0);

            if ((id & 0xFFFFu) == 0xFFFFu)
                continue;

            uint32_t header = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0x0C);
            uint8_t header_type = (uint8_t)((header >> 16) & 0x7Fu);
            uint32_t functions = (header_type & 0x80u) ? 8u : 1u;

            for (uint32_t fn = 0; fn < functions; ++fn) {
                uint32_t func_id =
                    pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0);

                if ((func_id & 0xFFFFu) == 0xFFFFu)
                    continue;

                uint32_t class_reg =
                    pci_read32((uint8_t)bus, (uint8_t)dev,
                               (uint8_t)fn, PCI_CLASS_REG);

                uint8_t subclass = (uint8_t)(class_reg >> 16);
                uint8_t class_code = (uint8_t)(class_reg >> 24);

                if (class_code != 0x04 || subclass != 0x01)
                    continue;

                uint32_t bar0 =
                    pci_read32((uint8_t)bus, (uint8_t)dev,
                               (uint8_t)fn, PCI_BAR0);
                uint32_t bar1 =
                    pci_read32((uint8_t)bus, (uint8_t)dev,
                               (uint8_t)fn, PCI_BAR1);

                if (!(bar0 & 1u) || !(bar1 & 1u))
                    continue;

                uint32_t nam = bar0 & ~3u;
                uint32_t nabm = bar1 & ~3u;

                if (nam == 0 || nabm == 0 ||
                    nam > 0xFFF0u || nabm > 0xFFF0u)
                    continue;

                mixer_base = (uint16_t)nam;
                busmaster_base = (uint16_t)nabm;

                uint32_t command =
                    pci_read32((uint8_t)bus, (uint8_t)dev,
                               (uint8_t)fn, PCI_COMMAND);

                uint16_t enabled = (uint16_t)command;
                enabled |= PCI_ENABLE_IO | PCI_ENABLE_BUSMASTER;

                pci_write16((uint8_t)bus, (uint8_t)dev,
                            (uint8_t)fn, PCI_COMMAND, enabled);
                return true;
            }
        }
    }

    return false;
}

static bool virtual_to_physical(const void *pointer, uint32_t *result)
{
    uint64_t virtual_address = (uint64_t)(uintptr_t)pointer;

    if (virtual_address < kernel_virt_base)
        return false;

    uint64_t offset = virtual_address - kernel_virt_base;
    uint64_t physical = kernel_phys_base + offset;

    if (physical > 0xFFFFFFFFu)
        return false;

    *result = (uint32_t)physical;
    return true;
}

static void mixer_write(uint8_t reg, uint16_t value)
{
    outw((uint16_t)(mixer_base + reg), value);
}

static uint16_t mixer_read(uint8_t reg)
{
    return inw((uint16_t)(mixer_base + reg));
}

static void stop_output(void)
{
    outb((uint16_t)(busmaster_base + AC97_PO_CONTROL), 0);
    outw((uint16_t)(busmaster_base + AC97_PO_STATUS),
         AC97_STATUS_CLEAR);
}

bool audio_init(uint64_t physical_base, uint64_t virtual_base)
{
    initialized = false;
    kernel_phys_base = physical_base;
    kernel_virt_base = virtual_base;

    uint32_t ignored;

    if (!virtual_to_physical(bdl, &ignored) ||
        !virtual_to_physical(dma_audio, &ignored))
        return false;

    if (!find_ac97())
        return false;

    /* Resume the controller without enabling audio interrupts. */
    outl((uint16_t)(busmaster_base + AC97_GLOBAL_CONTROL), 0x02u);

    /* Reset the codec mixer registers. */
    mixer_write(AC97_NAM_RESET, 0);

    /* Unmute and set master/PCM volume to maximum. */
    mixer_write(AC97_MASTER_VOL, 0x0000u);
    mixer_write(AC97_PCM_VOL, 0x0000u);

    /*
     * Request 48 kHz. Enable variable-rate audio if the codec
     * advertises it; otherwise the AC'97 default is 48 kHz.
     */
    uint16_t capabilities = mixer_read(AC97_EXT_CAPS);

    if (capabilities & 0x0001u) {
        mixer_write(AC97_EXT_CTRL,
                    (uint16_t)(mixer_read(AC97_EXT_CTRL) | 0x0001u));
        mixer_write(AC97_FRONT_DAC, AUDIO_RATE);
    }

    /* Reset the PCM output bus-master engine. */
    outb((uint16_t)(busmaster_base + AC97_PO_CONTROL),
         AC97_CONTROL_RESET);

    for (volatile uint32_t i = 0; i < 1000000u; ++i) {
        if (!(inb((uint16_t)(busmaster_base + AC97_PO_CONTROL)) &
              AC97_CONTROL_RESET))
            break;
        __asm__ volatile ("pause");
    }

    outw((uint16_t)(busmaster_base + AC97_PO_STATUS),
         AC97_STATUS_CLEAR);

    initialized = true;
    return true;
}

bool audio_available(void)
{
    return initialized;
}

/*
 * Minimal RIFF/WAVE reader:
 * - PCM format only
 * - 16-bit samples
 * - mono or stereo
 * - arbitrary data-chunk position
 *
 * Output is converted to 48 kHz, interleaved stereo PCM.
 */
static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool fourcc(const uint8_t *p, const char *tag)
{
    return p[0] == (uint8_t)tag[0] &&
           p[1] == (uint8_t)tag[1] &&
           p[2] == (uint8_t)tag[2] &&
           p[3] == (uint8_t)tag[3];
}

static int16_t sample_at(const uint8_t *data, uint32_t frame,
                         uint16_t channels, uint16_t channel)
{
    uint32_t source_channel = channels == 1 ? 0 : channel;
    uint32_t offset = (frame * channels + source_channel) * 2u;
    return (int16_t)read_le16(data + offset);
}

static void play_wav(const uint8_t *wav, uint32_t wav_size)
{
    if (!initialized || wav == NULL || wav_size < 44)
        return;

    if (!fourcc(wav, "RIFF") || !fourcc(wav + 8, "WAVE"))
        return;

    uint16_t format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t rate = 0;

    const uint8_t *data = NULL;
    uint32_t data_size = 0;

    uint32_t position = 12;

    while (position + 8u <= wav_size) {
        const uint8_t *chunk = wav + position;
        uint32_t chunk_size = read_le32(chunk + 4);
        uint32_t payload = position + 8u;

        if (payload > wav_size || chunk_size > wav_size - payload)
            return;

        if (fourcc(chunk, "fmt ") && chunk_size >= 16) {
            format = read_le16(wav + payload);
            channels = read_le16(wav + payload + 2);
            rate = read_le32(wav + payload + 4);
            bits = read_le16(wav + payload + 14);
        } else if (fourcc(chunk, "data")) {
            data = wav + payload;
            data_size = chunk_size;
        }

        uint32_t advance = 8u + chunk_size + (chunk_size & 1u);

        if (advance > wav_size - position)
            break;

        position += advance;
    }

    if (format != 1 || bits != 16 ||
        (channels != 1 && channels != 2) ||
        rate < 8000 || rate > 192000 ||
        data == NULL)
        return;

    uint32_t source_frame_bytes = channels * 2u;
    uint32_t source_frames = data_size / source_frame_bytes;

    if (source_frames == 0)
        return;

    uint64_t desired_frames =
        ((uint64_t)source_frames * AUDIO_RATE) / rate;

    if (desired_frames == 0)
        return;

    if (desired_frames > AUDIO_MAX_FRAMES)
        desired_frames = AUDIO_MAX_FRAMES;

    uint32_t output_frames = (uint32_t)desired_frames;
    uint32_t output_bytes = output_frames * 4u;

    for (uint32_t frame = 0; frame < output_frames; ++frame) {
        uint32_t source_frame =
            (uint32_t)(((uint64_t)frame * rate) / AUDIO_RATE);

        if (source_frame >= source_frames)
            source_frame = source_frames - 1;

        int16_t left =
            sample_at(data, source_frame, channels, 0);
        int16_t right =
            sample_at(data, source_frame, channels, 1);

        uint32_t out_offset = frame * 4u;

        dma_audio[out_offset] = (uint8_t)left;
        dma_audio[out_offset + 1] = (uint8_t)((uint16_t)left >> 8);
        dma_audio[out_offset + 2] = (uint8_t)right;
        dma_audio[out_offset + 3] = (uint8_t)((uint16_t)right >> 8);
    }

    uint32_t bdl_physical;

    if (!virtual_to_physical(bdl, &bdl_physical))
        return;

    uint32_t descriptor_count =
        (output_bytes + AUDIO_BUFFER_SIZE - 1u) / AUDIO_BUFFER_SIZE;

    if (descriptor_count == 0 || descriptor_count > AUDIO_BDL_ENTRIES)
        return;

    for (uint32_t i = 0; i < descriptor_count; ++i) {
        uint32_t offset = i * AUDIO_BUFFER_SIZE;
        uint32_t remaining = output_bytes - offset;
        uint32_t bytes = remaining > AUDIO_BUFFER_SIZE
                       ? AUDIO_BUFFER_SIZE : remaining;

        uint32_t physical;

        if (!virtual_to_physical(&dma_audio[offset], &physical))
            return;

        bdl[i].address = physical;
        bdl[i].control = (bytes / 2u) & 0xFFFFu;

        if (i == descriptor_count - 1u)
            bdl[i].control |= BDL_IOC | BDL_BUP;
    }

    /* Ensure the CPU's DMA data writes are visible before starting. */
    __asm__ volatile ("" ::: "memory");

    stop_output();

    outl((uint16_t)(busmaster_base + AC97_PO_BDBAR), bdl_physical);
    outb((uint16_t)(busmaster_base + AC97_PO_LVI),
         (uint8_t)(descriptor_count - 1u));

    outb((uint16_t)(busmaster_base + AC97_PO_CONTROL),
         AC97_CONTROL_RUN);

    /*
     * Poll until the controller halts or the bounded timeout expires.
     * The timeout is a fallback, not a precise clock.
     */
    bool completed = false;

    for (volatile uint32_t i = 0; i < AUDIO_WAIT_LIMIT; ++i) {
        uint16_t status =
            inw((uint16_t)(busmaster_base + AC97_PO_STATUS));

        if (status & AC97_STATUS_HALTED) {
            completed = true;
            break;
        }

        __asm__ volatile ("pause");
    }

    (void)completed;
    stop_output();
}

void audio_play_startup(void)
{
    play_wav(startup_wav, startup_wav_size);
}

void audio_play_poweroff(void)
{
    play_wav(poweroff_wav, poweroff_wav_size);
}
