#include "notes.h"
#include "graphics.h"

#include <stddef.h>
#include <stdint.h>

#define NOTES_TITLEBAR_HEIGHT 36
#define NOTES_BUFFER_CAPACITY 4096u
#define NOTES_TEXT_X 12
#define NOTES_TEXT_Y 56
#define NOTES_LINE_HEIGHT 12
#define NOTES_CHAR_WIDTH 8
#define NOTES_BUTTON_WIDTH 28
#define NOTES_BUTTON_HEIGHT 24
#define NOTES_BUTTON_GAP 4

static char notes_buffer[NOTES_BUFFER_CAPACITY];
static size_t notes_length;
static size_t notes_cursor;

static bool notes_running;
static bool notes_minimized_state;

static uint32_t notes_width;
static uint32_t notes_height;

static size_t notes_line_start(size_t position)
{
    while (position > 0 && notes_buffer[position - 1] != '\n') {
        --position;
    }

    return position;
}

static size_t notes_line_end(size_t position)
{
    while (position < notes_length &&
           notes_buffer[position] != '\n') {
        ++position;
    }

    return position;
}

static size_t notes_line_column(size_t position)
{
    return position - notes_line_start(position);
}

static size_t notes_previous_line_position(size_t position)
{
    size_t current_start = notes_line_start(position);

    if (current_start == 0) {
        return position;
    }

    size_t previous_end = current_start - 1;
    size_t previous_start = notes_line_start(previous_end);
    size_t column = position - current_start;
    size_t previous_length = previous_end - previous_start;

    if (column > previous_length) {
        column = previous_length;
    }

    return previous_start + column;
}

static size_t notes_next_line_position(size_t position)
{
    size_t current_start = notes_line_start(position);
    size_t current_column = position - current_start;
    size_t current_end = notes_line_end(position);

    if (current_end >= notes_length) {
        return position;
    }

    size_t next_start = current_end + 1;
    size_t next_end = notes_line_end(next_start);
    size_t next_length = next_end - next_start;

    if (current_column > next_length) {
        current_column = next_length;
    }

    return next_start + current_column;
}

static void notes_insert(char character)
{
    if (notes_length >= NOTES_BUFFER_CAPACITY - 1u) {
        return;
    }

    for (size_t i = notes_length; i > notes_cursor; --i) {
        notes_buffer[i] = notes_buffer[i - 1];
    }

    notes_buffer[notes_cursor] = character;
    ++notes_length;
    ++notes_cursor;
    notes_buffer[notes_length] = '\0';
}

static void notes_backspace(void)
{
    if (notes_cursor == 0) {
        return;
    }

    for (size_t i = notes_cursor - 1; i < notes_length - 1; ++i) {
        notes_buffer[i] = notes_buffer[i + 1];
    }

    --notes_cursor;
    --notes_length;
    notes_buffer[notes_length] = '\0';
}

static void notes_delete(void)
{
    if (notes_cursor >= notes_length) {
        return;
    }

    for (size_t i = notes_cursor; i < notes_length; ++i) {
        notes_buffer[i] = notes_buffer[i + 1];
    }

    --notes_length;
}

void notes_launch(void)
{
    notes_buffer[0] = '\0';
    notes_length = 0;
    notes_cursor = 0;

    notes_running = true;
    notes_minimized_state = false;

    notes_width = (uint32_t)gfx_width();
    notes_height = (uint32_t)gfx_height();
}

void notes_resume(void)
{
    if (notes_minimized_state) {
        notes_minimized_state = false;
        notes_running = true;
    }
}

void notes_minimize(void)
{
    if (notes_running) {
        notes_running = false;
        notes_minimized_state = true;
    }
}

void notes_close(void)
{
    notes_buffer[0] = '\0';
    notes_length = 0;
    notes_cursor = 0;

    notes_running = false;
    notes_minimized_state = false;
}

bool notes_is_running(void)
{
    return notes_running;
}

bool notes_is_minimized(void)
{
    return notes_minimized_state;
}

