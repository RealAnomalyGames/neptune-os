#include "filesystem.h"

#define FS_BITMAP_START_SECTOR 1u
#define FS_BITMAP_BITS_PER_SECTOR (FS_SECTOR_SIZE * 8u)
#define FS_MIN_VOLUME_SECTORS 1041u

#define FS_RECORD_FLAG_USED 0x01u
#define FS_RECORD_TYPE_FILE 1u
#define FS_RECORD_TYPE_DIRECTORY 2u

_Static_assert(sizeof(fs_volume_header_t) == FS_SECTOR_SIZE,
               "Volume header must be 512 bytes");
_Static_assert(sizeof(fs_record_t) == FS_RECORD_SIZE,
               "Filesystem record must be 128 bytes");

static void fs_zero_bytes(void *dst, size_t size)
{
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < size; ++i) p[i] = 0;
}

static void fs_copy_bytes(void *dst, const void *src, size_t size)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < size; ++i) d[i] = s[i];
}

static bool fs_bytes_equal(const uint8_t *a, const uint8_t *b,
                           size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static uint32_t fs_crc32(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u)
                ? ((crc >> 1) ^ 0xEDB88320u)
                : (crc >> 1);
        }
    }

    return ~crc;
}

static uint64_t fs_div_round_up(uint64_t n, uint64_t d)
{
    return n / d + ((n % d) != 0);
}

static bool fs_device_valid(const fs_block_device_t *device)
{
    return device != NULL && device->sector_count > 0
        && device->read != NULL && device->write != NULL;
}

static bool fs_device_read(fs_block_device_t *device, uint64_t start,
                           uint32_t count, void *buffer)
{
    if (!fs_device_valid(device) || count == 0 || buffer == NULL
        || start >= device->sector_count
        || (uint64_t)count > device->sector_count - start) {
        return false;
    }

    return device->read(device->context, start, count, buffer);
}

static bool fs_device_write(fs_block_device_t *device, uint64_t start,
                            uint32_t count, const void *buffer)
{
    if (!fs_device_valid(device) || count == 0 || buffer == NULL
        || start >= device->sector_count
        || (uint64_t)count > device->sector_count - start) {
        return false;
    }

    return device->write(device->context, start, count, buffer);
}

static bool fs_device_flush(fs_block_device_t *device)
{
    return fs_device_valid(device)
        && (device->flush == NULL || device->flush(device->context));
}

static bool fs_read_sector(fs_block_device_t *d, uint64_t sector,
                           void *buffer)
{
    return fs_device_read(d, sector, 1, buffer);
}

static bool fs_write_sector(fs_block_device_t *d, uint64_t sector,
                            const void *buffer)
{
    return fs_device_write(d, sector, 1, buffer);
}

static uint32_t fs_header_checksum(const fs_volume_header_t *header)
{
    fs_volume_header_t copy;
    fs_copy_bytes(&copy, header, sizeof(copy));
    copy.checksum = 0;
    return fs_crc32(&copy, sizeof(copy));
}

static uint32_t fs_record_checksum(const fs_record_t *record)
{
    fs_record_t copy;
    fs_copy_bytes(&copy, record, sizeof(copy));
    copy.checksum = 0;
    return fs_crc32(&copy, sizeof(copy));
}

static bool fs_record_valid(const fs_record_t *r, uint32_t expected_id)
{
    if (r->id != expected_id
        || !fs_bytes_equal(r->magic, (const uint8_t *)FS_RECORD_MAGIC,
                           FS_RECORD_MAGIC_SIZE)
        || !(r->flags & FS_RECORD_FLAG_USED)
        || r->name_length > FS_NAME_MAX
        || r->name[r->name_length] != 0
        || r->checksum != fs_record_checksum(r)) {
        return false;
    }

    if (r->type != FS_RECORD_TYPE_FILE
        && r->type != FS_RECORD_TYPE_DIRECTORY) {
        return false;
    }

    if (r->type == FS_RECORD_TYPE_DIRECTORY
        && (r->size != 0 || r->block_count != 0)) {
        return false;
    }

    if (r->block_count == 0 && r->size != 0) return false;

    return true;
}

static void fs_record_seal(fs_record_t *record)
{
    record->checksum = 0;
    record->checksum = fs_record_checksum(record);
}

