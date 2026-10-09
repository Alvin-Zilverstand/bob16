#include "../bob64/app.h"
#include "../bob64/gfx.h"
#include "../bob64/widgets.h"

#define ECHO_WINDOW_WIDTH 640u
#define ECHO_WINDOW_HEIGHT 400u
#define ECHO_LINE_CAPACITY 96u
#define ECHO_LINE_LIMIT 48u
#define ECHO_VISIBLE_LINES 26u

static u32 pixels[ECHO_WINDOW_WIDTH*ECHO_WINDOW_HEIGHT];
static char lines[ECHO_LINE_LIMIT][ECHO_LINE_CAPACITY];
static u32 line_count,scroll_offset;
static BOB64_RECT close_button={552,4,72,18};
static BOB64_BUTTON_STATE close_state;

static usize text_length(const char *text) {
    usize length=0;
    if(!text)return 0;
    while(text[length])length++;
    return length;
}

static int text_equals(const char *left,const char *right) {
    usize i=0;
    if(!left||!right)return 0;
    while(left[i]&&right[i]&&left[i]==right[i])i++;
    return !left[i]&&!right[i];
}

static void append_new_line(void) {
    if(line_count<ECHO_LINE_LIMIT)line_count++;
    else {
        const char marker[]="...";
        for(usize i=0;i<sizeof(marker);i++)
            lines[ECHO_LINE_LIMIT-1][i]=marker[i];
    }
}

static void append_text(const char *text) {
    if(!text)return;
    for(usize i=0;text[i];i++) {
        if(line_count>=ECHO_LINE_LIMIT)return;
        u32 row=line_count-1;
        usize column=text_length(lines[row]);
        if(text[i]=='\n') {
            append_new_line();
        } else {
            if(column>=ECHO_LINE_CAPACITY-1) {
                append_new_line();
                if(line_count>=ECHO_LINE_LIMIT)return;
                row=line_count-1;column=0;
            }
            lines[row][column]=text[i];
            lines[row][column+1]=0;
        }
    }
}

static void build_lines(const BOB64_APP_STARTUP *startup) {
    line_count=1;scroll_offset=0;
    for(u64 i=2;i<startup->ArgumentCount;i++) {
        if(i>2)append_text(" ");
        append_text(startup->Arguments[i]);
    }
}

static void reveal_scroll(void) {
    u32 maximum=line_count>ECHO_VISIBLE_LINES?
        line_count-ECHO_VISIBLE_LINES:0;
    if(scroll_offset>maximum)scroll_offset=maximum;
}

static int draw_window(BOB64_GFX *graphics) {
    bob64_gfx_fill_rect(graphics,0,0,ECHO_WINDOW_WIDTH,ECHO_WINDOW_HEIGHT,
                        0x001a2a3cu);
    bob64_gfx_fill_rect(graphics,0,0,ECHO_WINDOW_WIDTH,26,0x00335d8cu);
    bob64_gfx_text(graphics,12,8,"BOB64 ECHO",0x00ffffffu,1);
    if(bob64_button_draw(graphics,&close_button,"CLOSE",&close_state))return -1;
    bob64_gfx_text(graphics,16,34,"ARGUMENTS",0x008fc8ffu,1);
    for(u32 row=0;row<ECHO_VISIBLE_LINES&&scroll_offset+row<line_count;row++)
        bob64_gfx_text(graphics,16,52+(s32)row*12,
                       lines[scroll_offset+row],0x00e2ebf5u,1);
    bob64_gfx_line(graphics,12,374,628,374,0x006d849fu);
    bob64_gfx_text(graphics,16,382,"UP/DOWN OR WHEEL SCROLL   ESC CLOSE",
                   0x009eb3c7u,1);
    return 0;
}

static s64 run_gui(const BOB64_APP_STARTUP *startup) {
    BOB64_GFX graphics={pixels,ECHO_WINDOW_WIDTH,ECHO_WINDOW_HEIGHT,
                        ECHO_WINDOW_WIDTH*ECHO_WINDOW_HEIGHT};
    BOB64_EVENT event;
    u32 screen_width,screen_height;
    u64 window;
    s64 result=-1;
    build_lines(startup);
    if(bob64_app_get_display(&screen_width,&screen_height)||
       screen_width<ECHO_WINDOW_WIDTH||screen_height<ECHO_WINDOW_HEIGHT)return -2;
    window=bob64_app_window_create((s32)(screen_width-ECHO_WINDOW_WIDTH)/2,
        (s32)(screen_height-ECHO_WINDOW_HEIGHT)/2,
        ECHO_WINDOW_WIDTH,ECHO_WINDOW_HEIGHT);
    if(!window||(s64)window<0)return -3;
    if(bob64_app_window_focus(window)) {result=-4;goto cleanup;}
    if(bob64_app_write("bob!",4)!=4) {result=-5;goto cleanup;}
    for(;;) {
        reveal_scroll();
        if(draw_window(&graphics)) {result=-6;break;}
        result=bob64_app_window_present(window,pixels,ECHO_WINDOW_WIDTH,
                                        ECHO_WINDOW_HEIGHT);
        if(result!=(s64)(ECHO_WINDOW_WIDTH*ECHO_WINDOW_HEIGHT))break;
        if(bob64_app_wait_event(&event)) {result=-7;break;}
        if(event.Type==BOB64_EVENT_KEY_DOWN) {
            if(event.Character==0x1b||event.Character=='x'||event.Character=='X') {
                result=0;break;
            }
            if(event.Key==(BOB64_EVENT_KEY_EXTENDED|0x48)&&scroll_offset)
                scroll_offset--;
            else if(event.Key==(BOB64_EVENT_KEY_EXTENDED|0x50))scroll_offset++;
            else if(event.Key==(BOB64_EVENT_KEY_EXTENDED|0x49))
                scroll_offset=scroll_offset>ECHO_VISIBLE_LINES?
                    scroll_offset-ECHO_VISIBLE_LINES:0;
            else if(event.Key==(BOB64_EVENT_KEY_EXTENDED|0x51))
                scroll_offset+=ECHO_VISIBLE_LINES;
            else if(event.Key==(BOB64_EVENT_KEY_EXTENDED|0x47))scroll_offset=0;
            else if(event.Key==(BOB64_EVENT_KEY_EXTENDED|0x4f))
                scroll_offset=line_count;
        } else if(event.Type==BOB64_EVENT_MOUSE_WHEEL) {
            if(event.Wheel>0&&scroll_offset)scroll_offset--;
            else if(event.Wheel<0)scroll_offset++;
        } else if(event.Type==BOB64_EVENT_MOUSE_MOVE||
                  event.Type==BOB64_EVENT_MOUSE_BUTTON) {
            if(bob64_button_event(&close_button,&close_state,&event)) {
                result=0;break;
            }
        }
    }
cleanup:
    if(bob64_app_window_destroy(window)&&result==0)result=-8;
    return result;
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION||
       (startup->ArgumentCount&&!startup->Arguments))return -1;
    if(startup->ArgumentCount>1&&text_equals(startup->Arguments[1],"gui"))
        return run_gui(startup);
    for(u64 i=1;i<startup->ArgumentCount;i++) {
        const char *argument=startup->Arguments[i];
        if(i>1&&bob64_app_write_char(' ')<0)return -2;
        if(!argument||bob64_app_write(argument,text_length(argument))<0)return -3;
    }
    if(bob64_app_write_char('\n')<0)return -4;
    return 0;
}
