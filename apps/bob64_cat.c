#include "../bob64/app.h"
#include "../bob64/gfx.h"
#include "../bob64/widgets.h"
#include "../bob64/window.h"

static char file_buffer[BOB64_SYSCALL_MAX_BUFFER];
#define CAT_GUI_WIDTH 720u
#define CAT_GUI_HEIGHT 520u
#define CAT_GUI_COLUMNS 112u
#define CAT_GUI_ROWS 31u
#define CAT_GUI_FILE_CAPACITY (16u*BOB64_SYSCALL_MAX_BUFFER)
static u8 gui_text[CAT_GUI_FILE_CAPACITY+1];
static u32 gui_pixels[CAT_GUI_WIDTH*CAT_GUI_HEIGHT];
static BOB64_GFX gui_graphics;
static BOB64_WINDOW_MANAGER gui_manager;
static BOB64_WINDOW gui_window;
static BOB64_WINDOW_HANDLE gui_order[1];
static BOB64_RECT gui_close_button={12,32,72,18};
static BOB64_BUTTON_STATE gui_close_state;
static const char *gui_filename;
static usize gui_text_length,gui_top_offset;
static char gui_status[32];

static usize text_length(const char *text) {
    usize length=0;
    if(!text)return 0;
    while(text[length])length++;
    return length;
}

static void write_text(const char *text) {
    (void)bob64_app_write(text,text_length(text));
}

static int is_newline(u8 value) {return value=='\n'||value=='\r';}

static usize next_visual_line(usize offset) {
    usize column=0;
    while(offset<gui_text_length&&column<CAT_GUI_COLUMNS&&
          !is_newline(gui_text[offset])) {offset++;column++;}
    if(offset<gui_text_length&&is_newline(gui_text[offset])) {
        u8 first=gui_text[offset++];
        if(offset<gui_text_length&&is_newline(gui_text[offset])&&
           gui_text[offset]!=first)offset++;
    }
    return offset;
}

static usize previous_visual_line(usize offset) {
    usize current=0;
    if(!offset)return 0;
    while(current<offset) {
        usize next=next_visual_line(current);
        if(next>=offset)return current;
        if(next<=current)return current;
        current=next;
    }
    return current;
}

static void scroll_next_line(void) {
    if(gui_top_offset>=gui_text_length)return;
    usize next=next_visual_line(gui_top_offset);
    gui_top_offset=next>=gui_text_length?
        previous_visual_line(gui_text_length):next;
}

static void draw_visual_line(usize offset,s32 x,s32 y) {
    char line[CAT_GUI_COLUMNS+1];
    usize length=0;
    while(offset<gui_text_length&&length<CAT_GUI_COLUMNS&&
          !is_newline(gui_text[offset])) {
        u8 value=gui_text[offset++];
        line[length++]=(value>=32&&value<=126)?(char)value:
                       (value=='\t'?' ':'.');
    }
    line[length]=0;
    bob64_gfx_text(&gui_graphics,x,y,line,0x00e2ebf5u,1);
}

static void set_gui_status(const char *text) {
    usize i=0;
    while(i+1<sizeof(gui_status)&&text[i]){gui_status[i]=text[i];i++;}
    gui_status[i]=0;
}

