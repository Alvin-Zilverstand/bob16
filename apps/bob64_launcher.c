#include "../bob64/app.h"
#include "../bob64/gfx.h"
#include "../bob64/widgets.h"
#include "../bob64/window.h"

#define LAUNCHER_WIDTH 360u
#define LAUNCHER_HEIGHT 280u
#define LAUNCHER_ROWS 9u

static u32 pixels[LAUNCHER_WIDTH*LAUNCHER_HEIGHT];
static BOB64_GFX graphics;
static BOB64_WINDOW_MANAGER manager;
static BOB64_WINDOW window;
static BOB64_WINDOW_HANDLE order[1];
static BOB64_FILE_INFO files[BOB64_SYSCALL_MAX_FILES];
static u32 application_files[BOB64_SYSCALL_MAX_FILES];
static u32 application_count,selected_application,scroll;
static BOB64_RECT run_button={12,36,72,18};
static BOB64_RECT close_button={92,36,72,18};
static BOB64_BUTTON_STATE run_state,close_state;
static char status[32];

static usize length(const char *text) {
    usize result=0;
    if(text)while(text[result])result++;
    return result;
}

static void copy(char *to,const char *from,usize limit) {
    usize i=0;
    while(i+1<limit&&from[i]){to[i]=from[i];i++;}
    to[i]=0;
}

static int app_name(const char *name) {
    usize size=length(name);
    return size>5&&name[size-5]=='.'&&name[size-4]=='b'&&
           name[size-3]=='6'&&name[size-2]=='4'&&name[size-1]=='e';
}

static int self_name(const char *name) {
    static const char self[]="launcher.b64e";
    usize size=length(name);
    if(size!=sizeof(self)-1)return 0;
    for(usize i=0;i<size;i++) {
        char a=name[i],b=self[i];
        if(a>='A'&&a<='Z')a=(char)(a-'A'+'a');
        if(a!=b)return 0;
    }
    return 1;
}

static void refresh(void) {
    s64 count=bob64_app_list_files(files,BOB64_SYSCALL_MAX_FILES);
    application_count=0;
    if(count<0) {
        copy(status,"FILE LIST FAILED",sizeof(status));return;
    }
    for(u32 i=0;i<(u32)count;i++)
        if(app_name(files[i].Name)&&!self_name(files[i].Name))
            application_files[application_count++]=i;
    if(selected_application>=application_count)
        selected_application=application_count?application_count-1:0;
    if(scroll>selected_application)scroll=selected_application;
    copy(status,application_count?"SELECT AN APP":"NO APPS INSTALLED",
         sizeof(status));
}

static void reveal_selection(void) {
    if(selected_application<scroll)scroll=selected_application;
    else if(selected_application>=scroll+LAUNCHER_ROWS)
        scroll=selected_application-LAUNCHER_ROWS+1;
}

static void draw(void) {
    bob64_gfx_fill_rect(&graphics,0,0,LAUNCHER_WIDTH,LAUNCHER_HEIGHT,
                        0x001a2a3cu);
    bob64_gfx_fill_rect(&graphics,0,0,LAUNCHER_WIDTH,
                        BOB64_WINDOW_TITLE_BAR_HEIGHT,0x00335d8cu);
    bob64_gfx_text(&graphics,12,8,"APPLICATION LAUNCHER",0x00ffffffu,1);
    bob64_gfx_text(&graphics,12,28,"SELECT AN APP TO RUN",0x009eb3c7u,1);
    bob64_button_draw(&graphics,&run_button,"RUN APP",&run_state);
    bob64_button_draw(&graphics,&close_button,"CLOSE",&close_state);
    bob64_gfx_text(&graphics,180,42,status,0x008fc8ffu,1);
    for(u32 row=0;row<LAUNCHER_ROWS;row++) {
        u32 index=scroll+row;
        if(index>=application_count)break;
        s32 y=68+(s32)(row*20);
        if(index==selected_application)
            bob64_gfx_fill_rect(&graphics,8,y-3,344,17,0x00335d8cu);
        bob64_gfx_text(&graphics,14,y,
            files[application_files[index]].Name,
            index==selected_application?0x00ffffffu:0x00dbe8f4u,1);
    }
    if(bob64_app_window_present(window.Handle,graphics.Pixels,
       graphics.Width,graphics.Height)!=(s64)(LAUNCHER_WIDTH*LAUNCHER_HEIGHT))
        bob64_app_exit(-2);
}

