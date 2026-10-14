#ifndef NEPTUNE_FILESYSTEM_H
#define NEPTUNE_FILESYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Neptune OS Native Filesystem
 * Initial on-disk format: NFS 1.0
 *
 * Logical sector size: 512 bytes
 * Allocation block size: 4096 bytes
 */

#define FS_SECTOR_SIZE          512u
#define FS_BLOCK_SIZE           4096u
#define FS_SECTORS_PER_BLOCK    (FS_BLOCK_SIZE / FS_SECTOR_SIZE)

#define FS_FORMAT_VERSION       1u
#define FS_MAX_RECORDS          4096u
#define FS_RECORD_SIZE          128u
#define FS_RECORD_TABLE_SECTORS \
    ((FS_MAX_RECORDS * FS_RECORD_SIZE) / FS_SECTOR_SIZE)

#define FS_VOLUME_MAGIC         "NEPTUNE1"
#define FS_VOLUME_MAGIC_SIZE    8u

typedef enum {
    FS_OK = 0,
    FS_ERR_INVALID_ARGUMENT,
    FS_ERR_DEVICE_IO,
    FS_ERR_INVALID_VOLUME,
    FS_ERR_UNSUPPORTED_VERSION,
    FS_ERR_VOLUME_TOO_SMALL,
    FS_ERR_OUT_OF_RANGE,
    FS_ERR_NOT_MOUNTED,
    FS_ERR_NO_SPACE,
    FS_ERR_NOT_FOUND,
    FS_ERR_ALREADY_EXISTS,
    FS_ERR_NOT_DIRECTORY,
    FS_ERR_IS_DIRECTORY,
    FS_ERR_DIRECTORY_NOT_EMPTY,
    FS_ERR_INVALID_PATH,
    FS_ERR_CORRUPT_METADATA,
    FS_ERR_READ_ONLY
} fs_error_t;

/*
 * Block-device interface.
 *
 * The callbacks must transfer exactly the requested number of sectors
 * and return true only when the entire operation succeeds.
 *
 * context is owned by the caller and must remain valid while mounted.
 */
typedef struct {
    void *context;

    uint64_t sector_count;

    bool (*read)(
        void *context,
        uint64_t start_sector,
        uint32_t sector_count,
        void *buffer
    );

    bool (*write)(
        void *context,
        uint64_t start_sector,
        uint32_t sector_count,
        const void *buffer
    );

    bool (*flush)(void *context);
} fs_block_device_t;

/*
 * On-disk volume header.
 *
 * This structure is exactly one 512-byte sector.
 * Integer fields are stored in little-endian byte order.
 */
typedef struct __attribute__((packed)) {
    uint8_t magic[8];

    uint32_t version;
    uint32_t header_size;
    uint32_t sector_size;
    uint32_t block_size;

    uint64_t volume_sectors;

    uint64_t bitmap_start_sector;
    uint64_t bitmap_sector_count;

    uint64_t records_start_sector;
    uint64_t records_sector_count;

    uint64_t data_start_sector;
    uint64_t data_block_count;

    uint32_t max_records;
    uint32_t root_record_id;
    uint32_t flags;

    uint8_t reserved[416];

    uint32_t checksum;
} fs_volume_header_t;

/*
 * Runtime-mounted volume state.
 *
 * Keep this structure initialized by fs_mount(), not by manually
 * constructing its internal fields.
 */
typedef struct {
    fs_block_device_t *device;
    fs_volume_header_t header;
    bool mounted;
    bool read_only;
} fs_volume_t;

/*
 * Format a blank block device with NFS 1.0.
 *
 * WARNING: Formatting destroys the previous contents of the volume.
 */
fs_error_t fs_format(fs_block_device_t *device);

/* Mount and validate an existing NFS volume. */
fs_error_t fs_mount(
    fs_volume_t *volume,
    fs_block_device_t *device
);

/* Flush the device, if supported, and detach the mounted volume. */
fs_error_t fs_unmount(fs_volume_t *volume);

/* Query the last filesystem error as a stable enum value. */
const char *fs_error_string(fs_error_t error);

#endif