void notes_render(void)
{
    if (!notes_running) {
        return;
    }

    notes_width = (uint32_t)gfx_width();
    notes_height = (uint32_t)gfx_height();

    gfx_clear(NC_WHITE);

    /* Full-width application title bar. */
    gfx_fill_rect(
        0, 0, notes_width, NOTES_TITLEBAR_HEIGHT, NC_SILVER
    );
    gfx_hline(0, NOTES_TITLEBAR_HEIGHT - 1,
              notes_width, NC_GRAY);

    gfx_text(10, 13, "NOTES", NC_BLACK, 3);

    /* Minimize control: grey button with a minus. */
    int minimize_x =
        (int)notes_width - 2 * NOTES_BUTTON_WIDTH -
        NOTES_BUTTON_GAP - 6;

    int button_y = 6;

    gfx_fill_rect(
        minimize_x, button_y,
        NOTES_BUTTON_WIDTH, NOTES_BUTTON_HEIGHT, NC_GRAY
    );
    gfx_rect(
        minimize_x, button_y,
        NOTES_BUTTON_WIDTH, NOTES_BUTTON_HEIGHT, NC_WHITE
    );
    gfx_hline(
        minimize_x + 9, button_y + 15, 10, NC_WHITE
    );

    /* Close control: red button with an X. */
    int close_x = (int)notes_width - NOTES_BUTTON_WIDTH - 6;

    gfx_fill_rect(
        close_x, button_y,
        NOTES_BUTTON_WIDTH, NOTES_BUTTON_HEIGHT, NC_RED
    );
    gfx_rect(
        close_x, button_y,
        NOTES_BUTTON_WIDTH, NOTES_BUTTON_HEIGHT, NC_WHITE
    );

    for (int i = 0; i < 7; ++i) {
        gfx_pixel(close_x + 10 + i, button_y + 7 + i, NC_WHITE);
        gfx_pixel(close_x + 16 - i, button_y + 7 + i, NC_WHITE);
    }

    /* Editing area. */
    gfx_fill_rect(
        0, NOTES_TITLEBAR_HEIGHT,
        notes_width, notes_height - NOTES_TITLEBAR_HEIGHT,
        NC_WHITE
    );

    gfx_text(
        NOTES_TEXT_X, NOTES_TEXT_Y - 16,
        "UNTITLED NOTE", NC_DARK_BLUE, 2
    );

    int available_width = (int)notes_width - NOTES_TEXT_X * 2;
    int columns = available_width / NOTES_CHAR_WIDTH;

    if (columns < 1) {
        columns = 1;
    }

    int visible_rows =
        ((int)notes_height - NOTES_TEXT_Y - 12) / NOTES_LINE_HEIGHT;

    if (visible_rows < 1) {
        visible_rows = 1;
    }

    size_t cursor_row = 0;
    size_t cursor_column = 0;
    size_t row = 0;
    size_t column = 0;

    for (size_t i = 0; i < notes_cursor; ++i) {
        if (notes_buffer[i] == '\n' || column >= (size_t)columns) {
            ++row;
            column = 0;

            if (notes_buffer[i] != '\n') {
                ++column;
            }
        } else {
            ++column;
        }
    }

    cursor_row = row;
    cursor_column = column;

    size_t first_visible_row = 0;

    if (cursor_row >= (size_t)visible_rows) {
        first_visible_row = cursor_row - (size_t)visible_rows + 1u;
    }

    row = 0;
    column = 0;

    for (size_t i = 0; i < notes_length; ++i) {
        if (notes_buffer[i] == '\n' ||
            column >= (size_t)columns) {
            ++row;
            column = 0;

            if (notes_buffer[i] == '\n') {
                continue;
            }
        }

        if (row >= first_visible_row &&
            row < first_visible_row + (size_t)visible_rows) {
            char character[2] = { notes_buffer[i], '\0' };

            gfx_text(
                NOTES_TEXT_X + (int)(column * NOTES_CHAR_WIDTH),
                NOTES_TEXT_Y +
                    (int)(row - first_visible_row) * NOTES_LINE_HEIGHT,
                character, NC_BLACK, 1
            );
        }

        ++column;
    }

    /* Draw the insertion cursor. */
    if (cursor_row >= first_visible_row &&
        cursor_row < first_visible_row + (size_t)visible_rows) {
        int cursor_x =
            NOTES_TEXT_X + (int)(cursor_column * NOTES_CHAR_WIDTH);
        int cursor_y =
            NOTES_TEXT_Y +
            (int)(cursor_row - first_visible_row) * NOTES_LINE_HEIGHT;

        gfx_vline(cursor_x, cursor_y, 9, NC_BLUE);
    }

    gfx_fill_rect(
        0, (int)notes_height - 16,
        notes_width, 16, NC_SILVER
    );

    gfx_text(
        8, (int)notes_height - 12,
        "NOTES - UNSAVED SESSION", NC_BLACK, 3
    );

    gfx_present();
}

notes_action_t notes_handle_keyboard_event(
    const keyboard_event_t *event
)
{
    if (!notes_running || event == NULL || !event->pressed) {
        return NOTES_ACTION_NONE;
    }

    if (event->key == KEY_ESCAPE) {
        return NOTES_ACTION_CLOSE;
    }

    if (event->key == KEY_LEFT) {
        if (notes_cursor > 0) {
            --notes_cursor;
        }
    } else if (event->key == KEY_RIGHT) {
        if (notes_cursor < notes_length) {
            ++notes_cursor;
        }
    } else if (event->key == KEY_UP) {
        notes_cursor = notes_previous_line_position(notes_cursor);
    } else if (event->key == KEY_DOWN) {
        notes_cursor = notes_next_line_position(notes_cursor);
    } else if (event->key == KEY_BACKSPACE) {
        notes_backspace();
    } else if (event->key == KEY_DELETE) {
        notes_delete();
    } else if (event->key == KEY_ENTER ||
               event->key == KEY_KP_ENTER) {
        notes_insert('\n');
    } else if (event->character >= 32 &&
               event->character <= 126) {
        notes_insert(event->character);
    }

    return NOTES_ACTION_NONE;
}

notes_action_t notes_handle_mouse_state(const mouse_state_t *state)
{
    if (!notes_running || state == NULL ||
        !state->buttons_changed || !state->left_button) {
        return NOTES_ACTION_NONE;
    }

    int x = state->x;
    int y = state->y;

    int minimize_x =
        (int)notes_width - 2 * NOTES_BUTTON_WIDTH -
        NOTES_BUTTON_GAP - 6;

    int close_x = (int)notes_width - NOTES_BUTTON_WIDTH - 6;

    if (y >= 6 && y < 6 + NOTES_BUTTON_HEIGHT) {
        if (x >= minimize_x &&
            x < minimize_x + NOTES_BUTTON_WIDTH) {
            return NOTES_ACTION_MINIMIZE;
        }

        if (x >= close_x &&
            x < close_x + NOTES_BUTTON_WIDTH) {
            return NOTES_ACTION_CLOSE;
        }
    }

    return NOTES_ACTION_NONE;
}
