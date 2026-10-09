#include "../bob64/app.h"

static void write_text(const char *text) {
    usize length=0;
    while(text[length])length++;
    (void)bob64_app_write(text,length);
}

static void write_number(u64 value) {
    char digits[20];
    usize count=0;
    do {
        digits[count++]=(char)('0'+value%10);
        value/=10;
    } while(value&&count<sizeof(digits));
    while(count)(void)bob64_app_write_char((u8)digits[--count]);
}

static void write_labeled_number(const char *label,u64 value) {
    write_text(label);
    write_number(value);
    write_text("\n");
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    BOB64_FILE_INFO files[BOB64_SYSCALL_MAX_FILES];
    u32 width=0,height=0;
    s64 file_count,display_status;
    u64 total_bytes=0;
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION||startup->ArgumentCount!=0)
        return -1;
    if(bob64_app_query_abi()!=BOB64_SYSCALL_ABI_VERSION)return -2;
    display_status=bob64_app_get_display(&width,&height);
    file_count=bob64_app_list_files(files,BOB64_SYSCALL_MAX_FILES);
    if(file_count<0||(u64)file_count>BOB64_SYSCALL_MAX_FILES)return -4;
    for(s64 i=0;i<file_count;i++)total_bytes+=files[i].Size;
    write_text("bob64 system information\n");
    write_labeled_number("System call ABI: ",BOB64_SYSCALL_ABI_VERSION);
    if(display_status)write_text("Display: unavailable\n");
    else {
        write_text("Display: ");write_number(width);write_text(" x ");
        write_number(height);write_text(" pixels\n");
    }
    write_labeled_number("Uptime: ",bob64_app_ticks()/100);
    write_labeled_number("RAM files: ",(u64)file_count);
    write_labeled_number("RAM file data: ",total_bytes);
    write_text("Uptime unit: seconds\n");
    write_text("bob!\n");
    return 0;
}
