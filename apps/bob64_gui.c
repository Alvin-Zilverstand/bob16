#include "../bob64/app.h"
#include "../bob64/editor.h"
#include "../bob64/gfx.h"
#include "../bob64/window.h"

#define BOB64_GUI_MAX_WIDTH 1280u
#define BOB64_GUI_MAX_HEIGHT 800u
#define BOB64_GUI_MAX_WINDOWS 4u
#define BOB64_GUI_FILENAME_CAPACITY 64u
#define BOB64_GUI_SURFACE_PIXEL_CAPACITY 1048576u

static u32 pixels[BOB64_GUI_SURFACE_PIXEL_CAPACITY];
static BOB64_GFX graphics[BOB64_GUI_MAX_WINDOWS];
static struct {usize Offset,Bytes;u8 Used;} surface_allocations[BOB64_GUI_MAX_WINDOWS];
static BOB64_WINDOW_MANAGER manager;
static BOB64_WINDOW windows[BOB64_GUI_MAX_WINDOWS];
static BOB64_WINDOW_HANDLE order[BOB64_GUI_MAX_WINDOWS];
static char titles[BOB64_GUI_MAX_WINDOWS][16]={"Files","Editor","Window 3","Window 4"};
static char filename[BOB64_GUI_FILENAME_CAPACITY];
static char editor_status[32];
static BOB64_EDITOR editor;
static BOB64_WINDOW_HANDLE editor_window;
static BOB64_WINDOW_HANDLE files_window;
static u32 screen_width,screen_height;
static BOB64_FILE_INFO file_entries[BOB64_SYSCALL_MAX_FILES];
static u32 file_count,selected_file,file_scroll;
static int editor_save_allowed;
static int editor_exit_armed;
static int editor_open_armed;
static int file_delete_armed;

static usize gui_length(const char *text,usize limit) {
    usize length=0;
    if(!text)return limit;
    while(length<limit&&text[length])length++;
    return length;
}

static void gui_copy(char *destination,const char *source,usize length) {
    for(usize i=0;i<length;i++)destination[i]=source[i];
    destination[length]=0;
}

static BOB64_GFX *gui_graphics_for_handle(BOB64_WINDOW_HANDLE handle) {
    for(u32 i=0;i<BOB64_GUI_MAX_WINDOWS;i++)
        if(windows[i].Handle==handle)return &graphics[i];
    return 0;
}

static u32 *gui_surface_allocate(u32 slot,u32 width,u32 height) {
    usize bytes,candidate=0;
    if(slot>=BOB64_GUI_MAX_WINDOWS||surface_allocations[slot].Used||!width||!height||
       (u64)width*height>BOB64_GUI_SURFACE_PIXEL_CAPACITY)return 0;
    bytes=(usize)width*height*sizeof(u32);
    for(;;) {
        usize next=sizeof(pixels);
        int next_slot=-1;
        for(u32 i=0;i<BOB64_GUI_MAX_WINDOWS;i++)
            if(surface_allocations[i].Used&&surface_allocations[i].Offset>=candidate&&
               surface_allocations[i].Offset<next) {
                next=surface_allocations[i].Offset;next_slot=(int)i;
            }
        if(bytes<=next-candidate)break;
        if(next_slot<0)return 0;
        candidate=surface_allocations[next_slot].Offset+
                  surface_allocations[next_slot].Bytes;
        if(candidate>=sizeof(pixels))return 0;
    }
    if(candidate>sizeof(pixels)||bytes>sizeof(pixels)-candidate)return 0;
    surface_allocations[slot].Offset=candidate;
    surface_allocations[slot].Bytes=bytes;
    surface_allocations[slot].Used=1;
    return pixels+candidate/sizeof(u32);
}

static void gui_surface_release(u32 slot) {
    if(slot>=BOB64_GUI_MAX_WINDOWS)return;
    surface_allocations[slot].Offset=0;surface_allocations[slot].Bytes=0;
    surface_allocations[slot].Used=0;
}

