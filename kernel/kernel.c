#include "runtime.h"
#include "runtime.c"
#include "files.c"
#include "compiler.c"
#include "shell.c"
#include "input.c"
#include "nano.c"

int main(void) {
    int length; char *sample;
    println("bob!");
    println("bob16 OS: help COMMAND for usage; go bob.c runs the example.");
    println("Files are in RAM; halt exits. Type help to get started.");
    sample = "int main(void) { println(\"bob!\"); return 0; }";
    file_write("bob.c", sample, strlen(sample), 0);
    while (1) {
        print("bob> ");
        length = shell_read_line();
        if (length == -2) { bob_putc('\n'); return 0; }
        if (length < 0) println("Command too long.");
        else shell_command(command_line);
    }
    return 0;
}
