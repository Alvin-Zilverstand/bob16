#ifndef BOB64_MEMORY_H
#define BOB64_MEMORY_H

#include "efi.h"

#define BOB64_PAGE_EXTENT_LIMIT 128
#define BOB64_PAGE_ALLOCATION_LIMIT 16384
#define BOB64_PAGE_ALLOC_INVALID (-1)
#define BOB64_PAGE_ALLOC_FULL (-2)

typedef struct {
    u64 Base;
    u64 Pages;
} BOB64_PAGE_EXTENT;

/* Sorted, non-overlapping runs of currently available physical 4 KiB pages. */
typedef struct {
    usize Count;
    usize Capacity;
    BOB64_PAGE_EXTENT *Extents;
    usize AllocatedCount;
    BOB64_PAGE_EXTENT Allocated[BOB64_PAGE_ALLOCATION_LIMIT];
} BOB64_PAGE_ALLOCATOR;

int bob64_page_allocator_init(BOB64_PAGE_ALLOCATOR *allocator,
                              BOB64_PAGE_EXTENT *storage,usize capacity,
                              u8 physical_address_bits,
                              const void *memory_map,usize memory_map_size,
                              usize descriptor_size);
u64 bob64_page_alloc(BOB64_PAGE_ALLOCATOR *allocator,u64 pages);
int bob64_page_free(BOB64_PAGE_ALLOCATOR *allocator,u64 address,u64 pages);

#endif
