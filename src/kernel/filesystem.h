
#ifndef NEPTUNE_FILESYSTEM_H
#define NEPTUNE_FILESYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#define FS_RECORD_MAGIC         "NREC"
#define FS_RECORD_MAGIC_SIZE    4u
#define FS_NAME_MAX             83u
#define FS_PATH_MAX             255u

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

/* Exactly 512 bytes, stored in little-endian format. */
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
 * Exactly 128 bytes per record.
 * type: 1 = file, 2 = directory.
 * flags: bit 0 = record is allocated.
 */
typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    uint32_t id;
    uint8_t type;
    uint8_t flags;
    uint16_t name_length;
    uint32_t parent_id;
    uint64_t size;
    uint64_t first_block;
    uint64_t block_count;
    uint8_t name[84];
    uint32_t checksum;
} fs_record_t;

typedef enum {
    FS_TYPE_FILE = 1,
    FS_TYPE_DIRECTORY = 2
} fs_entry_type_t;

typedef struct {
    uint32_t id;
    fs_entry_type_t type;
    uint64_t size;
    char name[FS_NAME_MAX + 1u];
} fs_dir_entry_t;

typedef struct {
    fs_block_device_t *device;
    fs_volume_header_t header;
    bool mounted;
    bool read_only;
} fs_volume_t;

/* Formatting destroys existing volume contents. */
fs_error_t fs_format(fs_block_device_t *device);

fs_error_t fs_mount(
    fs_volume_t *volume,
    fs_block_device_t *device
);

fs_error_t fs_unmount(fs_volume_t *volume);

fs_error_t fs_create_file(
    fs_volume_t *volume,
    uint32_t parent_id,
    const char *name,
    uint32_t *record_id
);

fs_error_t fs_create_directory(
    fs_volume_t *volume,
    uint32_t parent_id,
    const char *name,
    uint32_t *record_id
);

fs_error_t fs_write_file(
    fs_volume_t *volume,
    uint32_t record_id,
    const void *data,
    uint64_t size
);

fs_error_t fs_read_file(
    fs_volume_t *volume,
    uint32_t record_id,
    void *buffer,
    uint64_t buffer_size,
    uint64_t *bytes_read
);

fs_error_t fs_delete(
    fs_volume_t *volume,
    uint32_t record_id
);

fs_error_t fs_list_directory(
    fs_volume_t *volume,
    uint32_t directory_id,
    fs_dir_entry_t *entries,
    size_t capacity,
    size_t *count
);

fs_error_t fs_find_path(
    fs_volume_t *volume,
    const char *path,
    uint32_t *record_id
);

const char *fs_error_string(fs_error_t error);

#endif
