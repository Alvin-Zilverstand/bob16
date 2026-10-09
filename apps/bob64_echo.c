#include "../bob64/app.h"

static usize text_length(const char *text) {
    usize length=0;
    if(!text)return 0;
    while(text[length])length++;
    return length;
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION||
       (startup->ArgumentCount&&!startup->Arguments))return -1;
    for(u64 i=1;i<startup->ArgumentCount;i++) {
        const char *argument=startup->Arguments[i];
        if(i>1&&bob64_app_write_char(' ')<0)return -2;
        if(!argument||bob64_app_write(argument,text_length(argument))<0)return -3;
    }
    if(bob64_app_write_char('\n')<0)return -4;
    return 0;
}
