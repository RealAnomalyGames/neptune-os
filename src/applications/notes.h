#ifndef NEPTUNE_NOTES_H
#define NEPTUNE_NOTES_H

#include <stdbool.h>
#include "keyboard.h"
#include "mouse.h"

typedef enum {
    NOTES_ACTION_NONE = 0,
    NOTES_ACTION_MINIMIZE,
    NOTES_ACTION_CLOSE
} notes_action_t;

void notes_launch(void);
void notes_resume(void);
void notes_minimize(void);
void notes_close(void);

bool notes_is_running(void);
bool notes_is_minimized(void);

void notes_render(void);

notes_action_t notes_handle_keyboard_event(
    const keyboard_event_t *event
);

notes_action_t notes_handle_mouse_state(
    const mouse_state_t *state
);

#endif
