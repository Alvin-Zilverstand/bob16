#include "../bob64/app.h"

static void write_text(const char *text) {
    usize length=0;
    while(text[length])length++;
    (void)bob64_app_write(text,length);
}

static void write_size(u64 value) {
    char digits[20];
    usize count=0;
    do {
        digits[count++]=(char)('0'+value%10);
        value/=10;
    } while(value&&count<sizeof(digits));
    while(count)bob64_app_write_char((u8)digits[--count]);
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    BOB64_FILE_INFO files[BOB64_SYSCALL_MAX_FILES];
    s64 count;
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION)return -1;
    count=bob64_app_list_files(files,BOB64_SYSCALL_MAX_FILES);
    if(count<0) {
        write_text("cannot list files\n");
        return count;
    }
    for(s64 i=0;i<count;i++) {
        write_text(files[i].Name);
        write_text("  ");
        write_size(files[i].Size);
        write_text(" bytes\n");
    }
    if(!count)write_text("no files\n");
    return 0;
}