static void launch_selected(void) {
    s64 child_status=-1,result;
    if(selected_application>=application_count) {
        copy(status,"NO APP SELECTED",sizeof(status));return;
    }
    const char *name=files[application_files[selected_application]].Name;
    const char *arguments[1]={name};
    result=bob64_app_run(name,length(name),1,arguments,&child_status);
    refresh();
    if(result||child_status)copy(status,"APP FAILED",sizeof(status));
    else copy(status,"APP EXITED OK",sizeof(status));
    (void)bob64_app_window_focus(window.Handle);
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    BOB64_EVENT event,screen_event,routed;
    s64 target;
    BOB64_WINDOW_HANDLE kernel_handle,app_handle;
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION||
       bob64_app_query_abi()!=BOB64_SYSCALL_ABI_VERSION)return -1;
    if(bob64_app_write("bob!",4)!=4)return -2;
    if(startup->ArgumentCount>1) {
        s64 child_status=-1,result;
        if(!startup->Arguments||!startup->Arguments[1])return -3;
        const char *name=startup->Arguments[1];
        result=bob64_app_run(name,length(name),
            (usize)(startup->ArgumentCount-1),startup->Arguments+1,
            &child_status);
        return result?result:child_status;
    }
    u32 screen_width,screen_height;
    if(bob64_app_get_display(&screen_width,&screen_height)||
       screen_width<LAUNCHER_WIDTH||screen_height<LAUNCHER_HEIGHT)return -3;
    if(bob64_wm_init(&manager,&window,1,order,screen_width,screen_height))return -4;
    kernel_handle=bob64_app_window_create(
        (s32)(screen_width-LAUNCHER_WIDTH)/2,
        (s32)(screen_height-LAUNCHER_HEIGHT)/2,
        LAUNCHER_WIDTH,LAUNCHER_HEIGHT);
    if(!kernel_handle||(s64)kernel_handle<0)return -5;
    manager.NextHandle=kernel_handle;
    app_handle=bob64_wm_create(&manager,
        (s32)(screen_width-LAUNCHER_WIDTH)/2,
        (s32)(screen_height-LAUNCHER_HEIGHT)/2,
        LAUNCHER_WIDTH,LAUNCHER_HEIGHT,(void *)"Application launcher");
    if(app_handle!=kernel_handle) {
        if(app_handle)bob64_wm_destroy(&manager,app_handle);
        bob64_app_window_destroy(kernel_handle);return -6;
    }
    graphics.Pixels=pixels;graphics.Width=LAUNCHER_WIDTH;
    graphics.Height=LAUNCHER_HEIGHT;
    graphics.Capacity=LAUNCHER_WIDTH*LAUNCHER_HEIGHT;
    refresh();
    for(;;) {
        draw();
        if(bob64_app_wait_event(&event))return -7;
        if(event.Type==BOB64_EVENT_KEY_DOWN&&event.Character==0x1b)break;
        screen_event=event;screen_event.X=event.ScreenX;
        screen_event.Y=event.ScreenY;
        target=bob64_wm_dispatch(&manager,&screen_event,&routed);
        if(event.Type==BOB64_EVENT_MOUSE_MOVE) {
            BOB64_EVENT local=event;
            local.X=event.ScreenX-window.X;local.Y=event.ScreenY-window.Y;
            bob64_button_event(&run_button,&run_state,&local);
            bob64_button_event(&close_button,&close_state,&local);
        }
        if(event.Type==BOB64_EVENT_MOUSE_WHEEL&&
           bob64_rect_contains(&(BOB64_RECT){window.X,window.Y+64,
               window.Width,window.Height-64},event.ScreenX,event.ScreenY)) {
            if(event.Wheel>0&&selected_application)selected_application--;
            else if(event.Wheel<0&&selected_application+1<application_count)
                selected_application++;
            reveal_selection();
        }
        if(event.Type==BOB64_EVENT_KEY_DOWN) {
            if(event.Key==(BOB64_EVENT_KEY_EXTENDED|0x48)) {
                if(selected_application)selected_application--;
                reveal_selection();
            } else if(event.Key==(BOB64_EVENT_KEY_EXTENDED|0x50)) {
                if(selected_application+1<application_count)selected_application++;
                reveal_selection();
            } else if(event.Character=='\n'||event.Character=='a'||
                      event.Character=='A'||event.Character=='r'||
                      event.Character=='R')launch_selected();
            else if(event.Character=='x'||event.Character=='X')break;
        }
        if(event.Type==BOB64_EVENT_MOUSE_BUTTON&&target==(s64)window.Handle) {
            BOB64_EVENT local=event;local.X=routed.X;local.Y=routed.Y;
            int run=bob64_button_event(&run_button,&run_state,&local);
            int close=bob64_button_event(&close_button,&close_state,&local);
            if(!(event.Buttons&BOB64_BUTTON_LEFT)&&close)break;
            if(!(event.Buttons&BOB64_BUTTON_LEFT)&&run)launch_selected();
            else if((event.Buttons&BOB64_BUTTON_LEFT)&&routed.Y>=64) {
                u32 row=(u32)(routed.Y-68)/20;
                if(routed.Y>=68&&scroll+row<application_count)
                    selected_application=scroll+row;
            }
        }
    }
    bob64_app_window_destroy(window.Handle);
    bob64_wm_destroy(&manager,window.Handle);
    return 0;
}