static s64 gui_view_file(const char *name,usize name_length) {
    BOB64_EVENT event,screen_event,routed;
    u32 screen_width,screen_height;
    s64 result=bob64_app_file_read_all(name,name_length,gui_text,
                                       CAT_GUI_FILE_CAPACITY);
    if(result<0)return result;
    gui_text_length=(usize)result;gui_text[gui_text_length]=0;
    gui_filename=name;gui_top_offset=0;
    set_gui_status(gui_text_length?"TEXT FILE":"EMPTY FILE");
    if(bob64_app_write("bob!",4)!=4)return -2;
    if(bob64_app_get_display(&screen_width,&screen_height)||
       screen_width<CAT_GUI_WIDTH||screen_height<CAT_GUI_HEIGHT)return -3;
    if(bob64_wm_init(&gui_manager,&gui_window,1,gui_order,
                     screen_width,screen_height))return -4;
    BOB64_WINDOW_HANDLE kernel_handle=bob64_app_window_create(
        (s32)(screen_width-CAT_GUI_WIDTH)/2,
        (s32)(screen_height-CAT_GUI_HEIGHT)/2,CAT_GUI_WIDTH,CAT_GUI_HEIGHT);
    if(!kernel_handle||(s64)kernel_handle<0)return -5;
    gui_manager.NextHandle=kernel_handle;
    BOB64_WINDOW_HANDLE app_handle=bob64_wm_create(&gui_manager,
        (s32)(screen_width-CAT_GUI_WIDTH)/2,
        (s32)(screen_height-CAT_GUI_HEIGHT)/2,CAT_GUI_WIDTH,CAT_GUI_HEIGHT,
        (void *)"Text viewer");
    if(app_handle!=kernel_handle) {
        if(app_handle)bob64_wm_destroy(&gui_manager,app_handle);
        bob64_app_window_destroy(kernel_handle);return -6;
    }
    gui_graphics.Pixels=gui_pixels;gui_graphics.Width=CAT_GUI_WIDTH;
    gui_graphics.Height=CAT_GUI_HEIGHT;
    gui_graphics.Capacity=CAT_GUI_WIDTH*CAT_GUI_HEIGHT;
    for(;;) {
        bob64_gfx_fill_rect(&gui_graphics,0,0,CAT_GUI_WIDTH,CAT_GUI_HEIGHT,
                            0x001a2a3cu);
        bob64_gfx_fill_rect(&gui_graphics,0,0,CAT_GUI_WIDTH,
                            BOB64_WINDOW_TITLE_BAR_HEIGHT,0x00335d8cu);
        bob64_gfx_text(&gui_graphics,12,8,"TEXT VIEWER",0x00ffffffu,1);
        bob64_button_draw(&gui_graphics,&gui_close_button,"CLOSE",
                          &gui_close_state);
        bob64_gfx_text(&gui_graphics,100,38,gui_filename,0x00e2ebf5u,1);
        bob64_gfx_text(&gui_graphics,550,38,gui_status,0x009eb3c7u,1);
        usize line_offset=gui_top_offset;
        for(u32 row=0;row<CAT_GUI_ROWS&&line_offset<gui_text_length;row++) {
            draw_visual_line(line_offset,12,62+(s32)(row*14));
            usize next=next_visual_line(line_offset);
            if(next<=line_offset)break;
            line_offset=next;
        }
        if(bob64_app_window_present(gui_window.Handle,gui_graphics.Pixels,
             gui_graphics.Width,gui_graphics.Height)!=
             (s64)(CAT_GUI_WIDTH*CAT_GUI_HEIGHT))return -7;
        if(bob64_app_wait_event(&event))return -8;
        screen_event=event;screen_event.X=event.ScreenX;
        screen_event.Y=event.ScreenY;
        s64 target=bob64_wm_dispatch(&gui_manager,&screen_event,&routed);
        if(event.Type==BOB64_EVENT_KEY_DOWN&&
           (event.Character==0x1b||event.Character=='x'||event.Character=='X'))
            break;
        if(event.Type==BOB64_EVENT_KEY_DOWN&&
           event.Key==(BOB64_EVENT_KEY_EXTENDED|0x48))
            gui_top_offset=previous_visual_line(gui_top_offset);
        else if(event.Type==BOB64_EVENT_KEY_DOWN&&
                event.Key==(BOB64_EVENT_KEY_EXTENDED|0x50))
            scroll_next_line();
        else if(event.Type==BOB64_EVENT_KEY_DOWN&&
                event.Key==(BOB64_EVENT_KEY_EXTENDED|0x49)) {
            for(u32 row=0;row<CAT_GUI_ROWS-1&&gui_top_offset;row++)
                gui_top_offset=previous_visual_line(gui_top_offset);
        } else if(event.Type==BOB64_EVENT_KEY_DOWN&&
                  event.Key==(BOB64_EVENT_KEY_EXTENDED|0x51)) {
            for(u32 row=0;row<CAT_GUI_ROWS-1&&gui_top_offset<gui_text_length;row++)
                scroll_next_line();
        }
        if(event.Type==BOB64_EVENT_MOUSE_WHEEL&&event.Wheel&&
           target==(s64)gui_window.Handle) {
            if(event.Wheel>0)gui_top_offset=previous_visual_line(gui_top_offset);
            else scroll_next_line();
        }
        if(event.Type==BOB64_EVENT_MOUSE_MOVE) {
            BOB64_EVENT local=event;local.X=event.ScreenX-gui_window.X;
            local.Y=event.ScreenY-gui_window.Y;
            bob64_button_event(&gui_close_button,&gui_close_state,&local);
        }
        if(event.Type==BOB64_EVENT_MOUSE_BUTTON&&
           target==(s64)gui_window.Handle) {
            BOB64_EVENT local=event;local.X=routed.X;local.Y=routed.Y;
            int clicked=bob64_button_event(&gui_close_button,&gui_close_state,
                                           &local);
            if(!(event.Buttons&BOB64_BUTTON_LEFT)&&clicked)break;
        }
    }
    bob64_app_window_destroy(gui_window.Handle);
    bob64_wm_destroy(&gui_manager,gui_window.Handle);
    gui_filename=0;
    return 0;
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    const char *name;
    usize name_length;
    s64 handle,result=0;
    u8 last_character=0;
    int read_any=0;
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION)return -1;
    if(startup->ArgumentCount==3&&startup->Arguments&&
       startup->Arguments[1]&&startup->Arguments[2]&&
       text_length(startup->Arguments[1])==3&&
       startup->Arguments[1][0]=='g'&&startup->Arguments[1][1]=='u'&&
       startup->Arguments[1][2]=='i') {
        name=startup->Arguments[2];
        name_length=text_length(name);
        if(!name_length||name_length>BOB64_SYSCALL_MAX_FILENAME)
            return -9;
        result=gui_view_file(name,name_length);
        if(result==-28)write_text("file too large for viewer\n");
        else if(result<0)write_text("cannot open viewer file\n");
        return result;
    }
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
