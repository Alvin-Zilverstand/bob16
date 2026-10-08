#include "bob.h"
#include "bob_fs.h"
#include "bob_string.h"

static void write_text(const char *text) {
    while (*text) bob_putc(*text++);
}

int main(void) {
    char name[24];
    char number[12];
    int size;
    int kind;
    int i;
    for (i = 0; i < 8; i++) {
        kind = bob_file_list(i, name, 24, &size);
        if (kind < 0) continue;
        write_text(name);
        bob_putc(' ');
        bob_format_int(size, number, 12);
        write_text(number);
        bob_putc(' ');
        if (kind == 0) write_text("text");
        else if (kind == 3) write_text("native app");
        else write_text("program");
        bob_putc('\n');
    }
    return 0;
}
