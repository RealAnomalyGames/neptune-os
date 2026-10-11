#ifndef NEPTUNE_KEYBOARD_H
#define NEPTUNE_KEYBOARD_H

#include <stdint.h>

typedef enum {
    KEY_NONE = 0,

    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F,
    KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L,
    KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
    KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X,
    KEY_Y, KEY_Z,

    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
    KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,

    KEY_SPACE,
    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_ESCAPE,

    KEY_MINUS,
    KEY_EQUAL,
    KEY_LEFT_BRACKET,
    KEY_RIGHT_BRACKET,
    KEY_SEMICOLON,
    KEY_APOSTROPHE,
    KEY_GRAVE,
    KEY_BACKSLASH,
    KEY_COMMA,
    KEY_PERIOD,
    KEY_SLASH,

    KEY_CAPS_LOCK,
    KEY_LEFT_SHIFT,
    KEY_RIGHT_SHIFT,
    KEY_LEFT_CTRL,
    KEY_RIGHT_CTRL,
    KEY_LEFT_ALT,
    KEY_RIGHT_ALT,

    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,

    KEY_F1, KEY_F2, KEY_F3, KEY_F4,
    KEY_F5, KEY_F6, KEY_F7, KEY_F8,
    KEY_F9, KEY_F10, KEY_F11, KEY_F12
} key_code_t;

typedef struct {
    key_code_t key;
    uint8_t scan_code;
    uint8_t pressed;
    uint8_t extended;
    char character;
} keyboard_event_t;

void keyboard_init(void);
void keyboard_poll(void);
int keyboard_read_event(keyboard_event_t *event);

#endif
