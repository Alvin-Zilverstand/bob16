/* Native shell editing; history is stored before command tokenization. */
#define shell_history ((char *)0xe400)
int history_count;
int input_length;
int input_cursor;
int input_echo;

void input_redraw(void) {
    int i; int width; int start; int end;
    if (!input_echo) return;
    width = bob_columns() - 6; if (width < 1) width = 1;
    start = 0; if (input_cursor >= width) start = input_cursor - width + 1;
    end = start + width; if (end > input_length) end = input_length;
    print("\r\033[2Kbob> ");
    for (i = start; i < end; i++) bob_putc(command_line[i]);
    for (i = input_cursor; i < end; i++) print("\033[D");
}
int input_key(void) {
    int key; int next;
    key = bob_key();
    if (key != 27) return key;
    if (input_echo) return 0;
    if (bob_key() != '[') return 0;
    next = bob_key();
    if (next == 'A') return 256;
    if (next == 'B') return 257;
    if (next == 'D') return 258;
    if (next == 'C') return 259;
    if (next == 'H') return 260;
    if (next == 'F') return 261;
    if (next == '3' && bob_key() == '~') return 262;
    return 0;
}
int input_matches(char *candidate, int start, int count) {
    int i;
    for (i = 0; i < count; i++)
        if (!candidate[i] || candidate[i] != command_line[start + i]) return 0;
    return 1;
}
void input_complete(void) {
    int start; int count; int matches; int i; int length; char *words; char candidate[24]; char selected[24];
    start = input_cursor;
    while (start && command_line[start - 1] != ' ') start--;
    count = input_cursor - start; matches = 0;
    if (input_cursor != input_length) return;
    if (!start) {
#ifdef BOBC_WIDE
        words = "help echo clear mem halt list ls dir read write edit load run run32 cc go copy rename delete alloc peek poke save restore";
#else
        words = "help echo clear mem halt list ls dir read write edit load run cc go copy rename delete alloc peek poke save restore";
#endif
        while (*words) {
            length = 0;
            while (*words && *words != ' ') candidate[length++] = *words++;
            candidate[length] = 0; if (*words) words++;
            if (input_matches(candidate, start, count)) {
                matches++; memcpy(selected, candidate, length + 1);
                if (input_echo) { if (matches == 1) bob_putc('\n'); print(candidate); print("  "); }
            }
        }
    } else {
        for (i = 0; i < FILE_COUNT; i++) {
            if (file_used[i] && input_matches(file_name(i), start, count)) {
                matches++; memcpy(selected, file_name(i), strlen(file_name(i)) + 1);
                if (input_echo) { if (matches == 1) bob_putc('\n'); print(selected); print("  "); }
            }
        }
    }
    if (matches == 1) {
        length = strlen(selected);
        if (start + length < 128) {
            memcpy(command_line + start, selected, length + 1);
            input_length = start + length; input_cursor = input_length;
        }
    }
    if (input_echo && matches) bob_putc('\n');
    input_redraw();
}
int shell_read_line(void) {
    int key; int i; int truncated; int browsing; char draft[128];
    input_length = 0; input_cursor = 0; truncated = 0; browsing = history_count;
    input_echo = bob_terminal(); command_line[0] = 0; draft[0] = 0;
    while (1) {
        key = input_key();
        if (key == 26) key = -1;
        if (key == -1 || key == '\n') break;
        if (key == '\r' || key == 0) continue;
        if (key == 256 || key == 257) {
            if (browsing == history_count) memcpy(draft, command_line, input_length + 1);
            if (key == 256 && browsing) browsing--;
            if (key == 257 && browsing < history_count) browsing++;
            if (browsing == history_count) memcpy(command_line, draft, strlen(draft) + 1);
            else memcpy(command_line, shell_history + browsing * 128, strlen(shell_history + browsing * 128) + 1);
            input_length = strlen(command_line); input_cursor = input_length;
        } else if (key == 258) { if (input_cursor) input_cursor--; }
        else if (key == 259) { if (input_cursor < input_length) input_cursor++; }
        else if (key == 260 || key == 1) input_cursor = 0;
        else if (key == 261 || key == 5) input_cursor = input_length;
        else if (key == 21) { input_length = 0; input_cursor = 0; command_line[0] = 0; }
        else if (key == 9) { input_complete(); continue; }
        else if (key == 262 || key == '\b' || key == 127) {
            if (key != 262 && input_cursor) input_cursor--;
            else if (key != 262) continue;
            if (input_cursor < input_length) {
                for (i = input_cursor; i < input_length; i++) command_line[i] = command_line[i + 1];
                input_length--;
            }
        } else if (key >= 32 && key <= 255) {
            if (input_length == 127) truncated = 1;
            else {
                for (i = input_length; i >= input_cursor; i--) command_line[i + 1] = command_line[i];
                command_line[input_cursor++] = key; input_length++;
            }
        }
        input_redraw();
    }
    if (input_echo) bob_putc('\n');
    if (truncated) return -1;
    if (key == -1 && !input_length) return -2;
    if (input_length && (history_count == 0 || strcmp(command_line, shell_history + (history_count - 1) * 128))) {
        if (history_count == 4) {
            for (i = 0; i < 384; i++) shell_history[i] = shell_history[i + 128];
            history_count = 3;
        }
        memcpy(shell_history + history_count * 128, command_line, input_length + 1); history_count++;
    }
    return input_length;
}
