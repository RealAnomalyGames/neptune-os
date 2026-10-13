#include "lockscreen.h"
#include "graphics.h"

#define USERNAME_CAPACITY 32
#define PASSWORD_CAPACITY 32

#define LOGIN_PANEL_WIDTH 340
#define LOGIN_PANEL_HEIGHT 184

typedef enum {
    LOGIN_FOCUS_USERNAME = 0,
    LOGIN_FOCUS_PASSWORD
} login_focus_t;

static uint32_t screen_width;
static uint32_t screen_height;

static char username[USERNAME_CAPACITY] = "Betauser";
static char password[PASSWORD_CAPACITY] = "";

static login_focus_t focused_field = LOGIN_FOCUS_USERNAME;
static int login_failed;

static int string_equal_ignore_case(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a;
        char cb = *b;

        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - 'a' + 'A');
        }

        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - 'a' + 'A');
        }

        if (ca != cb) {
            return 0;
        }

        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static int string_length(const char *text)
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
    int width = string_length(text) * 6 * scale;
    gfx_text(center_x - width / 2, y, text, color, scale);
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

    draw_centered_text(
        center_x, 116, "WELCOME", NC_SILVER, 1
    );
}

static void draw_login_panel(void)
{
    int panel_width = LOGIN_PANEL_WIDTH;
    int panel_height = LOGIN_PANEL_HEIGHT;

    if ((int)screen_width - 32 < panel_width) {
        panel_width = (int)screen_width - 32;
    }

    int panel_x = ((int)screen_width - panel_width) / 2;
    int panel_y = (int)screen_height * 54 / 100;

    if (panel_y + panel_height > (int)screen_height - 12) {
        panel_y = (int)screen_height - panel_height - 12;
    }

    gfx_fill_rect(
        panel_x, panel_y,
        panel_width, panel_height,
        NC_WHITE
    );

    gfx_rect(
        panel_x, panel_y,
        panel_width, panel_height,
        NC_BLACK
    );

    gfx_fill_rect(
        panel_x + 1, panel_y + 1,
        panel_width - 2, 28,
        NC_BLUE
    );

    gfx_text(
        panel_x + 12, panel_y + 8,
        "SIGN IN TO NEPTUNE OS",
        NC_WHITE, 1
    );

    int label_x = panel_x + 14;
    int field_x = panel_x + 108;
    int field_width = panel_width - 124;

    gfx_text(label_x, panel_y + 45, "USERNAME", NC_BLACK, 1);

    gfx_fill_rect(
        field_x, panel_y + 38,
        field_width, 23,
        focused_field == LOGIN_FOCUS_USERNAME
            ? NC_CYAN : NC_SILVER
    );

    gfx_rect(
        field_x, panel_y + 38,
        field_width, 23,
        NC_BLACK
    );

    gfx_text(
        field_x + 5, panel_y + 46,
        username, NC_BLACK, 1
    );

    gfx_text(label_x, panel_y + 79, "PASSWORD", NC_BLACK, 1);

    gfx_fill_rect(
        field_x, panel_y + 72,
        field_width, 23,
        focused_field == LOGIN_FOCUS_PASSWORD
            ? NC_CYAN : NC_SILVER
    );

    gfx_rect(
        field_x, panel_y + 72,
        field_width, 23,
        NC_BLACK
    );

    char masked[PASSWORD_CAPACITY];
    int password_length = string_length(password);

    for (int i = 0; i < password_length; i++) {
        masked[i] = '*';
    }

    masked[password_length] = '\0';

    gfx_text(
        field_x + 5, panel_y + 80,
        masked, NC_BLACK, 1
    );

    int button_width = 100;
    int button_x = panel_x + panel_width - button_width - 14;

    gfx_fill_rect(
        button_x, panel_y + 111,
        button_width, 27, NC_BLUE
    );

    gfx_rect(
        button_x, panel_y + 111,
        button_width, 27, NC_BLACK
    );

    gfx_text(
        button_x + 24, panel_y + 121,
        "SIGN IN", NC_WHITE, 1
    );

    if (login_failed) {
        gfx_text(
            panel_x + 14, panel_y + 150,
            "INVALID LOGIN", NC_RED, 1
        );
    } else {
        gfx_text(
            panel_x + 14, panel_y + 150,
            "ENTER TO CONTINUE", NC_DARK_BLUE, 1
        );
    }
}

