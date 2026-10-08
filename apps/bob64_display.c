#include "../bob64/app.h"

#define DISPLAY_LIMIT_WIDTH 1024u
#define DISPLAY_LIMIT_HEIGHT 768u

static u32 pixels[DISPLAY_LIMIT_WIDTH*DISPLAY_LIMIT_HEIGHT];

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    u32 width,height;
    BOB64_EVENT event;
    if(!startup||startup->AbiVersion!=BOB64_APP_ABI_VERSION||
       bob64_app_query_abi()!=BOB64_SYSCALL_ABI_VERSION||
       bob64_app_get_display(&width,&height))return -1;
    if(!width||!height||width>DISPLAY_LIMIT_WIDTH||height>DISPLAY_LIMIT_HEIGHT)
        return -2;
    for(u32 y=0;y<height;y++)for(u32 x=0;x<width;x++) {
        u32 red=(x*180u)/width;
        u32 green=(y*120u)/height;
        pixels[(usize)y*width+x]=(red<<16)|(green<<8)|32u;
    }
    if(bob64_app_present(pixels,width,height)!=(s64)((u64)width*height))return -3;
    if(bob64_app_write("bob!",4)!=4)return -4;
    for(;;) {
        if(bob64_app_wait_event(&event))return -5;
        if(event.Type==BOB64_EVENT_KEY_DOWN&&event.Character==0x1b)break;
    }
    return 0;
}
