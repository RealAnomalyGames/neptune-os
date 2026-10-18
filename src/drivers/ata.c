
#include "ata.h"

#include <stddef.h>
#include <stdint.h>

#define ATA_DATA          0x1F0u
#define ATA_ERROR         0x1F1u
#define ATA_FEATURES      0x1F1u
#define ATA_SECTOR_COUNT  0x1F2u
#define ATA_LBA_LOW       0x1F3u
#define ATA_LBA_MID       0x1F4u
#define ATA_LBA_HIGH      0x1F5u
#define ATA_DRIVE_HEAD    0x1F6u
#define ATA_STATUS        0x1F7u
#define ATA_COMMAND       0x1F7u
#define ATA_CONTROL       0x3F6u

#define ATA_CMD_IDENTIFY       0xECu
#define ATA_CMD_READ_SECTORS   0x20u
#define ATA_CMD_WRITE_SECTORS  0x30u
#define ATA_CMD_FLUSH_CACHE    0xE7u

#define ATA_SR_ERR   0x01u
#define ATA_SR_DRQ   0x08u
#define ATA_SR_DF    0x20u
#define ATA_SR_BSY   0x80u

#define ATA_POLL_LIMIT 1000000u
#define ATA_LBA28_MAX_SECTORS 0x10000000ull

static uint64_t ata_sector_count;
static int ata_ready;

static inline void ata_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1"
                      :
                      : "a"(value), "Nd"(port));
}

static inline uint8_t ata_inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile ("inb %1, %0"
                      : "=a"(value)
                      : "Nd"(port));

    return value;
}

static inline uint16_t ata_inw(uint16_t port)
{
    uint16_t value;

    __asm__ volatile ("inw %1, %0"
                      : "=a"(value)
                      : "Nd"(port));

    return value;
}

static inline void ata_outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1"
                      :
                      : "a"(value), "Nd"(port));
}

static void ata_delay_400ns(void)
{
    (void)ata_inb(ATA_CONTROL);
    (void)ata_inb(ATA_CONTROL);
    (void)ata_inb(ATA_CONTROL);
    (void)ata_inb(ATA_CONTROL);
}

static bool ata_wait_not_busy(uint8_t *status_out)
{
    for (uint32_t i = 0; i < ATA_POLL_LIMIT; ++i) {
        uint8_t status = ata_inb(ATA_STATUS);

        if (status == 0xFFu || status == 0x00u)
            return false;

        if (!(status & ATA_SR_BSY)) {
            if (status_out != NULL)
                *status_out = status;
            return true;
        }
    }

    return false;
}

static bool ata_wait_drq(void)
{
    for (uint32_t i = 0; i < ATA_POLL_LIMIT; ++i) {
        uint8_t status = ata_inb(ATA_STATUS);

        if (status == 0xFFu || status == 0x00u)
            return false;

        if (status & ATA_SR_BSY)
            continue;

        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return false;

        if (status & ATA_SR_DRQ)
            return true;
    }

    return false;
}

static bool ata_wait_command_complete(void)
{
    uint8_t status;

    if (!ata_wait_not_busy(&status))
        return false;

    return !(status & (ATA_SR_ERR | ATA_SR_DF));
}

