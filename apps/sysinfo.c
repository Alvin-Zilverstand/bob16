#include "bob.h"
#include "bob_fs.h"
#include "bob_system.h"
#include "bob_string.h"

static void write_text(const char *text) {
    while (*text) bob_putc(*text++);
}

static void show_value(const char *label, int value) {
    char number[12];
    write_text(label);
    bob_format_int(value, number, 12);
    write_text(number);
    bob_putc('\n');
}

int main(void) {
    int info[6];
    int capabilities[5];
    if (bob_system_info(info, 6) != 6 || info[0] != BOB_FS_API_VERSION) {
        bob_puts("System information service unavailable.");
        return 1;
    }
    if (bob_system_capabilities(capabilities, 4) != -1) {
        bob_puts("System capability validation failed.");
        return 2;
    }
    if (bob_system_capabilities(capabilities, 5) != 5 ||
        capabilities[0] != BOB_SYSTEM_API_VERSION) {
        bob_puts("System capabilities unavailable.");
        return 3;
    }
    bob_puts("bob32 system information");
    show_value("System API version: ", capabilities[0]);
    bob_puts((capabilities[1] & BOB_SYSTEM_DEVICE_CELL_FRAMEBUFFER) ?
             "Cell framebuffer: available" : "Cell framebuffer: unavailable");
    show_value("Display width (cells): ", capabilities[2]);
    show_value("Display height (cells): ", capabilities[3]);
    bob_puts((capabilities[4] & BOB_SYSTEM_INPUT_KEYBOARD) ?
             "Keyboard input: available" : "Keyboard input: unavailable");
    bob_puts((capabilities[4] & BOB_SYSTEM_INPUT_MOUSE) ?
             "Mouse input: available" : "Mouse input: unavailable");
    show_value("Application address space (words): ", info[1]);
    show_value("Mapped application pages: ", info[2]);
    show_value("Guest files: ", info[3]);
    show_value("Filesystem words used: ", info[4]);
    show_value("Filesystem words free: ", info[5]);
    return 0;
}
