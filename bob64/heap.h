#ifndef BOB64_HEAP_H
#define BOB64_HEAP_H

#include "types.h"

typedef int (*BOB64_HEAP_GROW)(void *context,void **region,usize *region_size);
/* Release a page-aligned tail region; return zero only after it is unmapped. */
typedef int (*BOB64_HEAP_SHRINK)(void *context,void *region,usize region_size);

typedef struct BOB64_HEAP_BLOCK BOB64_HEAP_BLOCK;
struct BOB64_HEAP_BLOCK {
    usize Size;
    BOB64_HEAP_BLOCK *Previous,*Next;
    u32 Magic;
    u8 Free;
    u8 Reserved[3];
};

typedef struct {
    BOB64_HEAP_BLOCK *First,*Last;
    BOB64_HEAP_GROW Grow;
    BOB64_HEAP_SHRINK Shrink;
    void *Context;
    void *Base;
    usize MappedBytes,MaximumBytes;
    u32 Lock;
} BOB64_HEAP;

int bob64_heap_init(BOB64_HEAP *heap,usize maximum_bytes,
                    BOB64_HEAP_GROW grow,void *context);
void bob64_heap_set_shrink(BOB64_HEAP *heap,BOB64_HEAP_SHRINK shrink);
void *bob64_heap_alloc(BOB64_HEAP *heap,usize size);
void *bob64_heap_calloc(BOB64_HEAP *heap,usize count,usize size);
int bob64_heap_free(BOB64_HEAP *heap,void *pointer);
usize bob64_heap_mapped_bytes(const BOB64_HEAP *heap);

_Static_assert(sizeof(BOB64_HEAP_BLOCK)==32,"bob64 heap block ABI alignment");

#endif