static fs_error_t fs_validate_header(const fs_volume_header_t *h,
                                     const fs_block_device_t *d)
{
    if (!fs_bytes_equal(h->magic, (const uint8_t *)FS_VOLUME_MAGIC,
                        FS_VOLUME_MAGIC_SIZE)) {
        return FS_ERR_INVALID_VOLUME;
    }

    if (h->checksum != fs_header_checksum(h))
        return FS_ERR_CORRUPT_METADATA;

    if (h->version != FS_FORMAT_VERSION)
        return FS_ERR_UNSUPPORTED_VERSION;

    if (h->header_size != sizeof(*h)
        || h->sector_size != FS_SECTOR_SIZE
        || h->block_size != FS_BLOCK_SIZE
        || h->volume_sectors != d->sector_count
        || h->max_records != FS_MAX_RECORDS
        || h->records_sector_count != FS_RECORD_TABLE_SECTORS
        || h->root_record_id != 0) {
        return FS_ERR_CORRUPT_METADATA;
    }

    if (d->sector_count < FS_MIN_VOLUME_SECTORS
        || h->bitmap_start_sector != FS_BITMAP_START_SECTOR
        || h->bitmap_sector_count == 0
        || h->data_block_count == 0) {
        return FS_ERR_INVALID_VOLUME;
    }

    if (h->bitmap_start_sector >= h->volume_sectors
        || h->bitmap_sector_count >
            h->volume_sectors - h->bitmap_start_sector
        || h->records_start_sector >= h->volume_sectors
        || h->records_sector_count >
            h->volume_sectors - h->records_start_sector
        || h->data_start_sector >= h->volume_sectors - 1u) {
        return FS_ERR_CORRUPT_METADATA;
    }

    if (h->bitmap_start_sector + h->bitmap_sector_count
            > h->records_start_sector
        || h->records_start_sector + h->records_sector_count
            > h->data_start_sector
        || h->data_start_sector % FS_SECTORS_PER_BLOCK != 0
        || h->data_block_count >
            (h->volume_sectors - 1u - h->data_start_sector)
                / FS_SECTORS_PER_BLOCK) {
        return FS_ERR_CORRUPT_METADATA;
    }

    if (h->bitmap_sector_count < fs_div_round_up(
            h->data_block_count, FS_BITMAP_BITS_PER_SECTOR)) {
        return FS_ERR_CORRUPT_METADATA;
    }

    return FS_OK;
}

static void fs_make_header(fs_volume_header_t *h, uint64_t sectors)
{
    fs_zero_bytes(h, sizeof(*h));
    fs_copy_bytes(h->magic, FS_VOLUME_MAGIC, FS_VOLUME_MAGIC_SIZE);

    h->version = FS_FORMAT_VERSION;
    h->header_size = sizeof(*h);
    h->sector_size = FS_SECTOR_SIZE;
    h->block_size = FS_BLOCK_SIZE;
    h->volume_sectors = sectors;
    h->bitmap_start_sector = FS_BITMAP_START_SECTOR;

    uint64_t possible_blocks = sectors / FS_SECTORS_PER_BLOCK;
    h->bitmap_sector_count = fs_div_round_up(
        possible_blocks, FS_BITMAP_BITS_PER_SECTOR);

    h->records_start_sector =
        h->bitmap_start_sector + h->bitmap_sector_count;
    h->records_sector_count = FS_RECORD_TABLE_SECTORS;

    uint64_t end = h->records_start_sector + h->records_sector_count;
    uint64_t remainder = end % FS_SECTORS_PER_BLOCK;
    if (remainder) end += FS_SECTORS_PER_BLOCK - remainder;

    h->data_start_sector = end;
    if (sectors > end + 1u) {
        h->data_block_count =
            (sectors - end - 1u) / FS_SECTORS_PER_BLOCK;
    }

    h->max_records = FS_MAX_RECORDS;
    h->root_record_id = 0;
    h->checksum = fs_header_checksum(h);
}

/* Convert a record ID to its sector and byte offset within that sector. */
static bool fs_record_location(const fs_volume_t *v, uint32_t id,
                               uint64_t *sector, size_t *offset)
{
    if (id >= FS_MAX_RECORDS) return false;

    uint32_t records_per_sector = FS_SECTOR_SIZE / FS_RECORD_SIZE;
    *sector = v->header.records_start_sector + id / records_per_sector;
    *offset = (size_t)(id % records_per_sector) * FS_RECORD_SIZE;
    return true;
}

