#define command_line ((char *)0x100)
#define edit_line ((char *)0x180)
#define edit_buffer ((char *)0xe000)
#define edit_undo ((char *)0xe200)
int editor_length;
int editor_undo_length;
int editor_has_undo;
char *arguments;
void nano_edit(char *name);

char *next_arg(void) {
    char *start;
    while (*arguments == ' ') arguments++;
    start = arguments;
    while (*arguments && *arguments != ' ') arguments++;
    if (*arguments) { *arguments = 0; arguments++; }
    return start;
}
char *remaining_args(void) { while (*arguments == ' ') arguments++; return arguments; }
int number_ok;
int parse_number(char *text) {
    int negative; int base; int digit; int number; int digits; int limit;
    number_ok = 0; negative = 0; base = 10; number = 0; digits = 0;
    if (*text == '-') { negative = 1; text++; }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { base = 16; text = text + 2; }
    while (*text) {
        digit = *text;
        if (digit >= '0' && digit <= '9') digit = digit - '0';
        else if (digit >= 'a' && digit <= 'f') digit = digit - 'a' + 10;
        else if (digit >= 'A' && digit <= 'F') digit = digit - 'A' + 10;
        else return 0;
        if (digit >= base) return 0;
        if (base == 16) {
            if (digits == 4) return 0;
            number = number * 16 + digit;
        } else if (!negative) {
            if (number < 0 || number > 6553 || (number == 6553 && digit > 5)) return 0;
            number = number * 10 + digit;
        } else {
            limit = 7; if (negative) limit = 8;
            if (number < -3276 || (number == -3276 && digit > limit)) return 0;
            number = number * 10 - digit;
        }
        digits++; text++;
    }
    if (!digits) return 0;
    number_ok = 1;
    if (base == 16) { if (negative) return -number; return number; }
    if (negative) return number;
    return number;
}
int editor_line(int line) {
    int i; int current;
    if (line < 1) return -1;
    current = 1; i = 0;
    while (i < editor_length && current < line) {
        if (edit_buffer[i] == '\n') current++;
        i++;
    }
    if (current == line) return i;
    return -1;
}
int editor_end(int position) {
    while (position < editor_length && edit_buffer[position] != '\n') position++;
    if (position < editor_length) position++;
    return position;
}
void editor_show(void) {
    int i; int line;
    i = 0; line = 1;
    while (i < editor_length) {
        print_dec(line); print(": ");
        while (i < editor_length && edit_buffer[i] != '\n') bob_putc(edit_buffer[i++]);
        if (i < editor_length) i++;
        bob_putc('\n'); line++;
    }
    if (!editor_length) println("(empty)");
}
int editor_changed(char *name) {
    int slot;
    slot = file_find(name);
    if (slot < 0) return editor_length != 0;
    return strcmp(edit_buffer, file_content(slot)) != 0;
}
int editor_replace(int start, int end, char *text, int newline) {
    int added; int delta; int i;
    added = strlen(text) + newline;
    delta = added - (end - start);
    if (editor_length + delta >= FILE_WORDS) { println("File too large; change rejected."); return 0; }
    memcpy(edit_undo, edit_buffer, editor_length + 1);
    editor_undo_length = editor_length; editor_has_undo = 1;
    if (delta > 0) {
        for (i = editor_length; i >= end; i--) edit_buffer[i + delta] = edit_buffer[i];
    } else {
        for (i = end; i <= editor_length; i++) edit_buffer[i + delta] = edit_buffer[i];
    }
    memcpy(edit_buffer + start, text, added - newline);
    if (newline) edit_buffer[start + added - 1] = '\n';
    editor_length = editor_length + delta;
    return 1;
}
int editor_save(char *name) {
    if (file_write(name, edit_buffer, editor_length, 0) < 0) { println("Cannot save file."); return 0; }
    println("Saved."); return 1;
}
void editor_append_line(char *text) {
    char joined[129]; int length;
    length = strlen(text);
    if (editor_length && edit_buffer[editor_length - 1] != '\n') {
        joined[0] = '\n'; memcpy(joined + 1, text, length + 1);
        editor_replace(editor_length, editor_length, joined, 1);
    } else editor_replace(editor_length, editor_length, text, 1);
}
void edit_file(char *name) {
    int n; int slot; int line; int start; int end; int i; int temporary; char *command; char *text;
    if (!*name || strlen(name) >= NAME_WORDS) { println("Invalid file name."); return; }
    slot = file_find(name);
    if (slot >= 0 && file_kinds[slot]) { println("Cannot edit a binary program."); return; }
    editor_length = 0; editor_has_undo = 0;
    if (slot >= 0) {
        editor_length = file_lengths[slot];
        memcpy(edit_buffer, file_content(slot), editor_length);
    }
    edit_buffer[editor_length] = 0;
    if (bob_terminal()) { nano_edit(name); return; }
    println("Editor: text appends; :p lists; :i N TEXT inserts; :r N TEXT replaces.");
    println(":d N deletes; :u undo/redo; :w saves; :q quits; :q! discards; . saves/quits.");
    println(":p N shows one line. :q protects unsaved edits; :wq also saves/quits.");
    editor_show();
    while (1) {
        print("edit> ");
        n = read_line(edit_line, 128);
        if (n == -2) { println("Edit cancelled at EOF."); return; }
        if (n < 0) { println("Line too long; change rejected."); continue; }
        if (strcmp(edit_line, ".") == 0 || strcmp(edit_line, ":wq") == 0) {
            if (editor_save(name)) return;
        } else if (strcmp(edit_line, ":q!") == 0) { println("Discarded unsaved changes."); return; }
        else if (strcmp(edit_line, ":q") == 0) {
            if (editor_changed(name)) println("Unsaved changes. Use :wq to save or :q! to discard.");
            else return;
        }
        else if (strcmp(edit_line, ":p") == 0) editor_show();
        else if (strcmp(edit_line, ":w") == 0) editor_save(name);
        else if (strcmp(edit_line, ":u") == 0) {
            if (!editor_has_undo) println("Nothing to undo.");
            else {
                end = editor_length; if (editor_undo_length > end) end = editor_undo_length;
                for (i = 0; i <= end; i++) {
                    temporary = edit_buffer[i]; edit_buffer[i] = edit_undo[i]; edit_undo[i] = temporary;
                }
                temporary = editor_length; editor_length = editor_undo_length; editor_undo_length = temporary;
                println("Undo/redo applied. Use :p to inspect.");
            }
        } else if (edit_line[0] == ':') {
            arguments = edit_line; command = next_arg();
            if (strcmp(command, ":p") == 0) {
                line = parse_number(next_arg()); start = editor_line(line);
                if (!number_ok || start < 0 || start == editor_length || *next_arg()) {
                    println("Usage: :p LINE (existing line number)"); continue;
                }
                print_dec(line); print(": "); end = editor_end(start);
                for (i = start; i < end; i++) bob_putc(edit_buffer[i]);
                if (end && edit_buffer[end - 1] != '\n') bob_putc('\n');
                continue;
            }
            if (strcmp(command, ":a") == 0) { editor_append_line(remaining_args()); continue; }
            if (strcmp(command, ":i") && strcmp(command, ":r") && strcmp(command, ":d")) {
                println("Unknown editor command."); continue;
            }
            line = parse_number(next_arg()); text = remaining_args();
            start = editor_line(line);
            if (!number_ok || start < 0 || (start == editor_length && strcmp(command, ":i"))) {
                println("Invalid line number."); continue;
            }
            end = start;
            if (strcmp(command, ":i")) end = editor_end(start);
            if (strcmp(command, ":d") == 0) editor_replace(start, end, "", 0);
            else editor_replace(start, end, text, 1);
        } else {
            editor_append_line(edit_line);
        }
    }
}
void load_words(char *name) {
    int slot; int count; int value; int words[511]; char *token;
    count = 0;
    while (1) {
        token = next_arg(); if (!*token) break;
        value = parse_number(token);
        if (!number_ok || count == 511) { println("Invalid machine words."); return; }
        words[count++] = value;
    }
    if (!count) { println("Usage: load name 0xWORD ... (finish with RET 0xE000)"); return; }
    slot = file_write(name, words, count, 1);
    if (slot < 0) println("Cannot create program file.");
    else println("Loaded.");
}
void run_file(char *name) {
    int slot; int status; int *program;
    slot = file_find(name);
    if (slot < 0 || !file_kinds[slot]) { println("Program not found. Use cc or load first."); return; }
    program = (int *)PROGRAM_BASE;
    memset(program, 0, PROGRAM_WORDS);
    memcpy(program, file_content(slot), file_lengths[slot]);
    status = bob_run(PROGRAM_BASE);
    if (status == -2) println("Program fault; shell restored.");
    else if (status == -3) println("Program timed out; shell restored.");
    else { print("Exit "); print_dec(status); bob_putc('\n'); }
}
void memory_status(void) {
    print("Heap "); print_hex(HEAP_BASE); print(".."); print_hex(0xdfff);
    print("; free "); print_dec(HEAP_WORDS - heap_used); println(" words");
    print("Programs "); print_hex(PROGRAM_BASE); print(".."); print_hex(0xbfff); println("");
    println("Files: 8 slots, 511 words each; stack above 0xE600.");
}
void command_help(char *name) {
    if (strcmp(name, "save") == 0 || strcmp(name, "restore") == 0) {
        println("save: snapshot all files to bob-files.b16 in the working directory (or BOB16_STORAGE). restore yes: replace RAM files with that snapshot. Save current work first. Changed kernels restore text only; recompile programs."); return;
    }
    if (strcmp(name, "edit") == 0) println("edit NAME: type/arrows; Ctrl+O saves, Ctrl+X exits, Ctrl+Z undo/redo. Unsaved exit asks Y/N. Redirected input uses the line editor; see C_OS.md.");
    else if (strcmp(name, "cc") == 0 || strcmp(name, "go") == 0)
        println("cc SOURCE [OUTPUT]: compile C; output defaults to app. go SOURCE [OUTPUT] also runs on success. Example: go bob.c. One int main(void); no includes. Use edit SOURCE to fix errors.");
    else if (strcmp(name, "write") == 0) println("write NAME TEXT: create/replace a text file. Example: write note bob! Names: 1..23 characters without spaces; 8 file slots, 511 words each. Use edit for multiple lines.");
    else if (strcmp(name, "read") == 0) println("read NAME: print text. Example: read bob.c. Use ls to see names; run executes program files.");
    else if (strcmp(name, "ls") == 0 || strcmp(name, "dir") == 0 || strcmp(name, "list") == 0)
        println("ls / dir / list: show names, lengths and text/program kinds. Files are in RAM; save keeps them between sessions.");
    else if (strcmp(name, "copy") == 0 || strcmp(name, "rename") == 0 || strcmp(name, "delete") == 0)
        println("copy OLD NEW | rename OLD NEW | delete NAME. Example: copy bob.c backup.c. Destinations must be new. Copy needs a free slot; rename does not. Delete is permanent.");
    else if (strcmp(name, "run") == 0) println("run NAME: execute a compiled/loaded program. Example: cc bob.c bob, then run bob. Exit shows return value; faults/timeouts restore the shell.");
    else if (strcmp(name, "load") == 0) println("load NAME 0xWORD ...: store machine words. Example: load empty 0x2180 0xE000, then run empty. RET 0xE000 returns; use cc for C source.");
    else if (strcmp(name, "peek") == 0 || strcmp(name, "poke") == 0)
        println("peek ADDRESS | poke ADDRESS VALUE: word addresses, hex 0x0000..0xFFFF or signed decimal. Example: poke 0xC000 98. Writes allowed only at 0xC000..0xDFFF. peek also shows signed value.");
    else if (strcmp(name, "alloc") == 0 || strcmp(name, "mem") == 0)
        println("alloc WORDS: allocate/zero 1..8192 heap words, print start address; no free. Example: alloc 16. mem shows remaining heap and memory regions.");
    else if (strcmp(name, "echo") == 0) println("echo TEXT: print text. Example: echo bob!");
    else if (strcmp(name, "clear") == 0) println("clear: clear an ANSI-capable terminal; does not remove files.");
    else if (strcmp(name, "halt") == 0) println("halt: stop emulator. Unsaved RAM files and allocations are lost; use save before quitting.");
    else if (strcmp(name, "help") == 0) println("help: list commands. help COMMAND: show usage and an example. Try help edit or help go.");
    else println("No help for that command. Type help for available names.");
}
void shell_command(char *line) {
    char *command; char *name; char *text; int value; int address; int *pointer;
    arguments = line; command = next_arg();
    if (!*command) return;
    if (strcmp(command, "help") == 0) {
        name = next_arg();
        if (*name) { command_help(name); return; }
        println("help | echo TEXT | clear | mem | halt");
        println("peek ADDRESS | poke ADDRESS VALUE | alloc WORDS");
        println("list / ls / dir | read NAME | write NAME TEXT | edit NAME");
        println("copy OLD NEW | rename OLD NEW | delete NAME (permanent in RAM)");
        println("load NAME 0xWORD ... | run NAME | cc SOURCE [OUTPUT] | go SOURCE [OUTPUT]");
        println("cc/go default output: app. Try go bob.c to compile and print bob!");
        println("Use hex addresses. poke is limited to heap RAM.");
        println("Type help COMMAND for usage/examples. Try help edit or help go.");
        println("save | restore yes (replaces RAM files); help save for storage details.");
        println("Shell keys: Up/Down history; Tab completion; Ctrl+U clears (Windows console).");
    } else if (strcmp(command, "save") == 0) {
        if (*next_arg()) println("Usage: save (all RAM files)");
        else file_snapshot(0);
    } else if (strcmp(command, "restore") == 0) {
        if (strcmp(next_arg(), "yes") || *next_arg()) println("restore yes replaces all RAM files. Use save first to keep current work.");
        else file_snapshot(1);
    } else if (strcmp(command, "echo") == 0) println(remaining_args());
    else if (strcmp(command, "clear") == 0) print("\033[2J\033[H");
    else if (strcmp(command, "mem") == 0) memory_status();
    else if (strcmp(command, "halt") == 0) bob_halt();
    else if (strcmp(command, "list") == 0 || strcmp(command, "ls") == 0 || strcmp(command, "dir") == 0) file_list();
    else if (strcmp(command, "copy") == 0 || strcmp(command, "rename") == 0 || strcmp(command, "delete") == 0) {
        name = next_arg(); text = next_arg();
        if (*next_arg() || (strcmp(command, "delete") == 0 && *text)) {
            println("Usage: copy OLD NEW | rename OLD NEW | delete NAME"); return;
        }
        file_manage(command, name, text);
    } else if (strcmp(command, "read") == 0) {
        value = file_find(next_arg());
        if (value < 0) println("File not found.");
        else if (file_kinds[value]) println("Binary program; use run.");
        else println(file_content(value));
    } else if (strcmp(command, "write") == 0) {
        name = next_arg(); text = remaining_args();
        if (file_write(name, text, strlen(text), 0) < 0) println("Cannot write file. Names: 1..23 printable characters; 8 slots. Use ls or help write.");
        else println("Saved.");
    } else if (strcmp(command, "edit") == 0) edit_file(next_arg());
    else if (strcmp(command, "load") == 0) { name = next_arg(); load_words(name); }
    else if (strcmp(command, "run") == 0) run_file(next_arg());
    else if (strcmp(command, "cc") == 0 || strcmp(command, "go") == 0) {
        name = next_arg(); text = next_arg();
        if (!*name || *next_arg()) { println("Usage: cc SOURCE [OUTPUT] | go SOURCE [OUTPUT]"); return; }
        if (!*text) text = "app";
        value = compile_command(name, text);
        if (value && strcmp(command, "go") == 0) run_file(text);
    } else if (strcmp(command, "alloc") == 0) {
        value = parse_number(next_arg());
        if (!number_ok) { println("Invalid allocation size. Use 1..8192 words; mem shows available space."); return; }
        pointer = alloc(value);
        if (!pointer) println("Allocation failed. Use a positive size within free space; see mem. No free command.");
        else { print_hex((int)pointer); bob_putc('\n'); }
    } else if (strcmp(command, "peek") == 0 || strcmp(command, "poke") == 0) {
        address = parse_number(next_arg());
        if (!number_ok) { println("Invalid address. Use hex 0x0000..0xFFFF or decimal 0..65535."); return; }
        pointer = (int *)address;
        if (strcmp(command, "poke") == 0) {
            value = parse_number(next_arg());
            if (!number_ok) { println("Invalid value. Use -32768..65535 or 0x0000..0xFFFF."); return; }
            if (address >= 0 || address < -16384 || address >= -8192) {
                println("poke is limited to 0xC000..0xDFFF."); return;
            }
            *pointer = value;
        }
        print_hex(address); print(": "); print_hex(*pointer); print(" ("); print_dec(*pointer); println(")");
    } else println("Unknown command. Type help.");
}
