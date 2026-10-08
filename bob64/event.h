#ifndef BOB64_EVENT_H
#define BOB64_EVENT_H

#include "types.h"

#define BOB64_EVENT_KEY_DOWN 1u
#define BOB64_EVENT_KEY_UP 2u
#define BOB64_EVENT_MOUSE_MOVE 3u
#define BOB64_EVENT_MOUSE_BUTTON 4u
#define BOB64_EVENT_MOUSE_WHEEL 5u
#define BOB64_EVENT_MOD_SHIFT 1u
#define BOB64_EVENT_MOD_CAPS_LOCK 2u
#define BOB64_EVENT_MOD_CONTROL 4u
#define BOB64_EVENT_MOD_ALT 8u
#define BOB64_EVENT_KEY_EXTENDED 0x100u

/* Set-1 scan codes; extended keys set BOB64_EVENT_KEY_EXTENDED. */
typedef struct {
    u32 Type;
    u32 Key;
    u32 Character;
    u32 Modifiers;
    s32 X,Y;
    s32 DeltaX,DeltaY;
    u32 Buttons;
    s32 Wheel;
    u64 WindowHandle;
    s32 ScreenX,ScreenY;
} BOB64_EVENT;

_Static_assert(sizeof(BOB64_EVENT)==56,"bob64 input-event ABI size");

#endif
