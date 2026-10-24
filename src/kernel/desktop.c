#include "desktop.h"
#include "graphics.h"
#include "filesystem.h"
#include "ata.h"
#include "notes.h"

#define TOPBAR_HEIGHT 40
#define SIDEBAR_WIDTH 184
#define NAV_START_Y 66
#define NAV_ITEM_HEIGHT 38
#define CONTENT_MARGIN 22

#define FILES_RAM_SECTORS 4096u
#define FILES_RAM_BYTES (FILES_RAM_SECTORS * FS_SECTOR_SIZE)
#define FILES_MAX_ENTRIES 24u
#define FILES_TEXT_CAPACITY 2048u
#define FILES_PATH_DEPTH 16u

static uint8_t files_ramdisk[FILES_RAM_BYTES];

static fs_block_device_t files_device;
static fs_volume_t files_volume;

static fs_block_device_t files_ata_device;
static int files_ata_detected;
static int files_using_ata;
static int files_format_confirmation;

static int files_ready;
static int files_error;
static int files_initialized;

static uint32_t files_directory_stack[FILES_PATH_DEPTH];
static char files_directory_names[FILES_PATH_DEPTH][FS_NAME_MAX + 1u];
static unsigned files_path_depth;

static fs_dir_entry_t files_entries[FILES_MAX_ENTRIES];
static size_t files_entry_count;
static size_t files_selected;

static char files_status[84] = "RAM TEST VOLUME";
static char files_input[FS_NAME_MAX + 1u];
static size_t files_input_length;
static int files_input_mode; /* 0=normal, 1=new file, 2=new folder */
static int files_editor_active;
static uint32_t files_editor_record;
static char files_editor_name[FS_NAME_MAX + 1u];
static char files_editor_buffer[FILES_TEXT_CAPACITY];
static size_t files_editor_length;
static size_t files_editor_cursor;

static void draw_programs_view(int x, int y);
static void files_draw_view(int x, int y, int width, int height);
static void files_open_selected(void);
static void files_commit_new_entry(void);
static void files_save_editor(void);
static void files_delete_selected(void);

static bool files_ram_read(void *context, uint64_t start_sector,
                           uint32_t sector_count, void *buffer)
{
    uint8_t *disk = (uint8_t *)context;

    if (disk == NULL || buffer == NULL || sector_count == 0
        || start_sector >= FILES_RAM_SECTORS
        || (uint64_t)sector_count > FILES_RAM_SECTORS - start_sector) {
        return false;
    }

    uint64_t offset = start_sector * FS_SECTOR_SIZE;
    uint64_t bytes = (uint64_t)sector_count * FS_SECTOR_SIZE;
    uint8_t *out = (uint8_t *)buffer;

    for (uint64_t i = 0; i < bytes; ++i) {
        out[i] = disk[offset + i];
    }

    return true;
}

static bool files_ram_write(void *context, uint64_t start_sector,
                            uint32_t sector_count, const void *buffer)
{
    uint8_t *disk = (uint8_t *)context;

    if (disk == NULL || buffer == NULL || sector_count == 0
        || start_sector >= FILES_RAM_SECTORS
        || (uint64_t)sector_count > FILES_RAM_SECTORS - start_sector) {
        return false;
    }

    uint64_t offset = start_sector * FS_SECTOR_SIZE;
    uint64_t bytes = (uint64_t)sector_count * FS_SECTOR_SIZE;
    const uint8_t *in = (const uint8_t *)buffer;

    for (uint64_t i = 0; i < bytes; ++i) {
        disk[offset + i] = in[i];
    }

    return true;
}

static bool files_ram_flush(void *context)
{
    return context != NULL;
}

static void files_set_status(const char *message)
{
    size_t i = 0;

    if (message == NULL) {
        files_status[0] = 0;
        return;
    }

    while (message[i] != 0 && i < sizeof(files_status) - 1u) {
        files_status[i] = message[i];
        ++i;
    }

    files_status[i] = 0;
}

static void files_set_error(fs_error_t error)
{
    files_error = (error != FS_OK);
    files_set_status(fs_error_string(error));
}

static void files_refresh_directory(void)
{
    files_entry_count = 0;

    if (!files_ready) {
        files_set_status("FILESYSTEM NOT READY");
        return;
    }

    fs_error_t error = fs_list_directory(
        &files_volume,
        files_directory_stack[files_path_depth],
        files_entries,
        FILES_MAX_ENTRIES,
        &files_entry_count
    );

    if (error != FS_OK) {
        files_set_error(error);
        return;
    }

    if (files_entry_count == 0) {
        files_selected = 0;
    } else if (files_selected >= files_entry_count) {
        files_selected = files_entry_count - 1u;
    }

    files_error = 0;

    if (files_using_ata) {
        files_set_status("PERSISTENT IDE STORAGE");
    } else {
        files_set_status("RAM STORAGE - NOT PERSISTENT");
    }
}

