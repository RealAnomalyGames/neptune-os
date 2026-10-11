#ifndef NEPTUNE_MOUSE_H
#define NEPTUNE_MOUSE_H

#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;
    int32_t delta_x;
    int32_t delta_y;

    uint8_t left_button;
    uint8_t right_button;
    uint8_t middle_button;

    uint8_t moved;
    uint8_t buttons_changed;
} mouse_state_t;

uint8_t mouse_init(uint32_t screen_width, uint32_t screen_height);
void mouse_process_byte(uint8_t byte);
void mouse_get_state(mouse_state_t *state);

#endif
