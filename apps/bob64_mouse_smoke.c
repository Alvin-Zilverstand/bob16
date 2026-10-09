#include "../bob64/app.h"
#include "../bob64/gfx.h"
#include "../bob64/widgets.h"

/* QEMU injects move, wheel, and button input through the selected mouse. */
#define MOUSE_TEST_WIDTH 160u
#define MOUSE_TEST_HEIGHT 96u

static u32 pixels[MOUSE_TEST_WIDTH*MOUSE_TEST_HEIGHT];
static const BOB64_RECT mouse_test_button={40,55,80,18};

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    BOB64_EVENT event;
    BOB64_GFX graphics;
    BOB64_BUTTON_STATE button_state={0};
    u32 screen_width,screen_height;
    u64 window;
    static const char move_passed[]="bob! live mouse move passed\n";
    static const char wheel_passed[]="bob! live mouse wheel passed\n";
    if(!startup||startup->AbiVersion!=BOB64_APP_ABI_VERSION)return -1;
    if(bob64_app_get_display(&screen_width,&screen_height)||
       screen_width<MOUSE_TEST_WIDTH||screen_height<MOUSE_TEST_HEIGHT)return -2;
    window=bob64_app_window_create((s32)((screen_width-MOUSE_TEST_WIDTH)/2),
        (s32)((screen_height-MOUSE_TEST_HEIGHT)/2),MOUSE_TEST_WIDTH,
        MOUSE_TEST_HEIGHT);
    if(!window||(s64)window<0)return -3;
    graphics.Pixels=pixels;graphics.Width=MOUSE_TEST_WIDTH;
    graphics.Height=MOUSE_TEST_HEIGHT;
    graphics.Capacity=MOUSE_TEST_WIDTH*MOUSE_TEST_HEIGHT;
    bob64_gfx_fill_rect(&graphics,0,0,MOUSE_TEST_WIDTH,MOUSE_TEST_HEIGHT,
                        0x001a2a3cu);
    bob64_gfx_fill_rect(&graphics,0,0,MOUSE_TEST_WIDTH,24,0x00335d8cu);
    bob64_gfx_text(&graphics,8,8,"bob!",0x00ffffffu,1);
    bob64_gfx_text(&graphics,8,38,"MOVE AND WHEEL",0x00e2ebf5u,1);
    if(bob64_button_draw(&graphics,&mouse_test_button,"CLICK",&button_state)) {
        bob64_app_window_destroy(window);return -12;
    }
    if(bob64_app_window_present(window,pixels,MOUSE_TEST_WIDTH,
       MOUSE_TEST_HEIGHT)!=(s64)(MOUSE_TEST_WIDTH*MOUSE_TEST_HEIGHT)||
       bob64_app_window_focus(window)<0) {
        bob64_app_window_destroy(window);return -4;
    }
    for(;;) {
        if(bob64_app_wait_event(&event)) {
            bob64_app_window_destroy(window);return -5;
        }
        if(event.Type==BOB64_EVENT_MOUSE_MOVE&&
           (event.DeltaX||event.DeltaY))break;
    }
    if(event.WindowHandle!=window||event.X<0||
       event.X>=(s32)MOUSE_TEST_WIDTH||event.Y<0||
       event.Y>=(s32)MOUSE_TEST_HEIGHT) {
        bob64_app_window_destroy(window);return -6;
    }
    if(bob64_app_write(move_passed,sizeof(move_passed)-1)!=
       (s64)(sizeof(move_passed)-1)) {
        bob64_app_window_destroy(window);return -7;
    }
    for(;;) {
        if(bob64_app_wait_event(&event)) {
            bob64_app_window_destroy(window);return -8;
        }
        if(event.Type==BOB64_EVENT_MOUSE_WHEEL&&event.Wheel)break;
    }
    if(event.WindowHandle!=window||event.X<0||
       event.X>=(s32)MOUSE_TEST_WIDTH||event.Y<0||
       event.Y>=(s32)MOUSE_TEST_HEIGHT) {
        bob64_app_window_destroy(window);return -9;
    }
    if(bob64_app_write(wheel_passed,sizeof(wheel_passed)-1)!=
       (s64)(sizeof(wheel_passed)-1)) {
        bob64_app_window_destroy(window);return -10;
    }
    for(;;) {
        int clicked;
        if(bob64_app_wait_event(&event)) {
            bob64_app_window_destroy(window);return -13;
        }
        if(event.Type!=BOB64_EVENT_MOUSE_BUTTON)continue;
        if(event.WindowHandle!=window||event.X<0||
           event.X>=(s32)MOUSE_TEST_WIDTH||event.Y<0||
           event.Y>=(s32)MOUSE_TEST_HEIGHT) {
            bob64_app_window_destroy(window);return -14;
        }
        clicked=bob64_button_event(&mouse_test_button,&button_state,&event);
        if(clicked)break;
    }
    static const char button_passed[]="bob! live mouse button passed\n";
    if(bob64_app_write(button_passed,sizeof(button_passed)-1)!=
       (s64)(sizeof(button_passed)-1)) {
        bob64_app_window_destroy(window);return -15;
    }
    return bob64_app_window_destroy(window)<0?-11:0;
}