static void files_initialize(void)
{
    if (files_initialized)
        return;

    files_initialized = 1;
    files_ready = 0;
    files_error = 0;
    files_ata_detected = 0;
    files_using_ata = 0;
    files_format_confirmation = 0;

    files_path_depth = 0;
    files_directory_stack[0] = 0;
    files_directory_names[0][0] = '\0';

    /*
     * Prefer persistent storage when the primary IDE master
     * is available.
     */
    if (ata_primary_master_init(&files_ata_device)) {
        files_ata_detected = 1;
        files_device = files_ata_device;
        files_using_ata = 1;

        fs_error_t error = fs_mount(&files_volume, &files_device);

        if (error == FS_OK) {
            files_ready = 1;
            files_set_status("IDE STORAGE MOUNTED");
            files_refresh_directory();
            return;
        }

        files_ready = 0;
        files_error = 1;

        switch (error) {
            case FS_ERR_INVALID_VOLUME:
                files_set_status("INVALID IDE FILESYSTEM");
                break;

            case FS_ERR_UNSUPPORTED_VERSION:
                files_set_status("UNSUPPORTED FILESYSTEM VERSION");
                break;

            case FS_ERR_CORRUPT_METADATA:
                files_set_status("CORRUPT FILESYSTEM METADATA");
                break;

            case FS_ERR_DEVICE_IO:
                files_set_status("IDE DISK I/O ERROR");
                break;

            case FS_ERR_VOLUME_TOO_SMALL:
                files_set_status("IDE DISK TOO SMALL");
                break;

            default:
                files_set_status("IDE FILESYSTEM MOUNT FAILED");
                break;
        }

/* Never format a disk because mounting failed. */
        return;
    }

    /*
     * No IDE disk was detected. Use the existing RAM-backed
     * volume as a fallback.
     */
    files_using_ata = 0;

    files_device.context = NULL;
    files_device.sector_count = FILES_RAM_SECTORS;
    files_device.read = files_ram_read;
    files_device.write = files_ram_write;
    files_device.flush = files_ram_flush;

    fs_error_t error = fs_format(&files_device);

    if (error != FS_OK) {
        files_error = (int)error;
        files_set_status("RAM VOLUME FORMAT FAILED");
        return;
    }

    error = fs_mount(&files_volume, &files_device);

    if (error != FS_OK) {
        files_error = (int)error;
        files_set_status("RAM VOLUME MOUNT FAILED");
        return;
    }

    uint32_t welcome_id;

    error = fs_create_file(
        &files_volume,
        files_directory_stack[0],
        "WELCOME.TXT",
        &welcome_id
    );

    if (error == FS_OK) {
        static const char welcome[] =
            "Welcome to Neptune OS Files.\n"
            "This is temporary RAM storage.\n"
            "Files are lost when the system restarts.\n";

        error = fs_write_file(
            &files_volume,
            welcome_id,
            welcome,
            sizeof(welcome) - 1
        );
    }

    if (error != FS_OK) {
        files_error = (int)error;
        files_set_status("RAM VOLUME READY WITH AN ERROR");
        return;
    }

    files_ready = 1;
    files_set_status("RAM STORAGE - NOT PERSISTENT");
    files_refresh_directory();
}

typedef enum {
    VIEW_DASHBOARD = 0,
    VIEW_PROGRAMS,
    VIEW_FILES,
    VIEW_SETTINGS,
    VIEW_ABOUT,
    VIEW_MANAGER,
    VIEW_LOG_OFF,
    VIEW_SHUT_DOWN,
    VIEW_COUNT
} desktop_view_t;

static uint32_t screen_width;
static uint32_t screen_height;
static desktop_view_t current_view = VIEW_DASHBOARD;
static int selected_item;
static int initialized;
static desktop_action_t pending_action = DESKTOP_ACTION_NONE;

static const char *nav_labels[VIEW_COUNT] = {
    "DESKTOP",
    "PROGRAMS",
    "FILES",
    "SETTINGS",
    "ABOUT",
    "MANAGER",
    "LOG OFF",
    "SHUT DOWN"
};

static void draw_text(int x, int y, const char *text, uint32_t color)
{
    gfx_text(x, y, text, color, 1);
}

static void draw_panel(int x, int y, int width, int height,
                       const char *title, const char *line1,
                       const char *line2)
{
    if (width < 20 || height < 20) {
        return;
    }

    gfx_fill_rect(x, y, width, height, NC_WHITE);
    gfx_rect(x, y, width, height, NC_GRAY);

    gfx_fill_rect(x + 1, y + 1, width - 2, 30, NC_BLUE);
    gfx_text(x + 10, y + 8, title, NC_WHITE, 2);

    if (line1 != 0) {
        draw_text(x + 10, y + 43, line1, NC_BLACK);
    }

    if (line2 != 0) {
        draw_text(x + 10, y + 61, line2, NC_DARK_BLUE);
    }
}

static void draw_topbar(void)
{
    gfx_fill_rect(0, 0, (int)screen_width, TOPBAR_HEIGHT, NC_BLACK);

    gfx_fill_rect(10, 6, 28, 28, NC_BLUE);
    gfx_rect(10, 6, 28, 28, NC_WHITE);
    gfx_text(18, 12, "N", NC_WHITE, 2);

    /* Larger Neptune OS branding. */
    gfx_text(48, 11, "NEPTUNE OS", NC_WHITE, 2);

    gfx_text(200, 16, "DESKTOP", NC_SILVER, 1);

    if (screen_width >= 700) {
        gfx_text((int)screen_width - 200, 16,
                 "DATE NOT SET", NC_SILVER, 1);
        gfx_text((int)screen_width - 80, 16,
                 "00 00", NC_WHITE, 1);
    }
}

