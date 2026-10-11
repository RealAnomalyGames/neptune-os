#include <stdint.h>
#include <limine.h>

#include "graphics.h"
#include "keyboard.h"
#include "exceptions.h"
#include "mouse.h"

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

#define INPUT_BUFFER_SIZE 64
#define SCREEN_BG NC_DARK_BLUE

#define CURSOR_WIDTH 8
#define CURSOR_HEIGHT 12

static uint32_t cursor_background[CURSOR_HEIGHT][CURSOR_WIDTH];
static int32_t cursor_x;
static int32_t cursor_y;
static uint8_t cursor_drawn;

static void halt(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void draw_hex_counter(
    int64_t x,
    int64_t y,
    uint64_t value
)
{
    static const char digits[] = "0123456789";
    char buffer[21];

    for (int i = 19; i >= 0; i--) {
        buffer[i] = digits[value % 10];
        value /= 10;
    }

    buffer[20] = '\0';
    gfx_text(x, y, buffer, NC_WHITE, 1);
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
    static const uint8_t cursor[CURSOR_HEIGHT][CURSOR_WIDTH] = {
        {1,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0},
        {1,2,2,1,0,0,0,0},
        {1,2,2,2,1,0,0,0},
        {1,2,2,2,2,1,0,0},
        {1,2,2,2,2,2,1,0},
        {1,2,2,2,2,2,2,1},
        {1,2,2,2,1,1,0,0},
        {1,2,1,0,1,2,1,0},
        {1,1,0,0,0,1,1,0},
        {0,0,0,0,0,0,0,0}
    };

    cursor_x = x;
    cursor_y = y;

    /* Save the pixels underneath the cursor first. */
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            cursor_background[row][col] =
                gfx_get_pixel(x + col, y + row);
        }
    }

    /* Draw the cursor over the saved background. */
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            if (cursor[row][col] == 1) {
                gfx_pixel(x + col, y + row, NC_BLACK);
            } else if (cursor[row][col] == 2) {
                gfx_pixel(x + col, y + row, NC_WHITE);
            }
        }
    }

    cursor_drawn = 1;
}

static void move_mouse_cursor(int32_t x, int32_t y)
{
    if (cursor_drawn && cursor_x == x && cursor_y == y) {
        return;
    }

    restore_mouse_cursor();
    draw_mouse_cursor(x, y);
}

static void draw_mouse_status(
    const mouse_state_t *mouse,
    uint8_t mouse_available
)
{
    uint64_t width = gfx_width();

    /* Use the right side on wide screens, below the keyboard
       test content on narrower screens. */
    int64_t panel_x = width >= 700 ? (int64_t)(width / 2) : 20;
    int64_t panel_y = width >= 700 ? 152 : 410;

    gfx_text(panel_x, panel_y, "MOUSE STATUS", NC_CYAN, 1);

    if (!mouse_available) {
        gfx_text(panel_x, panel_y + 24,
                 "MOUSE UNAVAILABLE", NC_RED, 1);
        return;
    }

    if (mouse == 0) {
        return;
    }

    gfx_text(panel_x, panel_y + 24, "X:", NC_WHITE, 1);
    draw_hex_counter(panel_x + 30, panel_y + 24,
                     (uint32_t)mouse->x);

    gfx_text(panel_x, panel_y + 46, "Y:", NC_WHITE, 1);
    draw_hex_counter(panel_x + 30, panel_y + 46,
                     (uint32_t)mouse->y);

    gfx_text(panel_x + 70, panel_y + 76,
         mouse->left_button ? "PRESSED" : "RELEASED",
         mouse->left_button ? NC_GREEN : NC_SILVER, 1);

    gfx_text(panel_x + 70, panel_y + 98,
         mouse->right_button ? "PRESSED" : "RELEASED",
         mouse->right_button ? NC_GREEN : NC_SILVER, 1);

    gfx_text(panel_x + 70, panel_y + 120,
         mouse->middle_button ? "PRESSED" : "RELEASED",
         mouse->middle_button ? NC_GREEN : NC_SILVER, 1);
}

