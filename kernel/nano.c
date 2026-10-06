/* Full-screen, non-modal editor for an interactive Windows console. */
int nano_cursor;
int nano_width;
void nano_text(char *text) {
    int i;
    for (i = 0; text[i] && i < nano_width - 1; i++) bob_putc(text[i]);
}
void nano_position(int row, int column) {
    print("\033["); print_dec(row); bob_putc(';'); print_dec(column); bob_putc('H');
}
int nano_start(int position) {
    while (position && edit_buffer[position - 1] != '\n') position--;
    return position;
}
int nano_finish(int position) {
    while (position < editor_length && edit_buffer[position] != '\n') position++;
    return position;
}
void nano_draw(char *name, char *message) {
    int rows; int width; int top; int position; int line; int row; int column; int left; int i;
    rows = bob_rows(); width = bob_columns();
    nano_width = width;
    if (rows < 5) rows = 5;
    column = nano_cursor - nano_start(nano_cursor); left = 0;
    if (column >= width - 1) left = column - width + 2;
    top = nano_start(nano_cursor);
    for (i = 0; i < rows - 5 && top; i++) top = nano_start(top - 1);
    print("\033[?25l\033[2J\033[H"); print("bob edit: "); print(name);
    if (editor_changed(name)) print(" *");
    position = top; row = 2; line = 2;
    while (row < rows - 1) {
        nano_position(row, 1); i = 0;
        while (position < editor_length && edit_buffer[position] != '\n') {
            if (i >= left && i - left < width - 1) {
                if (edit_buffer[position] >= 32) bob_putc(edit_buffer[position]);
                else bob_putc(' ');
            }
            i++; position++;
        }
        if (nano_cursor >= top && nano_cursor <= position) line = row;
        if (position >= editor_length) break;
        position++; top = position; row++;
    }
    nano_position(rows - 1, 1); nano_text(message);
    nano_position(rows, 1); nano_text("^O Save  ^X Exit  ^Z Undo  Arrows Move");
    nano_position(line, column - left + 1); print("\033[?25h");
}
void nano_edit(char *name) {
    int key; int start; int end; int column; int i; int temporary; char text[2]; char *message;
    nano_cursor = 0; message = "Ctrl+O saves; Ctrl+X exits.";
    while (1) {
        nano_draw(name, message); key = input_key(); message = "";
        if (key == -1) key = 24;
        if (key == 24) {
            if (!editor_changed(name)) break;
            nano_draw(name, "Save? Y=yes N=discard ^C=cancel");
            key = input_key();
            if (key == 'n' || key == 'N') break;
            if ((key == 'y' || key == 'Y') && editor_save(name)) break;
            continue;
        }
        if (key == 15) {
            if (editor_save(name)) message = "Saved to RAM; shell save for disk.";
            else message = "Save failed; changes kept.";
        } else if (key == 26 && editor_has_undo) {
            end = editor_length; if (editor_undo_length > end) end = editor_undo_length;
            for (i = 0; i <= end; i++) {
                temporary = edit_buffer[i]; edit_buffer[i] = edit_undo[i]; edit_undo[i] = temporary;
            }
            temporary = editor_length; editor_length = editor_undo_length; editor_undo_length = temporary;
            if (nano_cursor > editor_length) nano_cursor = editor_length;
            message = "Undo/redo applied.";
        } else if (key == 258) { if (nano_cursor) nano_cursor--; }
        else if (key == 259) { if (nano_cursor < editor_length) nano_cursor++; }
        else if (key == 260 || key == 1) nano_cursor = nano_start(nano_cursor);
        else if (key == 261 || key == 5) nano_cursor = nano_finish(nano_cursor);
        else if (key == 256 || key == 257) {
            start = nano_start(nano_cursor); column = nano_cursor - start;
            if (key == 256 && start) start = nano_start(start - 1);
            if (key == 257) { end = nano_finish(start); if (end < editor_length) start = end + 1; }
            end = nano_finish(start); nano_cursor = start + column;
            if (nano_cursor > end) nano_cursor = end;
        } else {
            start = nano_cursor; end = start; text[0] = 0; text[1] = 0;
            if (key == '\b' || key == 127) { if (!start) continue; start--; }
            else if (key == 262) { if (end == editor_length) continue; end++; }
            else if (key == '\n' || key == 9 || (key >= 32 && key < 256)) {
                text[0] = key; if (key == 9) text[0] = ' ';
            } else continue;
            if (editor_replace(start, end, text, 0)) nano_cursor = start + strlen(text);
            else message = "Full: 511 chars. Delete text first.";
        }
    }
    print("\033[2J\033[H");
}
