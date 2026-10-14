#ifndef NEPTUNE_AUDIO_H
#define NEPTUNE_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Initialize ICH AC'97 audio.
 *
 * Limine supplies the physical and virtual bases of the kernel.
 * These are used to translate DMA buffers into physical addresses.
 */
bool audio_init(uint64_t kernel_physical_base,
                uint64_t kernel_virtual_base);

bool audio_available(void);

void audio_play_startup(void);
void audio_play_poweroff(void);

#endif