static fs_error_t fs_read_record(fs_volume_t *v, uint32_t id,
                                 fs_record_t *record)
{
    uint64_t sector;
    size_t offset;
    uint8_t buffer[FS_SECTOR_SIZE];

    if (!fs_record_location(v, id, &sector, &offset))
        return FS_ERR_OUT_OF_RANGE;

    if (!fs_read_sector(v->device, sector, buffer))
        return FS_ERR_DEVICE_IO;

    fs_copy_bytes(record, buffer + offset, sizeof(*record));

    if (!(record->flags & FS_RECORD_FLAG_USED))
        return FS_ERR_NOT_FOUND;

    if (!fs_record_valid(record, id))
        return FS_ERR_CORRUPT_METADATA;

    return FS_OK;
}

static fs_error_t fs_write_record(fs_volume_t *v, fs_record_t *record)
{
    uint64_t sector;
    size_t offset;
    uint8_t buffer[FS_SECTOR_SIZE];

    if (!fs_record_location(v, record->id, &sector, &offset))
        return FS_ERR_OUT_OF_RANGE;

    fs_record_seal(record);

    if (!fs_read_sector(v->device, sector, buffer))
        return FS_ERR_DEVICE_IO;

    fs_copy_bytes(buffer + offset, record, sizeof(*record));

    if (!fs_write_sector(v->device, sector, buffer))
        return FS_ERR_DEVICE_IO;

    return FS_OK;
}

static bool fs_volume_ready(fs_volume_t *v)
{
    return v != NULL && v->mounted && v->device != NULL;
}

static bool fs_valid_name(const char *name, size_t *length)
{
    if (name == NULL || name[0] == 0) return false;

    size_t n = 0;
    while (name[n] != 0) {
        if (name[n] == '/' || name[n] < 32 || name[n] == 127)
            return false;
        if (++n > FS_NAME_MAX) return false;
    }

    if ((n == 1 && name[0] == '.') ||
        (n == 2 && name[0] == '.' && name[1] == '.')) {
        return false;
    }

    if (length != NULL) *length = n;
    return true;
}

static fs_error_t fs_find_child(fs_volume_t *v, uint32_t parent,
                                const char *name, uint32_t *id_out)
{
    for (uint32_t id = 0; id < FS_MAX_RECORDS; ++id) {
        uint64_t sector;
        size_t offset;
        uint8_t buffer[FS_SECTOR_SIZE];
        fs_record_t r;

        (void)fs_record_location(v, id, &sector, &offset);

        if (!fs_read_sector(v->device, sector, buffer))
            return FS_ERR_DEVICE_IO;

        fs_copy_bytes(&r, buffer + offset, sizeof(r));

        if (!(r.flags & FS_RECORD_FLAG_USED)) continue;
        if (!fs_record_valid(&r, id)) return FS_ERR_CORRUPT_METADATA;

        if (r.id == 0) continue;
        if (r.parent_id != parent) continue;

        size_t n = 0;
        while (name[n] != 0) ++n;

        if (r.name_length == n
            && fs_bytes_equal(r.name, (const uint8_t *)name, n)) {
            if (id_out != NULL) *id_out = id;
            return FS_OK;
        }
    }

    return FS_ERR_NOT_FOUND;
}

static fs_error_t fs_get_free_record(fs_volume_t *v, uint32_t *id_out)
{
    for (uint32_t id = 1; id < FS_MAX_RECORDS; ++id) {
        uint64_t sector;
        size_t offset;
        uint8_t buffer[FS_SECTOR_SIZE];

        (void)fs_record_location(v, id, &sector, &offset);

        if (!fs_read_sector(v->device, sector, buffer))
            return FS_ERR_DEVICE_IO;

        fs_record_t r;
        fs_copy_bytes(&r, buffer + offset, sizeof(r));

        if (!(r.flags & FS_RECORD_FLAG_USED)) {
            *id_out = id;
            return FS_OK;
        }

        if (!fs_record_valid(&r, id))
            return FS_ERR_CORRUPT_METADATA;
    }

    return FS_ERR_NO_SPACE;
}