static void ata_select_lba28(uint64_t lba)
{
    ata_outb(ATA_DRIVE_HEAD,
             (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    ata_delay_400ns();
}

static void ata_read_words(uint16_t *buffer, size_t word_count)
{
    __asm__ volatile ("cld; rep insw"
                      : "+D"(buffer), "+c"(word_count)
                      : "d"(ATA_DATA)
                      : "memory");
}

static void ata_write_words(const uint16_t *buffer, size_t word_count)
{
    __asm__ volatile ("cld; rep outsw"
                      : "+S"(buffer), "+c"(word_count)
                      : "d"(ATA_DATA)
                      : "memory");
}

static bool ata_read_one(uint64_t lba, void *buffer)
{
    if (!ata_ready || buffer == NULL
        || lba >= ata_sector_count
        || lba >= ATA_LBA28_MAX_SECTORS) {
        return false;
    }

    ata_select_lba28(lba);
    ata_outb(ATA_SECTOR_COUNT, 1);
    ata_outb(ATA_LBA_LOW,  (uint8_t)lba);
    ata_outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
    ata_outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    ata_outb(ATA_COMMAND, ATA_CMD_READ_SECTORS);

    if (!ata_wait_drq())
        return false;

    ata_read_words((uint16_t *)buffer, 256);

    return ata_wait_command_complete();
}

static bool ata_write_one(uint64_t lba, const void *buffer)
{
    if (!ata_ready || buffer == NULL
        || lba >= ata_sector_count
        || lba >= ATA_LBA28_MAX_SECTORS) {
        return false;
    }

    ata_select_lba28(lba);
    ata_outb(ATA_SECTOR_COUNT, 1);
    ata_outb(ATA_LBA_LOW,  (uint8_t)lba);
    ata_outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
    ata_outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    ata_outb(ATA_COMMAND, ATA_CMD_WRITE_SECTORS);

    if (!ata_wait_drq())
        return false;

    ata_write_words((const uint16_t *)buffer, 256);

    ata_delay_400ns();
    return ata_wait_command_complete();
}

static bool ata_device_read(void *context, uint64_t start_sector,
                            uint32_t count, void *buffer)
{
    (void)context;

    if (!ata_ready || buffer == NULL || count == 0)
        return false;

    if (start_sector >= ata_sector_count
        || (uint64_t)count > ata_sector_count - start_sector)
        return false;

    uint8_t *destination = (uint8_t *)buffer;

    for (uint32_t i = 0; i < count; ++i) {
        if (!ata_read_one(start_sector + i, destination))
            return false;

        destination += FS_SECTOR_SIZE;
    }

    return true;
}

static bool ata_device_write(void *context, uint64_t start_sector,
                             uint32_t count, const void *buffer)
{
    (void)context;

    if (!ata_ready || buffer == NULL || count == 0)
        return false;

    if (start_sector >= ata_sector_count
        || (uint64_t)count > ata_sector_count - start_sector)
        return false;

    const uint8_t *source = (const uint8_t *)buffer;

    for (uint32_t i = 0; i < count; ++i) {
        if (!ata_write_one(start_sector + i, source))
            return false;

        source += FS_SECTOR_SIZE;
    }

    return true;
}

static bool ata_device_flush(void *context)
{
    (void)context;

    if (!ata_ready)
        return false;

    ata_outb(ATA_COMMAND, ATA_CMD_FLUSH_CACHE);
    return ata_wait_command_complete();
}

bool ata_primary_master_init(fs_block_device_t *device)
{
    if (device == NULL)
        return false;

    ata_ready = 0;
    ata_sector_count = 0;

    device->context = NULL;
    device->sector_count = 0;
    device->read = NULL;
    device->write = NULL;
    device->flush = NULL;

    /* Select primary master and enable legacy status polling. */
    ata_outb(ATA_CONTROL, 0x00u);
    ata_outb(ATA_DRIVE_HEAD, 0xA0u);
    ata_delay_400ns();

    /* IDENTIFY requires the task-file registers to be cleared. */
    ata_outb(ATA_SECTOR_COUNT, 0);
    ata_outb(ATA_LBA_LOW, 0);
    ata_outb(ATA_LBA_MID, 0);
    ata_outb(ATA_LBA_HIGH, 0);
    ata_outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = ata_inb(ATA_STATUS);

    if (status == 0x00u || status == 0xFFu)
        return false;

    if (!ata_wait_not_busy(&status))
        return false;

    /* Nonzero LBA mid/high after IDENTIFY indicates a non-ATA device. */
    if (ata_inb(ATA_LBA_MID) != 0
        || ata_inb(ATA_LBA_HIGH) != 0) {
        return false;
    }

    if (!ata_wait_drq())
        return false;

    uint16_t identify[256];
    ata_read_words(identify, 256);

    if (!(identify[49] & (1u << 9)))
        return false;

    uint64_t sectors =
        ((uint64_t)identify[61] << 16) | identify[60];

    if (sectors == 0)
        return false;

    if (sectors > ATA_LBA28_MAX_SECTORS)
        sectors = ATA_LBA28_MAX_SECTORS;

    ata_sector_count = sectors;
    ata_ready = 1;

    device->context = NULL;
    device->sector_count = sectors;
    device->read = ata_device_read;
    device->write = ata_device_write;
    device->flush = ata_device_flush;

    return true;
}