static BOB64_WINDOW_HANDLE gui_create_window(s32 x,s32 y,u32 width,u32 height,
                                              const char *title) {
    BOB64_WINDOW_HANDLE kernel_handle;
    BOB64_WINDOW_HANDLE app_handle;
    BOB64_WINDOW *window;
    u32 slot=0;
    u32 *surface;
    while(slot<BOB64_GUI_MAX_WINDOWS&&windows[slot].Handle)slot++;
    if(slot>=BOB64_GUI_MAX_WINDOWS||!(surface=gui_surface_allocate(slot,width,height)))
        return 0;
    kernel_handle=bob64_app_window_create(x,y,width,height);
    if(!kernel_handle||(s64)kernel_handle<0) {
        gui_surface_release(slot);return 0;
    }
    manager.NextHandle=kernel_handle;
    app_handle=bob64_wm_create(&manager,x,y,width,height,(void *)title);
    if(app_handle!=kernel_handle) {
        if(app_handle)bob64_wm_destroy(&manager,app_handle);
        bob64_app_window_destroy(kernel_handle);gui_surface_release(slot);return 0;
    }
    window=bob64_wm_find(&manager,app_handle);
    slot=(u32)(window-windows);
    graphics[slot].Pixels=surface;graphics[slot].Width=width;
    graphics[slot].Height=height;
    graphics[slot].Capacity=(usize)BOB64_GUI_MAX_WIDTH*BOB64_GUI_MAX_HEIGHT;
    return app_handle;
}

static int gui_destroy_window(BOB64_WINDOW_HANDLE handle) {
    u32 slot=0;
    while(slot<BOB64_GUI_MAX_WINDOWS&&windows[slot].Handle!=handle)slot++;
    if(slot>=BOB64_GUI_MAX_WINDOWS)return -1;
    if(bob64_app_window_destroy(handle))return -1;
    if(bob64_wm_destroy(&manager,handle))return -1;
    gui_surface_release(slot);
    return 0;
}

static void gui_focus_window(BOB64_WINDOW_HANDLE handle) {
    if(bob64_wm_focus(&manager,handle)||bob64_app_window_focus(handle))
        gui_copy(editor_status,"FOCUS FAILED",sizeof("FOCUS FAILED"));
}

static int gui_load_file(const BOB64_APP_STARTUP *startup) {
    const char *requested="notes.txt";
    usize length=sizeof("notes.txt")-1;
    s64 result;
    if(startup->ArgumentCount>1) {
        requested=startup->Arguments[1];
        length=gui_length(requested,BOB64_GUI_FILENAME_CAPACITY);
        if(!length||length>=BOB64_GUI_FILENAME_CAPACITY)return -1;
    }
    for(usize i=0;i<length;i++) {
        char value=requested[i];
        if(!((value>='a'&&value<='z')||(value>='A'&&value<='Z')||
             (value>='0'&&value<='9')||value=='.'||value=='_'||value=='-'))return -1;
    }
    gui_copy(filename,requested,length);
    result=bob64_app_read_file(filename,length,editor.Text,BOB64_EDITOR_CAPACITY);
    if(result==-2) {
        bob64_editor_init(&editor,0,0);
        gui_copy(editor_status,"NEW FILE",sizeof("NEW FILE"));
        editor_save_allowed=1;
    } else if(result==-28) {
        bob64_editor_init(&editor,0,0);
        gui_copy(editor_status,"FILE TOO LARGE",sizeof("FILE TOO LARGE"));
        editor_save_allowed=0;
    } else if(result<0) {
        return -1;
    } else {
        bob64_editor_init(&editor,editor.Text,(u32)result);
        gui_copy(editor_status,"LOADED",sizeof("LOADED"));
        editor_save_allowed=1;
    }
    return 0;
}

static void gui_refresh_files(void) {
    s64 result=bob64_app_list_files(file_entries,BOB64_SYSCALL_MAX_FILES);
    if(result<0) {
        file_count=0;
        gui_copy(editor_status,result==-28?"TOO MANY FILES":"LIST FAILED",
                 result==-28?sizeof("TOO MANY FILES"):sizeof("LIST FAILED"));
        return;
    }
    file_count=(u32)result;
    if(selected_file>=file_count)selected_file=file_count?file_count-1:0;
    if(file_scroll>=file_count)file_scroll=file_count?file_count-1:0;
}

static void gui_reveal_selected_file(void) {
    BOB64_WINDOW *window=bob64_wm_find(&manager,files_window);
    u32 rows=window&&window->Height>58?(window->Height-58)/14:1;
    if(!rows)rows=1;
    if(selected_file<file_scroll)file_scroll=selected_file;
    else if(selected_file>=file_scroll+rows)file_scroll=selected_file-rows+1;
}

static int gui_name_equal(const char *left,const char *right) {
    while(*left&&*right&&*left==*right){left++;right++;}
    return !*left&&!*right;
}

