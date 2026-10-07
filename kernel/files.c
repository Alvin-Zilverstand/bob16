char file_names[192];
int file_lengths[8];
int file_kinds[8];
int file_used[8];

void file_snapshot(int operation) {
    int descriptor[4]; int result;
    descriptor[0] = (int)file_names; descriptor[1] = (int)file_lengths;
    descriptor[2] = (int)file_kinds; descriptor[3] = (int)file_used;
    result = bob_snapshot(descriptor, operation);
#ifdef BOBC_WIDE
    if (result == -1) println("Storage unavailable. Check bob-files.b32 location/permissions (or BOB16_STORAGE).");
#else
    if (result == -1) println("Storage unavailable. Check bob-files.b16 location/permissions (or BOB16_STORAGE).");
#endif
    else if (result < 0) println("Invalid saved files; RAM files unchanged.");
#ifdef BOBC_WIDE
    else if (operation == 0) println("Files saved to bob-files.b32 (or BOB16_STORAGE).");
#else
    else if (operation == 0) println("Files saved to bob-files.b16 (or BOB16_STORAGE).");
#endif
    else {
        println("Files restored; previous RAM files replaced.");
        if (result == 1) println("Kernel changed: old programs omitted. Compile restored C sources again.");
    }
}

char *file_name(int slot) { return file_names + slot * NAME_WORDS; }
/* The upper 4096 words are reserved for file contents, above the kernel stack.
   Supervised programs cannot write this region. */
