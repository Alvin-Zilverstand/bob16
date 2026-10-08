#include "bootstrap.h"

static u64 table_pool_allocate(void *context) {
    BOB64_BOOTSTRAP_SPACE *space=(BOB64_BOOTSTRAP_SPACE *)context;
    if(!space||space->TablePoolUsed>=space->TablePoolPages)return 0;
    return space->TablePoolBase+(space->TablePoolUsed++)*BOB64_PAGE_SIZE;
}

static u64 *table_pool_access(void *context,u64 physical_address) {
    BOB64_BOOTSTRAP_SPACE *space=(BOB64_BOOTSTRAP_SPACE *)context;
    if(!space||physical_address<space->TablePoolBase||
       (physical_address-space->TablePoolBase)%BOB64_PAGE_SIZE||
       (physical_address-space->TablePoolBase)/BOB64_PAGE_SIZE>=space->TablePoolUsed)
        return 0;
    return space->Access(space->AccessContext,physical_address);
}

int bob64_bootstrap_space_init(BOB64_BOOTSTRAP_SPACE *space,u8 physical_address_bits,
                               BOB64_TABLE_ACCESS access,void *access_context,
                               u64 table_pool_base,u64 table_pool_pages,
                               u64 image_base,u64 image_size,
                               u64 stack_base,u64 stack_size,u8 nx_supported) {
    u64 pool_bytes;
    u64 writable_nx=BOB64_PAGE_WRITE|(nx_supported?BOB64_PAGE_NX:0);
    int result;
    if(!space||!access||!table_pool_pages||
       table_pool_base&(BOB64_PAGE_SIZE-1)||
       table_pool_pages>(~(u64)0)/BOB64_PAGE_SIZE||
       !image_size||!stack_size||nx_supported>1)return BOB64_PAGING_INVALID;
    pool_bytes=table_pool_pages*BOB64_PAGE_SIZE;
    if(table_pool_base>(~(u64)0)-pool_bytes)return BOB64_PAGING_INVALID;
    space->TablePoolBase=table_pool_base;space->TablePoolPages=table_pool_pages;
    space->TablePoolUsed=0;space->Access=access;space->AccessContext=access_context;
    space->NxSupported=nx_supported;
    if(bob64_page_table_init(&space->PageTable,physical_address_bits,table_pool_allocate,
                             table_pool_access,space))return BOB64_PAGING_NO_MEMORY;
    result=bob64_page_map_range(&space->PageTable,image_base,image_base,image_size,
                                BOB64_PAGE_WRITE);
    if(result)return result;
    result=bob64_page_map_range(&space->PageTable,stack_base,stack_base,stack_size,
                                writable_nx);
    if(result)return result;
    result=bob64_page_map_range(&space->PageTable,table_pool_base,table_pool_base,
                                pool_bytes,writable_nx);
    if(result)return result;
    return 0;
}