static fs_error_t fs_bitmap_get(fs_volume_t *v, uint64_t block,
                                bool *used)
{
    if (block >= v->header.data_block_count)
        return FS_ERR_OUT_OF_RANGE;

    uint64_t sector = v->header.bitmap_start_sector
        + block / FS_BITMAP_BITS_PER_SECTOR;
    uint64_t bit = block % FS_BITMAP_BITS_PER_SECTOR;
    uint8_t buffer[FS_SECTOR_SIZE];

    if (!fs_read_sector(v->device, sector, buffer))
        return FS_ERR_DEVICE_IO;

    *used = (buffer[bit / 8u] & (uint8_t)(1u << (bit % 8u))) != 0;
    return FS_OK;
}

static fs_error_t fs_bitmap_set(fs_volume_t *v, uint64_t block,
                                bool used)
{
    if (block >= v->header.data_block_count)
        return FS_ERR_OUT_OF_RANGE;

    uint64_t sector = v->header.bitmap_start_sector
        + block / FS_BITMAP_BITS_PER_SECTOR;
    uint64_t bit = block % FS_BITMAP_BITS_PER_SECTOR;
    uint8_t buffer[FS_SECTOR_SIZE];

    if (!fs_read_sector(v->device, sector, buffer))
        return FS_ERR_DEVICE_IO;

    uint8_t mask = (uint8_t)(1u << (bit % 8u));
    if (used) buffer[bit / 8u] |= mask;
    else buffer[bit / 8u] &= (uint8_t)~mask;

    if (!fs_write_sector(v->device, sector, buffer))
        return FS_ERR_DEVICE_IO;

    return FS_OK;
}

static fs_error_t fs_allocate_blocks(fs_volume_t *v, uint64_t count,
                                     uint64_t *first_out)
{
    if (count == 0) {
        *first_out = 0;
        return FS_OK;
    }

    if (count > v->header.data_block_count)
        return FS_ERR_NO_SPACE;

    uint64_t run = 0;
    uint64_t start = 0;

    for (uint64_t b = 0; b < v->header.data_block_count; ++b) {
        bool used;
        fs_error_t e = fs_bitmap_get(v, b, &used);
        if (e != FS_OK) return e;

        if (used) {
            run = 0;
        } else {
            if (run == 0) start = b;
            ++run;

            if (run == count) {
                for (uint64_t i = 0; i < count; ++i) {
                    e = fs_bitmap_set(v, start + i, true);
                    if (e != FS_OK) {
                        for (uint64_t j = 0; j < i; ++j)
                            (void)fs_bitmap_set(v, start + j, false);
                        return e;
                    }
                }

                *first_out = start;
                return FS_OK;
            }
        }
    }

    return FS_ERR_NO_SPACE;
}

static fs_error_t fs_release_blocks(fs_volume_t *v, uint64_t first,
                                    uint64_t count)
{
    if (count == 0) return FS_OK;

    if (first >= v->header.data_block_count
        || count > v->header.data_block_count - first) {
        return FS_ERR_CORRUPT_METADATA;
    }

    for (uint64_t i = 0; i < count; ++i) {
        fs_error_t e = fs_bitmap_set(v, first + i, false);
        if (e != FS_OK) return e;
    }

    return FS_OK;
}

static fs_error_t fs_create_entry(fs_volume_t *v, uint32_t parent_id,
                                  const char *name, uint8_t type,
                                  uint32_t *id_out)
{
    if (!fs_volume_ready(v)) return FS_ERR_NOT_MOUNTED;
    if (v->read_only) return FS_ERR_READ_ONLY;

    size_t name_len;
    if (!fs_valid_name(name, &name_len) || id_out == NULL)
        return FS_ERR_INVALID_ARGUMENT;

    fs_record_t parent;
    fs_error_t e = fs_read_record(v, parent_id, &parent);
    if (e != FS_OK) return e;
    if (parent.type != FS_RECORD_TYPE_DIRECTORY)
        return FS_ERR_NOT_DIRECTORY;

    e = fs_find_child(v, parent_id, name, NULL);
    if (e == FS_OK) return FS_ERR_ALREADY_EXISTS;
    if (e != FS_ERR_NOT_FOUND) return e;

    uint32_t id;
    e = fs_get_free_record(v, &id);
    if (e != FS_OK) return e;

    fs_record_t r;
    fs_zero_bytes(&r, sizeof(r));
    fs_copy_bytes(r.magic, FS_RECORD_MAGIC, FS_RECORD_MAGIC_SIZE);
    r.id = id;
    r.type = type;
    r.flags = FS_RECORD_FLAG_USED;
    r.name_length = (uint16_t)name_len;
    r.parent_id = parent_id;
    fs_copy_bytes(r.name, name, name_len);
    r.name[name_len] = 0;
    fs_record_seal(&r);

    e = fs_write_record(v, &r);
    if (e != FS_OK) return e;

    *id_out = id;
    return FS_OK;
}

