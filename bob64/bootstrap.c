#include "bootstrap.h"

static u64 table_pool_allocate(void *context) {
    BOB64_BOOTSTRAP_SPACE *space=(BOB64_BOOTSTRAP_SPACE *)context;
    u64 index;
    if(!space)return 0;
    if(!space->TablePoolGrowing&&space->TablePoolGrow&&
       space->TablePoolPages-space->TablePoolUsed<=
         BOB64_BOOTSTRAP_TABLE_GROW_RESERVE&&
       space->TablePoolChunkCount<BOB64_BOOTSTRAP_TABLE_CHUNK_LIMIT) {
        u64 base=0,pages=0;
        space->TablePoolGrowing=1;
        int result=space->TablePoolGrow(space->TablePoolGrowContext,space,
                                        &base,&pages);
        space->TablePoolGrowing=0;
        if(!result&&base&&pages&&!(base&(BOB64_PAGE_SIZE-1))&&
           pages<=((~(u64)0-base)/BOB64_PAGE_SIZE)&&
           pages<=((~(u64)0-space->TablePoolPages))&&
           base<=~(u64)0-pages*BOB64_PAGE_SIZE) {
            BOB64_BOOTSTRAP_TABLE_CHUNK *chunk=
                &space->TablePoolChunks[space->TablePoolChunkCount++];
            chunk->Base=base;chunk->Pages=pages;
            space->TablePoolPages+=pages;
        }
    }
    if(space->TablePoolUsed>=space->TablePoolPages)return 0;
    index=space->TablePoolUsed++;
    if(index<space->TablePoolInitialPages)
        return space->TablePoolBase+index*BOB64_PAGE_SIZE;
    index-=space->TablePoolInitialPages;
    for(usize i=0;i<space->TablePoolChunkCount;i++) {
        BOB64_BOOTSTRAP_TABLE_CHUNK *chunk=&space->TablePoolChunks[i];
        if(index<chunk->Pages)return chunk->Base+index*BOB64_PAGE_SIZE;
        index-=chunk->Pages;
    }
    space->TablePoolUsed--;
    return 0;
}

static u64 *table_pool_access(void *context,u64 physical_address) {
    BOB64_BOOTSTRAP_SPACE *space=(BOB64_BOOTSTRAP_SPACE *)context;
    if(!space)return 0;
    if(physical_address>=space->TablePoolBase&&
       (physical_address-space->TablePoolBase)%BOB64_PAGE_SIZE==0) {
        u64 index=(physical_address-space->TablePoolBase)/BOB64_PAGE_SIZE;
        if(index<space->TablePoolInitialPages&&index<space->TablePoolUsed)
            return space->Access(space->AccessContext,physical_address);
    }
    u64 index=space->TablePoolInitialPages;
    for(usize i=0;i<space->TablePoolChunkCount;i++) {
        BOB64_BOOTSTRAP_TABLE_CHUNK *chunk=&space->TablePoolChunks[i];
        u64 size=chunk->Pages*BOB64_PAGE_SIZE;
        if(physical_address>=chunk->Base&&physical_address-chunk->Base<size&&
           (physical_address-chunk->Base)%BOB64_PAGE_SIZE==0) {
            u64 page=(physical_address-chunk->Base)/BOB64_PAGE_SIZE;
            if(index+page<space->TablePoolUsed)
                return space->Access(space->AccessContext,physical_address);
            return 0;
        }
        index+=chunk->Pages;
    }
    return 0;
}

void bob64_bootstrap_space_set_table_growth(BOB64_BOOTSTRAP_SPACE *space,
        BOB64_BOOTSTRAP_TABLE_GROW grow,void *context) {
    if(!space)return;
    space->TablePoolGrow=grow;space->TablePoolGrowContext=context;
}

int bob64_bootstrap_map_runtime_services(BOB64_BOOTSTRAP_SPACE *space,
        const void *memory_map,usize memory_map_size,usize descriptor_size,
        u64 runtime_services_address) {
    const u8 *bytes=(const u8 *)memory_map;
    int runtime_table_in_data=0;
    if(!space||!memory_map||!memory_map_size||
       descriptor_size<sizeof(EFI_MEMORY_DESCRIPTOR)||
       memory_map_size%descriptor_size||!runtime_services_address)return -1;
    for(usize offset=0;offset<memory_map_size;offset+=descriptor_size) {
        const EFI_MEMORY_DESCRIPTOR *descriptor=
            (const EFI_MEMORY_DESCRIPTOR *)(bytes+offset);
        u64 base=descriptor->PhysicalStart,pages=descriptor->NumberOfPages,size;
        u64 flags;
        if(!(descriptor->Attribute&EFI_MEMORY_RUNTIME)||!pages)continue;
        if(base&(BOB64_PAGE_SIZE-1)||pages>~(u64)0/BOB64_PAGE_SIZE)return -1;
        size=pages*BOB64_PAGE_SIZE;
        if(base>~(u64)0-size)return -1;
        if(descriptor->Type==EFI_RUNTIME_SERVICES_CODE)
            flags=0;
        else
            flags=BOB64_PAGE_WRITE|(space->NxSupported?BOB64_PAGE_NX:0);
        if(descriptor->Type==EFI_MEMORY_MAPPED_IO||
           descriptor->Attribute&EFI_MEMORY_UC)
            flags|=BOB64_PAGE_PCD|BOB64_PAGE_PWT;
        else if(descriptor->Attribute&EFI_MEMORY_WT)
            flags|=BOB64_PAGE_PWT;
        for(u64 page=0;page<size;page+=BOB64_PAGE_SIZE) {
            u64 physical;
            int translated=bob64_page_translate(&space->PageTable,base+page,
                                                 &physical,0);
            if(translated==1) {
                if(physical!=base+page||bob64_page_protect(&space->PageTable,
                                      base+page,flags))return -1;
            } else if(translated||bob64_page_map(&space->PageTable,base+page,
                                                  base+page,flags))return -1;
        }
        if(descriptor->Type==EFI_RUNTIME_SERVICES_DATA&&
           runtime_services_address>=base&&
           runtime_services_address-base<size)runtime_table_in_data=1;
    }
    if(!runtime_table_in_data)return -1;
    u64 physical;
    return bob64_page_translate(&space->PageTable,runtime_services_address,
                                &physical,0)==1&&
           physical==runtime_services_address?0:-1;
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
    space->TablePoolInitialPages=table_pool_pages;
    space->TablePoolUsed=0;space->Access=access;space->AccessContext=access_context;
    space->TablePoolChunkCount=0;space->TablePoolGrow=0;
    space->TablePoolGrowContext=0;space->TablePoolGrowing=0;
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
