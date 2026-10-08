#ifndef BOB64_KEYBOARD_H
#define BOB64_KEYBOARD_H

#include "types.h"
#include "event.h"

typedef struct {
    u8 LeftShift,RightShift,LeftControl,RightControl,LeftAlt,RightAlt;
    u8 CapsLock,Extended,PauseRemaining;
} BOB64_KEYBOARD_STATE;

void bob64_keyboard_reset(void);
int bob64_keyboard_decode(BOB64_KEYBOARD_STATE *state,u8 scancode);
int bob64_keyboard_decode_event(BOB64_KEYBOARD_STATE *state,u8 scancode,
                                BOB64_EVENT *event);
int bob64_keyboard_poll(void);
int bob64_keyboard_poll_event(BOB64_EVENT *event);
/* IRQ producer and foreground consumer; the queue drops new events when full. */
int bob64_keyboard_irq_capture(u8 scancode);
int bob64_keyboard_irq_service(void);
int bob64_keyboard_irq_pop_event(BOB64_EVENT *event);
void bob64_keyboard_irq_set_active(int active);

#endif
