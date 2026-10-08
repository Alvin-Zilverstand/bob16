#ifndef BOB_SYSTEM_H
#define BOB_SYSTEM_H

#include "bob.h"

#define BOB_SYSTEM_API_VERSION 1
#define BOB_SYSTEM_CAPABILITIES 11
#define BOB_SYSTEM_DEVICE_CELL_FRAMEBUFFER 1
#define BOB_SYSTEM_INPUT_KEYBOARD 1
#define BOB_SYSTEM_INPUT_MOUSE 2

/* Returns version, device flags, cell width/height, and input flags. */
int bob_system_capabilities(int *values,int capacity) {
    int request[7];
    request[0]=(BOB_SYSTEM_API_VERSION << 16) | BOB_SYSTEM_CAPABILITIES;
    request[1]=(int)values;request[2]=capacity;
    request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

#endif