fs_error_t fs_format(fs_block_device_t *device)
{
    if (!fs_device_valid(device)) return FS_ERR_INVALID_ARGUMENT;
    if (device->sector_count < FS_MIN_VOLUME_SECTORS)
        return FS_ERR_VOLUME_TOO_SMALL;

    fs_volume_header_t h;
    fs_make_header(&h, device->sector_count);
    if (h.data_block_count == 0) return FS_ERR_VOLUME_TOO_SMALL;

    uint8_t sector[FS_SECTOR_SIZE];
    fs_zero_bytes(sector, sizeof(sector));

    for (uint64_t i = 0; i < h.bitmap_sector_count; ++i) {
        if (!fs_write_sector(device, h.bitmap_start_sector + i, sector))
            return FS_ERR_DEVICE_IO;
    }

    for (uint64_t i = 0; i < h.records_sector_count; ++i) {
        if (!fs_write_sector(device, h.records_start_sector + i, sector))
            return FS_ERR_DEVICE_IO;
    }

    /* Initialize record zero as the root directory. */
    fs_record_t root;
    fs_zero_bytes(&root, sizeof(root));
    fs_copy_bytes(root.magic, FS_RECORD_MAGIC, FS_RECORD_MAGIC_SIZE);
    root.id = 0;
    root.type = FS_RECORD_TYPE_DIRECTORY;
    root.flags = FS_RECORD_FLAG_USED;
    root.parent_id = 0;
    root.name[0] = '/';
    root.name_length = 1;
    root.checksum = fs_record_checksum(&root);

    fs_copy_bytes(sector, &root, sizeof(root));
    if (!fs_write_sector(device, h.records_start_sector, sector))
        return FS_ERR_DEVICE_IO;

    /* Backup header first, then primary. */
    if (!fs_write_sector(device, device->sector_count - 1u, &h))
        return FS_ERR_DEVICE_IO;
    if (!fs_device_flush(device)) return FS_ERR_DEVICE_IO;

    if (!fs_write_sector(device, 0, &h))
        return FS_ERR_DEVICE_IO;
    if (!fs_device_flush(device)) return FS_ERR_DEVICE_IO;

    return FS_OK;
}

fs_error_t fs_mount(fs_volume_t *volume, fs_block_device_t *device)
{
    if (volume == NULL || !fs_device_valid(device))
        return FS_ERR_INVALID_ARGUMENT;

    fs_zero_bytes(volume, sizeof(*volume));

    if (device->sector_count < FS_MIN_VOLUME_SECTORS)
        return FS_ERR_VOLUME_TOO_SMALL;

    fs_volume_header_t h;
    if (!fs_read_sector(device, 0, &h)) return FS_ERR_DEVICE_IO;

    fs_error_t primary_error = fs_validate_header(&h, device);

    if (primary_error != FS_OK) {
        if (!fs_read_sector(device, device->sector_count - 1u, &h))
            return FS_ERR_DEVICE_IO;

        fs_error_t backup_error = fs_validate_header(&h, device);
        if (backup_error != FS_OK) {
            if (backup_error == FS_ERR_UNSUPPORTED_VERSION
                || primary_error == FS_ERR_UNSUPPORTED_VERSION)
                return FS_ERR_UNSUPPORTED_VERSION;

            if (backup_error == FS_ERR_CORRUPT_METADATA
                || primary_error == FS_ERR_CORRUPT_METADATA)
                return FS_ERR_CORRUPT_METADATA;

            return FS_ERR_INVALID_VOLUME;
        }
    }

    volume->device = device;
    fs_copy_bytes(&volume->header, &h, sizeof(h));
    volume->mounted = true;
    volume->read_only = false;

    /* Verify the root record before declaring the volume usable. */
    fs_record_t root;
    uint8_t sector[FS_SECTOR_SIZE];
    if (!fs_read_sector(device, h.records_start_sector, sector)) {
        fs_zero_bytes(volume, sizeof(*volume));
        return FS_ERR_DEVICE_IO;
    }

    fs_copy_bytes(&root, sector, sizeof(root));
    if (!fs_record_valid(&root, 0)
        || root.type != FS_RECORD_TYPE_DIRECTORY
        || root.parent_id != 0
        || root.name_length != 1
        || root.name[0] != '/') {
        fs_zero_bytes(volume, sizeof(*volume));
        return FS_ERR_CORRUPT_METADATA;
    }

    return FS_OK;
}