#define ICON_SIZE 12

static const uint32_t icon_palette[16] = {
    NC_RED, NC_GREEN, NC_BLUE, NC_YELLOW,
    NC_MAGENTA, NC_CYAN, NC_DARK_RED, NC_DARK_GREEN,
    NC_DARK_BLUE, NC_OLIVE, NC_PURPLE, NC_TEAL,
    NC_BLACK, NC_GRAY, NC_SILVER, NC_WHITE
};

/*
 * Each character is one pixel.
 * '.' = transparent
 * '0' through 'F' = palette index
 *
 * Icons: Desktop, Programs, Files, Settings,
 *        About, Manager, Log Off, Shut Down.
 */
static const char *nav_icon_matrix[8][ICON_SIZE] = {
    {   /* 0: Desktop */
        "............",
        ".CCCCCCCCCC.",
        "CEEEEEEEEEEC",
        "CEBBBBBBBBEC",
        "CEBBBBBBBBEC",
        "CEBBBBBBBBEC",
        "CEBBBBBBBBEC",
        "CEBBBBBBBBEC",
        "CEEEEDDEEEEC",
        "CEEEEEEEEEEC",
        ".CCCCCCCCCC.",
        "............"
    },
    {   /* 1: Programs */
        ".CCCCCCCCCC.",
        "CDDDDDDDDDDC",
        "CDCCCCCC0CDC",
        "CDDDDDDDDDDC",
        "CDEEEEEEEEDC",
        "CDEEEEEEEEDC",
        "CDEEEEEEEEDC",
        "CDEEEEEEEEDC",
        "CDEEEEEEEEDC",
        "CDEEEEEEEEDC",
        "CDDDDDDDDDDC",
        ".CCCCCCCCCC."
    },
    {   /* 2: Files */
        "............",
        "....CC......",
        "...CF3C.....",
        "..CF3F3CCC..",
        ".C3F3F3F3FC.",
        ".CF3F3F3F3C.",
        ".C3F3F3F3FC.",
        ".CF3F3F3F3C.",
        ".C3F3F3F3FC.",
        "..CCCCCCCC..",
        "............",
        "............"
    },
    {   /* 3: Settings */
        "......CCC...",
        ".CCCCCDDDCC.",
        "CDDDDDDEDDDC",
        ".CCCCCDDDCC.",
        "CDDDCCCCCCC.",
        "CDEDDDDDDDDC",
        "CDDDCCCCCCC.",
        ".CCCCDDDCCC.",
        "CDDDDDEDDDDC",
        ".CCCCDDDCCC.",
        ".....CCC....",
        "............"
    },
    {   /* 4: About */
        "....CCCC....",
        "..CCFFFFCC..",
        ".CFF4A4AFFC.",
        ".CF4AFF4AFC.",
        "CF4A4FFA4AFC",
        "CFA4A4A4A4FC",
        "CF4A4FFA4AFC",
        "CFA4AFF4A4FC",
        ".CFA4FFA4FC.",
        ".CFFA4A4FFC.",
        "..CCFFFFCC..",
        "....CCCC...."
    },
    {   /* 5: Manager */
        ".CCCCCCCCCC.",
        "CEEEEEEEEEEC",
        "CECCCCCCCCEC",
        "CECC7CCCCCEC",
        "CEC7C7CCCCEC",
        "CEC7C7CCCCEC",
        "CE7CC7CC77EC",
        "CECCC7C7CCEC",
        "CECCCC7CCCEC",
        "CECCCCCCCCEC",
        "CEEEEEEEEEEC",
        ".CCCCCCCCCC."
    },
    {   /* 6: Log Off */
        "............",
        ".CC.........",
        ".C6C....C...",
        ".C96C..CFC..",
        ".C6CCCCCFFC.",
        ".CCFFFFFFFFC",
        ".CCFFFFFFFFC",
        ".C9CC..CFFC.",
        ".C69C..CFC..",
        ".C9C....C...",
        ".CC.........",
        "............"
    },
    {   /* 7: Shut Down */
        ".....FF.....",
        "....FFFF....",
        "..FF0FF0FF..",
        "..F00FF00F..",
        ".F000FF000F.",
        ".F00000000F.",
        ".F00000000F.",
        ".F00000000F.",
        "..F000000F..",
        "..FF0000FF..",
        "....FFFF....",
        "............"
    }
};

static void draw_nav_icon(int index, int x, int y)
{
    if (index < 0 || index >= 8) {
        return;
    }

    for (int row = 0; row < ICON_SIZE; row++) {
        for (int col = 0; col < ICON_SIZE; col++) {
            char pixel = nav_icon_matrix[index][row][col];

            if (pixel == '.') {
                continue;
            }

            int palette_index;

            if (pixel >= '0' && pixel <= '9') {
                palette_index = pixel - '0';
            } else if (pixel >= 'A' && pixel <= 'F') {
                palette_index = pixel - 'A' + 10;
            } else {
                continue;
            }

            gfx_fill_rect(
                x + col,
                y + row,
                1,
                1,
                icon_palette[palette_index]
            );
        }
    }
}

static const char *notes_icon_matrix[12] = {
    ".CCCCCCCCCC.",
    "C5555555555C",
    "C5555555555C",
    "C5555555555C",
    "CBBBBBBBBBBC",
    "CB55555555BC",
    "CBBBBBBBBBBC",
    "CB55555555BC",
    "CBBBBBBBBBBC",
    "CB55555555BC",
    "CBBBBBBBBBBC",
    ".CCCCCCCCCC."
};