static void gui_open_selected_file(void) {
    s64 result;
    if(selected_file>=file_count) {
        gui_copy(editor_status,"NO FILE SELECTED",sizeof("NO FILE SELECTED"));return;
    }
    const char *name=file_entries[selected_file].Name;
    if(editor.Dirty&&!editor_open_armed) {
        gui_copy(editor_status,"OPEN AGAIN TO DISCARD",sizeof("OPEN AGAIN TO DISCARD"));
        editor_open_armed=1;return;
    }
    if(gui_name_equal(name,filename)) {
        gui_focus_window(editor_window);
        gui_copy(editor_status,"CURRENT FILE",sizeof("CURRENT FILE"));
        editor_open_armed=0;return;
    }
    result=bob64_app_read_file(name,gui_length(name,BOB64_GUI_FILENAME_CAPACITY),
                               editor.Text,BOB64_EDITOR_CAPACITY);
    if(result==-28) {
        gui_copy(editor_status,"FILE TOO LARGE",sizeof("FILE TOO LARGE"));
        editor_save_allowed=0;return;
    }
    if(result<0) {
        gui_copy(editor_status,"OPEN FAILED",sizeof("OPEN FAILED"));return;
    }
    gui_copy(filename,name,gui_length(name,BOB64_GUI_FILENAME_CAPACITY));
    bob64_editor_init(&editor,editor.Text,(u32)result);
    editor_save_allowed=1;editor_open_armed=0;
    gui_copy(editor_status,"LOADED",sizeof("LOADED"));
    gui_focus_window(editor_window);
}

static void gui_delete_selected_file(void) {
    const char *name;
    s64 result;
    if(selected_file>=file_count) {
        gui_copy(editor_status,"NO FILE SELECTED",sizeof("NO FILE SELECTED"));return;
    }
    name=file_entries[selected_file].Name;
    if(gui_name_equal(name,filename)) {
        file_delete_armed=0;
        gui_copy(editor_status,"SAVE OR OPEN OTHER FILE",sizeof("SAVE OR OPEN OTHER FILE"));
        return;
    }
    if(!file_delete_armed) {
        file_delete_armed=1;
        gui_copy(editor_status,"D AGAIN TO DELETE",sizeof("D AGAIN TO DELETE"));
        return;
    }
    result=bob64_app_delete_file(name,gui_length(name,BOB64_GUI_FILENAME_CAPACITY));
    file_delete_armed=0;
    if(result)gui_copy(editor_status,"DELETE FAILED",sizeof("DELETE FAILED"));
    else {gui_refresh_files();gui_copy(editor_status,"DELETED",sizeof("DELETED"));}
}

