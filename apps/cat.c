#include "bob.h"
#include "bob_fs.h"

int main(int argc, char **argv) {
    char text[512];
    int length;
    int i;
    if (argc != 2) { bob_puts("Usage: cat FILE"); return 1; }
    length = bob_file_read(argv[1], text, 512);
    if (length < 0) { bob_puts("Cannot read text file."); return 1; }
    for (i = 0; i < length; i++) bob_putc(text[i]);
    if(length>0&&text[length-1]!='\n')bob_putc('\n');
    return 0;
}