static void draw_notes_icon(int x, int y)
{
    for (int row = 0; row < 12; row++) {
        for (int col = 0; col < 12; col++) {
            char pixel = notes_icon_matrix[row][col];
            uint32_t color;

            switch (pixel) {
                case '5':
                    color = NC_CYAN;
                    break;
                case 'B':
                    color = NC_TEAL;
                    break;
                case 'C':
                    color = NC_BLACK;
                    break;
                case '.':
                default:
                    continue;
            }

            gfx_pixel(x + col, y + row, color);
        }
    }
}

static void draw_sidebar(void)
{
    gfx_fill_rect(0, TOPBAR_HEIGHT, SIDEBAR_WIDTH,
                  (int)screen_height - TOPBAR_HEIGHT, NC_BLACK);

    draw_text(16, 48, "Workspace", NC_GRAY);
  
    for (int i = 0; i < 8; i++) {
        int item_y = NAV_START_Y + i * NAV_ITEM_HEIGHT;

        if (current_view == (desktop_view_t)i) {
            gfx_fill_rect(8, item_y, SIDEBAR_WIDTH - 16,
                          NAV_ITEM_HEIGHT - 3, NC_BLUE);
        }

        uint32_t icon_color =
            (current_view == i) ? NC_WHITE : NC_SILVER;

        draw_nav_icon(i, 19, item_y + 12);

        draw_text(43, item_y + 14, nav_labels[i],
                  (current_view == i) ? NC_WHITE : NC_SILVER);
    }

    gfx_vline(SIDEBAR_WIDTH - 1, TOPBAR_HEIGHT,
              (int)screen_height - TOPBAR_HEIGHT, NC_GRAY);
}

static void draw_dashboard(int x, int y, int width, int height)
{
    gfx_text(x, y, "WELCOME TO NEPTUNE OS", NC_WHITE, 3);

    int gap = 12;
    int card_width = (width - gap) / 2;
    int card_height = 92;

    if (card_width < 100) {
        card_width = width;
    }

    int row2_y = y + 78 + card_height + gap;

    draw_panel(x, y + 54, card_width, card_height,
               "APPOINTMENTS",
               "No Appointments Yet",
               "Calendar Support Planned");

    draw_panel(x + card_width + gap, y + 54,
               card_width, card_height,
               "MESSAGES",
               "No New Messages",
               "Messaging Support Planned");

    draw_panel(x, row2_y, card_width, card_height,
               "TASKS",
               "Nothing to Do Yet",
               "Task Support Planned");

    draw_panel(x + card_width + gap, row2_y,
               card_width, card_height,
               "EVENTS",
               "No Upcoming Events",
               "Event Support Planned");

    if (height > 330) {
        draw_text(x, row2_y + card_height + 20,
                  "Select an Item from the Sidebar to Continue",
                  NC_WHITE);
    }
}


