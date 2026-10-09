#ifndef BOB64_PAGING_H
#define BOB64_PAGING_H

#include "types.h"

#define BOB64_PAGE_SIZE 4096ULL
#define BOB64_PAGE_PRESENT 0x001ULL
#define BOB64_PAGE_WRITE 0x002ULL
#define BOB64_PAGE_USER 0x004ULL
#define BOB64_PAGE_PWT 0x008ULL
#define BOB64_PAGE_PCD 0x010ULL
#define BOB64_PAGE_GLOBAL 0x100ULL
#define BOB64_PAGE_NX (1ULL<<63)
#define BOB64_PAGE_ADDRESS_MASK 0x000ffffffffff000ULL
#define BOB64_PAGE_ALLOWED_FLAGS (BOB64_PAGE_WRITE|BOB64_PAGE_USER|BOB64_PAGE_PWT|\
                                  BOB64_PAGE_PCD|BOB64_PAGE_GLOBAL|BOB64_PAGE_NX)
#define BOB64_PAGING_INVALID (-1)
#define BOB64_PAGING_NO_MEMORY (-2)
#define BOB64_PAGING_ALREADY_MAPPED (-3)
#define BOB64_PAGING_NOT_MAPPED (-4)

/* Allocate returns a physical page; map returns its currently accessible address. */
typedef u64 (*BOB64_TABLE_ALLOCATE)(void *context);
typedef u64 *(*BOB64_TABLE_ACCESS)(void *context,u64 physical_address);

typedef struct {
    u64 RootPhysical;
    u8 PhysicalAddressBits;
    BOB64_TABLE_ALLOCATE Allocate;
    BOB64_TABLE_ACCESS Access;
    void *Context;
} BOB64_PAGE_TABLE;

int bob64_page_table_init(BOB64_PAGE_TABLE *table,u8 physical_address_bits,
                          BOB64_TABLE_ALLOCATE allocate,BOB64_TABLE_ACCESS access,
                          void *context);
/* Clone shared kernel mappings while replacing a 2 MiB-aligned user arena. */
int bob64_page_table_clone_isolated(BOB64_PAGE_TABLE *destination,
                                    const BOB64_PAGE_TABLE *source,
                                    u64 isolated_base,u64 isolated_size);
int bob64_page_map(BOB64_PAGE_TABLE *table,u64 virtual_address,u64 physical_address,
                   u64 flags);
/* A failed range map removes every leaf mapping added by that call. */
int bob64_page_map_range(BOB64_PAGE_TABLE *table,u64 virtual_address,
                         u64 physical_address,u64 byte_size,u64 flags);
int bob64_page_protect(BOB64_PAGE_TABLE *table,u64 virtual_address,u64 flags);
int bob64_page_unmap(BOB64_PAGE_TABLE *table,u64 virtual_address,
                     u64 *physical_address,u64 *previous_flags);
int bob64_page_translate(const BOB64_PAGE_TABLE *table,u64 virtual_address,
                         u64 *physical_address,u64 *flags);

#endif
