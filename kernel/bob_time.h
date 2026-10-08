#ifndef BOB_TIME_H
#define BOB_TIME_H

#include "bob.h"

#define BOB_TIME_API_VERSION 1
#define BOB_TIME_GET_UTC_SECONDS 7

/* Writes low/high 32-bit words of UTC seconds since 1970-01-01. */
int bob_time_utc_seconds(int *words) {
    int request[7];
    request[0]=(BOB_TIME_API_VERSION << 16) | BOB_TIME_GET_UTC_SECONDS;
    request[1]=(int)words;request[2]=2;request[3]=0;request[4]=0;
    request[5]=0;request[6]=0;
    return bob_os_service(request);
}

#endif