static void gui_draw_window(const BOB64_WINDOW *window,int focused) {
    BOB64_WINDOW local=*window;
    BOB64_GFX *graphics;
    local.X=0;local.Y=0;window=&local;
    graphics=gui_graphics_for_handle(window->Handle);
    if(!graphics)return;
    const char *title=(const char *)window->Context;
    u32 border=focused?0x00a4c8ffu:0x006d8095u;
    u32 title_bar=focused?0x00335d8cu:0x00334050u;
    bob64_gfx_fill_rect(graphics,window->X,window->Y,window->Width,
                        window->Height,0x0009111bu);
    bob64_gfx_fill_rect(graphics,window->X+2,window->Y+2,window->Width-4,
                        window->Height-4,0x001a2a3cu);
    bob64_gfx_fill_rect(graphics,window->X,window->Y,window->Width,
                        BOB64_WINDOW_TITLE_BAR_HEIGHT,title_bar);
    bob64_gfx_line(graphics,window->X,window->Y,
                   window->X+(s32)window->Width-1,window->Y,border);
    bob64_gfx_line(graphics,window->X,window->Y+(s32)window->Height-1,
                   window->X+(s32)window->Width-1,
                   window->Y+(s32)window->Height-1,border);
    bob64_gfx_line(graphics,window->X,window->Y,window->X,
                   window->Y+(s32)window->Height-1,border);
    bob64_gfx_line(graphics,window->X+(s32)window->Width-1,window->Y,
                   window->X+(s32)window->Width-1,
                   window->Y+(s32)window->Height-1,border);
    bob64_gfx_text(graphics,window->X+12,window->Y+8,title,0x00ffffffu,1);
    if(window->Handle==editor_window) {
        u32 columns=(window->Width-24)/6;
        u32 rows=(window->Height-76)/10;
        u32 column=0,row=0;
        u32 cursor_row,cursor_column;
        s32 text_x=window->X+12,text_y=window->Y+46;
        int caret_visible=0;
        if(columns==0)columns=1;
        bob64_editor_visual_position(&editor,columns,&cursor_row,&cursor_column);
        (void)cursor_column;
        if(cursor_row<editor.ScrollRow)editor.ScrollRow=cursor_row;
        else if(rows&&cursor_row>=editor.ScrollRow+rows)
            editor.ScrollRow=cursor_row-rows+1;
        bob64_gfx_text(graphics,window->X+12,window->Y+30,filename,
                       0x009eb3c7u,1);
        for(u32 i=0;i<=editor.Length;i++) {
            if(column>=columns&&(i==editor.Length||editor.Text[i]!='\n')) {
                row++;column=0;
            }
            if(i==editor.Cursor) {
                s32 caret_x=text_x+(s32)(column*6);
                s32 caret_y=text_y+(s32)((row-editor.ScrollRow)*10);
                if(row>=editor.ScrollRow&&row<editor.ScrollRow+rows)
                    bob64_gfx_line(graphics,caret_x,caret_y,
                    caret_x,caret_y+7,0x00ffffffu);
                caret_visible=1;
            }
            if(i==editor.Length)break;
            if(editor.Text[i]=='\n') {
                row++;column=0;
                continue;
            }
            if(row>=editor.ScrollRow&&row<editor.ScrollRow+rows) {
                char character[2]={editor.Text[i],0};
                bob64_gfx_text(graphics,text_x+(s32)(column*6),
                    text_y+(s32)((row-editor.ScrollRow)*10),character,
                    0x00e2ebf5u,1);
            }
            column++;
        }
        if(!caret_visible&&row>=editor.ScrollRow&&row<editor.ScrollRow+rows)
            bob64_gfx_line(graphics,text_x+(s32)(column*6),
                text_y+(s32)((row-editor.ScrollRow)*10),
                text_x+(s32)(column*6),text_y+(s32)((row-editor.ScrollRow)*10)+7,
                0x00ffffffu);
        bob64_gfx_line(graphics,window->X+8,
            window->Y+(s32)window->Height-22,
            window->X+(s32)window->Width-8,
            window->Y+(s32)window->Height-22,0x0044576au);
        bob64_gfx_text(graphics,window->X+12,
            window->Y+(s32)window->Height-16,editor_status,0x008fc8ffu,1);
        bob64_gfx_text(graphics,window->X+(s32)window->Width/2,
            window->Y+(s32)window->Height-16,"CTRL S SAVE",0x009eb3c7u,1);
    } else if(window->Handle==files_window) {
        u32 rows=window->Height>58?(window->Height-58)/14:0;
        bob64_gfx_text(graphics,window->X+10,window->Y+31,
            "UP DOWN OPEN R REFRESH",0x009eb3c7u,1);
        bob64_gfx_text(graphics,window->X+10,window->Y+41,
            "D TWICE DELETE",0x009eb3c7u,1);
        bob64_gfx_text(graphics,window->X+10,window->Y+49,editor_status,
                       0x008fc8ffu,1);
        for(u32 row=0;row<rows&&file_scroll+row<file_count;row++) {
            u32 index=file_scroll+row;
            s32 y=window->Y+56+(s32)(row*14);
            if(index==selected_file)
                bob64_gfx_fill_rect(graphics,window->X+6,y-2,
                    window->Width-12,12,0x00335d8cu);
            bob64_gfx_text(graphics,window->X+10,y,
                file_entries[index].Name,index==selected_file?
                0x00ffffffu:0x00dbe8f4u,1);
        }
        if(!file_count)bob64_gfx_text(graphics,window->X+10,window->Y+60,
            "NO FILES",0x008fc8ffu,1);
    } else if(window->Handle==manager.Focused) {
        bob64_gfx_text(graphics,window->X+14,window->Y+46,
            "BOB64 DESKTOP",0x007ed6ffu,1);
        bob64_gfx_text(graphics,window->X+14,window->Y+66,
            "RUN DESKTOP B64E FILE",0x00dbe8f4u,1);
        bob64_gfx_text(graphics,window->X+14,window->Y+84,
            "TAB SWITCH WINDOWS",0x00dbe8f4u,1);
        bob64_gfx_text(graphics,window->X+14,window->Y+102,
            "ESC RETURN TO SHELL",0x00dbe8f4u,1);
        bob64_gfx_text(graphics,window->X+14,window->Y+120,
            "N NEW X CLOSE OTHER",0x00dbe8f4u,1);
    } else {
        bob64_gfx_text(graphics,window->X+14,window->Y+46,
            "DRAG WINDOW TITLE",0x00dbe8f4u,1);
    }
}