static void files_draw_view(int x, int y, int width, int height)
{
    if (width <= 0 || height < 80) {
        return;
    }

    gfx_fill_rect(x, y, width, height, NC_DARK_BLUE);
    draw_text(x, y, "FILE EXPLORER", NC_WHITE);

    /* Build the current folder path. */
    char path[FS_PATH_MAX + 1u];
    size_t pos = 0;

    path[pos++] = '/';
    path[pos] = '\0';

    for (unsigned i = 1; i <= files_path_depth; ++i) {
        if (pos > 1 && pos < FS_PATH_MAX) {
            path[pos++] = '/';
        }

        const char *name = files_directory_names[i];

        for (size_t j = 0;
             name[j] != '\0' && pos < FS_PATH_MAX;
             ++j) {
            path[pos++] = name[j];
        }

        path[pos] = '\0';
    }

    draw_text(x, y + 24, "PATH:", NC_SILVER);
    draw_text(x + 48, y + 24, path, NC_WHITE);

    gfx_fill_rect(x, y + 44, width, 22, NC_BLACK);
    draw_text(x + 6, y + 51, "NAME", NC_WHITE);
    draw_text(x + width - 110, y + 51, "TYPE / SIZE", NC_WHITE);

    if (files_editor_active) {
        int editor_height = height - 120;

        if (editor_height < 20) {
            editor_height = 20;
        }

        gfx_fill_rect(x, y + 70, width, editor_height, NC_WHITE);
        gfx_rect(x, y + 70, width, editor_height, NC_GRAY);

        draw_text(x + 8, y + 78, files_editor_name, NC_BLUE);

        size_t line_start = 0;
        int line_y = y + 98;
        int line_limit = y + 70 + editor_height - 8;

        for (size_t i = 0; i <= files_editor_length; ++i) {
            if (i == files_editor_length ||
                files_editor_buffer[i] == '\n') {
                char line[81];
                size_t n = i - line_start;

                if (n > sizeof(line) - 1u) {
                    n = sizeof(line) - 1u;
                }

                for (size_t j = 0; j < n; ++j) {
                    line[j] = files_editor_buffer[line_start + j];
                }

                line[n] = '\0';
                draw_text(x + 8, line_y, line, NC_BLACK);

                line_y += 16;
                line_start = i + 1u;

                if (line_y > line_limit) {
                    break;
                }
            }
        }

        draw_text(x, y + height - 34,
                  "TYPE TO EDIT | F2 SAVE | ESC CLOSE", NC_WHITE);
    } else if (files_input_mode != 0) {
        draw_text(
            x + 6, y + 82,
            files_input_mode == 1 ? "NEW FILE NAME:" : "NEW FOLDER NAME:",
            NC_WHITE
        );

        gfx_fill_rect(x + 6, y + 102, width - 12, 24, NC_WHITE);
        gfx_rect(x + 6, y + 102, width - 12, 24, NC_GRAY);
        draw_text(x + 12, y + 110, files_input, NC_BLACK);

        draw_text(x + 6, y + 140,
                  "ENTER CONFIRMS | ESC CANCELS", NC_SILVER);
    } else {
        int row_y = y + 70;

        for (size_t i = 0;
             i < files_entry_count && i < 15;
             ++i) {
            fs_dir_entry_t *entry = &files_entries[i];

            if (i == files_selected) {
                gfx_fill_rect(x, row_y, width, 18, NC_BLUE);
            }

            uint32_t text_color =
                (i == files_selected) ? NC_WHITE : NC_SILVER;

            draw_text(x + 6, row_y + 4, entry->name, text_color);

            if (entry->type == FS_TYPE_DIRECTORY) {
                draw_text(x + width - 110, row_y + 4,
                          "<DIR>",
                          (i == files_selected) ? NC_WHITE : NC_CYAN);
            } else {
                char size_text[24];
                char reversed[20];
                uint64_t size = entry->size;
                size_t digits = 0;
                size_t out = 0;

                do {
                    reversed[digits++] = (char)('0' + size % 10u);
                    size /= 10u;
                } while (size != 0 && digits < sizeof(reversed));

                while (digits > 0 && out < sizeof(size_text) - 1u) {
                    size_text[out++] = reversed[--digits];
                }

                size_text[out] = '\0';

                draw_text(x + width - 110, row_y + 4,
                          size_text, text_color);
            }

            row_y += 18;
        }

        if (files_entry_count == 0) {
            draw_text(x + 8, y + 82, "THIS FOLDER IS EMPTY", NC_SILVER);
        }

        draw_text(x, y + height - 56,
                  "UP/DOWN SELECT  ENTER OPEN  BACKSPACE UP", NC_WHITE);
        draw_text(x, y + height - 38,
          "N NEW FILE  D NEW FOLDER  DELETE REMOVE", NC_WHITE);

        if (files_ata_detected && !files_ready) {
            if (files_format_confirmation) {
                draw_text(x, y + height - 64,
                          "ERASE DISK? ENTER=YES ESC=NO",
                          NC_WHITE);
            } else if (files_error == (int)FS_ERR_INVALID_VOLUME) {
                draw_text(x + 0, y + height - 64,
                          "INVALID OR UNFORMATTED DISK",
                          NC_WHITE);
                draw_text(x + 0, y + height - 48,
                          "F2 INITIALIZE - ERASES DISK",
                          NC_WHITE);
            } else {
                draw_text(x, y + height - 64,
                          "IDE DISK NEEDS INITIALIZATION",
                          NC_WHITE);
                draw_text(x, y + height - 48,
                          "PRESS F2 TO INITIALIZE",
                          NC_WHITE);
            }
        }
    }

    gfx_fill_rect(x, y + height - 18, width, 18, NC_BLACK);
    draw_text(x + 4, y + height - 13, files_status,
              files_error ? NC_RED : NC_CYAN);
}

static void files_open_selected(void)
{
    if (!files_ready || files_selected >= files_entry_count) {
        return;
    }

    fs_dir_entry_t *entry = &files_entries[files_selected];

    if (entry->type == FS_TYPE_DIRECTORY) {
        if (files_path_depth + 1u >= FILES_PATH_DEPTH) {
            files_error = 1;
            files_set_status("MAXIMUM DIRECTORY DEPTH REACHED");
            return;
        }

        ++files_path_depth;
        files_directory_stack[files_path_depth] = entry->id;

        size_t i = 0;
        while (entry->name[i] != '\0' && i < FS_NAME_MAX) {
            files_directory_names[files_path_depth][i] = entry->name[i];
            ++i;
        }

        files_directory_names[files_path_depth][i] = '\0';
        files_selected = 0;
        files_refresh_directory();
        return;
    }

    uint64_t bytes_read = 0;

    fs_error_t error = fs_read_file(
        &files_volume,
        entry->id,
        files_editor_buffer,
        sizeof(files_editor_buffer) - 1u,
        &bytes_read
    );

    if (error != FS_OK) {
        files_set_error(error);
        return;
    }

    for (size_t i = 0; i < bytes_read; ++i) {
        if (files_editor_buffer[i] == '\0') {
            files_error = 1;
            files_set_status("BINARY FILES CANNOT BE EDITED AS TEXT");
            return;
        }
    }

    files_editor_buffer[bytes_read] = '\0';
    files_editor_length = (size_t)bytes_read;
    files_editor_cursor = files_editor_length;
    files_editor_record = entry->id;

    size_t i = 0;
    while (entry->name[i] != '\0' && i < FS_NAME_MAX) {
        files_editor_name[i] = entry->name[i];
        ++i;
    }

    files_editor_name[i] = '\0';
    files_editor_active = 1;
    files_error = 0;
    files_set_status("TEXT FILE OPEN");
}

