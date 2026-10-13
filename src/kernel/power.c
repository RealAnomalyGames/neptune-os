#include "power.h"
#include "graphics.h"

#define POWER_ORANGE 0xFF8000u

static uint32_t screen_width;
static uint32_t screen_height;
static int selected_option;

static void cpu_halt_forever(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void port_out8(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static void port_out16(uint16_t port, uint16_t value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static int text_length(const char *text)
{
    int length = 0;

    while (text[length]) {
        length++;
    }

    return length;
}

static void draw_centered_text(
    int center_x,
    int y,
    const char *text,
    uint32_t color,
    int scale
) {
    int text_width = text_length(text) * 6 * scale;

    gfx_text(
        center_x - text_width / 2,
        y,
        text,
        color,
        scale
    );
}

static void draw_logo(void)
{
    int center_x = (int)screen_width / 2;

    gfx_fill_rect(center_x - 20, 34, 40, 40, NC_BLUE);
    gfx_rect(center_x - 20, 34, 40, 40, NC_WHITE);
    gfx_text(center_x - 9, 47, "N", NC_WHITE, 3);

    draw_centered_text(
        center_x, 88, "NEPTUNE OS", NC_WHITE, 2
    );
}

void power_init(uint32_t width, uint32_t height)
{
    screen_width = width;
    screen_height = height;
    selected_option = 0;
}

void power_render(void)
{
    gfx_clear(NC_DARK_BLUE);
    draw_logo();

    draw_centered_text(
        (int)screen_width / 2,
        158,
        "POWER OPTIONS",
        NC_WHITE,
        2
    );

    int button_width = 220;
    int button_height = 42;
    int gap = 18;

    int total_width = button_width * 2 + gap;
    int start_x = ((int)screen_width - total_width) / 2;
    int button_y = (int)screen_height / 2;

    for (int i = 0; i < 2; i++) {
        int button_x = start_x + i * (button_width + gap);

        uint32_t background =
            selected_option == i ? NC_BLUE : NC_GRAY;

        gfx_fill_rect(
            button_x, button_y,
            button_width, button_height,
            background
        );

        gfx_rect(
            button_x, button_y,
            button_width, button_height,
            NC_WHITE
        );
    }

    draw_centered_text(
        start_x + button_width / 2,
        button_y + 17,
        "SHUT DOWN",
        NC_WHITE,
        1
    );

    draw_centered_text(
        start_x + button_width + gap + button_width / 2,
        button_y + 17,
        "RESTART",
        NC_WHITE,
        1
    );

    draw_centered_text(
        (int)screen_width / 2,
        button_y + 65,
        "ESC TO CANCEL",
        NC_SILVER,
        1
    );

    gfx_present();
}

power_action_t power_handle_keyboard_event(
    const keyboard_event_t *event
) {
    if (event == 0 || !event->pressed) {
        return POWER_ACTION_NONE;
    }

    if (event->key == KEY_ESCAPE) {
        return POWER_ACTION_CANCEL;
    }

    if (event->key == KEY_LEFT || event->key == KEY_RIGHT ||
        event->key == KEY_UP || event->key == KEY_DOWN) {

        selected_option = selected_option == 0 ? 1 : 0;
        power_render();
        return POWER_ACTION_NONE;
    }

    if (event->key == KEY_ENTER ||
        event->key == KEY_KP_ENTER) {
        return selected_option == 0
            ? POWER_ACTION_SHUTDOWN
            : POWER_ACTION_RESTART;
    }

    return POWER_ACTION_NONE;
}

power_action_t power_handle_mouse_state(
    const mouse_state_t *state
) {
    if (state == 0 || !state->buttons_changed ||
        !state->left_button) {
        return POWER_ACTION_NONE;
    }

    int button_width = 220;
    int button_height = 42;
    int gap = 18;
    int total_width = button_width * 2 + gap;
    int start_x = ((int)screen_width - total_width) / 2;
    int button_y = (int)screen_height / 2;

    if (state->y < button_y ||
        state->y >= button_y + button_height) {
        return POWER_ACTION_NONE;
    }

    if (state->x >= start_x &&
        state->x < start_x + button_width) {
        return POWER_ACTION_SHUTDOWN;
    }

    int restart_x = start_x + button_width + gap;

    if (state->x >= restart_x &&
        state->x < restart_x + button_width) {
        return POWER_ACTION_RESTART;
    }

    return POWER_ACTION_NONE;
}

void power_start(power_action_t action)
{
    if (action != POWER_ACTION_SHUTDOWN &&
        action != POWER_ACTION_RESTART) {
        return;
    }

    gfx_clear(NC_DARK_BLUE);
    draw_logo();

    const char *status =
        action == POWER_ACTION_SHUTDOWN
            ? "SHUTTING DOWN NEPTUNE OS"
            : "RESTARTING NEPTUNE OS";

    draw_centered_text(
        (int)screen_width / 2,
        (int)screen_height * 65 / 100,
        status,
        POWER_ORANGE,
        2
    );

    gfx_present();

    /*
     * TODO: Play the Neptune OS shutdown sound when audio
     * drivers are available. Used for both shutdown and restart.
     */

    /* Brief delay to allow the status screen to be seen. */
    for (volatile uint32_t delay = 0; delay < 8000000; delay++) {
        __asm__ volatile ("pause");
    }

    if (action == POWER_ACTION_SHUTDOWN) {
        /*
         * Common emulator power-off interfaces.
         * ACPI-aware virtual machines may honor port 0x604.
         * Bochs-style environments may honor port 0xB004.
         */
        port_out16(0x0604, 0x2000);
        port_out16(0xB004, 0x2000);

        /* Do not continue running the desktop if power-off fails. */
        cpu_halt_forever();
    }

    /*
     * Request an x86 reset through the common reset-control port.
     */
    port_out8(0x0CF9, 0x06);

    /* Fallback: request reset from the keyboard controller. */
    for (volatile uint32_t delay = 0; delay < 100000; delay++) {
        __asm__ volatile ("pause");
    }

    port_out8(0x0064, 0xFE);

    /* If neither reset request works, stop safely. */
    cpu_halt_forever();
}