static int gui_draw(s32 cursor_x,s32 cursor_y) {
    for(u32 i=0;i<manager.Count;i++) {
        BOB64_WINDOW *window=bob64_wm_find(&manager,manager.ZOrder[i]);
        BOB64_GFX *surface;
        if(!window||!(window->Flags&BOB64_WINDOW_VISIBLE))continue;
        surface=gui_graphics_for_handle(window->Handle);
        if(!surface)continue;
        bob64_gfx_fill_rect(surface,0,0,surface->Width,surface->Height,
                            0x000d1723u);
        for(u32 y=0;y<surface->Height;y+=4)
            bob64_gfx_line(surface,0,(s32)y,(s32)surface->Width-1,(s32)y,
                           0x00132130u);
        gui_draw_window(window,window->Handle==manager.Focused);
        if(cursor_x>=window->X&&cursor_y>=window->Y&&
           (u32)(cursor_x-window->X)<window->Width&&
           (u32)(cursor_y-window->Y)<window->Height) {
            s32 x=cursor_x-window->X,y=cursor_y-window->Y;
            bob64_gfx_line(surface,x-5,y,x+5,y,0x00000000u);
            bob64_gfx_line(surface,x,y-5,x,y+5,0x00000000u);
            bob64_gfx_line(surface,x-3,y,x+3,y,0x00ffffffu);
            bob64_gfx_line(surface,x,y-3,x,y+3,0x00ffffffu);
        }
        if(bob64_app_window_present(window->Handle,surface->Pixels,
            surface->Width,surface->Height)!=(s64)((u64)surface->Width*surface->Height))
            return -1;
    }
    return 0;
}

static void gui_save_editor(void) {
    if(!editor_save_allowed) {
        gui_copy(editor_status,"FILE TOO LARGE",sizeof("FILE TOO LARGE"));
        return;
    }
    s64 result=bob64_app_write_file(filename,gui_length(filename,
        BOB64_GUI_FILENAME_CAPACITY),editor.Text,editor.Length);
    if(result==(s64)editor.Length) {
        editor.Dirty=0;gui_copy(editor_status,"SAVED",sizeof("SAVED"));
    } else gui_copy(editor_status,"SAVE FAILED",sizeof("SAVE FAILED"));
}

static void gui_editor_place_cursor(u32 target_row,u32 target_column,u32 columns) {
    u32 row=0,column=0;
    if(!columns)columns=1;
    target_row+=editor.ScrollRow;
    for(u32 i=0;i<=editor.Length;i++) {
        if(column>=columns&&(i==editor.Length||editor.Text[i]!='\n')) {
            row++;column=0;
        }
        if(row==target_row&&column>=target_column) {
            editor.Cursor=i;return;
        }
        if(i==editor.Length)break;
        if(editor.Text[i]=='\n') {
            if(row==target_row){editor.Cursor=i;return;}
            row++;column=0;continue;
        }
        column++;
    }
    editor.Cursor=editor.Length;
}

static void gui_focus_next(void) {
    if(!manager.Count)return;
    u32 start=0;
    for(u32 i=0;i<manager.Count;i++)if(manager.ZOrder[i]==manager.Focused) {
        start=i+1;break;
    }
    for(u32 offset=0;offset<manager.Count;offset++) {
        BOB64_WINDOW_HANDLE handle=manager.ZOrder[(start+offset)%manager.Count];
        BOB64_WINDOW *window=bob64_wm_find(&manager,handle);
        if(window&&(window->Flags&BOB64_WINDOW_VISIBLE)) {
            gui_focus_window(handle);return;
        }
    }
}