static void files_commit_new_entry(void)
{
    if (!files_ready) {
        files_error = 1;
        files_set_status("FILESYSTEM NOT READY");
        return;
    }

    if (files_input_length == 0) {
        files_error = 1;
        files_set_status("NAME CANNOT BE EMPTY");
        return;
    }

    uint32_t new_id = 0;
    uint32_t parent_id = files_directory_stack[files_path_depth];

    fs_error_t error;

    if (files_input_mode == 1) {
        error = fs_create_file(
            &files_volume, parent_id, files_input, &new_id
        );

        if (error == FS_OK) {
            error = fs_write_file(&files_volume, new_id, "", 0);
        }
    } else {
        error = fs_create_directory(
            &files_volume, parent_id, files_input, &new_id
        );
    }

    files_input_mode = 0;
    files_input_length = 0;
    files_input[0] = '\0';

    /* Refresh first so the operation result remains visible. */
    files_refresh_directory();

    if (error != FS_OK) {
        files_set_error(error);
    } else {
        files_error = 0;
        files_set_status("ENTRY CREATED");
    }
}

static void files_save_editor(void)
{
    if (!files_ready || !files_editor_active) {
        return;
    }

    fs_error_t error = fs_write_file(
        &files_volume,
        files_editor_record,
        files_editor_buffer,
        files_editor_length
    );

    if (error != FS_OK) {
        files_set_error(error);
        return;
    }

    files_editor_active = 0;
    files_refresh_directory();

    files_error = 0;
    files_set_status("FILE SAVED");
}

static void files_delete_selected(void)
{
    if (!files_ready || files_selected >= files_entry_count) {
        return;
    }

    uint32_t record_id = files_entries[files_selected].id;
    fs_error_t error = fs_delete(&files_volume, record_id);

    files_refresh_directory();

    if (error != FS_OK) {
        files_set_error(error);
    } else {
        files_error = 0;
        files_set_status("ENTRY DELETED");
    }
}

static void draw_placeholder_view(int x, int y, int width, int height)
{
    (void)width;
    (void)height;

    draw_text(x, y, nav_labels[current_view], NC_WHITE);

    switch (current_view) {
        case VIEW_PROGRAMS:
            draw_programs_view(x, y);
            break;

        case VIEW_FILES:
            files_draw_view(x, y, width, height);
            break;
        
        case VIEW_SETTINGS:
            draw_panel(x, y + 56, 300, 92, "SETTINGS",
                       "System Settings",
                       "CONFIGURATION OPTIONS PLANNED");
            break;

        case VIEW_ABOUT:
            draw_panel(x, y + 56, 300, 112, "ABOUT NEPTUNE OS",
                       "Neptune Corporation",
                       "Build 004 Desktop Preview");
            break;

        case VIEW_MANAGER:
            if (notes_is_minimized()) {
                draw_panel(x, y + 56, 320, 92,
                           "MANAGER",
                           "Notes - Minimized",
                           "ENTER OR CLICK TO RESUME");
            } else {
                draw_panel(x, y + 56, 320, 92,
                           "MANAGER",
                           "No Minimized Applications",
                           "Minimized apps appear here");
            }
            break;

        case VIEW_LOG_OFF:
            draw_panel(x, y + 56, 320, 112, "LOG OFF",
                       "LOG OFF IS NOT IMPLEMENTED YET",
                       "Press ESC to Return");
            break;

        case VIEW_SHUT_DOWN:
            draw_panel(x, y + 56, 340, 112, "SHUT DOWN",
                       "POWER CONTROL IS NOT IMPLEMENTED",
                       "Press ESC to Return");
            break;

        default:
            break;
    }
}

static void draw_programs_view(int x, int y)
{
    int list_y = y + 56;

    /* Applications list panel */
    gfx_fill_rect(x, list_y, 340, 150, NC_WHITE);
    gfx_rect(x, list_y, 340, 150, NC_GRAY);

    /* Panel heading */
    gfx_fill_rect(x + 1, list_y + 1, 338, 28, NC_BLUE);
    draw_text(x + 10, list_y + 9, "APPLICATIONS", NC_WHITE);

    /* Notes list entry */
    gfx_fill_rect(x + 1, list_y + 30, 338, 44, NC_SILVER);

    draw_notes_icon(x + 12, list_y + 46);
    draw_text(x + 34, list_y + 44, "Notes", NC_BLACK);
    draw_text(x + 34, list_y + 58, "Text editor", NC_DARK_BLUE);
}

void desktop_init(uint32_t width, uint32_t height)
{
    screen_width = width;
    screen_height = height;
    current_view = VIEW_DASHBOARD;
    selected_item = VIEW_DASHBOARD;
    pending_action = DESKTOP_ACTION_NONE;
    initialized = (width > SIDEBAR_WIDTH + 100 && height > 180);
    if (initialized) {
        files_initialize();
    }
}

