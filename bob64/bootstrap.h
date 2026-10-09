#ifndef BOB64_BOOTSTRAP_H
#define BOB64_BOOTSTRAP_H

#include "paging.h"
#include "efi.h"

#define BOB64_BOOTSTRAP_TABLE_CHUNK_LIMIT 64u
#define BOB64_BOOTSTRAP_TABLE_GROW_RESERVE 8ULL
#define BOB64_BOOTSTRAP_TABLE_GROW_PAGES 64ULL

typedef struct BOB64_BOOTSTRAP_SPACE BOB64_BOOTSTRAP_SPACE;
typedef int (*BOB64_BOOTSTRAP_TABLE_GROW)(void *context,
        BOB64_BOOTSTRAP_SPACE *space,u64 *base,u64 *pages);

typedef struct {
    u64 Base,Pages;
} BOB64_BOOTSTRAP_TABLE_CHUNK;

struct BOB64_BOOTSTRAP_SPACE {
    BOB64_PAGE_TABLE PageTable;
    u64 TablePoolBase;
    u64 TablePoolPages;
    u64 TablePoolInitialPages;
    u64 TablePoolUsed;
    BOB64_BOOTSTRAP_TABLE_CHUNK TablePoolChunks[BOB64_BOOTSTRAP_TABLE_CHUNK_LIMIT];
    usize TablePoolChunkCount;
    BOB64_BOOTSTRAP_TABLE_GROW TablePoolGrow;
    void *TablePoolGrowContext;
    u8 TablePoolGrowing;
    BOB64_TABLE_ACCESS Access;
    void *AccessContext;
    u8 NxSupported;
};

/* Builds identity mappings for the EFI image, replacement stack and table pool. */
int bob64_bootstrap_space_init(BOB64_BOOTSTRAP_SPACE *space,u8 physical_address_bits,
                               BOB64_TABLE_ACCESS access,void *access_context,
                               u64 table_pool_base,u64 table_pool_pages,
                               u64 image_base,u64 image_size,
                               u64 stack_base,u64 stack_size,u8 nx_supported);
void bob64_bootstrap_space_set_table_growth(BOB64_BOOTSTRAP_SPACE *space,
        BOB64_BOOTSTRAP_TABLE_GROW grow,void *context);
/* Keep UEFI runtime code/data identity-mapped for post-ExitBootServices use. */
int bob64_bootstrap_map_runtime_services(BOB64_BOOTSTRAP_SPACE *space,
        const void *memory_map,usize memory_map_size,usize descriptor_size,
        u64 runtime_services_address);

#endif
