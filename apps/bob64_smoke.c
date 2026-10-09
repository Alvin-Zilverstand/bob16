#include "../bob64/app.h"
#include "../bob64/libc.h"

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    static const char message[]="bob!";
    static const char file_name[]="bob-app.txt";
    static const char stream_name[]="bob-stream.txt";
    static const char file_data[]="written by a C app";
    static const char string_haystack[]="prefix bob64! suffix";
    char file_copy[32],string_test[16]="",string_padding[8],formatted[40];
    if(!startup||startup->AbiVersion!=BOB64_APP_ABI_VERSION)return -1;
    if(bob64_app_query_abi()!=BOB64_SYSCALL_ABI_VERSION)return -2;
    u64 ticks_before=bob64_app_ticks();
    if(bob64_app_write(message,sizeof(message)-1)!=(s64)(sizeof(message)-1))return -3;
    if(bob64_app_write_file(file_name,sizeof(file_name)-1,file_data,
                            sizeof(file_data)-1)!=(s64)(sizeof(file_data)-1))return -4;
    s64 copied=bob64_app_read_file(file_name,sizeof(file_name)-1,file_copy,
                                   sizeof(file_copy));
    if(copied!=(s64)(sizeof(file_data)-1))return -5;
    if(bob64_memcmp(file_copy,file_data,sizeof(file_data)-1))return -6;
    s64 handle=bob64_app_file_open(stream_name,sizeof(stream_name)-1,
        BOB64_FILE_OPEN_READ|BOB64_FILE_OPEN_WRITE|BOB64_FILE_OPEN_CREATE|
        BOB64_FILE_OPEN_TRUNCATE);
    if(handle<=0)return -7;
    if(bob64_app_file_write(handle,file_data,sizeof(file_data)-1)!=
       (s64)(sizeof(file_data)-1))return -8;
    if(bob64_app_file_seek(handle,0)!=0)return -9;
    copied=bob64_app_file_read(handle,file_copy,sizeof(file_copy));
    if(copied!=(s64)(sizeof(file_data)-1)||
       bob64_memcmp(file_copy,file_data,sizeof(file_data)-1))return -10;
    if(bob64_app_file_close(handle))return -11;
    if(bob64_app_ticks()<ticks_before)return -12;
    if(bob64_strcpy(string_test,"bob")!=string_test||
       bob64_strncat(string_test,"64!ignored",3)!=string_test||
       bob64_strcmp(string_test,"bob64!")||
       bob64_strncmp(string_test,"bob64?",5)||
       bob64_strchr(string_test,'6')!=string_test+3||
       bob64_strrchr(string_test,'!')!=string_test+5||
       bob64_strstr(string_haystack,"bob64!")!=string_haystack+7||
       bob64_memchr(string_test,'4',sizeof(string_test))!=string_test+4)
        return -13;
    if(bob64_strncpy(string_padding,"bob",sizeof(string_padding))!=string_padding||
       bob64_strnlen(string_padding,sizeof(string_padding))!=3||
       string_padding[3]!=0||string_padding[7]!=0)return -14;
    if(bob64_snprintf(formatted,sizeof(formatted),"%s:%+05d:%#llx:%zu",
          "bob!",-7,(unsigned long long)42,(usize)3)!=17||
       bob64_strcmp(formatted,"bob!:-0007:0x2a:3"))return -15;
    return 0;
}