static void gui_key(const BOB64_EVENT *event) {
    if(event->Type!=BOB64_EVENT_KEY_DOWN)return;
    if(event->Character=='\t')gui_focus_next();
    else if(manager.Focused==files_window) {
        if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x48)) {
            if(selected_file)selected_file--;
            gui_reveal_selected_file();
            file_delete_armed=editor_open_armed=0;
        } else if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x50)) {
            if(selected_file+1<file_count)selected_file++;
            gui_reveal_selected_file();
            file_delete_armed=editor_open_armed=0;
        } else if(event->Character=='r'||event->Character=='R') {
            gui_refresh_files();file_delete_armed=editor_open_armed=0;
        } else if(event->Character=='o'||event->Character=='O'||
                  event->Character=='\n')gui_open_selected_file();
        else if(event->Character=='d'||event->Character=='D')gui_delete_selected_file();
        else file_delete_armed=editor_open_armed=0;
    }
    else if(event->Character=='n'&&manager.Count<BOB64_GUI_MAX_WINDOWS) {
        u32 index=manager.Count;
        BOB64_WINDOW_HANDLE handle=gui_create_window(
            (s32)(screen_width/5+index*24),(s32)(screen_height/5+index*20),
            screen_width/3,screen_height/3,titles[index]);
        if(!handle)gui_copy(editor_status,"WINDOW CREATE FAILED",
                            sizeof("WINDOW CREATE FAILED"));
    } else if(event->Character=='x'&&manager.Count>1&&manager.Focused!=editor_window) {
        if(gui_destroy_window(manager.Focused))
            gui_copy(editor_status,"WINDOW CLOSE FAILED",sizeof("WINDOW CLOSE FAILED"));
    }
    else if(manager.Focused==editor_window) {
        if((event->Modifiers&BOB64_EVENT_MOD_CONTROL)&&
           (event->Character=='s'||event->Character=='S'))gui_save_editor();
        else bob64_editor_handle_key(&editor,event);
    }
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    BOB64_EVENT event;
    BOB64_EVENT routed;
    BOB64_EVENT screen_event;
    s32 cursor_x,cursor_y;
    if(!startup||startup->AbiVersion!=BOB64_APP_ABI_VERSION||
       bob64_app_query_abi()!=BOB64_SYSCALL_ABI_VERSION)return -1;
    if(bob64_app_write("bob!",4)!=4)return -3;
    if(bob64_app_get_display(&screen_width,&screen_height))return -6;
    if(screen_width<480||screen_height<300||screen_width>BOB64_GUI_MAX_WIDTH||
       screen_height>BOB64_GUI_MAX_HEIGHT)return -7;
    if(bob64_wm_init(&manager,windows,BOB64_GUI_MAX_WINDOWS,order,
                     screen_width,screen_height))return -8;
    if(gui_load_file(startup))return -9;
    gui_refresh_files();
    cursor_x=(s32)(screen_width/2);cursor_y=(s32)(screen_height/2);
    if(!gui_create_window(8,24,screen_width/3,screen_height-32,titles[0])||
       !gui_create_window((s32)(screen_width/3+16),24,
         screen_width-screen_width/3-24,screen_height-32,titles[1]))return -2;
    files_window=manager.ZOrder[0];
    editor_window=manager.Focused;
    for(;;) {
        if(gui_draw(cursor_x,cursor_y))return -4;
        if(bob64_app_wait_event(&event))return -5;
        if(event.Type==BOB64_EVENT_KEY_DOWN&&event.Character==0x1b) {
            if(editor.Dirty&&!editor_exit_armed) {
                gui_copy(editor_status,"ESC AGAIN TO QUIT",sizeof("ESC AGAIN TO QUIT"));
                editor_exit_armed=1;continue;
            }
            break;
        }
        if(event.Type==BOB64_EVENT_KEY_DOWN)editor_exit_armed=0;
        if(event.Type==BOB64_EVENT_MOUSE_MOVE||event.Type==BOB64_EVENT_MOUSE_BUTTON||
           event.Type==BOB64_EVENT_MOUSE_WHEEL)
            cursor_x=event.ScreenX,cursor_y=event.ScreenY;
        screen_event=event;screen_event.X=event.ScreenX;screen_event.Y=event.ScreenY;
        bob64_wm_dispatch(&manager,&screen_event,&routed);
        if(event.Type==BOB64_EVENT_MOUSE_BUTTON&&(event.Buttons&1u)&&
           manager.Focused==files_window&&routed.X>=8&&routed.Y>=56) {
            u32 row=(u32)(routed.Y-56)/14;
            if(file_scroll+row<file_count)selected_file=file_scroll+row;
        }
        if(event.Type==BOB64_EVENT_MOUSE_BUTTON&&(event.Buttons&1u)&&
           manager.Focused==editor_window&&routed.X>=12&&routed.Y>=46) {
            BOB64_WINDOW *window=bob64_wm_find(&manager,editor_window);
            if(window)gui_editor_place_cursor((u32)(routed.Y-46)/10,
                (u32)(routed.X-12)/6,(window->Width-24)/6);
        }
        gui_key(&event);
        if(editor.Dirty)gui_copy(editor_status,"UNSAVED",sizeof("UNSAVED"));
    }
    return 0;
}
