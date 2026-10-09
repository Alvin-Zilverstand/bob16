#include "../bob64/app.h"
#include "../bob64/gfx.h"
#include "../bob64/widgets.h"
#include "../bob64/window.h"

#define LS_GUI_WIDTH 480u
#define LS_GUI_HEIGHT 380u
#define LS_GUI_ROWS 15u

static u32 gui_pixels[LS_GUI_WIDTH*LS_GUI_HEIGHT];
static BOB64_GFX gui_graphics;
static BOB64_WINDOW_MANAGER gui_manager;
static BOB64_WINDOW gui_window;
static BOB64_WINDOW_HANDLE gui_order[1];
static BOB64_FILE_INFO gui_files[BOB64_SYSCALL_MAX_FILES];
static u32 gui_file_count,gui_selected,gui_scroll;
static BOB64_RECT gui_close_button={12,36,72,18};
static BOB64_RECT gui_open_button={92,36,72,18};
static BOB64_BUTTON_STATE gui_close_state;
static BOB64_BUTTON_STATE gui_open_state;
static char gui_status[32];
static u32 gui_last_clicked=BOB64_SYSCALL_MAX_FILES;
static u64 gui_last_click_tick;

static usize text_length(const char *text) {
    usize length=0;
    if(text)while(text[length])length++;
    return length;
}

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

static void format_size(char *buffer,u64 value) {
    char digits[20];
    usize count=0;
    do {digits[count++]=(char)('0'+value%10);value/=10;}
    while(value&&count<sizeof(digits));
    usize offset=0;
    while(count)buffer[offset++]=digits[--count];
    buffer[offset]=0;
}

static void set_status(const char *text) {
    usize i=0;
    while(i+1<sizeof(gui_status)&&text[i]){gui_status[i]=text[i];i++;}
    gui_status[i]=0;
}

static int executable_name(const char *name) {
    usize size=text_length(name);
    return size>5&&name[size-5]=='.'&&name[size-4]=='b'&&
           name[size-3]=='6'&&name[size-2]=='4'&&name[size-1]=='e';
}

static void reveal_selection(void) {
    if(gui_selected<gui_scroll)gui_scroll=gui_selected;
    else if(gui_selected>=gui_scroll+LS_GUI_ROWS)
        gui_scroll=gui_selected-LS_GUI_ROWS+1;
}

static void open_selected(void) {
    if(gui_selected>=gui_file_count) {
        set_status("NO FILE SELECTED");return;
    }
    const char *selected=gui_files[gui_selected].Name;
    const char *app_name=selected;
    const char *arguments[3];
    usize argument_count=1,app_name_length=text_length(selected);
    s64 child_status=-1;
    if(executable_name(selected))arguments[0]=selected;
    else {
        app_name="cat.b64e";app_name_length=8;
        arguments[0]=app_name;arguments[1]="gui";arguments[2]=selected;
        argument_count=3;
    }
    s64 result=bob64_app_run(app_name,app_name_length,argument_count,
                             arguments,&child_status);
    if(result||child_status)set_status("APP FAILED");
    else set_status("OPENED");
    s64 count=bob64_app_list_files(gui_files,BOB64_SYSCALL_MAX_FILES);
    if(count>=0)gui_file_count=(u32)count;
    if(gui_selected>=gui_file_count)
        gui_selected=gui_file_count?gui_file_count-1:0;
    reveal_selection();
}

