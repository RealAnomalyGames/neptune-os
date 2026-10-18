#ifndef NEPTUNE_ATA_H
#define NEPTUNE_ATA_H

#include <stdbool.h>
#include "filesystem.h"

/*
 * Detect the primary IDE channel's master device and expose it
 * through the existing filesystem block-device interface.
 *
 * This implementation uses polling and LBA28 addressing.
 * It does not initialize or format the disk.
 */
bool ata_primary_master_init(fs_block_device_t *device);

#endif
