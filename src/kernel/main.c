
#include <stdint.h>
#include <limine.h>

#include "graphics.h"
#include "keyboard.h"
#include "exceptions.h"
#include "mouse.h"
#include "desktop.h"
#include "lockscreen.h"
#include "power.h"
#include "audio.h"

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_kernel_address_request
kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

#define CURSOR_WIDTH 8
#define CURSOR_HEIGHT 12

typedef enum {
    SCREEN_LOCK = 0,
    SCREEN_DESKTOP,
    SCREEN_POWER
} active_screen_t;

static active_screen_t active_screen = SCREEN_LOCK;

static uint32_t cursor_background[CURSOR_HEIGHT][CURSOR_WIDTH];

static int32_t cursor_x;
static int32_t cursor_y;
static uint8_t cursor_drawn;

static const uint8_t cursor_bitmap[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {1,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0},
    {1,2,2,1,0,0,0,0},
    {1,2,2,2,1,0,0,0},
    {1,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,1},
    {1,2,2,2,1,1,0,0},
    {1,2,1,1,2,1,0,0},
    {1,1,0,0,1,2,1,0},
    {0,0,0,0,0,1,1,0}
};

static void halt(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void restore_mouse_cursor(void)
{
    if (!cursor_drawn) {
        return;
    }

    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            gfx_pixel(
                cursor_x + col,
                cursor_y + row,
                cursor_background[row][col]
            );
        }
    }

    cursor_drawn = 0;
}

static void draw_mouse_cursor(int32_t x, int32_t y)
{
    cursor_x = x;
    cursor_y = y;

    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            cursor_background[row][col] =
                gfx_get_pixel(x + col, y + row);
        }
    }

    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            if (cursor_bitmap[row][col] == 1) {
                gfx_pixel(x + col, y + row, NC_BLACK);
            } else if (cursor_bitmap[row][col] == 2) {
                gfx_pixel(x + col, y + row, NC_WHITE);
            }
        }
    }

    cursor_drawn = 1;
}

static void move_mouse_cursor(int32_t new_x, int32_t new_y)
{
    if (cursor_drawn &&
        cursor_x == new_x &&
        cursor_y == new_y) {
        return;
    }

    if (!cursor_drawn) {
        draw_mouse_cursor(new_x, new_y);

        gfx_present_rect(
            new_x, new_y,
            CURSOR_WIDTH, CURSOR_HEIGHT
        );
        return;
    }

    int32_t old_x = cursor_x;
    int32_t old_y = cursor_y;

    restore_mouse_cursor();
    draw_mouse_cursor(new_x, new_y);

    gfx_present_rect(
        old_x, old_y,
        CURSOR_WIDTH, CURSOR_HEIGHT
    );

    gfx_present_rect(
        new_x, new_y,
        CURSOR_WIDTH, CURSOR_HEIGHT
    );
}

static void render_active_screen(void)
{
    switch (active_screen) {
        case SCREEN_LOCK:
            lockscreen_render();
            break;

        case SCREEN_DESKTOP:
            desktop_render();
            break;

        case SCREEN_POWER:
            power_render();
            break;
    }
}

static void refresh_active_screen(
    const mouse_state_t *mouse,
    uint8_t mouse_available
) {
    /*
     * Each screen redraw replaces the pixels underneath
     * the cursor. Discard the previous saved background.
     */
    cursor_drawn = 0;

    render_active_screen();

    if (mouse_available && mouse != 0) {
        draw_mouse_cursor(mouse->x, mouse->y);

        gfx_present_rect(
            mouse->x, mouse->y,
            CURSOR_WIDTH, CURSOR_HEIGHT
        );
    }
}

static void enter_lock_screen(void)
{
    lockscreen_init(
        (uint32_t)gfx_width(),
        (uint32_t)gfx_height()
    );

    active_screen = SCREEN_LOCK;
}

static void enter_desktop(void)
{
    desktop_init(
        (uint32_t)gfx_width(),
        (uint32_t)gfx_height()
    );

    active_screen = SCREEN_DESKTOP;
}

static void enter_power_screen(void)
{
    power_init(
        (uint32_t)gfx_width(),
        (uint32_t)gfx_height()
    );

    active_screen = SCREEN_POWER;
}

