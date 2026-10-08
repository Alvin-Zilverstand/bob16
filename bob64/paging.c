#include "paging.h"

#define BOB64_PAGE_LARGE 0x080ULL
#define BOB64_U64_MAX (~(u64)0)

static int canonical_address(u64 address) {
    u64 top=address>>48;
    return ((address>>47)&1)?top==0xffffULL:top==0;
}

static u64 physical_address_mask(u8 bits) {
    return ((1ULL<<bits)-1)&BOB64_PAGE_ADDRESS_MASK;
}

static int physical_page_valid(u64 address,u8 bits) {
    u64 mask;
    if(bits<36||bits>52)return 0;
    mask=physical_address_mask(bits);
    return !(address&(BOB64_PAGE_SIZE-1))&&!(address&~mask);
}

static u64 *table_entries(const BOB64_PAGE_TABLE *table,u64 physical) {
    if(!table||!table->Access||!physical_page_valid(physical,table->PhysicalAddressBits))return 0;
    return table->Access(table->Context,physical);
}

int bob64_page_table_init(BOB64_PAGE_TABLE *table,u8 physical_address_bits,
                          BOB64_TABLE_ALLOCATE allocate,BOB64_TABLE_ACCESS access,
                          void *context) {
    u64 root;
    u64 *entries;
    if(!table||!allocate||!access||physical_address_bits<36||physical_address_bits>52)
        return BOB64_PAGING_INVALID;
    root=allocate(context);
    if(!root||!physical_page_valid(root,physical_address_bits))return BOB64_PAGING_NO_MEMORY;
    entries=access(context,root);
    if(!entries)return BOB64_PAGING_INVALID;
    for(usize i=0;i<512;i++)entries[i]=0;
    table->RootPhysical=root;table->PhysicalAddressBits=physical_address_bits;
    table->Allocate=allocate;table->Access=access;
    table->Context=context;
    return 0;
}

static int copy_table_page(BOB64_PAGE_TABLE *destination,
                           const BOB64_PAGE_TABLE *source,u64 source_physical,
                           u64 *destination_physical) {
    u64 physical=destination->Allocate(destination->Context);
    u64 *target;
    if(!physical||!physical_page_valid(physical,destination->PhysicalAddressBits))
        return BOB64_PAGING_NO_MEMORY;
    target=destination->Access(destination->Context,physical);
    if(!target)return BOB64_PAGING_NO_MEMORY;
    if(source_physical) {
        u64 *original=table_entries(source,source_physical);
        if(!original)return BOB64_PAGING_INVALID;
        for(usize i=0;i<512;i++)target[i]=original[i];
    } else for(usize i=0;i<512;i++)target[i]=0;
    *destination_physical=physical;
    return 0;
}

int bob64_page_table_clone_isolated(BOB64_PAGE_TABLE *destination,
                                    const BOB64_PAGE_TABLE *source,
                                    u64 isolated_base,u64 isolated_size) {
    u64 *source_root,*destination_root,*source_pdpt,*destination_pdpt;
    u64 *destination_pd,source_entry,new_pdpt,new_pd;
    u64 end,last,base_page,end_page;
    usize pml4_index,pdpt_index,first_pd,last_pd;
    int result;
    if(!destination||!source||!destination->RootPhysical||!source->RootPhysical||
       destination->RootPhysical==source->RootPhysical||!isolated_size||
       (isolated_base&((1ULL<<21)-1))||
       (isolated_size&((1ULL<<21)-1))||isolated_size>BOB64_U64_MAX-isolated_base)
        return BOB64_PAGING_INVALID;
    end=isolated_base+isolated_size;last=end-1;
    if(!canonical_address(isolated_base)||!canonical_address(last)||
       (((isolated_base>>47)&1)!=((last>>47)&1))||
       ((isolated_base>>39)&0x1ff)!=((last>>39)&0x1ff)||
       ((isolated_base>>30)&0x1ff)!=((last>>30)&0x1ff))
        return BOB64_PAGING_INVALID;
    source_root=table_entries(source,source->RootPhysical);
    destination_root=table_entries(destination,destination->RootPhysical);
    if(!source_root||!destination_root)return BOB64_PAGING_INVALID;
    for(usize i=0;i<512;i++)destination_root[i]=source_root[i];
    pml4_index=(usize)((isolated_base>>39)&0x1ff);
    pdpt_index=(usize)((isolated_base>>30)&0x1ff);
    source_entry=source_root[pml4_index];
    result=copy_table_page(destination,source,
        (source_entry&BOB64_PAGE_PRESENT)?source_entry&BOB64_PAGE_ADDRESS_MASK:0,
        &new_pdpt);
    if(result)return result;
    destination_root[pml4_index]=new_pdpt|
        ((source_entry&~BOB64_PAGE_ADDRESS_MASK)|BOB64_PAGE_PRESENT|
         BOB64_PAGE_WRITE|BOB64_PAGE_USER);
    destination_pdpt=destination->Access(destination->Context,new_pdpt);
    if(!destination_pdpt)return BOB64_PAGING_INVALID;
    source_pdpt=(source_entry&BOB64_PAGE_PRESENT)?
        table_entries(source,source_entry&BOB64_PAGE_ADDRESS_MASK):0;
    if((source_entry&BOB64_PAGE_PRESENT)&&!source_pdpt)return BOB64_PAGING_INVALID;
    source_entry=source_pdpt?source_pdpt[pdpt_index]:0;
    if((source_entry&BOB64_PAGE_PRESENT)&&(source_entry&BOB64_PAGE_LARGE))
        return BOB64_PAGING_INVALID;
    result=copy_table_page(destination,source,
        (source_entry&BOB64_PAGE_PRESENT)?source_entry&BOB64_PAGE_ADDRESS_MASK:0,
        &new_pd);
    if(result)return result;
    destination_pdpt[pdpt_index]=new_pd|
        ((source_entry&~BOB64_PAGE_ADDRESS_MASK)|BOB64_PAGE_PRESENT|
         BOB64_PAGE_WRITE|BOB64_PAGE_USER);
    destination_pd=destination->Access(destination->Context,new_pd);
    if(!destination_pd)return BOB64_PAGING_INVALID;
    if(source_entry&BOB64_PAGE_PRESENT) {
        u64 *source_pd=table_entries(source,source_entry&BOB64_PAGE_ADDRESS_MASK);
        if(!source_pd)return BOB64_PAGING_INVALID;
    }
    base_page=isolated_base>>21;end_page=end>>21;
    first_pd=(usize)(base_page&0x1ff);last_pd=(usize)((end_page-1)&0x1ff);
    if(first_pd>last_pd)return BOB64_PAGING_INVALID;
    for(usize i=first_pd;i<=last_pd;i++)destination_pd[i]=0;
    return 0;
}

