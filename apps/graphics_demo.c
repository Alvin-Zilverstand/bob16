#include "bob.h"
#include "bob_gfx.h"
#include "bob_event.h"

int main(void) {
    int resolution[2];
    int event[4];
    char bitmap[8] = {'/', '-', '\\', '|', '\\', '-', '/', '|'};
    if (bob_gfx_enter()) return 1;
    if (bob_gfx_resolution(resolution) != 2 || resolution[0] != 80 || resolution[1] != 25) {
        bob_gfx_leave();
        return 2;
    }
    bob_gfx_clear(' ', 0);
    bob_gfx_fill_rect(1, 1, 78, 23, '.', 1);
    bob_gfx_fill_rect(2, 2, 76, 3, ' ', 4);
    bob_gfx_text(4, 3, "BOB32 GRAPHICS DEMO", 15);
    bob_gfx_line(3, 7, 76, 7, '=', 14);
    bob_gfx_pixel(40, 16, '*', 10);
    bob_gfx_fill_rect(8, 10, 12, 5, '#', 2);
    bob_gfx_blit(28, 10, 4, 2, bitmap, 12);
    bob_gfx_text(4, 19, "Press any key to return to the shell", 15);
    if (bob_gfx_present()) { bob_gfx_leave(); return 3; }
    if (bob_event_wait(event) != 1 || event[0] != BOB_EVENT_CHAR || event[1] != 'x') {
        bob_gfx_leave();
        return 4;
    }
    if (bob_gfx_leave()) return 5;
    bob_puts("bob!");
    return 0;
}
