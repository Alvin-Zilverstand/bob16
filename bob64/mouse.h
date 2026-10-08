#ifndef BOB64_MOUSE_H
#define BOB64_MOUSE_H

#include "event.h"

typedef struct {
    u32 Width,Height;
    s32 X,Y;
    u8 Packet[4];
    u8 PacketLength,PacketSize,Buttons,DeviceId,Enabled;
} BOB64_MOUSE_STATE;

int bob64_mouse_reset(BOB64_MOUSE_STATE *state,u32 width,u32 height);
int bob64_mouse_decode(BOB64_MOUSE_STATE *state,u8 value,BOB64_EVENT *event);
int bob64_mouse_init(u32 width,u32 height);
int bob64_mouse_poll_event(BOB64_EVENT *event);
int bob64_mouse_irq_queue_init(u32 width,u32 height);
int bob64_mouse_irq_capture(u8 value);
int bob64_mouse_irq_service(void);
int bob64_mouse_irq_pop_event(BOB64_EVENT *event);
void bob64_mouse_irq_set_active(int active);

#endif
