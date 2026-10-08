#ifndef BOB_FS_H
#define BOB_FS_H

#include "bob.h"

/* Stable request protocol used by the bob32 filesystem service (trap 15). */
#define BOB_FS_LIST 1
#define BOB_FS_READ 2
#define BOB_FS_WRITE 3
#define BOB_FS_DELETE 4
#define BOB_FS_SYSTEM_INFO 5
#define BOB_FS_RUN_APP 6
#define BOB_FS_WRITE_WORDS 8
#define BOB_FS_READ_WORDS 9
#define BOB_FS_KIND 10
#define BOB_FS_API_VERSION 1
#define BOB_FS_REQUEST(operation) ((BOB_FS_API_VERSION << 16) | (operation))
#define BOB_FILE_KIND_TEXT 0
#define BOB_FILE_KIND_BOB16_PROGRAM 1
#define BOB_FILE_KIND_BOB32_PROGRAM 2
#define BOB_FILE_KIND_NATIVE_APP 3

/* LIST returns the file kind (0..3), or -1 at end. length receives word size. */
int bob_file_list(int index, char *name, int capacity, int *length) {
    int request[7];
    request[0]=BOB_FS_REQUEST(BOB_FS_LIST);request[1]=index;request[2]=(int)name;
    request[3]=capacity;request[4]=(int)length;request[5]=0;request[6]=0;
    return bob_os_service(request)-1;
}

/* READ returns text length, or a negative value for missing/invalid input. */
int bob_file_read(const char *name, char *buffer, int capacity) {
    int request[7];
    request[0]=BOB_FS_REQUEST(BOB_FS_READ);request[1]=(int)name;request[2]=(int)buffer;
    request[3]=capacity;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

/* WRITE creates/replaces a text file. Length is measured in characters. */
int bob_file_write(const char *name, const char *text, int length) {
    int request[7];
    request[0]=BOB_FS_REQUEST(BOB_FS_WRITE);request[1]=(int)name;request[2]=(int)text;
    request[3]=length;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

/* Store raw bob16/bob32 program words (kinds 1/2); native images require import. */
int bob_file_write_words(const char *name, int *words, int length, int kind) {
    int request[7];
    request[0]=BOB_FS_REQUEST(BOB_FS_WRITE_WORDS);request[1]=(int)name;
    request[2]=(int)words;request[3]=length;request[4]=kind;
    request[5]=0;request[6]=0;
    return bob_os_service(request);
}

/* Read raw program/native image words into an application-owned buffer. */
int bob_file_read_words(const char *name, int *words, int capacity) {
    int request[7];
    request[0]=BOB_FS_REQUEST(BOB_FS_READ_WORDS);request[1]=(int)name;
    request[2]=(int)words;request[3]=capacity;request[4]=0;
    request[5]=0;request[6]=0;
    return bob_os_service(request);
}

/* Return a file kind (0..3) by name, or -1 when it does not exist. */
int bob_file_kind(const char *name) {
    int request[7];
    request[0]=BOB_FS_REQUEST(BOB_FS_KIND);request[1]=(int)name;
    request[2]=0;request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

int bob_file_delete(const char *name) {
    int request[7];
    request[0]=BOB_FS_REQUEST(BOB_FS_DELETE);request[1]=(int)name;request[2]=0;
    request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

/* Fills version, app-address-space size, mapped pages and filesystem usage. */
int bob_system_info(int *values, int capacity) {
    int request[7];
    request[0]=BOB_FS_REQUEST(BOB_FS_SYSTEM_INFO);request[1]=(int)values;
    request[2]=capacity;request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

#endif
