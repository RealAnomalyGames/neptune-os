#ifndef NEPTUNE_KEYBOARD_H
#define NEPTUNE_KEYBOARD_H

#include <stdint.h>

typedef enum {
    KEY_UNKNOWN = 0,
    KEY_ESCAPE,
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
    KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
    KEY_MINUS, KEY_EQUALS, KEY_BACKSPACE, KEY_TAB,
    KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T,
    KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
    KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET, KEY_ENTER,
    KEY_LEFT_CTRL,
    KEY_A, KEY_S, KEY_D, KEY_F, KEY_G,
    KEY_H, KEY_J, KEY_K, KEY_L,
    KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE,
    KEY_LEFT_SHIFT, KEY_BACKSLASH,
    KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B,
    KEY_N, KEY_M, KEY_COMMA, KEY_PERIOD, KEY_SLASH,
    KEY_RIGHT_SHIFT, KEY_KP_MULTIPLY, KEY_LEFT_ALT,
    KEY_SPACE, KEY_CAPS_LOCK,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,
    KEY_NUM_LOCK, KEY_SCROLL_LOCK,
    KEY_KP_7, KEY_KP_8, KEY_KP_9, KEY_KP_MINUS,
    KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_PLUS,
    KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_0, KEY_KP_PERIOD,
    KEY_F11, KEY_F12,

    KEY_KP_ENTER,
    KEY_RIGHT_CTRL,
    KEY_KP_DIVIDE,
    KEY_RIGHT_ALT,
    KEY_HOME,
    KEY_UP,
    KEY_PAGE_UP,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_END,
    KEY_DOWN,
    KEY_PAGE_DOWN,
    KEY_INSERT,
    KEY_DELETE,
    KEY_LEFT_GUI,
    KEY_RIGHT_GUI,
    KEY_MENU
} key_code_t;

typedef struct {
    key_code_t key;
    uint8_t scan_code;
    uint8_t pressed;
    uint8_t extended;
    char character; /* Printable ASCII character, or '\0'. */
} keyboard_event_t;

/* Uses the standard PC PS/2 controller ports and Set 1 scan codes.
   The controller must already be configured for Set 1 translation. */
void keyboard_init(void);

/* Poll hardware and queue any available key events. */
void keyboard_poll(void);

/* Returns 1 when an event is available, otherwise 0. */
int keyboard_read_event(keyboard_event_t *event);

#endif