void desktop_render(void)
{
    if (!initialized) {
        return;
    }

    if (notes_is_running()) {
        notes_render();
        return;
    }

    gfx_clear(NC_DARK_BLUE);
    draw_topbar();
    draw_sidebar();

    int content_x = SIDEBAR_WIDTH + CONTENT_MARGIN;
    int content_y = TOPBAR_HEIGHT + 20;
    int content_width = (int)screen_width - content_x - CONTENT_MARGIN;
    int content_height = (int)screen_height - content_y - 16;

    if (content_width < 80 || content_height < 60) {
        gfx_present();
        return;
    }

    if (current_view == VIEW_DASHBOARD) {
        draw_dashboard(content_x, content_y,
                       content_width, content_height);
    } else {
        draw_placeholder_view(content_x, content_y,
                              content_width, content_height);
    }

    gfx_present();
}


static void activate_view(desktop_view_t view)
{
    if (view == VIEW_LOG_OFF) {
        pending_action = DESKTOP_ACTION_LOG_OFF;
        return;
    }

    if (view == VIEW_SHUT_DOWN) {
        pending_action = DESKTOP_ACTION_SHUT_DOWN;
        return;
    }

    current_view = view;
    selected_item = view;
    desktop_render();
}

desktop_action_t desktop_take_action(void)
{
    desktop_action_t action = pending_action;
    pending_action = DESKTOP_ACTION_NONE;
    return action;
}

void desktop_handle_keyboard_event(const keyboard_event_t *event)
{
    if (!initialized || event == 0 || !event->pressed) {
        return;
    }

    if (notes_is_running()) {
        notes_action_t action =
            notes_handle_keyboard_event(event);

        if (action == NOTES_ACTION_CLOSE) {
            notes_close();
            current_view = VIEW_DASHBOARD;
            selected_item = VIEW_DASHBOARD;
        } else if (action == NOTES_ACTION_MINIMIZE) {
            notes_minimize();
            current_view = VIEW_DASHBOARD;
            selected_item = VIEW_DASHBOARD;
        }

        desktop_render();
        return;
    }

    if (current_view == VIEW_FILES &&
        files_ata_detected && !files_ready) {
        if (files_format_confirmation) {
            if (event->key == KEY_ESCAPE) {
                files_format_confirmation = 0;
                files_set_status("DISK INITIALIZATION CANCELLED");
                return;
            }

            if (event->key == KEY_ENTER ||
                event->key == KEY_KP_ENTER) {
                files_format_confirmation = 0;

                fs_error_t error = fs_format(&files_ata_device);

                if (error != FS_OK) {
                    files_error = (int)error;
                    files_set_status("IDE FORMAT FAILED");
                    return;
                }

                error = fs_mount(&files_volume, &files_ata_device);

                if (error != FS_OK) {
                    files_error = (int)error;
                    files_set_status("IDE MOUNT FAILED");
                    return;
                }

                files_device = files_ata_device;
                files_using_ata = 1;
                files_ready = 1;
                files_error = 0;
                files_path_depth = 0;
                files_directory_stack[0] = 0;
                files_directory_names[0][0] = '\0';

                files_refresh_directory();
                return;
            }

            return;
        }

        if (event->key == KEY_F2) {
            files_format_confirmation = 1;
            files_set_status("ERASE DISK? ENTER=YES ESC=NO");
            return;
        }

        return;
    }

    if (current_view == VIEW_FILES) {
        if (event->key == KEY_UP) {
            files_set_status("KEYBOARD: UP RECEIVED");
        } else if (event->key == KEY_DOWN) {
            files_set_status("KEYBOARD: DOWN RECEIVED");
        } else if (event->key == KEY_DELETE) {
            files_set_status("KEYBOARD: DELETE RECEIVED");
        } else if (event->key == KEY_ENTER ||
                   event->key == KEY_KP_ENTER) {
            files_set_status("KEYBOARD: ENTER RECEIVED");
        } else if (event->key == KEY_N) {
            files_set_status("KEYBOARD: N RECEIVED");
        } else if (event->key == KEY_D) {
            files_set_status("KEYBOARD: D RECEIVED");
        }
        if (files_editor_active) {
            if (event->key == KEY_ESCAPE) {
                files_editor_active = 0;
                files_set_status("EDITOR CLOSED - UNSAVED CHANGES LOST");
            } else if (event->key == KEY_F2) {
                files_save_editor();
            } else if (event->key == KEY_BACKSPACE) {
                if (files_editor_length > 0) {
                    --files_editor_length;
                    files_editor_buffer[files_editor_length] = 0;
                    files_editor_cursor = files_editor_length;
                    files_set_status("UNSAVED CHANGES");
                }
            } else if (event->key == KEY_ENTER ||
                       event->key == KEY_KP_ENTER) {
                if (files_editor_length < FILES_TEXT_CAPACITY - 1u) {
                    files_editor_buffer[files_editor_length++] = '\n';
                    files_editor_buffer[files_editor_length] = 0;
                    files_editor_cursor = files_editor_length;
                    files_set_status("UNSAVED CHANGES");
                }
            } else if (event->character >= 32 &&
                       event->character <= 126) {
                if (files_editor_length < FILES_TEXT_CAPACITY - 1u) {
                    files_editor_buffer[files_editor_length++] =
                        event->character;
                    files_editor_buffer[files_editor_length] = 0;
                    files_editor_cursor = files_editor_length;
                    files_set_status("UNSAVED CHANGES");
                } else {
                    files_set_status("EDITOR BUFFER FULL");
                }
            }

            desktop_render();
            return;
        }

        if (files_input_mode != 0) {
            if (event->key == KEY_ESCAPE) {
                files_input_mode = 0;
                files_input_length = 0;
                files_input[0] = 0;
                files_set_status("CANCELLED");
            } else if (event->key == KEY_BACKSPACE) {
                if (files_input_length > 0) {
                    files_input[--files_input_length] = 0;
                }
            } else if (event->key == KEY_ENTER ||
                       event->key == KEY_KP_ENTER) {
                files_commit_new_entry();
            } else if (event->character >= 32 &&
                       event->character <= 126) {
                if (files_input_length < FS_NAME_MAX) {
                    files_input[files_input_length++] = event->character;
                    files_input[files_input_length] = 0;
                } else {
                    files_set_status("NAME TOO LONG");
                }
            }

            desktop_render();
            return;
        }

        if (event->key == KEY_ESCAPE) {
            current_view = VIEW_DASHBOARD;
            selected_item = VIEW_DASHBOARD;
        } else if (event->key == KEY_UP) {
            if (files_entry_count > 0 && files_selected > 0) {
                --files_selected;
            }
        } else if (event->key == KEY_DOWN) {
            if (files_entry_count > 0 &&
                files_selected + 1u < files_entry_count &&
                files_selected + 1u < FILES_MAX_ENTRIES) {
                ++files_selected;
            }
        } else if (event->key == KEY_ENTER ||
                   event->key == KEY_KP_ENTER) {
            files_open_selected();
        } else if (event->key == KEY_BACKSPACE) {
            if (files_path_depth > 0) {
                --files_path_depth;
                files_selected = 0;
                files_refresh_directory();
            } else {
                files_set_status("ALREADY AT ROOT");
            }
        } else if (event->key == KEY_DELETE) {
            files_delete_selected();
        } else if (event->key == KEY_N) {
            files_input_mode = 1;
            files_input_length = 0;
            files_input[0] = 0;
            files_set_status("ENTER A FILE NAME");
        } else if (event->key == KEY_D) {
            files_input_mode = 2;
            files_input_length = 0;
            files_input[0] = 0;
            files_set_status("ENTER A FOLDER NAME");
        }

        desktop_render();
        return;
    }

    if (event->key == KEY_ESCAPE) {
        current_view = VIEW_DASHBOARD;
        selected_item = VIEW_DASHBOARD;
        desktop_render();
        return;
    }

    if (event->key == KEY_UP) {
        selected_item--;

        if (selected_item < 0) {
            selected_item = VIEW_COUNT - 1;
        }

        current_view = (desktop_view_t)selected_item;
        desktop_render();
        return;
    }

    if (event->key == KEY_DOWN) {
        selected_item++;

        if (selected_item >= VIEW_COUNT) {
            selected_item = 0;
        }

        current_view = (desktop_view_t)selected_item;
        desktop_render();
        return;
    }

    if (event->key == KEY_ENTER ||
        event->key == KEY_KP_ENTER) {

        if (selected_item == VIEW_PROGRAMS) {
            current_view = VIEW_PROGRAMS;
            notes_launch();
            desktop_render();
            return;
        }

        if (selected_item == VIEW_MANAGER &&
            notes_is_minimized()) {
            notes_resume();
            desktop_render();
            return;
        }

        activate_view((desktop_view_t)selected_item);
    }
}

