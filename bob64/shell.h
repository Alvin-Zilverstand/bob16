#ifndef BOB64_SHELL_H
#define BOB64_SHELL_H

#include "filesystem.h"
#include "snapshot.h"

#define BOB64_SHELL_LINE_CAPACITY 128
#define BOB64_SHELL_ARGUMENT_LIMIT 16
typedef void (*BOB64_SHELL_WRITE)(void *context,const char *text);
typedef void (*BOB64_SHELL_CLEAR)(void *context);
typedef s64 (*BOB64_SHELL_RUN)(void *context,const char *name,usize argument_count,
                               const char *const *arguments,int *started);
typedef int (*BOB64_SHELL_COMPILE)(void *context,const char *source_name,
                                   const char *output_name,usize *error_offset);

typedef struct {
    char Line[BOB64_SHELL_LINE_CAPACITY];
    usize Length;
    u8 IgnoreLineFeed;
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
} BOB64_SHELL;

int bob64_shell_init(BOB64_SHELL *shell,BOB64_PAGE_ALLOCATOR *allocator,
                     BOB64_HEAP *heap,BOB64_FILESYSTEM *filesystem,
                     BOB64_SHELL_WRITE write,
                     BOB64_SHELL_CLEAR clear,void *context);
void bob64_shell_input(BOB64_SHELL *shell,int character);
void bob64_shell_set_runner(BOB64_SHELL *shell,BOB64_SHELL_RUN run);
void bob64_shell_set_compiler(BOB64_SHELL *shell,BOB64_SHELL_COMPILE compile);

#endif
