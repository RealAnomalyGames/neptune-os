#include "keyboard.h"
#include "mouse.h"

#define PS2_DATA_PORT   0x60
#define PS2_STATUS_PORT 0x64
#define PS2_OUTPUT_FULL 0x01

#define KEYBOARD_QUEUE_SIZE 64
#define KEYBOARD_QUEUE_MASK (KEYBOARD_QUEUE_SIZE - 1)

static keyboard_event_t event_queue[KEYBOARD_QUEUE_SIZE];
static volatile uint32_t queue_read;
static volatile uint32_t queue_write;

static uint8_t extended_prefix;

static uint8_t left_shift_down;
static uint8_t right_shift_down;
static uint8_t left_ctrl_down;
static uint8_t right_ctrl_down;
static uint8_t left_alt_down;
static uint8_t right_alt_down;
static uint8_t caps_lock_on;

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );
    return value;
}

static int queue_push(keyboard_event_t event)
{
    uint32_t next = (queue_write + 1) & KEYBOARD_QUEUE_MASK;

    if (next == queue_read) {
        return 0;
    }

    event_queue[queue_write] = event;
    queue_write = next;
    return 1;
}

int keyboard_read_event(keyboard_event_t *event)
{
    if (event == 0 || queue_read == queue_write) {
        return 0;
    }

    *event = event_queue[queue_read];
    queue_read = (queue_read + 1) & KEYBOARD_QUEUE_MASK;
    return 1;
}

static key_code_t translate_scan_code(uint8_t code, uint8_t extended)
{
    if (extended) {
        switch (code) {
            case 0x1C: return KEY_KP_ENTER;
            case 0x1D: return KEY_RIGHT_CTRL;
            case 0x35: return KEY_KP_DIVIDE;
            case 0x38: return KEY_RIGHT_ALT;
            case 0x47: return KEY_HOME;
            case 0x48: return KEY_UP;
            case 0x49: return KEY_PAGE_UP;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x4F: return KEY_END;
            case 0x50: return KEY_DOWN;
            case 0x51: return KEY_PAGE_DOWN;
            case 0x52: return KEY_INSERT;
            case 0x53: return KEY_DELETE;
            case 0x5B: return KEY_LEFT_GUI;
            case 0x5C: return KEY_RIGHT_GUI;
            case 0x5D: return KEY_MENU;
            default:   return KEY_UNKNOWN;
        }
    }

    switch (code) {
        case 0x01: return KEY_ESCAPE;
        case 0x02: return KEY_1;
        case 0x03: return KEY_2;
        case 0x04: return KEY_3;
        case 0x05: return KEY_4;
        case 0x06: return KEY_5;
        case 0x07: return KEY_6;
        case 0x08: return KEY_7;
        case 0x09: return KEY_8;
        case 0x0A: return KEY_9;
        case 0x0B: return KEY_0;
        case 0x0C: return KEY_MINUS;
        case 0x0D: return KEY_EQUALS;
        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;

        case 0x10: return KEY_Q;
        case 0x11: return KEY_W;
        case 0x12: return KEY_E;
        case 0x13: return KEY_R;
        case 0x14: return KEY_T;
        case 0x15: return KEY_Y;
        case 0x16: return KEY_U;
        case 0x17: return KEY_I;
        case 0x18: return KEY_O;
        case 0x19: return KEY_P;
        case 0x1A: return KEY_LEFT_BRACKET;
        case 0x1B: return KEY_RIGHT_BRACKET;
        case 0x1C: return KEY_ENTER;
        case 0x1D: return KEY_LEFT_CTRL;

        case 0x1E: return KEY_A;
        case 0x1F: return KEY_S;
        case 0x20: return KEY_D;
        case 0x21: return KEY_F;
        case 0x22: return KEY_G;
        case 0x23: return KEY_H;
        case 0x24: return KEY_J;
        case 0x25: return KEY_K;
        case 0x26: return KEY_L;
        case 0x27: return KEY_SEMICOLON;
        case 0x28: return KEY_APOSTROPHE;
        case 0x29: return KEY_GRAVE;
        case 0x2A: return KEY_LEFT_SHIFT;
        case 0x2B: return KEY_BACKSLASH;

        case 0x2C: return KEY_Z;
        case 0x2D: return KEY_X;
        case 0x2E: return KEY_C;
        case 0x2F: return KEY_V;
        case 0x30: return KEY_B;
        case 0x31: return KEY_N;
        case 0x32: return KEY_M;
        case 0x33: return KEY_COMMA;
        case 0x34: return KEY_PERIOD;
        case 0x35: return KEY_SLASH;
        case 0x36: return KEY_RIGHT_SHIFT;
        case 0x37: return KEY_KP_MULTIPLY;
        case 0x38: return KEY_LEFT_ALT;
        case 0x39: return KEY_SPACE;
        case 0x3A: return KEY_CAPS_LOCK;

        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;

        case 0x45: return KEY_NUM_LOCK;
        case 0x46: return KEY_SCROLL_LOCK;
        case 0x47: return KEY_KP_7;
        case 0x48: return KEY_KP_8;
        case 0x49: return KEY_KP_9;
        case 0x4A: return KEY_KP_MINUS;
        case 0x4B: return KEY_KP_4;
        case 0x4C: return KEY_KP_5;
        case 0x4D: return KEY_KP_6;
        case 0x4E: return KEY_KP_PLUS;
        case 0x4F: return KEY_KP_1;
        case 0x50: return KEY_KP_2;
        case 0x51: return KEY_KP_3;
        case 0x52: return KEY_KP_0;
        case 0x53: return KEY_KP_PERIOD;

        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;
        default:   return KEY_UNKNOWN;
    }
}

