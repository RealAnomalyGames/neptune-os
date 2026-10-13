#ifndef NEPTUNE_DESKTOP_H
#define NEPTUNE_DESKTOP_H

#include <stdint.h>
#include "keyboard.h"
#include "mouse.h"

typedef enum {
    DESKTOP_ACTION_NONE = 0,
    DESKTOP_ACTION_LOG_OFF,
    DESKTOP_ACTION_SHUT_DOWN
} desktop_action_t;

void desktop_init(uint32_t width, uint32_t height);
void desktop_render(void);

void desktop_handle_keyboard_event(
    const keyboard_event_t *event
);

void desktop_handle_mouse_state(
    const mouse_state_t *state
);

desktop_action_t desktop_take_action(void);

#endif
