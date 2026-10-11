#include <stdint.h>
#include <limine.h>
#include "graphics.h"
#include "keyboard.h"

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

static void halt(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void draw_test_screen(void)
{
    static const uint32_t palette[16] = {
        NC_RED, NC_GREEN, NC_BLUE, NC_YELLOW,
        NC_MAGENTA, NC_CYAN, NC_DARK_RED, NC_DARK_GREEN,
        NC_DARK_BLUE, NC_OLIVE, NC_PURPLE, NC_TEAL,
        NC_BLACK, NC_GRAY, NC_SILVER, NC_WHITE
    };

    uint64_t width = gfx_width();
    uint64_t height = gfx_height();

    gfx_clear(NC_DARK_BLUE);

    /* Header */
    gfx_fill_rect(0, 0, width, 56, NC_WHITE);
    gfx_text(18, 12, "NEPTUNE OS", NC_DARK_BLUE, 3);
    gfx_text(20, 66, "BUILD 002", NC_WHITE, 2);
    gfx_text(20, 92, "GRAPHICS ENGINE TEST", NC_CYAN, 1);

    /* Palette swatches */
    gfx_text(20, 120, "16 COLOR PALETTE", NC_WHITE, 1);

    uint64_t margin = 20;
    uint64_t gap = 5;
    uint64_t swatch_width =
        (width > margin * 2 + gap * 15)
        ? (width - margin * 2 - gap * 15) / 16
        : 1;

    for (uint64_t i = 0; i < 16; i++) {
        int64_t x = (int64_t)(margin + i * (swatch_width + gap));

        gfx_fill_rect(x, 140, swatch_width, 36, palette[i]);
        gfx_rect(x, 140, swatch_width, 36, NC_WHITE);
    }

    /* Filled rectangles and borders */
    gfx_text(20, 194, "SHAPE TEST", NC_WHITE, 1);

    gfx_fill_rect(20, 216, 100, 54, NC_RED);
    gfx_rect(20, 216, 100, 54, NC_WHITE);

    gfx_fill_rect(140, 216, 100, 54, NC_GREEN);
    gfx_rect(140, 216, 100, 54, NC_WHITE);

    gfx_fill_rect(260, 216, 100, 54, NC_BLUE);
    gfx_rect(260, 216, 100, 54, NC_WHITE);

    /* Horizontal and vertical line tests */
    gfx_text(20, 294, "LINE TEST", NC_WHITE, 1);

    gfx_hline(20, 320, width > 40 ? width - 40 : 1, NC_YELLOW);
    gfx_vline(20, 334, height > 354 ? height - 354 : 1, NC_MAGENTA);
    gfx_vline(28, 334, height > 354 ? height - 354 : 1, NC_CYAN);

    /* Footer */
    gfx_fill_rect(0, height - 30, width, 30, NC_BLACK);
    gfx_text(12, height - 21, "GRAPHICS INITIALIZED", NC_GREEN, 1);
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

    keyboard_init();
    draw_test_screen();
    
    
    char input_buffer[64] = {0};
    uint32_t input_length = 0;

    uint64_t screen_width = gfx_width();
    uint64_t screen_height = gfx_height();

    /* Keep the input interface inside a small, fixed panel. */
    if (screen_width > 40 && screen_height > 400) {
        gfx_fill_rect(10, 340, screen_width - 20, 52, NC_DARK_BLUE);
        gfx_text(20, 346, "TYPE TEXT BELOW", NC_WHITE, 1);
        gfx_text(20, 364, ">", NC_WHITE, 1);
    }

    for (;;) {
        keyboard_poll();

        keyboard_event_t event;

        while (keyboard_read_event(&event)) {
            if (!event.pressed) {
                continue;
            }

            int changed = 0;

            if (event.key == KEY_BACKSPACE) {
                if (input_length > 0) {
                    input_length--;
                    input_buffer[input_length] = '\0';
                    changed = 1;
                }
            } else if (event.character == '\n') {
                input_length = 0;
                input_buffer[0] = '\0';
                changed = 1;
            } else if (event.character >= 32 &&
                       event.character <= 126 &&
                       input_length < sizeof(input_buffer) - 1 &&
                       input_length < (screen_width > 40
                           ? (screen_width - 40) / 6
                           : 0)) {
                input_buffer[input_length] = event.character;
                input_length++;
                input_buffer[input_length] = '\0';
                changed = 1;
            }

            if (changed &&
                screen_width > 40 &&
                screen_height > 400) {
                uint64_t field_width = screen_width - 40;

                if (field_width > 384) {
                    field_width = 384;
                }

                gfx_fill_rect(
                    20, 360,
                    field_width, 16,
                    NC_DARK_BLUE
                );

                gfx_text(20, 364, ">", NC_WHITE, 1);
                gfx_text(32, 364, input_buffer, NC_GREEN, 1);
            }
        }

        __asm__ volatile ("pause");
    }
}