static void handle_desktop_action(void)
{
    desktop_action_t action = desktop_take_action();

    if (action == DESKTOP_ACTION_LOG_OFF) {
        enter_lock_screen();
    } else if (action == DESKTOP_ACTION_SHUT_DOWN) {
        enter_power_screen();
    }
}

static void handle_power_action(power_action_t action)
{
    if (action == POWER_ACTION_CANCEL) {
        enter_desktop();
    } else if (action == POWER_ACTION_SHUTDOWN ||
               action == POWER_ACTION_RESTART) {
        power_start(action);

        /* power_start should never return. */
        halt();
    }
}

void _start(void)
{
    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        halt();
    }

    struct limine_framebuffer_response *response =
        framebuffer_request.response;

    if (response == 0 || response->framebuffer_count == 0) {
        halt();
    }

    if (!gfx_init(response->framebuffers[0])) {
        halt();
    }

    /* Preserve the existing exception and input initialization. */
    exceptions_init();
    keyboard_init();

    uint8_t mouse_available =
        mouse_init(gfx_width(), gfx_height());

    if (kernel_address_request.response != NULL) {
        audio_init(
            kernel_address_request.response->physical_base,
            kernel_address_request.response->virtual_base
        );
    }

    /* Initialize the screens, but start at the lock screen. */
    lockscreen_init(
        (uint32_t)gfx_width(),
        (uint32_t)gfx_height()
    );

    desktop_init(
        (uint32_t)gfx_width(),
        (uint32_t)gfx_height()
    );

    power_init(
        (uint32_t)gfx_width(),
        (uint32_t)gfx_height()
    );

    mouse_state_t mouse = {0};
    mouse_get_state(&mouse);

    active_screen = SCREEN_LOCK;
    refresh_active_screen(&mouse, mouse_available);

    for (;;) {
        keyboard_poll();
        mouse_get_state(&mouse);

        uint8_t refresh = 0;
        uint8_t cursor_moved = mouse.moved;

        keyboard_event_t event;

        while (keyboard_read_event(&event)) {
            if (!event.pressed) {
                continue;
            }

            switch (active_screen) {
                case SCREEN_LOCK:
                    if (lockscreen_handle_keyboard_event(&event)) {
                        enter_desktop();
                    }

                    refresh = 1;
                    break;

                case SCREEN_DESKTOP:
                    desktop_handle_keyboard_event(&event);
                    handle_desktop_action();

                    /*
                     * Only redraw if the active screen changed,
                     * or for keys that affect desktop navigation.
                     */
                    if (active_screen != SCREEN_DESKTOP ||
                        event.key == KEY_ESCAPE ||
                        event.key == KEY_UP ||
                        event.key == KEY_DOWN ||
                        event.key == KEY_ENTER ||
                        event.key == KEY_KP_ENTER) {
                        refresh = 1;
                    }
                    break;

                case SCREEN_POWER: {
                    power_action_t action =
                        power_handle_keyboard_event(&event);

                    if (action != POWER_ACTION_NONE) {
                        handle_power_action(action);
                        refresh = 1;
                    } else if (event.key == KEY_LEFT ||
                               event.key == KEY_RIGHT ||
                               event.key == KEY_UP ||
                               event.key == KEY_DOWN) {
                        refresh = 1;
                    }
                    break;
                }
            }
        }

        if (mouse_available && mouse.buttons_changed) {
            switch (active_screen) {
                case SCREEN_LOCK:
                    if (lockscreen_handle_mouse_state(&mouse)) {
                        enter_desktop();
                    }
                    refresh = 1;
                    break;

                case SCREEN_DESKTOP:
                    desktop_handle_mouse_state(&mouse);
                    handle_desktop_action();
                    refresh = 1;
                    break;

                case SCREEN_POWER: {
                    power_action_t action =
                        power_handle_mouse_state(&mouse);

                    if (action != POWER_ACTION_NONE) {
                        handle_power_action(action);
                    }

                    refresh = 1;
                    break;
                }
            }
        }

        if (refresh) {
            refresh_active_screen(&mouse, mouse_available);
        } else if (mouse_available && cursor_moved) {
            move_mouse_cursor(mouse.x, mouse.y);
        }

        __asm__ volatile ("pause");
    }
}
