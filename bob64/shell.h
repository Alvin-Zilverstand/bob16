#ifndef BOB64_SHELL_H
#define BOB64_SHELL_H

#include "filesystem.h"
#include "snapshot.h"

#define BOB64_SHELL_LINE_CAPACITY 128
#define BOB64_SHELL_ARGUMENT_LIMIT 16
#define BOB64_SHELL_HISTORY_LIMIT 8
#define BOB64_SHELL_INPUT_UP 0x10000
#define BOB64_SHELL_INPUT_DOWN 0x10001
typedef void (*BOB64_SHELL_WRITE)(void *context,const char *text);
typedef void (*BOB64_SHELL_CLEAR)(void *context);
typedef s64 (*BOB64_SHELL_RUN)(void *context,const char *name,usize argument_count,
                               const char *const *arguments,int *started);
typedef int (*BOB64_SHELL_COMPILE)(void *context,const char *source_name,
                                   const char *output_name,usize *error_offset);
typedef int (*BOB64_SHELL_SNAPSHOT_SAVE)(void *context,const void *snapshot,
                                         usize snapshot_size);
typedef int (*BOB64_SHELL_SNAPSHOT_RESTORE)(void *context,
                                            BOB64_FILESYSTEM *filesystem);

typedef struct {
    char Line[BOB64_SHELL_LINE_CAPACITY];
    usize Length;
    char History[BOB64_SHELL_HISTORY_LIMIT][BOB64_SHELL_LINE_CAPACITY];
    char HistoryDraft[BOB64_SHELL_LINE_CAPACITY];
    usize HistoryCount,HistoryIndex,HistoryDraftLength;
    u8 IgnoreLineFeed;
    u8 HistoryBrowsing;
    BOB64_PAGE_ALLOCATOR *PageAllocator;
    BOB64_HEAP *Heap;
    BOB64_FILESYSTEM *Filesystem;
    void *Snapshot;
    usize SnapshotSize;
    BOB64_SHELL_WRITE Write;
    BOB64_SHELL_CLEAR Clear;
    BOB64_SHELL_RUN Run;
    BOB64_SHELL_COMPILE Compile;
    void *Context;
    BOB64_SHELL_SNAPSHOT_SAVE SnapshotSave;
    BOB64_SHELL_SNAPSHOT_RESTORE SnapshotRestore;
    void *SnapshotContext;
} BOB64_SHELL;

int bob64_shell_init(BOB64_SHELL *shell,BOB64_PAGE_ALLOCATOR *allocator,
                     BOB64_HEAP *heap,BOB64_FILESYSTEM *filesystem,
                     BOB64_SHELL_WRITE write,
                     BOB64_SHELL_CLEAR clear,void *context);
void bob64_shell_input(BOB64_SHELL *shell,int character);
void bob64_shell_set_runner(BOB64_SHELL *shell,BOB64_SHELL_RUN run);
void bob64_shell_set_compiler(BOB64_SHELL *shell,BOB64_SHELL_COMPILE compile);
void bob64_shell_set_snapshot_storage(BOB64_SHELL *shell,
        BOB64_SHELL_SNAPSHOT_SAVE save,BOB64_SHELL_SNAPSHOT_RESTORE restore,
        void *context);

#endif