static int gui_run(const BOB64_APP_STARTUP *startup) {
    BOB64_EVENT event,screen_event,routed;
    s64 target;
    u32 screen_width,screen_height;
    s64 result=bob64_app_get_display(&screen_width,&screen_height);
    if(result||screen_width<LS_GUI_WIDTH||screen_height<LS_GUI_HEIGHT)return -2;
    if(bob64_wm_init(&gui_manager,&gui_window,1,gui_order,
                     screen_width,screen_height))return -3;
    gui_last_clicked=BOB64_SYSCALL_MAX_FILES;gui_last_click_tick=0;
    BOB64_WINDOW_HANDLE kernel_handle=bob64_app_window_create(
        (s32)(screen_width-LS_GUI_WIDTH)/2,
        (s32)(screen_height-LS_GUI_HEIGHT)/2,LS_GUI_WIDTH,LS_GUI_HEIGHT);
    if(!kernel_handle||(s64)kernel_handle<0)return -4;
    gui_manager.NextHandle=kernel_handle;
    BOB64_WINDOW_HANDLE app_handle=bob64_wm_create(&gui_manager,
        (s32)(screen_width-LS_GUI_WIDTH)/2,
        (s32)(screen_height-LS_GUI_HEIGHT)/2,LS_GUI_WIDTH,LS_GUI_HEIGHT,
        (void *)"Files");
    if(app_handle!=kernel_handle) {
        if(app_handle)bob64_wm_destroy(&gui_manager,app_handle);
        bob64_app_window_destroy(kernel_handle);return -5;
    }
    gui_graphics.Pixels=gui_pixels;gui_graphics.Width=LS_GUI_WIDTH;
    gui_graphics.Height=LS_GUI_HEIGHT;
    gui_graphics.Capacity=LS_GUI_WIDTH*LS_GUI_HEIGHT;
    s64 count=bob64_app_list_files(gui_files,BOB64_SYSCALL_MAX_FILES);
    if(count<0) {
        set_status("LIST FAILED");
        gui_file_count=0;
    } else {
        gui_file_count=(u32)count;
        set_status("ENTER TO OPEN");
    }
    if(startup->ArgumentCount==3&&startup->Arguments[2]) {
        int found=0;
        for(u32 i=0;i<gui_file_count;i++) {
            if(text_length(startup->Arguments[2])==
                   text_length(gui_files[i].Name)) {
                usize n=0;
                while(startup->Arguments[2][n]&&
                      startup->Arguments[2][n]==gui_files[i].Name[n])n++;
                if(!startup->Arguments[2][n]&&!gui_files[i].Name[n]) {
                    gui_selected=i;found=1;break;
                }
            }
        }
        if(found)reveal_selection();
        else set_status("FILE NOT FOUND");
    }
    for(;;) {
        bob64_gfx_fill_rect(&gui_graphics,0,0,LS_GUI_WIDTH,LS_GUI_HEIGHT,
                            0x001a2a3cu);
        bob64_gfx_fill_rect(&gui_graphics,0,0,LS_GUI_WIDTH,
                            BOB64_WINDOW_TITLE_BAR_HEIGHT,0x00335d8cu);
        bob64_gfx_text(&gui_graphics,12,8,"BOB64 FILES",0x00ffffffu,1);
        bob64_gfx_text(&gui_graphics,12,26,"DOUBLE-CLICK OR ENTER/OPEN",
                       0x009eb3c7u,1);
        bob64_button_draw(&gui_graphics,&gui_close_button,"CLOSE",
                          &gui_close_state);
        bob64_button_draw(&gui_graphics,&gui_open_button,"OPEN",
                          &gui_open_state);
        bob64_gfx_text(&gui_graphics,180,42,gui_status,0x008fc8ffu,1);
        for(u32 row=0;row<LS_GUI_ROWS;row++) {
            u32 index=gui_scroll+row;
            if(index>=gui_file_count)break;
            s32 y=70+(s32)(row*18);
            if(index==gui_selected)
                bob64_gfx_fill_rect(&gui_graphics,8,y-2,464,16,0x00335d8cu);
            bob64_gfx_text(&gui_graphics,14,y,gui_files[index].Name,
                index==gui_selected?0x00ffffffu:0x00dbe8f4u,1);
            char size_text[24];
            format_size(size_text,gui_files[index].Size);
            bob64_gfx_text(&gui_graphics,380,y,size_text,0x009eb3c7u,1);
            bob64_gfx_text(&gui_graphics,434,y,"B",0x009eb3c7u,1);
        }
        if(!gui_file_count)bob64_gfx_text(&gui_graphics,14,74,"NO FILES",
                                           0x008fc8ffu,1);
        if(bob64_app_window_present(gui_window.Handle,gui_graphics.Pixels,
             gui_graphics.Width,gui_graphics.Height)!=
             (s64)(LS_GUI_WIDTH*LS_GUI_HEIGHT))return -6;
        if(bob64_app_wait_event(&event))return -7;
        screen_event=event;screen_event.X=event.ScreenX;
        screen_event.Y=event.ScreenY;
        target=bob64_wm_dispatch(&gui_manager,&screen_event,&routed);
        if(event.Type==BOB64_EVENT_KEY_DOWN&&
           (event.Character==0x1b||event.Character=='x'||event.Character=='X'))
            break;
        if(event.Type==BOB64_EVENT_KEY_DOWN&&
           (event.Character=='\n'||event.Character=='o'||event.Character=='O')) {
            gui_last_clicked=BOB64_SYSCALL_MAX_FILES;
            open_selected();
        }
        if(event.Type==BOB64_EVENT_KEY_DOWN&&
           event.Key==(BOB64_EVENT_KEY_EXTENDED|0x48)) {
            gui_last_clicked=BOB64_SYSCALL_MAX_FILES;
            if(gui_selected)gui_selected--;
            if(gui_selected<gui_scroll)gui_scroll=gui_selected;
        } else if(event.Type==BOB64_EVENT_KEY_DOWN&&
           event.Key==(BOB64_EVENT_KEY_EXTENDED|0x50)) {
            gui_last_clicked=BOB64_SYSCALL_MAX_FILES;
            if(gui_selected+1<gui_file_count)gui_selected++;
            if(gui_selected>=gui_scroll+LS_GUI_ROWS)
                gui_scroll=gui_selected-LS_GUI_ROWS+1;
        }
        if(event.Type==BOB64_EVENT_MOUSE_WHEEL&&event.Wheel&&
           target==(s64)gui_window.Handle) {
            gui_last_clicked=BOB64_SYSCALL_MAX_FILES;
            if(event.Wheel>0&&gui_selected)gui_selected--;
            else if(event.Wheel<0&&gui_selected+1<gui_file_count)gui_selected++;
            if(gui_selected<gui_scroll)gui_scroll=gui_selected;
            if(gui_selected>=gui_scroll+LS_GUI_ROWS)
                gui_scroll=gui_selected-LS_GUI_ROWS+1;
        }
        if(event.Type==BOB64_EVENT_MOUSE_MOVE) {
            BOB64_EVENT local=event;
            local.X=event.ScreenX-gui_window.X;
            local.Y=event.ScreenY-gui_window.Y;
            bob64_button_event(&gui_close_button,&gui_close_state,&local);
            bob64_button_event(&gui_open_button,&gui_open_state,&local);
        }
        if(event.Type==BOB64_EVENT_MOUSE_BUTTON&&target==(s64)gui_window.Handle) {
            BOB64_EVENT local=event;local.X=routed.X;local.Y=routed.Y;
            int clicked=bob64_button_event(&gui_close_button,
                                           &gui_close_state,&local);
            if(!(event.Buttons&BOB64_BUTTON_LEFT)&&clicked)break;
            int open=bob64_button_event(&gui_open_button,
                                        &gui_open_state,&local);
            if(!(event.Buttons&BOB64_BUTTON_LEFT)&&open)open_selected();
            if((event.Buttons&BOB64_BUTTON_LEFT)&&routed.Y>=70) {
                u32 row=(u32)(routed.Y-70)/18;
                if(gui_scroll+row<gui_file_count) {
                    gui_selected=gui_scroll+row;
                    if(gui_last_clicked!=gui_scroll+row)
                        gui_last_clicked=BOB64_SYSCALL_MAX_FILES;
                }
            } else if(!(event.Buttons&BOB64_BUTTON_LEFT)&&routed.Y>=70) {
                u32 row=(u32)(routed.Y-70)/18;
                u32 index=gui_scroll+row;
                if(index<gui_file_count) {
                    u64 now=bob64_app_ticks();
                    if(gui_last_clicked==index&&now-gui_last_click_tick<=50) {
                        gui_last_clicked=BOB64_SYSCALL_MAX_FILES;
                        open_selected();
                    } else {
                        gui_last_clicked=index;
                        gui_last_click_tick=now;
                    }
                }
            } else if(!(event.Buttons&BOB64_BUTTON_LEFT)) {
                gui_last_clicked=BOB64_SYSCALL_MAX_FILES;
            }
        }
    }
    bob64_app_window_destroy(gui_window.Handle);
    bob64_wm_destroy(&gui_manager,gui_window.Handle);
    return 0;
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    BOB64_FILE_INFO files[BOB64_SYSCALL_MAX_FILES];
    s64 count;
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION)return -1;
    if((startup->ArgumentCount==2||startup->ArgumentCount==3)&&
       startup->Arguments&&
       startup->Arguments[1]&&text_length(startup->Arguments[1])==3&&
       startup->Arguments[1][0]=='g'&&startup->Arguments[1][1]=='u'&&
       startup->Arguments[1][2]=='i') {
        if(bob64_app_write("bob!",4)!=4)return -8;
        return gui_run(startup);
    }
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