static int next_level(BOB64_PAGE_TABLE *table,u64 *parent,usize index,u64 **child) {
    u64 entry=parent[index],physical;
    if(entry&BOB64_PAGE_PRESENT) {
        if(entry&BOB64_PAGE_LARGE)return BOB64_PAGING_INVALID;
        physical=entry&BOB64_PAGE_ADDRESS_MASK;
        *child=table_entries(table,physical);
        return *child?0:BOB64_PAGING_INVALID;
    }
    physical=table->Allocate(table->Context);
    if(!physical||!physical_page_valid(physical,table->PhysicalAddressBits))
        return BOB64_PAGING_NO_MEMORY;
    *child=table_entries(table,physical);
    if(!*child)return BOB64_PAGING_INVALID;
    for(usize i=0;i<512;i++)(*child)[i]=0;
    /* Parent permissions are permissive; leaf PTEs enforce user/write policy. */
    parent[index]=physical|BOB64_PAGE_PRESENT|BOB64_PAGE_WRITE|BOB64_PAGE_USER;
    return 0;
}

int bob64_page_map(BOB64_PAGE_TABLE *table,u64 virtual_address,u64 physical_address,
                   u64 flags) {
    u64 *level,*next;
    int result;
    if(!table||!table->RootPhysical||!table->Allocate||!table->Access||
       !canonical_address(virtual_address)||
       (virtual_address&(BOB64_PAGE_SIZE-1))||
       !physical_page_valid(physical_address,table->PhysicalAddressBits)||
       (flags&~BOB64_PAGE_ALLOWED_FLAGS))return BOB64_PAGING_INVALID;
    level=table_entries(table,table->RootPhysical);
    if(!level)return BOB64_PAGING_INVALID;
    result=next_level(table,level,(usize)((virtual_address>>39)&0x1ff),&next);
    if(result)return result;
    level=next;
    result=next_level(table,level,(usize)((virtual_address>>30)&0x1ff),&next);
    if(result)return result;
    level=next;
    result=next_level(table,level,(usize)((virtual_address>>21)&0x1ff),&next);
    if(result)return result;
    level=next;
    usize index=(usize)((virtual_address>>12)&0x1ff);
    if(level[index]&BOB64_PAGE_PRESENT)return BOB64_PAGING_ALREADY_MAPPED;
    level[index]=physical_address|BOB64_PAGE_PRESENT|flags;
    return 0;
}

