#include "../bob64/app.h"

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    s64 child_status=-1;
    if(!startup||startup->AbiVersion!=BOB64_APP_ABI_VERSION||
       bob64_app_run("bob.b64e",8,1,(const char *const[]){"bob.b64e"},
                     &child_status)||child_status)return -1;
    if(bob64_app_write("bob!",4)!=4)return -2;
    return 0;
}
