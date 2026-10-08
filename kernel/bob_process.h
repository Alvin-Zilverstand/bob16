#ifndef BOB_PROCESS_H
#define BOB_PROCESS_H

#include "bob_fs.h"

/* Run a stored native bob32 app; returns service status and writes child status. */
int bob_app_run(const char *name,const char *arguments,int *exit_code) {
    int request[7];
    request[0]=BOB_FS_REQUEST(BOB_FS_RUN_APP);request[1]=(int)name;request[2]=(int)arguments;
    request[3]=(int)exit_code;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

#endif