int *file_content(int slot) {
#ifdef BOBC_WIDE
    int i; int offset;
    offset = 0;
    for (i = 0; i < slot; i++) if (file_used[i]) offset += file_lengths[i] + 1;
    return (int *)0xf000 + offset;
#else
    return (int *)0xf000 + slot * FILE_WORDS;
#endif
}
int file_find(char *name) {
    int i;
    for (i = 0; i < FILE_COUNT; i++)
        if (file_used[i] && strcmp(file_name(i), name) == 0) return i;
    return -1;
}
int file_slot(char *name) {
    int slot; int length; int i;
    length = strlen(name);
    if (length == 0 || length >= NAME_WORDS) return -1;
    for (i = 0; i < length; i++) if (name[i] < 33 || name[i] > 126) return -1;
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
int file_prepare_native(char *name) {
    int i; int count; int length;
    length = strlen(name);
    if (!length || length >= NAME_WORDS || file_find(name) >= 0) return -1;
    for (i = 0; i < length; i++) if (name[i] < 33 || name[i] > 126) return -1;
    count = 0;
    for (i = 0; i < FILE_COUNT; i++) if (file_used[i]) {
        if (i != count) {
            memcpy(file_name(count), file_name(i), NAME_WORDS);
            file_lengths[count] = file_lengths[i]; file_kinds[count] = file_kinds[i]; file_used[count] = 1;
            file_used[i] = 0;
        }
        count++;
    }
    if (count == FILE_COUNT) return -1;
    memset(file_name(count), 0, NAME_WORDS); memcpy(file_name(count), name, length);
    file_lengths[count] = 0; file_kinds[count] = 3; file_used[count] = 1;
    return count;
}
int file_write(char *name, void *data, int length, int kind) {
#ifdef BOBC_WIDE
    int slot; int i; int used; int available; int targetOffset; int sourceSlot;
    if (length < 0 || length >= FILE_WORDS || (!kind && length >= TEXT_WORDS)) return -1;
    if (!kind) for (i = 0; i < length; i++) if (!((int *)data)[i] || ((int *)data)[i] > 255) return -1;
    int oldSlot = file_find(name);
    slot = file_slot(name);
    if (slot < 0) return -1;
    sourceSlot = -1;
    for (i = 0; i < FILE_COUNT; i++) if (file_used[i] && data == file_content(i)) sourceSlot = i;
    used = 0;
    for (i = 0; i < FILE_COUNT; i++) if (file_used[i] && i != slot) used += file_lengths[i] + 1;
    available = FILE_WORDS - used;
    if (length + 1 > available) {
        if (oldSlot < 0) { file_used[slot] = 0; file_lengths[slot] = 0; file_kinds[slot] = 0; memset(file_name(slot), 0, NAME_WORDS); }
        return -1;
    }
    if (sourceSlot == slot) return slot;
    /* Compact first so variable-sized files never leave unusable gaps. */
    for (i = 0; i < FILE_COUNT; i++) if (file_used[i] && i != slot) {
        int *source; int *destination; int j;
        source = (int *)0xf000; destination = (int *)0xf000;
        for (j = 0; j < i; j++) if (file_used[j] && j != slot) destination += file_lengths[j] + 1;
        for (j = 0; j < i; j++) if (file_used[j]) source += file_lengths[j] + 1;
        if (source != destination && destination < source)
            for (j = 0; j <= file_lengths[i]; j++) destination[j] = source[j];
    }
    /* Insert the rewritten target at its slot-ordered offset. */
    targetOffset = 0;
    for (i = 0; i < slot; i++) if (file_used[i] && i != slot) targetOffset += file_lengths[i] + 1;
    { int total = 0;
      for (i = 0; i < FILE_COUNT; i++) if (file_used[i] && i != slot) total += file_lengths[i] + 1;
      for (i = total - 1; i >= targetOffset; i--) ((int *)0xf000)[i + length + 1] = ((int *)0xf000)[i];
      if (sourceSlot >= 0) {
          int sourceOffset = 0;
          for (i = 0; i < sourceSlot; i++) if (file_used[i] && i != slot) sourceOffset += file_lengths[i] + 1;
          if (sourceSlot > slot) sourceOffset += length + 1;
          data = (int *)0xf000 + sourceOffset;
      }
      memcpy((int *)0xf000 + targetOffset, data, length);
      ((int *)0xf000)[targetOffset + length] = 0;
      file_lengths[slot] = length; file_kinds[slot] = kind;
    }
    return slot;
#else
    int slot;
    if (length < 0 || length >= FILE_WORDS) return -1;
    slot = file_slot(name);
    if (slot < 0) return -1;
    memcpy(file_content(slot), data, length);
    file_content(slot)[length] = 0;
    file_lengths[slot] = length; file_kinds[slot] = kind;
    return slot;
#endif
}
void file_list(void) {
    int i; int count;
    count = 0;
    for (i = 0; i < FILE_COUNT; i++) {
        if (file_used[i]) {
            print(file_name(i)); print("  "); print_dec(file_lengths[i]);
            if (file_kinds[i] == 3) println(" words (native bob32 image)");
            else if (file_kinds[i]) println(" words (program)");
            else println(" chars (text)");
            count++;
        }
    }
    if (!count) println("No files.");
    print("Used "); print_dec(count);
#ifdef BOBC_WIDE
    println("/8 slots; shared 4096-word storage. Text/resident files max 511; native apps use remaining space.");
#else
    println("/8 slots; limit 511 words per file. delete frees a slot.");
#endif
}
void file_manage(char *operation, char *name, char *destination) {
    int slot; int target;
    if (!*name) { println("Usage: copy OLD NEW | rename OLD NEW | delete NAME"); return; }
    slot = file_find(name);
    if (slot < 0) { println("File not found. Use ls to see names."); return; }
    if (strcmp(operation, "delete") == 0) {
#ifdef BOBC_WIDE
        int sourceOffset; int destinationOffset; int i; int j;
        sourceOffset = 0; destinationOffset = 0;
        for (i = 0; i < FILE_COUNT; i++) if (file_used[i]) {
            if (i == slot) { sourceOffset += file_lengths[i] + 1; continue; }
            if (sourceOffset != destinationOffset)
                for (j = 0; j <= file_lengths[i]; j++) ((int *)0xf000)[destinationOffset + j] = ((int *)0xf000)[sourceOffset + j];
            sourceOffset += file_lengths[i] + 1; destinationOffset += file_lengths[i] + 1;
        }
#endif
        file_used[slot] = 0; file_lengths[slot] = 0; file_kinds[slot] = 0;
        memset(file_name(slot), 0, NAME_WORDS);
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
