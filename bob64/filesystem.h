#ifndef BOB64_FILESYSTEM_H
#define BOB64_FILESYSTEM_H

#include "heap.h"
#include "memory.h"

#define BOB64_FS_NAME_CAPACITY 64

typedef struct BOB64_FS_FILE BOB64_FS_FILE;
struct BOB64_FS_FILE {
    BOB64_FS_FILE *Next;
    char *Name;
    char *Data;
    usize Length;
};

typedef struct {
    BOB64_FS_FILE *First;
    BOB64_HEAP *Heap;
    usize FileCount,BytesUsed;
} BOB64_FILESYSTEM;

typedef int (*BOB64_FS_LIST_CALLBACK)(void *context,const char *name,usize length);

int bob64_fs_init(BOB64_FILESYSTEM *filesystem,BOB64_HEAP *heap);
/* File contents are length-delimited and may contain any byte value. */
int bob64_fs_write(BOB64_FILESYSTEM *filesystem,const char *name,
                   const void *data,usize length);
int bob64_fs_read(const BOB64_FILESYSTEM *filesystem,const char *name,
                  const char **data,usize *length);
int bob64_fs_read_at(const BOB64_FILESYSTEM *filesystem,const char *name,
                     u64 offset,void *buffer,usize capacity,usize *read_count);
int bob64_fs_write_at(BOB64_FILESYSTEM *filesystem,const char *name,
                      u64 offset,const void *buffer,usize length);
int bob64_fs_delete(BOB64_FILESYSTEM *filesystem,const char *name);
void bob64_fs_clear(BOB64_FILESYSTEM *filesystem);
usize bob64_fs_list(const BOB64_FILESYSTEM *filesystem,
                    BOB64_FS_LIST_CALLBACK callback,void *context);

#endif
