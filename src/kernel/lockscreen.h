#ifndef NEPTUNE_LOCKSCREEN_H
#define NEPTUNE_LOCKSCREEN_H

#include <stdint.h>
#include "keyboard.h"
#include "mouse.h"

void lockscreen_init(uint32_t width, uint32_t height);
void lockscreen_render(void);

/* Returns 1 when login succeeds, otherwise 0. */
int lockscreen_handle_keyboard_event(const keyboard_event_t *event);
int lockscreen_handle_mouse_state(const mouse_state_t *state);

#endif
