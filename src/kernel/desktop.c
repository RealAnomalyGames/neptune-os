#include "desktop.h"
#include "graphics.h"

#define TOPBAR_HEIGHT 40
#define SIDEBAR_WIDTH 184
#define NAV_START_Y 66
#define NAV_ITEM_HEIGHT 38
#define CONTENT_MARGIN 22

typedef enum {
    VIEW_DASHBOARD = 0,
    VIEW_PROGRAMS,
    VIEW_DOCUMENTS,
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

static void draw_sidebar(void)
{
    gfx_fill_rect(0, TOPBAR_HEIGHT, SIDEBAR_WIDTH,
                  (int)screen_height - TOPBAR_HEIGHT, NC_BLACK);

    draw_text(16, 48, "Workspace", NC_GRAY);
  
    for (int i = 0; i < 8; i++) {
        int item_y = NAV_START_Y + i * NAV_ITEM_HEIGHT;

        if (current_view == i) {
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

static void draw_placeholder_view(int x, int y, int width, int height)
{
    (void)width;
    (void)height;

    draw_text(x, y, nav_labels[current_view], NC_WHITE);
    draw_text(x, y + 28, "THIS AREA IS UNDER DEVELOPMENT", NC_SILVER);

    switch (current_view) {
        case VIEW_PROGRAMS:
            draw_panel(x, y + 56, 300, 92, "PROGRAMS",
                       "No Applications Installed",
                       "APPLICATION SUPPORT PLANNED");
            break;

        case VIEW_DOCUMENTS:
            draw_panel(x, y + 56, 300, 92, "DOCUMENTS",
                       "No Documents Available",
                       "FILE MANAGER SUPPORT PLANNED");
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
            draw_panel(x, y + 56, 320, 92, "OPEN APPLICATIONS",
                       "NEPTUNE DESKTOP",
                       "No Other Applications Open");
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

void desktop_init(uint32_t width, uint32_t height)
{
    screen_width = width;
    screen_height = height;
    current_view = VIEW_DASHBOARD;
    selected_item = VIEW_DASHBOARD;
    initialized = (width > SIDEBAR_WIDTH + 100 && height > 180);
}

void desktop_render(void)
{
    if (!initialized) {
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

void desktop_handle_keyboard_event(const keyboard_event_t *event)
{
    if (!initialized || event == 0 || !event->pressed) {
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

    if (event->key == KEY_ENTER || event->key == KEY_KP_ENTER) {
        current_view = (desktop_view_t)selected_item;
        desktop_render();
    }
}

void desktop_handle_mouse_state(const mouse_state_t *state)
{
    if (!initialized || state == 0) {
        return;
    }

    if (!state->buttons_changed || !state->left_button) {
        return;
    }

    int x = state->x;
    int y = state->y;

    if (x < 8 || x >= SIDEBAR_WIDTH - 8) {
        return;
    }

    for (int i = 0; i < VIEW_COUNT; i++) {
        int item_y = NAV_START_Y + i * NAV_ITEM_HEIGHT;

        if (y >= item_y && y < item_y + NAV_ITEM_HEIGHT) {
            current_view = (desktop_view_t)i;
            selected_item = i;
            desktop_render();
            return;
        }
    }
}