fs_error_t fs_unmount(fs_volume_t *volume)
{
    if (!fs_volume_ready(volume)) return FS_ERR_NOT_MOUNTED;
    if (!fs_device_flush(volume->device)) return FS_ERR_DEVICE_IO;
    fs_zero_bytes(volume, sizeof(*volume));
    return FS_OK;
}

fs_error_t fs_create_file(fs_volume_t *v, uint32_t parent_id,
                          const char *name, uint32_t *record_id)
{
    return fs_create_entry(v, parent_id, name,
                           FS_RECORD_TYPE_FILE, record_id);
}

fs_error_t fs_create_directory(fs_volume_t *v, uint32_t parent_id,
                                const char *name, uint32_t *record_id)
{
    return fs_create_entry(v, parent_id, name,
                           FS_RECORD_TYPE_DIRECTORY, record_id);
}

fs_error_t fs_write_file(fs_volume_t *v, uint32_t record_id,
                         const void *data, uint64_t size)
{
    if (!fs_volume_ready(v)) return FS_ERR_NOT_MOUNTED;
    if (v->read_only) return FS_ERR_READ_ONLY;
    if (size != 0 && data == NULL) return FS_ERR_INVALID_ARGUMENT;

    fs_record_t r;
    fs_error_t e = fs_read_record(v, record_id, &r);
    if (e != FS_OK) return e;
    if (r.type == FS_RECORD_TYPE_DIRECTORY) return FS_ERR_IS_DIRECTORY;

    uint64_t needed = fs_div_round_up(size, FS_BLOCK_SIZE);
    uint64_t new_first = 0;

    /*
     * Allocate and write new contents before switching the record.
     * The old allocation is retained if writing the new contents fails.
     */
    e = fs_allocate_blocks(v, needed, &new_first);
    if (e != FS_OK) return e;

    const uint8_t *source = (const uint8_t *)data;
    uint64_t remaining = size;
    uint8_t sector[FS_SECTOR_SIZE];

    for (uint64_t i = 0; i < needed * FS_SECTORS_PER_BLOCK; ++i) {
        fs_zero_bytes(sector, sizeof(sector));

        size_t chunk = remaining > FS_SECTOR_SIZE
            ? FS_SECTOR_SIZE : (size_t)remaining;

        if (chunk != 0) {
            fs_copy_bytes(sector, source, chunk);
            source += chunk;
            remaining -= chunk;
        }

        uint64_t target = v->header.data_start_sector
            + (new_first * FS_SECTORS_PER_BLOCK) + i;

        if (!fs_write_sector(v->device, target, sector)) {
            (void)fs_release_blocks(v, new_first, needed);
            return FS_ERR_DEVICE_IO;
        }
    }

    uint64_t old_first = r.first_block;
    uint64_t old_count = r.block_count;

    r.first_block = new_first;
    r.block_count = needed;
    r.size = size;

    e = fs_write_record(v, &r);
    if (e != FS_OK) {
        (void)fs_release_blocks(v, new_first, needed);
        return e;
    }

    e = fs_release_blocks(v, old_first, old_count);
    if (e != FS_OK) return e;

    return fs_device_flush(v->device) ? FS_OK : FS_ERR_DEVICE_IO;
}

