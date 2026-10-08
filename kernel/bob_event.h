#ifndef BOB_EVENT_H
#define BOB_EVENT_H

#include "bob.h"

#define BOB_EVENT_API_VERSION 1
#define BOB_EVENT_KEY_DOWN 1
#define BOB_EVENT_CHAR 2
#define BOB_EVENT_KEY_UP 3
#define BOB_EVENT_MOUSE_MOVE 4
#define BOB_EVENT_MOUSE_BUTTON_DOWN 5
#define BOB_EVENT_MOUSE_BUTTON_UP 6
#define BOB_EVENT_MOUSE_WHEEL 7
#define BOB_EVENT_POLL 28
#define BOB_MOUSE_BUTTON_LEFT 1
#define BOB_MOUSE_BUTTON_RIGHT 2
#define BOB_MOUSE_BUTTON_MIDDLE 4

/* Four words: type, key/character/buttons/wheel delta, x, y. Blocks until input or EOF. */
int bob_event_wait(int *event) {
    int request[7];
    request[0]=(BOB_EVENT_API_VERSION << 16) | 26;
    request[1]=(int)event;request[2]=4;
    request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

/* Returns 1 with an event, 0 when no console event is available, negative on bad input. */
int bob_event_poll(int *event) {
    int request[7];
    request[0]=(BOB_EVENT_API_VERSION << 16) | BOB_EVENT_POLL;
    request[1]=(int)event;request[2]=4;
    request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

#endif
