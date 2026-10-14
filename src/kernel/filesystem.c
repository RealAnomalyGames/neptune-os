#include "filesystem.h"

#define FS_BITMAP_START_SECTOR  1u
#define FS_BITMAP_BITS_PER_SECTOR (FS_SECTOR_SIZE * 8u)

#define FS_MIN_VOLUME_SECTORS \
    (FS_BITMAP_START_SECTOR + FS_RECORD_TABLE_SECTORS + \
     FS_SECTORS_PER_BLOCK + 2u)

_Static_assert(sizeof(fs_volume_header_t) == FS_SECTOR_SIZE,
               "Filesystem header must be exactly one sector");

static void fs_zero_bytes(void *destination, size_t count)
{
    uint8_t *bytes = (uint8_t *)destination;

    for (size_t i = 0; i < count; ++i) {
        bytes[i] = 0;
    }
}

static void fs_copy_bytes(
    void *destination,
    const void *source,
    size_t count
)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;

    for (size_t i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
}

static bool fs_bytes_equal(
    const uint8_t *a,
    const uint8_t *b,
    size_t count
)
{
    for (size_t i = 0; i < count; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }

    return true;
}


static bool fs_device_valid(const fs_block_device_t *device)
{
    return device != NULL
        && device->sector_count > 0
        && device->read != NULL
        && device->write != NULL;
}

static bool fs_device_read(
    fs_block_device_t *device,
    uint64_t start_sector,
    uint32_t sector_count,
    void *buffer
)
{
    if (!fs_device_valid(device)
        || sector_count == 0
        || buffer == NULL
        || start_sector >= device->sector_count
        || (uint64_t)sector_count >
            device->sector_count - start_sector) {
        return false;
    }

    return device->read(
        device->context,
        start_sector,
        sector_count,
        buffer
    );
}

static bool fs_device_write(
    fs_block_device_t *device,
    uint64_t start_sector,
    uint32_t sector_count,
    const void *buffer
)
{
    if (!fs_device_valid(device)
        || sector_count == 0
        || buffer == NULL
        || start_sector >= device->sector_count
        || (uint64_t)sector_count >
            device->sector_count - start_sector) {
        return false;
    }

    return device->write(
        device->context,
        start_sector,
        sector_count,
        buffer
    );
}

static bool fs_device_flush(fs_block_device_t *device)
{
    if (!fs_device_valid(device)) {
        return false;
    }

    /* A device without a flush callback has no explicit flush operation. */
    return device->flush == NULL
        || device->flush(device->context);
}


/*
 * CRC-32/ISO-HDLC.
 *
 * This detects accidental corruption; it is not a security mechanism.
 */
static uint32_t fs_crc32(const void *data, size_t count)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];

        for (unsigned bit = 0; bit < 8; ++bit) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

static uint32_t fs_header_checksum(
    const fs_volume_header_t *header
)
{
    fs_volume_header_t copy;

    fs_copy_bytes(&copy, header, sizeof(copy));
    copy.checksum = 0;

    return fs_crc32(&copy, sizeof(copy));
}

static bool fs_read_sector(
    fs_block_device_t *device,
    uint64_t sector,
    void *buffer
)
{
    if (sector >= device->sector_count) {
        return false;
    }

    return device->read(
        device->context,
        sector,
        1,
        buffer
    );
}

static bool fs_write_sector(
    fs_block_device_t *device,
    uint64_t sector,
    const void *buffer
)
{
    if (sector >= device->sector_count) {
        return false;
    }

    return device->write(
        device->context,
        sector,
        1,
        buffer
    );
}