fs_error_t fs_read_file(fs_volume_t *v, uint32_t record_id,
                        void *buffer, uint64_t buffer_size,
                        uint64_t *bytes_read)
{
    if (bytes_read != NULL) *bytes_read = 0;
    if (!fs_volume_ready(v)) return FS_ERR_NOT_MOUNTED;

    fs_record_t r;
    fs_error_t e = fs_read_record(v, record_id, &r);
    if (e != FS_OK) return e;
    if (r.type == FS_RECORD_TYPE_DIRECTORY) return FS_ERR_IS_DIRECTORY;
    if (r.size != 0 && buffer == NULL) return FS_ERR_INVALID_ARGUMENT;
    if (buffer_size < r.size) return FS_ERR_OUT_OF_RANGE;

    uint8_t sector[FS_SECTOR_SIZE];
    uint8_t *destination = (uint8_t *)buffer;
    uint64_t remaining = r.size;

    for (uint64_t i = 0; remaining != 0; ++i) {
        uint64_t source_sector = v->header.data_start_sector
            + r.first_block * FS_SECTORS_PER_BLOCK + i;

        if (!fs_read_sector(v->device, source_sector, sector))
            return FS_ERR_DEVICE_IO;

        size_t chunk = remaining > FS_SECTOR_SIZE
            ? FS_SECTOR_SIZE : (size_t)remaining;

        fs_copy_bytes(destination, sector, chunk);
        destination += chunk;
        remaining -= chunk;
    }

    if (bytes_read != NULL) *bytes_read = r.size;
    return FS_OK;
}

fs_error_t fs_delete(fs_volume_t *v, uint32_t record_id)
{
    if (!fs_volume_ready(v)) return FS_ERR_NOT_MOUNTED;
    if (v->read_only) return FS_ERR_READ_ONLY;
    if (record_id == v->header.root_record_id)
        return FS_ERR_INVALID_ARGUMENT;

    fs_record_t r;
    fs_error_t e = fs_read_record(v, record_id, &r);
    if (e != FS_OK) return e;

    if (r.type == FS_RECORD_TYPE_DIRECTORY) {
        for (uint32_t id = 1; id < FS_MAX_RECORDS; ++id) {
            uint64_t sector;
            size_t offset;
            uint8_t buffer[FS_SECTOR_SIZE];
            fs_record_t child;

            (void)fs_record_location(v, id, &sector, &offset);
            if (!fs_read_sector(v->device, sector, buffer))
                return FS_ERR_DEVICE_IO;

            fs_copy_bytes(&child, buffer + offset, sizeof(child));
            if (!(child.flags & FS_RECORD_FLAG_USED)) continue;
            if (!fs_record_valid(&child, id))
                return FS_ERR_CORRUPT_METADATA;

            if (child.parent_id == record_id)
                return FS_ERR_DIRECTORY_NOT_EMPTY;
        }
    }

    e = fs_release_blocks(v, r.first_block, r.block_count);
    if (e != FS_OK) return e;

    r.flags = 0;
    r.size = 0;
    r.first_block = 0;
    r.block_count = 0;
    r.name_length = 0;
    fs_zero_bytes(r.name, sizeof(r.name));

    e = fs_write_record(v, &r);
    if (e != FS_OK) return e;

    return fs_device_flush(v->device) ? FS_OK : FS_ERR_DEVICE_IO;
}

fs_error_t fs_list_directory(fs_volume_t *v, uint32_t directory_id,
                             fs_dir_entry_t *entries, size_t capacity,
                             size_t *count)
{
    if (count == NULL || (capacity != 0 && entries == NULL))
        return FS_ERR_INVALID_ARGUMENT;
    *count = 0;

    if (!fs_volume_ready(v)) return FS_ERR_NOT_MOUNTED;

    fs_record_t directory;
    fs_error_t e = fs_read_record(v, directory_id, &directory);
    if (e != FS_OK) return e;
    if (directory.type != FS_RECORD_TYPE_DIRECTORY)
        return FS_ERR_NOT_DIRECTORY;

    size_t found = 0;

    for (uint32_t id = 1; id < FS_MAX_RECORDS; ++id) {
        uint64_t sector;
        size_t offset;
        uint8_t buffer[FS_SECTOR_SIZE];
        fs_record_t r;

        (void)fs_record_location(v, id, &sector, &offset);
        if (!fs_read_sector(v->device, sector, buffer))
            return FS_ERR_DEVICE_IO;

        fs_copy_bytes(&r, buffer + offset, sizeof(r));
        if (!(r.flags & FS_RECORD_FLAG_USED)) continue;
        if (!fs_record_valid(&r, id)) return FS_ERR_CORRUPT_METADATA;
        if (r.parent_id != directory_id) continue;

        if (found >= capacity) {
            *count = found;
            return FS_ERR_OUT_OF_RANGE;
        }

        entries[found].id = r.id;
        entries[found].type = (fs_entry_type_t)r.type;
        entries[found].size = r.size;
        fs_copy_bytes(entries[found].name, r.name, r.name_length);
        entries[found].name[r.name_length] = 0;
        ++found;
    }

    *count = found;
    return FS_OK;
}

