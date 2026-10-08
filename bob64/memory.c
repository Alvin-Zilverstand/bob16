#include "memory.h"

#define BOB64_LOW_MEMORY_LIMIT 0x100000ULL
#define BOB64_U64_MAX (~(u64)0)

static int extent_end(u64 base,u64 pages,u64 *end) {
    if(!pages||pages>(BOB64_U64_MAX-base)/EFI_PAGE_SIZE)return 0;
    *end=base+pages*EFI_PAGE_SIZE;
    return 1;
}

static void remove_extent(BOB64_PAGE_ALLOCATOR *allocator,usize index) {
    for(usize i=index;i+1<allocator->Count;i++)allocator->Extents[i]=allocator->Extents[i+1];
    allocator->Count--;
}

static int add_extent(BOB64_PAGE_ALLOCATOR *allocator,u64 base,u64 pages) {
    usize index=0;
    u64 end,next_end;
    if(!pages)return 0;
    if((base&(EFI_PAGE_SIZE-1))||!extent_end(base,pages,&end))return BOB64_PAGE_ALLOC_INVALID;
    while(index<allocator->Count&&allocator->Extents[index].Base<base)index++;
    if(index>0) {
        BOB64_PAGE_EXTENT *previous=&allocator->Extents[index-1];
        if(!extent_end(previous->Base,previous->Pages,&next_end)||next_end>base)
            return BOB64_PAGE_ALLOC_INVALID;
    }
    if(index<allocator->Count&&end>allocator->Extents[index].Base)
        return BOB64_PAGE_ALLOC_INVALID;
    if(index>0) {
        BOB64_PAGE_EXTENT *previous=&allocator->Extents[index-1];
        if(previous->Base+previous->Pages*EFI_PAGE_SIZE==base) {
            previous->Pages+=pages;
            if(index<allocator->Count&&end==allocator->Extents[index].Base) {
                previous->Pages+=allocator->Extents[index].Pages;
                remove_extent(allocator,index);
            }
            return 0;
        }
    }
    if(index<allocator->Count&&end==allocator->Extents[index].Base) {
        allocator->Extents[index].Base=base;
        allocator->Extents[index].Pages+=pages;
        return 0;
    }
    if(allocator->Count>=allocator->Capacity)return BOB64_PAGE_ALLOC_FULL;
    for(usize i=allocator->Count;i>index;i--)allocator->Extents[i]=allocator->Extents[i-1];
    allocator->Extents[index].Base=base;
    allocator->Extents[index].Pages=pages;
    allocator->Count++;
    return 0;
}

int bob64_page_allocator_init(BOB64_PAGE_ALLOCATOR *allocator,
                              BOB64_PAGE_EXTENT *storage,usize capacity,
                              u8 physical_address_bits,
                              const void *memory_map,usize memory_map_size,
                              usize descriptor_size) {
    const u8 *bytes=(const u8 *)memory_map;
    u64 physical_limit;
    if(!allocator||!storage||!capacity||capacity>BOB64_PAGE_EXTENT_LIMIT||
       physical_address_bits<36||physical_address_bits>52||
       !memory_map||descriptor_size<sizeof(EFI_MEMORY_DESCRIPTOR)||
       !memory_map_size||memory_map_size%descriptor_size)return BOB64_PAGE_ALLOC_INVALID;
    physical_limit=1ULL<<physical_address_bits;
    allocator->Count=0;allocator->Capacity=capacity;allocator->Extents=storage;
    allocator->AllocatedCount=0;
    for(usize offset=0;offset<memory_map_size;offset+=descriptor_size) {
        const EFI_MEMORY_DESCRIPTOR *descriptor=(const EFI_MEMORY_DESCRIPTOR *)(bytes+offset);
        u64 base=descriptor->PhysicalStart,pages=descriptor->NumberOfPages,end;
        if(descriptor->Type!=EFI_MEMORY_CONVENTIONAL||!pages)continue;
        if(!extent_end(base,pages,&end))return BOB64_PAGE_ALLOC_INVALID;
        if(base>=physical_limit)continue;
        if(end>physical_limit)pages=(physical_limit-base)/EFI_PAGE_SIZE;
        if(base<BOB64_LOW_MEMORY_LIMIT) {
            u64 skipped=(BOB64_LOW_MEMORY_LIMIT-base+EFI_PAGE_SIZE-1)/EFI_PAGE_SIZE;
            if(skipped>=pages)continue;
            base+=skipped*EFI_PAGE_SIZE;
            pages-=skipped;
        }
        int result=add_extent(allocator,base,pages);
        if(result)return result;
    }
    return (int)allocator->Count;
}

u64 bob64_page_alloc(BOB64_PAGE_ALLOCATOR *allocator,u64 pages) {
    if(!allocator||!allocator->Extents||!pages||
       allocator->AllocatedCount==BOB64_PAGE_ALLOCATION_LIMIT)return 0;
    for(usize i=0;i<allocator->Count;i++)if(allocator->Extents[i].Pages>=pages) {
        u64 address=allocator->Extents[i].Base;
        allocator->Extents[i].Base+=pages*EFI_PAGE_SIZE;
        allocator->Extents[i].Pages-=pages;
        if(!allocator->Extents[i].Pages)remove_extent(allocator,i);
        allocator->Allocated[allocator->AllocatedCount].Base=address;
        allocator->Allocated[allocator->AllocatedCount].Pages=pages;
        allocator->AllocatedCount++;
        return address;
    }
    return 0;
}

int bob64_page_free(BOB64_PAGE_ALLOCATOR *allocator,u64 address,u64 pages) {
    usize index=0;
    int result;
    if(!allocator||!allocator->Extents||address<BOB64_LOW_MEMORY_LIMIT||
       (address&(EFI_PAGE_SIZE-1))||!pages)return BOB64_PAGE_ALLOC_INVALID;
    while(index<allocator->AllocatedCount&&
          (allocator->Allocated[index].Base!=address||allocator->Allocated[index].Pages!=pages))index++;
    if(index==allocator->AllocatedCount)return BOB64_PAGE_ALLOC_INVALID;
    result=add_extent(allocator,address,pages);
    if(result)return result;
    for(usize i=index;i+1<allocator->AllocatedCount;i++)allocator->Allocated[i]=allocator->Allocated[i+1];
    allocator->AllocatedCount--;
    return 0;
}