static bool fs_header_valid(
    const fs_volume_header_t *header,
    const fs_block_device_t *device
)
{
    if (!fs_bytes_equal(
            header->magic,
            (const uint8_t *)FS_VOLUME_MAGIC,
            FS_VOLUME_MAGIC_SIZE)) {
        return false;
    }

    if (header->version != FS_FORMAT_VERSION) {
        return false;
    }

    if (header->header_size != sizeof(fs_volume_header_t)
        || header->sector_size != FS_SECTOR_SIZE
        || header->block_size != FS_BLOCK_SIZE) {
        return false;
    }

    if (header->volume_sectors != device->sector_count) {
        return false;
    }

    if (header->max_records != FS_MAX_RECORDS
        || header->records_sector_count != FS_RECORD_TABLE_SECTORS) {
        return false;
    }

    if (header->volume_sectors < FS_MIN_VOLUME_SECTORS) {
        return false;
    }

    if (header->bitmap_start_sector != FS_BITMAP_START_SECTOR) {
        return false;
    }

    /*
     * Reject arithmetic overflow and regions that extend beyond
     * the volume or overlap the backup header.
     */
    if (header->bitmap_sector_count == 0
        || header->records_sector_count == 0
        || header->data_block_count == 0) {
        return false;
    }

    if (header->bitmap_start_sector >= header->volume_sectors
        || header->bitmap_sector_count >
           header->volume_sectors - header->bitmap_start_sector) {
        return false;
    }

    if (header->records_start_sector >= header->volume_sectors
        || header->records_sector_count >
           header->volume_sectors - header->records_start_sector) {
        return false;
    }

    if (header->data_start_sector >= header->volume_sectors - 1u) {
        return false;
    }

    if (header->bitmap_start_sector + header->bitmap_sector_count >
        header->records_start_sector) {
        return false;
    }

    if (header->records_start_sector + header->records_sector_count >
        header->data_start_sector) {
        return false;
    }

    if (header->data_block_count >
        (header->volume_sectors - 1u - header->data_start_sector)
            / FS_SECTORS_PER_BLOCK) {
        return false;
    }

    /*
     * The bitmap must be large enough to describe every data block.
     */
    if (header->bitmap_sector_count <
        (header->data_block_count + FS_BITMAP_BITS_PER_SECTOR - 1u)
            / FS_BITMAP_BITS_PER_SECTOR) {
        return false;
    }

    if (header->checksum != fs_header_checksum(header)) {
        return false;
    }

    return true;
}

static void fs_make_header(
    fs_volume_header_t *header,
    uint64_t volume_sectors
)
{
    fs_zero_bytes(header, sizeof(*header));

    fs_copy_bytes(
        header->magic,
        FS_VOLUME_MAGIC,
        FS_VOLUME_MAGIC_SIZE
    );

    header->version = FS_FORMAT_VERSION;
    header->header_size = sizeof(fs_volume_header_t);
    header->sector_size = FS_SECTOR_SIZE;
    header->block_size = FS_BLOCK_SIZE;
    header->volume_sectors = volume_sectors;

    header->bitmap_start_sector = FS_BITMAP_START_SECTOR;

    /*
     * Reserve enough bitmap space for the theoretical data-block
     * count, then align the data region to an allocation-block boundary.
     */
    uint64_t possible_blocks =
        volume_sectors / FS_SECTORS_PER_BLOCK;

    uint64_t bitmap_sectors =
        (possible_blocks + FS_BITMAP_BITS_PER_SECTOR - 1u)
        / FS_BITMAP_BITS_PER_SECTOR;

    header->bitmap_sector_count = bitmap_sectors;

    header->records_start_sector =
        header->bitmap_start_sector + bitmap_sectors;

    header->records_sector_count = FS_RECORD_TABLE_SECTORS;

    uint64_t records_end =
        header->records_start_sector + header->records_sector_count;

    uint64_t alignment_remainder =
        records_end % FS_SECTORS_PER_BLOCK;

    if (alignment_remainder != 0) {
        records_end +=
            FS_SECTORS_PER_BLOCK - alignment_remainder;
    }

    header->data_start_sector = records_end;

    if (volume_sectors > records_end + 1u) {
        header->data_block_count =
            (volume_sectors - records_end - 1u)
            / FS_SECTORS_PER_BLOCK;
    } else {
        header->data_block_count = 0;
    }

    header->max_records = FS_MAX_RECORDS;
    header->root_record_id = 0;
    header->flags = 0;

    header->checksum = fs_header_checksum(header);
}

