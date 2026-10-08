#include "process.h"

static usize bounded_length(const char *text,usize limit) {
    usize length=0;
    if(!text)return limit;
    while(length<limit&&text[length])length++;
    return length;
}

static void copy_bytes(u8 *destination,const u8 *source,usize length) {
    for(usize i=0;i<length;i++)destination[i]=source[i];
}

static void clear_bytes(u8 *destination,usize length) {
    for(usize i=0;i<length;i++)destination[i]=0;
}

int bob64_process_init(BOB64_PROCESS *process,BOB64_PROCESS_PAGE *page_storage,
                      usize page_capacity,const BOB64_PROCESS_OPERATIONS *operations) {
    if(!process||!page_storage||!operations||!operations->AllocatePage||
       !operations->FreePage||!operations->MapPage||!operations->ProtectPage||
       !operations->UnmapPage||
       page_capacity>BOB64_PROCESS_PAGE_LIMIT)return -1;
    process->Pages=page_storage;process->PageCapacity=page_capacity;process->PageCount=0;
    process->Operations=*operations;process->EntryAddress=0;process->StartupAddress=0;
    process->InitialStackPointer=0;process->Loaded=0;
    return 0;
}

static int map_page(BOB64_PROCESS *process,u64 virtual_address,u64 flags) {
    u64 physical;
    u8 *writable=0;
    BOB64_PROCESS_PAGE *page;
    if(process->PageCount>=process->PageCapacity)return -1;
    physical=process->Operations.AllocatePage(process->Operations.Context);
    if(!physical)return -1;
    page=process->Pages+process->PageCount++;
    page->VirtualAddress=virtual_address;page->PhysicalAddress=physical;page->Flags=flags;
    page->WritableAddress=0;page->Allocated=1;page->Mapped=0;
    if(process->Operations.MapPage(process->Operations.Context,virtual_address,
                                   physical,flags,&writable))return -1;
    page->WritableAddress=writable;page->Mapped=1;
    if(!writable)return -1;
    clear_bytes(writable,(usize)BOB64_PAGE_SIZE);
    return 0;
}

static BOB64_PROCESS_PAGE *find_page(BOB64_PROCESS *process,u64 virtual_address) {
    u64 page_address=virtual_address&~(BOB64_PAGE_SIZE-1);
    for(usize i=0;i<process->PageCount;i++)
        if(process->Pages[i].Mapped&&process->Pages[i].VirtualAddress==page_address)
            return process->Pages+i;
    return 0;
}

static int write_virtual(BOB64_PROCESS *process,u64 address,const void *source,
                         usize length) {
    const u8 *bytes=(const u8 *)source;
    while(length) {
        BOB64_PROCESS_PAGE *page=find_page(process,address);
        usize offset=(usize)(address&(BOB64_PAGE_SIZE-1));
        usize part=(usize)BOB64_PAGE_SIZE-offset;
        if(!page)return -1;
        if(part>length)part=length;
        copy_bytes(page->WritableAddress+offset,bytes,part);
        bytes+=part;length-=part;address+=part;
    }
    return 0;
}

static int prepare_arguments(BOB64_PROCESS *process,usize argument_count,
                             const char *const *arguments) {
    u64 string_addresses[BOB64_PROCESS_MAX_ARGUMENTS];
    u64 cursor=BOB64_PROCESS_STACK_BASE+BOB64_PROCESS_STACK_SIZE;
    u64 argv_address,startup_address,initial_rsp;
    BOB64_APP_STARTUP startup;
    u64 zero=0;
    if(argument_count>BOB64_PROCESS_MAX_ARGUMENTS||
       (argument_count&&!arguments))return -1;
    for(usize i=argument_count;i>0;i--) {
        usize length=bounded_length(arguments[i-1],(usize)BOB64_PROCESS_STACK_SIZE);
        if(length==(usize)BOB64_PROCESS_STACK_SIZE||
           (u64)length+1>cursor-BOB64_PROCESS_STACK_BASE)return -1;
        cursor-=(u64)length+1;string_addresses[i-1]=cursor;
        if(write_virtual(process,cursor,arguments[i-1],length)||
           write_virtual(process,cursor+length,"",1))return -1;
    }
    cursor&=~7ULL;
    if((u64)(argument_count+1)*sizeof(u64)>cursor-BOB64_PROCESS_STACK_BASE)return -1;
    cursor-=(u64)(argument_count+1)*sizeof(u64);argv_address=cursor;
    for(usize i=0;i<argument_count;i++)
        if(write_virtual(process,argv_address+i*sizeof(u64),&string_addresses[i],sizeof(u64)))return -1;
    if(write_virtual(process,argv_address+argument_count*sizeof(u64),&zero,sizeof(zero)))return -1;
    cursor&=~7ULL;
    if(sizeof(startup)>cursor-BOB64_PROCESS_STACK_BASE)return -1;
    cursor-=sizeof(startup);startup_address=cursor;
    startup.StructSize=(u32)sizeof(startup);startup.AbiVersion=BOB64_APP_ABI_VERSION;
    startup.ArgumentCount=(u64)argument_count;
    startup.Arguments=(const char *const *)(uintptr_t)argv_address;
    startup.Flags=0;
    if(write_virtual(process,startup_address,&startup,sizeof(startup)))return -1;
    cursor&=~15ULL;
    if(cursor<BOB64_PROCESS_STACK_BASE+40)return -1;
    initial_rsp=cursor-40;
    if(write_virtual(process,initial_rsp,&zero,sizeof(zero)))return -1;
    process->StartupAddress=startup_address;process->InitialStackPointer=initial_rsp;
    return 0;
}