void desktop_handle_mouse_state(const mouse_state_t *state)
{
    if (!initialized || state == 0) {
        return;
    }

    /* While Notes is open, mouse input belongs to Notes. */
    if (notes_is_running()) {
        notes_action_t action =
            notes_handle_mouse_state(state);

        if (action == NOTES_ACTION_MINIMIZE) {
            notes_minimize();
            current_view = VIEW_DASHBOARD;
            selected_item = VIEW_DASHBOARD;
            desktop_render();
        } else if (action == NOTES_ACTION_CLOSE) {
            notes_close();
            current_view = VIEW_DASHBOARD;
            selected_item = VIEW_DASHBOARD;
            desktop_render();
        }

        return;
    }

    if (!state->buttons_changed || !state->left_button) {
        return;
    }

    int x = state->x;
    int y = state->y;

    /* Sidebar navigation. */
    if (x >= 8 && x < SIDEBAR_WIDTH - 8) {
        for (int i = 0; i < VIEW_COUNT; i++) {
            int item_y = NAV_START_Y + i * NAV_ITEM_HEIGHT;

            if (y >= item_y && y < item_y + NAV_ITEM_HEIGHT) {
                selected_item = i;
                activate_view((desktop_view_t)i);
                return;
            }
        }

        return;
    }

    /* Main desktop content area. */
    int content_x = SIDEBAR_WIDTH + CONTENT_MARGIN;
    int content_y = TOPBAR_HEIGHT + 20;

    if (current_view == VIEW_PROGRAMS) {
        int item_x = content_x;
        int item_y = content_y + 56 + 30;

        if (x >= item_x && x < item_x + 340 &&
            y >= item_y && y < item_y + 44) {
            notes_launch();
            notes_render();
            return;
        }
    }

    if (current_view == VIEW_MANAGER &&
        notes_is_minimized()) {
        int tile_x = content_x;
        int tile_y = content_y + 56;

        if (x >= tile_x && x < tile_x + 320 &&
            y >= tile_y && y < tile_y + 92) {
            notes_resume();
            desktop_render();
            return;
        }
    }
}
