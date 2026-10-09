#include "../bob64/app.h"
#include "../bob64/gfx.h"
#include "../bob64/widgets.h"

#define DEMO_WIDTH 640u
#define DEMO_HEIGHT 400u

static u32 pixels[DEMO_WIDTH*DEMO_HEIGHT];
static const BOB64_RECT close_button={552,8,72,22};
static BOB64_BUTTON_STATE close_state;

static void draw_demo(BOB64_GFX *graphics) {
    u32 width=graphics->Width,height=graphics->Height;
    s32 margin=(s32)(width/40u);
    s32 header_height=(s32)(height/7u);
    s32 panel_y=(s32)(height/3u);
    u32 panel_width=(width-(u32)(margin*5))/4u;
    u32 panel_height=height/4u;
    bob64_gfx_fill_rect(graphics,0,0,width,height,0x101a2au);
    bob64_gfx_fill_rect(graphics,margin,margin,width-(u32)(margin*2),
                        height-(u32)(margin*2),0x1b2b40u);
    bob64_gfx_fill_rect(graphics,margin,margin,width-(u32)(margin*2),
                        (u32)header_height,0x345d89u);
    bob64_gfx_text(graphics,margin*2,margin*2,"BOB64 GRAPHICS DEMO",
                   0x00ffffffu,3);
    (void)bob64_button_draw(graphics,&close_button,"CLOSE",&close_state);
    bob64_gfx_line(graphics,margin,header_height+margin*2,
        (s32)width-margin,header_height+margin*2,0x00f0c879u);
    bob64_gfx_text(graphics,margin*2,panel_y-margin*2,
                   "LINES RECTANGLES AND COLORS",0x00d7e5f5u,2);
    const u32 colors[]={0x00e85d75u,0x00edaa42u,0x004fc3a1u,0x005e8ee8u};
    for(u32 i=0;i<4;i++) {
        s32 x=margin+(s32)(i*(panel_width+(u32)margin));
        s32 y=panel_y;
        bob64_gfx_fill_rect(graphics,x,y,panel_width,panel_height,colors[i]);
        bob64_gfx_fill_rect(graphics,x+8,y+8,panel_width-16,
                            panel_height-16,0x00223248u);
        bob64_gfx_line(graphics,x+14,y+panel_height-18,
            x+(s32)panel_width-14,y+18,colors[i]);
        bob64_gfx_line(graphics,x+14,y+18,
            x+(s32)panel_width-14,y+panel_height-18,colors[i]);
    }
    bob64_gfx_fill_rect(graphics,margin,panel_y+(s32)panel_height+margin,
                        width-(u32)(margin*2),3,0x006d849fu);
    bob64_gfx_text(graphics,margin*2,
        (s32)height-margin*6,"CLICK CLOSE OR PRESS X / ESCAPE",
        0x00ffffffu,2);
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    BOB64_GFX graphics;
    BOB64_EVENT event;
    u32 screen_width,screen_height;
    u64 window=0;
    s64 result=-1;
    if(!startup||startup->AbiVersion!=BOB64_APP_ABI_VERSION||
       bob64_app_query_abi()!=BOB64_SYSCALL_ABI_VERSION||
       bob64_app_get_display(&screen_width,&screen_height)||
       screen_width<DEMO_WIDTH||screen_height<DEMO_HEIGHT)return -1;
    graphics.Pixels=pixels;graphics.Width=DEMO_WIDTH;graphics.Height=DEMO_HEIGHT;
    graphics.Capacity=DEMO_WIDTH*DEMO_HEIGHT;
    draw_demo(&graphics);
    window=bob64_app_window_create((s32)((screen_width-DEMO_WIDTH)/2u),
        (s32)((screen_height-DEMO_HEIGHT)/2u),DEMO_WIDTH,DEMO_HEIGHT);
    if(!window||(s64)window<0)return -2;
    if(bob64_app_window_focus(window)) {result=-3;goto cleanup;}
    result=bob64_app_window_present(window,pixels,DEMO_WIDTH,DEMO_HEIGHT);
    if(result!=(s64)(DEMO_WIDTH*DEMO_HEIGHT))goto cleanup;
    if(bob64_app_write("bob!",4)!=4) {result=-4;goto cleanup;}
    for(;;) {
        if(bob64_app_wait_event(&event)) {result=-5;break;}
        if(event.Type==BOB64_EVENT_KEY_DOWN&&
           (event.Character=='x'||event.Character=='X'||event.Character==0x1b)) {
            result=0;break;
        }
        if(event.Type==BOB64_EVENT_MOUSE_MOVE||
           event.Type==BOB64_EVENT_MOUSE_BUTTON) {
            if(bob64_button_event(&close_button,&close_state,&event)) {
                result=0;break;
            }
            draw_demo(&graphics);
            result=bob64_app_window_present(window,pixels,DEMO_WIDTH,DEMO_HEIGHT);
            if(result!=(s64)(DEMO_WIDTH*DEMO_HEIGHT))goto cleanup;
        }
    }
cleanup:
    if(bob64_app_window_destroy(window)&&result==0)result=-6;
    return result;
}