static void draw_input_screen(
    const char *input,
    uint64_t key_presses,
    uint64_t characters,
    uint64_t backspaces,
    uint64_t enters,
    const mouse_state_t *mouse,
    uint8_t mouse_available
)
{
    uint64_t width = gfx_width();
    uint64_t height = gfx_height();

    gfx_clear(SCREEN_BG);

    /* Header */
    gfx_fill_rect(0, 0, width, 56, NC_WHITE);
    gfx_text(18, 12, "NEPTUNE OS", NC_DARK_BLUE, 3);
    gfx_text(20, 66, "BUILD 003-D", NC_WHITE, 2);
    gfx_text(20, 92, "INTERACTIVE INPUT TEST", NC_CYAN, 1);

    /* Keyboard status */
    gfx_fill_rect(20, 120, 12, 12, NC_GREEN);
    gfx_text(40, 120, "KEYBOARD STATUS: ACTIVE", NC_WHITE, 1);

    /* Input panel */
    gfx_text(20, 152, "TYPE TEXT BELOW", NC_YELLOW, 1);
    gfx_rect(18, 174, width > 36 ? width - 36 : 1,
             38, NC_WHITE);
    gfx_text(26, 186, input, NC_GREEN, 1);

    /* Instructions */
    gfx_text(20, 224, "BACKSPACE: DELETE LAST CHARACTER",
             NC_SILVER, 1);
    gfx_text(20, 244, "ENTER: CLEAR INPUT LINE",
             NC_SILVER, 1);
    gfx_text(20, 264, "MAXIMUM INPUT: 63 CHARACTERS",
             NC_SILVER, 1);

    /* Keyboard counters */
    gfx_text(20, 300, "KEY PRESSES:", NC_CYAN, 1);
    draw_hex_counter(180, 300, key_presses);

    gfx_text(20, 322, "CHARACTERS:", NC_CYAN, 1);
    draw_hex_counter(180, 322, characters);

    gfx_text(20, 344, "BACKSPACES:", NC_CYAN, 1);
    draw_hex_counter(180, 344, backspaces);

    gfx_text(20, 366, "ENTER PRESSES:", NC_CYAN, 1);
    draw_hex_counter(180, 366, enters);

    /* Mouse panel */
    draw_mouse_status(mouse, mouse_available);

    /* Footer */
    gfx_fill_rect(
        0,
        height > 30 ? height - 30 : 0,
        width,
        height > 30 ? 30 : height,
        NC_BLACK
    );

    gfx_text(12, height > 21 ? height - 21 : 0,
             "NEPTUNE OS - INPUT TEST", NC_GREEN, 1);
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

    exceptions_init();
    keyboard_init();

    uint8_t mouse_available =
        mouse_init(gfx_width(), gfx_height());

    char input_buffer[INPUT_BUFFER_SIZE] = {0};
    uint32_t input_length = 0;

    uint64_t key_presses = 0;
    uint64_t characters = 0;
    uint64_t backspaces = 0;
    uint64_t enters = 0;

    mouse_state_t mouse;
    mouse_get_state(&mouse);

    draw_input_screen(
        input_buffer,
        key_presses,
        characters,
        backspaces,
        enters,
        &mouse,
        mouse_available
    );

    for (;;) {
        keyboard_poll();

        /* Read the mouse state after the shared controller poll. */
        mouse_get_state(&mouse);

        uint8_t redraw =
            mouse.moved || mouse.buttons_changed;

        keyboard_event_t event;

        while (keyboard_read_event(&event)) {
            if (!event.pressed) {
                continue;
            }

            key_presses++;

            if (event.key == KEY_BACKSPACE) {
                backspaces++;

                if (input_length > 0) {
                    input_length--;
                    input_buffer[input_length] = '\0';
                }
            } else if (event.character == '\n') {
                enters++;
                input_length = 0;
                input_buffer[0] = '\0';
            } else if (
                event.character >= 32 &&
                event.character <= 126 &&
                input_length < INPUT_BUFFER_SIZE - 1
            ) {
                input_buffer[input_length++] = event.character;
                input_buffer[input_length] = '\0';
                characters++;
            }

            redraw = 1;
        }

        if (redraw) {
            draw_input_screen(
                input_buffer,
                key_presses,
                characters,
                backspaces,
                enters,
                &mouse,
                mouse_available
            );
        }

        __asm__ volatile ("pause");
    }
}
