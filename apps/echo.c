#include "bob.h"

static void write_text(const char *text) {
    while (*text) bob_putc(*text++);
}

int main(int argc, char **argv) {
    int i;
    for (i = 1; i < argc; i++) {
        if (i > 1) bob_putc(' ');
        write_text(argv[i]);
    }
    bob_putc('\n');
    return 0;
}