int bob64_process_load(BOB64_PROCESS *process,const void *file,usize file_size,
                       usize argument_count,const char *const *arguments) {
    BOB64_EXEC_IMAGE parsed;
    const BOB64_EXEC_IMAGE *image=&parsed;
    usize image_pages;
    if(!process||process->Loaded||process->PageCount||
       !process->Pages||!process->Operations.NxSupported||
       bob64_exec_parse(file,file_size,&parsed)||
       !image->Image||image->AbiVersion!=BOB64_APP_ABI_VERSION||image->Flags||
       !image->CodeSize||(image->CodeSize&(BOB64_PAGE_SIZE-1))||
       image->CodeSize>image->FileSize||image->FileSize>image->MemorySize||
       !image->MemorySize||image->MemorySize>BOB64_PROCESS_IMAGE_LIMIT||
       image->EntryOffset>=image->CodeSize||
       image->MemorySize>~(u64)0-BOB64_PROCESS_IMAGE_BASE||
       argument_count>BOB64_PROCESS_MAX_ARGUMENTS||
       process->PageCapacity<BOB64_PROCESS_STACK_SIZE/BOB64_PAGE_SIZE+
                            (image->MemorySize+BOB64_PAGE_SIZE-1)/BOB64_PAGE_SIZE)
        return -1;
    image_pages=(usize)((image->MemorySize+BOB64_PAGE_SIZE-1)/BOB64_PAGE_SIZE);
    for(usize page=0;page<image_pages;page++) {
        u64 address=BOB64_PROCESS_IMAGE_BASE+(u64)page*BOB64_PAGE_SIZE;
        u64 flags=BOB64_PAGE_USER|BOB64_PAGE_WRITE|BOB64_PAGE_NX;
        if(map_page(process,address,flags))goto fail;
    }
    for(u64 offset=0;offset<image->FileSize;) {
        usize page=(usize)(offset/BOB64_PAGE_SIZE);
        usize in_page=(usize)(offset&(BOB64_PAGE_SIZE-1));
        usize part=(usize)BOB64_PAGE_SIZE-in_page;
        if((u64)part>image->FileSize-offset)part=(usize)(image->FileSize-offset);
        copy_bytes(process->Pages[page].WritableAddress+in_page,image->Image+(usize)offset,part);
        offset+=part;
    }
    for(usize page=0;page<(usize)(image->CodeSize/BOB64_PAGE_SIZE);page++) {
        BOB64_PROCESS_PAGE *record=process->Pages+page;
        if(process->Operations.ProtectPage(process->Operations.Context,
             record->VirtualAddress,BOB64_PAGE_USER))goto fail;
        record->Flags=BOB64_PAGE_USER;
    }
    for(usize page=0;page<BOB64_PROCESS_STACK_SIZE/BOB64_PAGE_SIZE;page++)
        if(map_page(process,BOB64_PROCESS_STACK_BASE+(u64)page*BOB64_PAGE_SIZE,
                    BOB64_PAGE_USER|BOB64_PAGE_WRITE|BOB64_PAGE_NX))goto fail;
    if(prepare_arguments(process,argument_count,arguments))goto fail;
    process->EntryAddress=BOB64_PROCESS_IMAGE_BASE+image->EntryOffset;
    process->Loaded=1;
    return 0;
fail:
    bob64_process_unload(process);
    return -1;
}

int bob64_process_unload(BOB64_PROCESS *process) {
    int result=0;
    if(!process||!process->Pages)return -1;
    for(usize i=process->PageCount;i>0;i--) {
        BOB64_PROCESS_PAGE *page=process->Pages+(i-1);
        if(page->Mapped) {
            if(process->Operations.UnmapPage(process->Operations.Context,page->VirtualAddress)) {
                result=-1;continue;
            }
            page->Mapped=0;page->WritableAddress=0;
        }
        if(page->Allocated) {
            if(process->Operations.FreePage(process->Operations.Context,page->PhysicalAddress)) {
                result=-1;continue;
            }
            page->Allocated=0;
        }
    }
    usize remaining=0;
    for(usize i=0;i<process->PageCount;i++)
        if(process->Pages[i].Mapped||process->Pages[i].Allocated)
            process->Pages[remaining++]=process->Pages[i];
    process->PageCount=remaining;
    if(!remaining) {
        process->Loaded=0;process->EntryAddress=0;process->StartupAddress=0;
        process->InitialStackPointer=0;
    }
    return result;
}
