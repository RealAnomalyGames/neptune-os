#include "mouse.h"

#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64
#define PS2_COMMAND_PORT    0x64

#define PS2_STATUS_OUTPUT   0x01
#define PS2_STATUS_INPUT    0x02

#define PS2_CMD_ENABLE_AUX  0xA8
#define PS2_CMD_READ_CONFIG 0x20
#define PS2_CMD_WRITE_CONFIG 0x60
#define PS2_CMD_WRITE_AUX   0xD4

#define MOUSE_CMD_DEFAULTS  0xF6
#define MOUSE_CMD_ENABLE    0xF4
#define MOUSE_ACK           0xFA

static mouse_state_t mouse_state;
static uint32_t screen_width;
static uint32_t screen_height;

static uint8_t packet[3];
static uint8_t packet_index;
static uint8_t initialized;

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0"
                      : "=a"(value)
                      : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1"
                      :
                      : "a"(value), "Nd"(port));
}

static uint8_t wait_input_empty(void)
{
    for (uint32_t i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT)) {
            return 1;
        }
        __asm__ volatile ("pause");
    }

    return 0;
}

static uint8_t wait_output_full(void)
{
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) {
            return 1;
        }
        __asm__ volatile ("pause");
    }

    return 0;
}

static uint8_t controller_read_config(uint8_t *config)
{
    if (!wait_input_empty()) {
        return 0;
    }

    outb(PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG);

    if (!wait_output_full()) {
        return 0;
    }

    *config = inb(PS2_DATA_PORT);
    return 1;
}

static uint8_t controller_write_config(uint8_t config)
{
    if (!wait_input_empty()) {
        return 0;
    }

    outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG);

    if (!wait_input_empty()) {
        return 0;
    }

    outb(PS2_DATA_PORT, config);
    return wait_input_empty();
}

static uint8_t mouse_send_command(uint8_t command)
{
    if (!wait_input_empty()) {
        return 0;
    }

    outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_AUX);

    if (!wait_input_empty()) {
        return 0;
    }

    outb(PS2_DATA_PORT, command);

    if (!wait_output_full()) {
        return 0;
    }

    return inb(PS2_DATA_PORT) == MOUSE_ACK;
}

uint8_t mouse_init(uint32_t width, uint32_t height)
{
    initialized = 0;
    packet_index = 0;

    screen_width = width;
    screen_height = height;

    if (width == 0 || height == 0) {
        return 0;
    }

    mouse_state = (mouse_state_t){0};
    mouse_state.x = (int32_t)(width / 2);
    mouse_state.y = (int32_t)(height / 2);

    /* Enable the auxiliary PS/2 port. */
    if (!wait_input_empty()) {
        return 0;
    }

    outb(PS2_COMMAND_PORT, PS2_CMD_ENABLE_AUX);

    uint8_t config;

    if (!controller_read_config(&config)) {
        return 0;
    }

    /* Enable the auxiliary clock, but keep IRQ12 disabled.
       Neptune OS polls the controller instead of using IRQs. */
    config &= (uint8_t)~0x20;
    config &= (uint8_t)~0x02;

    if (!controller_write_config(config)) {
        return 0;
    }

    if (!mouse_send_command(MOUSE_CMD_DEFAULTS)) {
        return 0;
    }

    if (!mouse_send_command(MOUSE_CMD_ENABLE)) {
        return 0;
    }

    initialized = 1;
    return 1;
}

void mouse_process_byte(uint8_t byte)
{
    if (!initialized) {
        return;
    }

    if (packet_index == 0) {
        /* Bit 3 must be set in the first packet byte. */
        if (!(byte & 0x08)) {
            return;
        }

        packet[0] = byte;
        packet_index = 1;
        return;
    }

    packet[packet_index++] = byte;

    if (packet_index < 3) {
        return;
    }

    packet_index = 0;

    uint8_t flags = packet[0];

    /* Ignore packets with X or Y overflow. */
    if (flags & 0xC0) {
        return;
    }

    int32_t dx = (int8_t)packet[1];
    int32_t dy = (int8_t)packet[2];

    mouse_state.delta_x = dx;
    mouse_state.delta_y = -dy;

    int32_t old_x = mouse_state.x;
    int32_t old_y = mouse_state.y;

    mouse_state.x += dx;
    mouse_state.y -= dy;

    if (mouse_state.x < 0) {
        mouse_state.x = 0;
    }

    if (mouse_state.y < 0) {
        mouse_state.y = 0;
    }

    if (mouse_state.x >= (int32_t)screen_width) {
        mouse_state.x = (int32_t)screen_width - 1;
    }

    if (mouse_state.y >= (int32_t)screen_height) {
        mouse_state.y = (int32_t)screen_height - 1;
    }

    if (old_x != mouse_state.x || old_y != mouse_state.y) {
        mouse_state.moved = 1;
    }

    uint8_t left = flags & 0x01;
    uint8_t right = (flags >> 1) & 0x01;
    uint8_t middle = (flags >> 2) & 0x01;

    if (left != mouse_state.left_button ||
        right != mouse_state.right_button ||
        middle != mouse_state.middle_button) {
        mouse_state.buttons_changed = 1;
    }

    mouse_state.left_button = left;
    mouse_state.right_button = right;
    mouse_state.middle_button = middle;
}

void mouse_get_state(mouse_state_t *state)
{
    if (state == 0) {
        return;
    }

    *state = mouse_state;

    /* These are event flags, not persistent button states. */
    mouse_state.moved = 0;
    mouse_state.buttons_changed = 0;
}