int bob64_page_map_range(BOB64_PAGE_TABLE *table,u64 virtual_address,
                         u64 physical_address,u64 byte_size,u64 flags) {
    u64 offset,pages,span,last_delta,last_virtual,last_physical;
    u64 virtual_page,physical_page;
    if(!table||!byte_size)return BOB64_PAGING_INVALID;
    offset=virtual_address&(BOB64_PAGE_SIZE-1);
    if((physical_address&(BOB64_PAGE_SIZE-1))!=offset||
       byte_size>BOB64_U64_MAX-offset)return BOB64_PAGING_INVALID;
    span=byte_size+offset;
    if(span>BOB64_U64_MAX-(BOB64_PAGE_SIZE-1))return BOB64_PAGING_INVALID;
    pages=(span+BOB64_PAGE_SIZE-1)/BOB64_PAGE_SIZE;
    last_delta=(pages-1)*BOB64_PAGE_SIZE;
    virtual_page=virtual_address&~(BOB64_PAGE_SIZE-1);
    physical_page=physical_address&~(BOB64_PAGE_SIZE-1);
    if(virtual_page>BOB64_U64_MAX-last_delta||physical_page>BOB64_U64_MAX-last_delta)
        return BOB64_PAGING_INVALID;
    last_virtual=virtual_page+last_delta;
    last_physical=physical_page+last_delta;
    if(!canonical_address(virtual_page)||!canonical_address(last_virtual)||
       (((virtual_page>>47)&1)!=((last_virtual>>47)&1))||
       !physical_page_valid(physical_page,table->PhysicalAddressBits)||
       !physical_page_valid(last_physical,table->PhysicalAddressBits))
        return BOB64_PAGING_INVALID;
    for(u64 i=0;i<pages;i++) {
        int result=bob64_page_map(table,virtual_page+i*BOB64_PAGE_SIZE,
                                  physical_page+i*BOB64_PAGE_SIZE,flags);
        if(result)return result;
    }
    return 0;
}

static int locate_leaf(const BOB64_PAGE_TABLE *table,u64 virtual_address,u64 **leaf) {
    static const u8 shifts[]={39,30,21};
    u64 physical,*level;
    if(!table||!table->RootPhysical||!table->Access||!leaf||
       !canonical_address(virtual_address)||
       (virtual_address&(BOB64_PAGE_SIZE-1)))return BOB64_PAGING_INVALID;
    physical=table->RootPhysical;
    for(usize depth=0;depth<3;depth++) {
        level=table_entries(table,physical);
        if(!level)return BOB64_PAGING_INVALID;
        u64 entry=level[(virtual_address>>shifts[depth])&0x1ff];
        if(!(entry&BOB64_PAGE_PRESENT))return BOB64_PAGING_NOT_MAPPED;
        if(entry&BOB64_PAGE_LARGE)return BOB64_PAGING_INVALID;
        physical=entry&BOB64_PAGE_ADDRESS_MASK;
    }
    level=table_entries(table,physical);
    if(!level)return BOB64_PAGING_INVALID;
    *leaf=&level[(virtual_address>>12)&0x1ff];
    if(!(**leaf&BOB64_PAGE_PRESENT))return BOB64_PAGING_NOT_MAPPED;
    return 0;
}

/* Caller invalidates the TLB entry when changing a live address space. */
int bob64_page_protect(BOB64_PAGE_TABLE *table,u64 virtual_address,u64 flags) {
    u64 *leaf;
    int result;
    if(flags&~BOB64_PAGE_ALLOWED_FLAGS)return BOB64_PAGING_INVALID;
    result=locate_leaf(table,virtual_address,&leaf);
    if(result)return result;
    *leaf=(*leaf&BOB64_PAGE_ADDRESS_MASK)|BOB64_PAGE_PRESENT|flags;
    return 0;
}

/* Caller invalidates the TLB entry when removing a live mapping. */
int bob64_page_unmap(BOB64_PAGE_TABLE *table,u64 virtual_address,
                     u64 *physical_address,u64 *previous_flags) {
    u64 *leaf,entry;
    int result=locate_leaf(table,virtual_address,&leaf);
    if(result)return result;
    entry=*leaf;*leaf=0;
    if(physical_address)*physical_address=entry&BOB64_PAGE_ADDRESS_MASK;
    if(previous_flags)*previous_flags=entry&~BOB64_PAGE_ADDRESS_MASK;
    return 0;
}

int bob64_page_translate(const BOB64_PAGE_TABLE *table,u64 virtual_address,
                         u64 *physical_address,u64 *flags) {
    static const u8 shifts[]={39,30,21,12};
    u64 physical,entry,*level;
    int effective_user=1,effective_write=1,effective_nx=0;
    if(!table||!table->RootPhysical||!physical_address||!canonical_address(virtual_address))
        return BOB64_PAGING_INVALID;
    physical=table->RootPhysical;
    for(usize depth=0;depth<4;depth++) {
        level=table_entries(table,physical);
        if(!level)return BOB64_PAGING_INVALID;
        entry=level[(virtual_address>>shifts[depth])&0x1ff];
        if(!(entry&BOB64_PAGE_PRESENT))return 0;
        if(depth<3&&(entry&BOB64_PAGE_LARGE))return BOB64_PAGING_INVALID;
        if(!(entry&BOB64_PAGE_USER))effective_user=0;
        if(!(entry&BOB64_PAGE_WRITE))effective_write=0;
        if(entry&BOB64_PAGE_NX)effective_nx=1;
        physical=entry&BOB64_PAGE_ADDRESS_MASK;
    }
    *physical_address=physical|(virtual_address&(BOB64_PAGE_SIZE-1));
    if(flags) {
        *flags=entry&~BOB64_PAGE_ADDRESS_MASK;
        if(!effective_user)*flags&=~BOB64_PAGE_USER;
        if(!effective_write)*flags&=~BOB64_PAGE_WRITE;
        if(effective_nx)*flags|=BOB64_PAGE_NX;
    }
    return 1;
}
