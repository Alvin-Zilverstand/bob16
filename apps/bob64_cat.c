#include "../bob64/app.h"

static char file_buffer[BOB64_SYSCALL_MAX_BUFFER];

static usize text_length(const char *text) {
    usize length=0;
    if(!text)return 0;
    while(text[length])length++;
    return length;
}

static void write_text(const char *text) {
    (void)bob64_app_write(text,text_length(text));
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    const char *name;
    usize name_length;
    s64 handle,result=0;
    u8 last_character=0;
    int read_any=0;
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION)return -1;
    if(startup->ArgumentCount!=2||!startup->Arguments||
       !(name=startup->Arguments[1])||!(name_length=text_length(name))) {
        write_text("usage: cat FILE\n");
        return 1;
    }
    handle=bob64_app_file_open(name,name_length,BOB64_FILE_OPEN_READ);
    if(handle<0) {
        write_text("cannot read file\n");
        return -2;
    }
    for(;;) {
        s64 count=bob64_app_file_read(handle,file_buffer,sizeof(file_buffer));
        if(count<0) {result=-3;break;}
        if(!count)break;
        if(bob64_app_write(file_buffer,(usize)count)!=count) {
            result=-4;break;
        }
        last_character=(u8)file_buffer[(usize)count-1];
        read_any=1;
    }
    if(bob64_app_file_close(handle)&&!result)result=-5;
    if(!result&&read_any&&last_character!='\n'&&
       bob64_app_write_char('\n')<0)result=-6;
    return result;
}
