#include "../src/kernel/filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_DISK_BYTES (16u * 1024u * 1024u)
#define TEST_SECTOR_COUNT (TEST_DISK_BYTES / FS_SECTOR_SIZE)

static uint8_t test_disk[TEST_DISK_BYTES];

static bool fail_reads;
static bool fail_writes;
static bool fail_flushes;

static bool test_read(
    void *context,
    uint64_t start_sector,
    uint32_t sector_count,
    void *buffer
)
{
    uint8_t *disk = (uint8_t *)context;

    if (fail_reads || buffer == NULL) {
        return false;
    }

    if (start_sector > TEST_SECTOR_COUNT ||
        sector_count > TEST_SECTOR_COUNT - start_sector) {
        return false;
    }

    memcpy(
        buffer,
        disk + start_sector * FS_SECTOR_SIZE,
        (size_t)sector_count * FS_SECTOR_SIZE
    );

    return true;
}

static bool test_write(
    void *context,
    uint64_t start_sector,
    uint32_t sector_count,
    const void *buffer
)
{
    uint8_t *disk = (uint8_t *)context;

    if (fail_writes || buffer == NULL) {
        return false;
    }

    if (start_sector > TEST_SECTOR_COUNT ||
        sector_count > TEST_SECTOR_COUNT - start_sector) {
        return false;
    }

    memcpy(
        disk + start_sector * FS_SECTOR_SIZE,
        buffer,
        (size_t)sector_count * FS_SECTOR_SIZE
    );

    return true;
}

static bool test_flush(void *context)
{
    (void)context;
    return !fail_flushes;
}

static unsigned tests_run;
static unsigned tests_failed;

#define CHECK(condition, description) do { \
    ++tests_run; \
    if (condition) { \
        printf("[PASS] %s\n", description); \
    } else { \
        printf("[FAIL] %s\n", description); \
        ++tests_failed; \
    } \
} while (0)

static fs_block_device_t make_device(void)
{
    fs_block_device_t device = {
        .context = test_disk,
        .sector_count = TEST_SECTOR_COUNT,
        .read = test_read,
        .write = test_write,
        .flush = test_flush
    };

    return device;
}

int main(void)
{
    fs_block_device_t device = make_device();
    fs_volume_t volume;

    memset(test_disk, 0, sizeof(test_disk));

    printf("Neptune OS Build 006-B filesystem tests\n");
    printf("Test disk: %u bytes, %llu sectors\n",
           (unsigned)TEST_DISK_BYTES,
           (unsigned long long)device.sector_count);

    /* Raw block-device behavior. */
    uint8_t input[FS_SECTOR_SIZE];
    uint8_t output[FS_SECTOR_SIZE];

    for (size_t i = 0; i < sizeof(input); ++i) {
        input[i] = (uint8_t)(i & 0xFFu);
    }

    CHECK(device.write(
              device.context, 10, 1, input),
          "Write one test sector");

    memset(output, 0, sizeof(output));

    CHECK(device.read(
              device.context, 10, 1, output)
          && memcmp(input, output, sizeof(input)) == 0,
          "Read sector and verify its contents");

    CHECK(!device.read(
              device.context, device.sector_count, 1, output),
          "Reject a read beyond the end of the disk");

    CHECK(!device.write(
              device.context, device.sector_count - 1, 2, input),
          "Reject a write extending beyond the disk");

    /* Filesystem formatting. */
    CHECK(fs_format(&device) == FS_OK,
          "Format a blank volume");

    CHECK(memcmp(test_disk, "NEPTUNE1", 8) == 0,
          "Write the NFS volume signature");

    CHECK(memcmp(
              test_disk +
                  (device.sector_count - 1) * FS_SECTOR_SIZE,
              "NEPTUNE1", 8) == 0,
          "Write the backup volume header");

    /* Mount and unmount. */
    CHECK(fs_mount(&volume, &device) == FS_OK,
          "Mount the formatted volume");

    CHECK(volume.mounted && volume.device == &device,
          "Record mounted volume state");

    CHECK(volume.header.version == FS_FORMAT_VERSION,
          "Read the filesystem format version");

    CHECK(fs_unmount(&volume) == FS_OK,
          "Unmount the volume");

    CHECK(!volume.mounted,
          "Clear mounted state after unmount");

    /* Backup header recovery. */
    test_disk[0] ^= 0xFFu;

    CHECK(fs_mount(&volume, &device) == FS_OK,
          "Recover using the backup header");

    CHECK(volume.mounted,
          "Keep volume mounted after backup recovery");

    CHECK(fs_unmount(&volume) == FS_OK,
          "Unmount recovered volume");

    /* Reject a volume with both headers damaged. */
    test_disk[0] ^= 0xFFu;

    uint64_t backup_offset =
        (device.sector_count - 1) * FS_SECTOR_SIZE;

    test_disk[backup_offset] ^= 0xFFu;

    CHECK(fs_mount(&volume, &device) == FS_ERR_INVALID_VOLUME,
          "Reject a volume with both headers damaged");

    /* Device failure reporting. */
    fail_reads = true;

    CHECK(fs_mount(&volume, &device) == FS_ERR_DEVICE_IO,
          "Report a block-device read failure");

    fail_reads = false;

    CHECK(fs_format(&device) == FS_OK,
          "Reformat after the corruption test");

    fail_writes = true;

    CHECK(fs_format(&device) == FS_ERR_DEVICE_IO,
          "Report a block-device write failure");

    fail_writes = false;

    CHECK(fs_mount(&volume, &device) == FS_OK,
          "Mount remains possible after failed reformat");

    fail_flushes = true;

    CHECK(fs_unmount(&volume) == FS_ERR_DEVICE_IO,
          "Report a flush failure");

    fail_flushes = false;

    CHECK(fs_unmount(&volume) == FS_OK,
          "Retry unmount after restoring flush");

    printf("\nResults: %u tests, %u failures\n",
           tests_run, tests_failed);

    return tests_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
