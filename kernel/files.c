char file_names[192];
int file_lengths[8];
int file_kinds[8];
int file_used[8];

char *file_name(int slot) { return file_names + slot * NAME_WORDS; }
/* The upper 4096 words are reserved for file contents, above the kernel stack.
   Supervised programs cannot write this region. */
int *file_content(int slot) { return (int *)0xf000 + slot * FILE_WORDS; }
int file_find(char *name) {
    int i;
    for (i = 0; i < FILE_COUNT; i++)
        if (file_used[i] && strcmp(file_name(i), name) == 0) return i;
    return -1;
}
int file_slot(char *name) {
    int slot; int length;
    length = strlen(name);
    if (length == 0 || length >= NAME_WORDS) return -1;
    slot = file_find(name);
    if (slot >= 0) return slot;
    for (slot = 0; slot < FILE_COUNT; slot++) {
        if (!file_used[slot]) {
            memset(file_name(slot), 0, NAME_WORDS);
            memcpy(file_name(slot), name, length);
            file_used[slot] = 1;
            return slot;
        }
    }
    return -1;
}
int file_write(char *name, int *data, int length, int kind) {
    int slot;
    if (length < 0 || length >= FILE_WORDS) return -1;
    slot = file_slot(name);
    if (slot < 0) return -1;
    memcpy(file_content(slot), data, length);
    file_content(slot)[length] = 0;
    file_lengths[slot] = length;
    file_kinds[slot] = kind;
    return slot;
}
void file_list(void) {
    int i; int count;
    count = 0;
    for (i = 0; i < FILE_COUNT; i++) {
        if (file_used[i]) {
            print(file_name(i)); print("  "); print_dec(file_lengths[i]);
            if (file_kinds[i]) println(" words (program)");
            else println(" chars (text)");
            count++;
        }
    }
    if (!count) println("No files.");
}
void file_manage(char *operation, char *name, char *destination) {
    int slot; int target;
    if (!*name) { println("Usage: copy OLD NEW | rename OLD NEW | delete NAME"); return; }
    slot = file_find(name);
    if (slot < 0) { println("File not found. Use ls to see names."); return; }
    if (strcmp(operation, "delete") == 0) {
        file_used[slot] = 0; file_lengths[slot] = 0; file_kinds[slot] = 0;
        memset(file_name(slot), 0, NAME_WORDS);
        memset(file_content(slot), 0, FILE_WORDS);
        println("Deleted."); return;
    }
    if (!*destination || strlen(destination) >= NAME_WORDS) {
        println("Destination needs 1..23 characters."); return;
    }
    if (file_find(destination) >= 0) {
        println("Destination exists; choose another name or delete it first."); return;
    }
    if (strcmp(operation, "rename") == 0) {
        memset(file_name(slot), 0, NAME_WORDS);
        memcpy(file_name(slot), destination, strlen(destination));
        println("Renamed."); return;
    }
    target = file_write(destination, file_content(slot), file_lengths[slot], file_kinds[slot]);
    if (target < 0) println("File slots full (8). Delete a file and retry.");
    else println("Copied.");
}