void lockscreen_init(uint32_t width, uint32_t height)
{
    screen_width = width;
    screen_height = height;

    username[0] = 'B';
    username[1] = 'e';
    username[2] = 't';
    username[3] = 'a';
    username[4] = 'u';
    username[5] = 's';
    username[6] = 'e';
    username[7] = 'r';
    username[8] = '\0';

    password[0] = '\0';

    focused_field = LOGIN_FOCUS_USERNAME;
    login_failed = 0;
}

void lockscreen_render(void)
{
    gfx_clear(NC_DARK_BLUE);

    draw_logo();
    draw_login_panel();

    gfx_present();
}

static int try_login(void)
{
    if (string_equal_ignore_case(username, "Betauser") &&
        password[0] == '\0') {

        /* TODO: Play the Neptune OS startup sound once audio
           drivers are available. */

        login_failed = 0;
        return 1;
    }

    login_failed = 1;
    return 0;
}

static void append_character(char *buffer, char character)
{
    int length = string_length(buffer);

    if (length >= USERNAME_CAPACITY - 1) {
        return;
    }

    if (character < 32 || character > 126) {
        return;
    }

    buffer[length] = character;
    buffer[length + 1] = '\0';
}

static void remove_character(char *buffer)
{
    int length = string_length(buffer);

    if (length > 0) {
        buffer[length - 1] = '\0';
    }
}

int lockscreen_handle_keyboard_event(const keyboard_event_t *event)
{
    if (event == 0 || !event->pressed) {
        return 0;
    }

    if (event->key == KEY_TAB) {
        focused_field =
            focused_field == LOGIN_FOCUS_USERNAME
                ? LOGIN_FOCUS_PASSWORD
                : LOGIN_FOCUS_USERNAME;

        lockscreen_render();
        return 0;
    }

    if (event->key == KEY_ESCAPE) {
        return 0;
    }

    if (event->key == KEY_ENTER ||
        event->key == KEY_KP_ENTER) {

        if (focused_field == LOGIN_FOCUS_USERNAME) {
            focused_field = LOGIN_FOCUS_PASSWORD;
            lockscreen_render();
            return 0;
        }

        if (try_login()) {
            return 1;
        }

        lockscreen_render();
        return 0;
    }

    if (event->key == KEY_BACKSPACE) {
        if (focused_field == LOGIN_FOCUS_USERNAME) {
            remove_character(username);
        } else {
            remove_character(password);
        }

        login_failed = 0;
        lockscreen_render();
        return 0;
    }

    if (event->character >= 32 && event->character <= 126) {
        if (focused_field == LOGIN_FOCUS_USERNAME) {
            append_character(username, event->character);
        } else {
            append_character(password, event->character);
        }

        login_failed = 0;
        lockscreen_render();
    }

    return 0;
}

int lockscreen_handle_mouse_state(const mouse_state_t *state)
{
    if (state == 0 || !state->buttons_changed ||
        !state->left_button) {
        return 0;
    }

    int panel_width = LOGIN_PANEL_WIDTH;

    if ((int)screen_width - 32 < panel_width) {
        panel_width = (int)screen_width - 32;
    }

    int panel_x = ((int)screen_width - panel_width) / 2;
    int panel_y = (int)screen_height * 54 / 100;

    if (panel_y + LOGIN_PANEL_HEIGHT > (int)screen_height - 12) {
        panel_y = (int)screen_height - LOGIN_PANEL_HEIGHT - 12;
    }

    int field_x = panel_x + 108;
    int field_width = panel_width - 124;

    if (state->x >= field_x &&
        state->x < field_x + field_width) {

        if (state->y >= panel_y + 38 &&
            state->y < panel_y + 61) {
            focused_field = LOGIN_FOCUS_USERNAME;
            lockscreen_render();
            return 0;
        }

        if (state->y >= panel_y + 72 &&
            state->y < panel_y + 95) {
            focused_field = LOGIN_FOCUS_PASSWORD;
            lockscreen_render();
            return 0;
        }
    }

    int button_x = panel_x + panel_width - 114;

    if (state->x >= button_x &&
        state->x < button_x + 100 &&
        state->y >= panel_y + 111 &&
        state->y < panel_y + 138) {

        if (try_login()) {
            return 1;
        }

        lockscreen_render();
    }

    return 0;
}
