#include "../bob64/app.h"
#include "../bob64/gfx.h"
#include "../bob64/widgets.h"
#include "../bob64/window.h"

#define INFO_GUI_WIDTH 520u
#define INFO_GUI_HEIGHT 340u

static u32 gui_pixels[INFO_GUI_WIDTH*INFO_GUI_HEIGHT];
static BOB64_GFX gui_graphics;
static BOB64_WINDOW_MANAGER gui_manager;
static BOB64_WINDOW gui_window;
static BOB64_WINDOW_HANDLE gui_order[1];
static BOB64_RECT gui_close_button={12,34,72,18};
static BOB64_BUTTON_STATE gui_close_state;
static BOB64_FILE_INFO gui_files[BOB64_SYSCALL_MAX_FILES];
static u32 gui_width,gui_height;
static s64 gui_display_status,gui_file_count;
static u64 gui_total_bytes;

static void write_text(const char *text) {
    usize length=0;
    while(text[length])length++;
    (void)bob64_app_write(text,length);
}

static int text_equals(const char *left,const char *right) {
    usize i=0;
    if(!left||!right)return 0;
    while(left[i]&&right[i]&&left[i]==right[i])i++;
    return !left[i]&&!right[i];
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

static void format_number(char *text,u64 value) {
    char digits[20];
    usize count=0,offset=0;
    do {digits[count++]=(char)('0'+value%10);value/=10;}
    while(value&&count<sizeof(digits));
    while(count)text[offset++]=digits[--count];
    text[offset]=0;
}

static int refresh_gui_info(void) {
    gui_display_status=bob64_app_get_display(&gui_width,&gui_height);
    gui_file_count=bob64_app_list_files(gui_files,BOB64_SYSCALL_MAX_FILES);
    if(gui_file_count<0||(u64)gui_file_count>BOB64_SYSCALL_MAX_FILES)return -1;
    gui_total_bytes=0;
    for(s64 i=0;i<gui_file_count;i++)gui_total_bytes+=gui_files[i].Size;
    return 0;
}

static void draw_labeled_number(const char *label,u64 value,s32 y) {
    char number[24];
    format_number(number,value);
    bob64_gfx_text(&gui_graphics,24,y,label,0x00dbe8f4u,1);
    bob64_gfx_text(&gui_graphics,256,y,number,0x008fc8ffu,1);
}

static s64 run_gui(void) {
    BOB64_EVENT event,screen_event,routed;
    u32 screen_width,screen_height;
    if(refresh_gui_info())return -3;
    if(bob64_app_write("bob!",4)!=4)return -4;
    if(bob64_app_get_display(&screen_width,&screen_height)||
       screen_width<INFO_GUI_WIDTH||screen_height<INFO_GUI_HEIGHT)return -5;
    if(bob64_wm_init(&gui_manager,&gui_window,1,gui_order,
                     screen_width,screen_height))return -6;
    BOB64_WINDOW_HANDLE kernel_handle=bob64_app_window_create(
        (s32)(screen_width-INFO_GUI_WIDTH)/2,
        (s32)(screen_height-INFO_GUI_HEIGHT)/2,INFO_GUI_WIDTH,INFO_GUI_HEIGHT);
    if(!kernel_handle||(s64)kernel_handle<0)return -7;
    gui_manager.NextHandle=kernel_handle;
    BOB64_WINDOW_HANDLE app_handle=bob64_wm_create(&gui_manager,
        (s32)(screen_width-INFO_GUI_WIDTH)/2,
        (s32)(screen_height-INFO_GUI_HEIGHT)/2,INFO_GUI_WIDTH,INFO_GUI_HEIGHT,
        (void *)"System information");
    if(app_handle!=kernel_handle) {
        if(app_handle)bob64_wm_destroy(&gui_manager,app_handle);
        bob64_app_window_destroy(kernel_handle);return -8;
    }
    gui_graphics.Pixels=gui_pixels;gui_graphics.Width=INFO_GUI_WIDTH;
    gui_graphics.Height=INFO_GUI_HEIGHT;
    gui_graphics.Capacity=INFO_GUI_WIDTH*INFO_GUI_HEIGHT;
    for(;;) {
        bob64_gfx_fill_rect(&gui_graphics,0,0,INFO_GUI_WIDTH,INFO_GUI_HEIGHT,
                            0x001a2a3cu);
        bob64_gfx_fill_rect(&gui_graphics,0,0,INFO_GUI_WIDTH,
                            BOB64_WINDOW_TITLE_BAR_HEIGHT,0x00335d8cu);
        bob64_gfx_text(&gui_graphics,12,8,"BOB64 SYSTEM INFORMATION",
                       0x00ffffffu,1);
        bob64_button_draw(&gui_graphics,&gui_close_button,"CLOSE",
                          &gui_close_state);
        bob64_gfx_text(&gui_graphics,112,39,"R REFRESH",0x009eb3c7u,1);
        draw_labeled_number("System call ABI",BOB64_SYSCALL_ABI_VERSION,76);
        if(gui_display_status)bob64_gfx_text(&gui_graphics,24,104,
            "Display unavailable",0x00dbe8f4u,1);
        else {
            char resolution[24],height_text[16];
            format_number(resolution,gui_width);
            format_number(height_text,gui_height);
            bob64_gfx_text(&gui_graphics,24,104,"Display",0x00dbe8f4u,1);
            bob64_gfx_text(&gui_graphics,256,104,resolution,
                           0x008fc8ffu,1);
            bob64_gfx_text(&gui_graphics,292,104,"x",0x00dbe8f4u,1);
            bob64_gfx_text(&gui_graphics,310,104,height_text,
                           0x008fc8ffu,1);
        }
        draw_labeled_number("Uptime (seconds)",bob64_app_ticks()/100,132);
        draw_labeled_number("RAM files",(u64)gui_file_count,160);
        draw_labeled_number("RAM file data (bytes)",gui_total_bytes,188);
        bob64_gfx_text(&gui_graphics,24,236,"ESCAPE OR CLOSE RETURNS",
                       0x009eb3c7u,1);
        if(bob64_app_window_present(gui_window.Handle,gui_graphics.Pixels,
             gui_graphics.Width,gui_graphics.Height)!=
             (s64)(INFO_GUI_WIDTH*INFO_GUI_HEIGHT))return -9;
        if(bob64_app_wait_event(&event))return -10;
        screen_event=event;screen_event.X=event.ScreenX;
        screen_event.Y=event.ScreenY;
        s64 target=bob64_wm_dispatch(&gui_manager,&screen_event,&routed);
        if(event.Type==BOB64_EVENT_KEY_DOWN&&
           (event.Character==0x1b||event.Character=='x'||event.Character=='X'))
            break;
        if(event.Type==BOB64_EVENT_KEY_DOWN&&
           (event.Character=='r'||event.Character=='R'))refresh_gui_info();
        if(event.Type==BOB64_EVENT_MOUSE_MOVE) {
            BOB64_EVENT local=event;local.X=event.ScreenX-gui_window.X;
            local.Y=event.ScreenY-gui_window.Y;
            bob64_button_event(&gui_close_button,&gui_close_state,&local);
        }
        if(event.Type==BOB64_EVENT_MOUSE_BUTTON&&
           target==(s64)gui_window.Handle) {
            BOB64_EVENT local=event;local.X=routed.X;local.Y=routed.Y;
            int clicked=bob64_button_event(&gui_close_button,
                                           &gui_close_state,&local);
            if(!(event.Buttons&BOB64_BUTTON_LEFT)&&clicked)break;
        }
    }
    bob64_app_window_destroy(gui_window.Handle);
    bob64_wm_destroy(&gui_manager,gui_window.Handle);
    return 0;
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    BOB64_FILE_INFO files[BOB64_SYSCALL_MAX_FILES];
    u32 width=0,height=0;
    s64 file_count,display_status;
    u64 total_bytes=0;
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION||
       startup->ArgumentCount>2||
       (startup->ArgumentCount&&!startup->Arguments))
        return -1;
    if(bob64_app_query_abi()!=BOB64_SYSCALL_ABI_VERSION)return -2;
    if(startup->ArgumentCount==2&&startup->Arguments&&
       text_equals(startup->Arguments[1],"gui"))return run_gui();
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
