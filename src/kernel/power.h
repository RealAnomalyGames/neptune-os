#ifndef NEPTUNE_POWER_H
#define NEPTUNE_POWER_H

#include <stdint.h>
#include "keyboard.h"
#include "mouse.h"

typedef enum {
    POWER_ACTION_NONE = 0,
    POWER_ACTION_SHUTDOWN,
    POWER_ACTION_RESTART,
    POWER_ACTION_CANCEL
} power_action_t;

void power_init(uint32_t width, uint32_t height);
void power_render(void);

power_action_t power_handle_keyboard_event(
    const keyboard_event_t *event
);

power_action_t power_handle_mouse_state(
    const mouse_state_t *state
);

/* Displays the status screen and requests the chosen operation. */
void power_start(power_action_t action);

#endif