fs_error_t fs_format(fs_block_device_t *device)
{
    if (!fs_device_valid(device)) {
        return FS_ERR_INVALID_ARGUMENT;
    }

    if (device->sector_count < FS_MIN_VOLUME_SECTORS) {
        return FS_ERR_VOLUME_TOO_SMALL;
    }

    fs_volume_header_t header;
    fs_make_header(&header, device->sector_count);

    if (header.data_block_count == 0) {
        return FS_ERR_VOLUME_TOO_SMALL;
    }

    /*
     * Ensure the bitmap has capacity for the final data-block count.
     */
    uint64_t required_bitmap_sectors =
        (header.data_block_count + FS_BITMAP_BITS_PER_SECTOR - 1u)
        / FS_BITMAP_BITS_PER_SECTOR;

    if (header.bitmap_sector_count < required_bitmap_sectors) {
        return FS_ERR_CORRUPT_METADATA;
    }

    uint8_t sector[FS_SECTOR_SIZE];
    fs_zero_bytes(sector, sizeof(sector));

    /*
     * Clear allocation bitmap.
     */
    for (uint64_t i = 0; i < header.bitmap_sector_count; ++i) {
        if (!fs_write_sector(
                device,
                header.bitmap_start_sector + i,
                sector)) {
            return FS_ERR_DEVICE_IO;
        }
    }

    /*
     * Clear the file-record table.
     */
    for (uint64_t i = 0; i < header.records_sector_count; ++i) {
        if (!fs_write_sector(
                device,
                header.records_start_sector + i,
                sector)) {
            return FS_ERR_DEVICE_IO;
        }
    }

    /*
     * Write backup header first, then primary header.
     * This does not make formatting fully crash-atomic, but it
     * preserves a secondary copy if the primary write fails.
     */
    if (!fs_write_sector(
            device,
            device->sector_count - 1u,
            &header)) {
        return FS_ERR_DEVICE_IO;
    }

    if (!fs_write_sector(device, 0, &header)) {
        return FS_ERR_DEVICE_IO;
    }

    if (device->flush != NULL
        && !device->flush(device->context)) {
        return FS_ERR_DEVICE_IO;
    }

    return FS_OK;
}

fs_error_t fs_mount(
    fs_volume_t *volume,
    fs_block_device_t *device
)
{
    if (volume == NULL || !fs_device_valid(device)) {
        return FS_ERR_INVALID_ARGUMENT;
    }

    fs_zero_bytes(volume, sizeof(*volume));

    if (device->sector_count < FS_MIN_VOLUME_SECTORS) {
        return FS_ERR_VOLUME_TOO_SMALL;
    }

    fs_volume_header_t header;

    if (!fs_read_sector(device, 0, &header)) {
        return FS_ERR_DEVICE_IO;
    }

    if (!fs_header_valid(&header, device)) {
        /*
         * Try the backup header if the primary header is invalid.
         */
        if (!fs_read_sector(
                device,
                device->sector_count - 1u,
                &header)) {
            return FS_ERR_DEVICE_IO;
        }

        if (!fs_header_valid(&header, device)) {
            return FS_ERR_INVALID_VOLUME;
        }
    }

    volume->device = device;
    fs_copy_bytes(&volume->header, &header, sizeof(header));
    volume->mounted = true;
    volume->read_only = false;

    return FS_OK;
}

fs_error_t fs_unmount(fs_volume_t *volume)
{
    if (volume == NULL || !volume->mounted || volume->device == NULL) {
        return FS_ERR_NOT_MOUNTED;
    }

    if (volume->device->flush != NULL
        && !volume->device->flush(volume->device->context)) {
        return FS_ERR_DEVICE_IO;
    }

    fs_zero_bytes(volume, sizeof(*volume));

    return FS_OK;
}

const char *fs_error_string(fs_error_t error)
{
    switch (error) {
        case FS_OK:
            return "Success";
        case FS_ERR_INVALID_ARGUMENT:
            return "Invalid argument";
        case FS_ERR_DEVICE_IO:
            return "Block-device I/O failure";
        case FS_ERR_INVALID_VOLUME:
            return "Invalid or damaged filesystem volume";
        case FS_ERR_UNSUPPORTED_VERSION:
            return "Unsupported filesystem version";
        case FS_ERR_VOLUME_TOO_SMALL:
            return "Volume is too small";
        case FS_ERR_OUT_OF_RANGE:
            return "Block or sector is out of range";
        case FS_ERR_NOT_MOUNTED:
            return "Filesystem is not mounted";
        case FS_ERR_NO_SPACE:
            return "No space left on volume";
        case FS_ERR_NOT_FOUND:
            return "File or directory not found";
        case FS_ERR_ALREADY_EXISTS:
            return "File or directory already exists";
        case FS_ERR_NOT_DIRECTORY:
            return "Path component is not a directory";
        case FS_ERR_IS_DIRECTORY:
            return "Path refers to a directory";
        case FS_ERR_DIRECTORY_NOT_EMPTY:
            return "Directory is not empty";
        case FS_ERR_INVALID_PATH:
            return "Invalid path";
        case FS_ERR_CORRUPT_METADATA:
            return "Filesystem metadata is corrupt";
        case FS_ERR_READ_ONLY:
            return "Filesystem is read-only";
        default:
            return "Unknown filesystem error";
    }
}
