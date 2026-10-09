#include "../bob64/app.h"

/* The QEMU runtime test injects a mouse move through the emulated PS/2 device. */
s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    BOB64_EVENT event;
    static const char passed[]="bob64 live mouse event passed\n";
    if(!startup||startup->AbiVersion!=BOB64_APP_ABI_VERSION)return -1;
    if(bob64_app_wait_event(&event))return -2;
    if(event.Type!=BOB64_EVENT_MOUSE_MOVE||
       (!event.DeltaX&&!event.DeltaY))
        return -3;
    if(bob64_app_write(passed,sizeof(passed)-1)!=(s64)(sizeof(passed)-1))
        return -4;
    return 0;
}
