#ifndef BOB64_BOOTSTRAP_H
#define BOB64_BOOTSTRAP_H

#include "paging.h"

typedef struct {
    BOB64_PAGE_TABLE PageTable;
    u64 TablePoolBase;
    u64 TablePoolPages;
    u64 TablePoolUsed;
    BOB64_TABLE_ACCESS Access;
    void *AccessContext;
    u8 NxSupported;
} BOB64_BOOTSTRAP_SPACE;

/* Builds identity mappings for the EFI image, replacement stack and table pool. */
int bob64_bootstrap_space_init(BOB64_BOOTSTRAP_SPACE *space,u8 physical_address_bits,
                               BOB64_TABLE_ACCESS access,void *access_context,
                               u64 table_pool_base,u64 table_pool_pages,
                               u64 image_base,u64 image_size,
                               u64 stack_base,u64 stack_size,u8 nx_supported);

#endif