fs_error_t fs_find_path(fs_volume_t *v, const char *path,
                        uint32_t *record_id)
{
    if (!fs_volume_ready(v)) return FS_ERR_NOT_MOUNTED;
    if (path == NULL || record_id == NULL || path[0] != '/')
        return FS_ERR_INVALID_PATH;

    size_t path_length = 0;
    while (path[path_length] != 0) {
        if (++path_length > FS_PATH_MAX) return FS_ERR_INVALID_PATH;
    }

    uint32_t current = v->header.root_record_id;
    size_t pos = 1;

    if (path[pos] == 0) {
        *record_id = current;
        return FS_OK;
    }

    while (pos < path_length) {
        char component[FS_NAME_MAX + 1u];
        size_t n = 0;

        while (pos < path_length && path[pos] == '/') ++pos;
        if (pos >= path_length) break;

        while (pos < path_length && path[pos] != '/') {
            if (n >= FS_NAME_MAX) return FS_ERR_INVALID_PATH;
            component[n++] = path[pos++];
        }

        if (n == 0) continue;
        component[n] = 0;

        if ((n == 1 && component[0] == '.')
            || (n == 2 && component[0] == '.'
                && component[1] == '.')) {
            return FS_ERR_INVALID_PATH;
        }

        uint32_t child_id;
        fs_error_t e = fs_find_child(v, current, component, &child_id);
        if (e != FS_OK) return e;

        fs_record_t child;
        e = fs_read_record(v, child_id, &child);
        if (e != FS_OK) return e;

        size_t lookahead = pos;
        while (lookahead < path_length && path[lookahead] == '/')
            ++lookahead;

        if (lookahead < path_length
            && child.type != FS_RECORD_TYPE_DIRECTORY) {
            return FS_ERR_NOT_DIRECTORY;
        }

        current = child_id;
    }

    *record_id = current;
    return FS_OK;
}

const char *fs_error_string(fs_error_t error)
{
    switch (error) {
        case FS_OK: return "Success";
        case FS_ERR_INVALID_ARGUMENT: return "Invalid argument";
        case FS_ERR_DEVICE_IO: return "Block-device I/O failure";
        case FS_ERR_INVALID_VOLUME: return "Invalid or damaged filesystem volume";
        case FS_ERR_UNSUPPORTED_VERSION: return "Unsupported filesystem version";
        case FS_ERR_VOLUME_TOO_SMALL: return "Volume is too small";
        case FS_ERR_OUT_OF_RANGE: return "Block, sector, or buffer is out of range";
        case FS_ERR_NOT_MOUNTED: return "Filesystem is not mounted";
        case FS_ERR_NO_SPACE: return "No space left on volume";
        case FS_ERR_NOT_FOUND: return "File or directory not found";
        case FS_ERR_ALREADY_EXISTS: return "File or directory already exists";
        case FS_ERR_NOT_DIRECTORY: return "Path component is not a directory";
        case FS_ERR_IS_DIRECTORY: return "Path refers to a directory";
        case FS_ERR_DIRECTORY_NOT_EMPTY: return "Directory is not empty";
        case FS_ERR_INVALID_PATH: return "Invalid path";
        case FS_ERR_CORRUPT_METADATA: return "Filesystem metadata is corrupt";
        case FS_ERR_READ_ONLY: return "Filesystem is read-only";
        default: return "Unknown filesystem error";
    }
}