static char key_to_ascii(key_code_t key)
{
    int shift = left_shift_down || right_shift_down;
    int caps = caps_lock_on;

    switch (key) {
        case KEY_A: return shift != caps ? 'A' : 'a';
        case KEY_B: return shift != caps ? 'B' : 'b';
        case KEY_C: return shift != caps ? 'C' : 'c';
        case KEY_D: return shift != caps ? 'D' : 'd';
        case KEY_E: return shift != caps ? 'E' : 'e';
        case KEY_F: return shift != caps ? 'F' : 'f';
        case KEY_G: return shift != caps ? 'G' : 'g';
        case KEY_H: return shift != caps ? 'H' : 'h';
        case KEY_I: return shift != caps ? 'I' : 'i';
        case KEY_J: return shift != caps ? 'J' : 'j';
        case KEY_K: return shift != caps ? 'K' : 'k';
        case KEY_L: return shift != caps ? 'L' : 'l';
        case KEY_M: return shift != caps ? 'M' : 'm';
        case KEY_N: return shift != caps ? 'N' : 'n';
        case KEY_O: return shift != caps ? 'O' : 'o';
        case KEY_P: return shift != caps ? 'P' : 'p';
        case KEY_Q: return shift != caps ? 'Q' : 'q';
        case KEY_R: return shift != caps ? 'R' : 'r';
        case KEY_S: return shift != caps ? 'S' : 's';
        case KEY_T: return shift != caps ? 'T' : 't';
        case KEY_U: return shift != caps ? 'U' : 'u';
        case KEY_V: return shift != caps ? 'V' : 'v';
        case KEY_W: return shift != caps ? 'W' : 'w';
        case KEY_X: return shift != caps ? 'X' : 'x';
        case KEY_Y: return shift != caps ? 'Y' : 'y';
        case KEY_Z: return shift != caps ? 'Z' : 'z';
        default: break;
        }
    
    if (key >= KEY_1 && key <= KEY_0) {
        static const char normal[]  = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        unsigned int index = (unsigned int)(key - KEY_1);
        return shift ? shifted[index] : normal[index];
    }
    
    switch (key) {
        case KEY_SPACE:          return ' ';
        case KEY_TAB:            return '\t';
        case KEY_ENTER:
        case KEY_KP_ENTER:       return '\n';
        case KEY_BACKSPACE:      return '\b';
        case KEY_MINUS:          return shift ? '_' : '-';
        case KEY_EQUALS:         return shift ? '+' : '=';
        case KEY_LEFT_BRACKET:   return shift ? '{' : '[';
        case KEY_RIGHT_BRACKET:  return shift ? '}' : ']';
        case KEY_SEMICOLON:      return shift ? ':' : ';';
        case KEY_APOSTROPHE:     return shift ? '"' : '\'';
        case KEY_GRAVE:          return shift ? '~' : '`';
        case KEY_BACKSLASH:      return shift ? '|' : '\\';
        case KEY_COMMA:          return shift ? '<' : ',';
        case KEY_SLASH:          return shift ? '?' : '/';
        case KEY_KP_MULTIPLY:    return '*';
        case KEY_KP_MINUS:       return '-';
        case KEY_KP_PLUS:        return '+';
        case KEY_KP_DIVIDE:      return '/';
        default:                 return '\0';
    }
}

void keyboard_process_scancode(uint8_t scan)
{
    /* Extended scan-code prefix. */
    if (scan == 0xE0) {
        extended_prefix = 1;
        return;
    }

    /* Ignore the multi-byte Pause/Break sequence. */
    if (scan == 0xE1) {
        extended_prefix = 0;
        return;
    }

    uint8_t extended = extended_prefix;
    extended_prefix = 0;

    uint8_t pressed = (scan & 0x80) == 0;
    uint8_t code = scan & 0x7F;

    key_code_t key = translate_scan_code(code, extended);

    if (key == KEY_UNKNOWN) {
        return;
    }

    /* Update modifier state before converting the key to ASCII. */
    switch (key) {
        case KEY_LEFT_SHIFT:
            left_shift_down = pressed;
            break;

        case KEY_RIGHT_SHIFT:
            right_shift_down = pressed;
            break;

        case KEY_LEFT_CTRL:
            left_ctrl_down = pressed;
            break;

        case KEY_RIGHT_CTRL:
            right_ctrl_down = pressed;
            break;

        case KEY_LEFT_ALT:
            left_alt_down = pressed;
            break;

        case KEY_RIGHT_ALT:
            right_alt_down = pressed;
            break;

        case KEY_CAPS_LOCK:
            if (pressed) {
                caps_lock_on = !caps_lock_on;
            }
            break;

        default:
            break;
    }

    keyboard_event_t event = {
        .key = key,
        .scan_code = code,
        .pressed = pressed,
        .extended = extended,
        .character = pressed ? key_to_ascii(key) : '\0'
    };

    (void)queue_push(event);
}

void keyboard_init(void)
{
    queue_read = 0;
    queue_write = 0;
    extended_prefix = 0;
    left_shift_down = 0;
    right_shift_down = 0;
    left_ctrl_down = 0;
    right_ctrl_down = 0;
    left_alt_down = 0;
    right_alt_down = 0;
    caps_lock_on = 0;
}

void keyboard_poll(void)
{
    for (;;) {
        uint8_t status = inb(0x64);

        if (!(status & 0x01)) {
            break;
        }

        uint8_t data = inb(0x60);

        if (status & 0x20) {
            mouse_process_byte(data);
        } else {
            keyboard_process_scancode(data);
        }
    }
}
