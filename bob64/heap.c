#include "heap.h"

#define BOB64_HEAP_BLOCK_MAGIC 0xB064B10Cu
#define BOB64_HEAP_ALIGNMENT 16u
#define BOB64_HEAP_MINIMUM_REGION 4096u

static u64 heap_lock(BOB64_HEAP *heap) {
    u64 flags=0;
#ifdef BOB64_UEFI_ABI
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
#endif
    while(__atomic_exchange_n(&heap->Lock,1,__ATOMIC_ACQUIRE))
        __asm__ volatile("pause" ::: "memory");
    return flags;
}

static void heap_unlock(BOB64_HEAP *heap,u64 flags) {
    __atomic_store_n(&heap->Lock,0,__ATOMIC_RELEASE);
#ifdef BOB64_UEFI_ABI
    if(flags&(1ull<<9))__asm__ volatile("sti" ::: "memory");
#else
    (void)flags;
#endif
}

static int align_size(usize value,usize *aligned) {
    if(value>~(usize)0-(BOB64_HEAP_ALIGNMENT-1))return 0;
    *aligned=(value+(BOB64_HEAP_ALIGNMENT-1))&~(usize)(BOB64_HEAP_ALIGNMENT-1);
    return 1;
}

static BOB64_HEAP_BLOCK *block_at(void *address,usize bytes) {
    BOB64_HEAP_BLOCK *block;
    if(bytes<sizeof(*block)+BOB64_HEAP_ALIGNMENT||
       (uintptr_t)address&(BOB64_HEAP_ALIGNMENT-1))return 0;
    block=(BOB64_HEAP_BLOCK *)address;
    block->Size=bytes-sizeof(*block);
    block->Previous=0;block->Next=0;block->Magic=BOB64_HEAP_BLOCK_MAGIC;
    block->Free=1;block->Reserved[0]=block->Reserved[1]=block->Reserved[2]=0;
    return block;
}

int bob64_heap_init(BOB64_HEAP *heap,usize maximum_bytes,
                    BOB64_HEAP_GROW grow,void *context) {
    if(!heap||!grow||maximum_bytes<BOB64_HEAP_MINIMUM_REGION)return -1;
    heap->First=0;heap->Last=0;heap->Grow=grow;heap->Context=context;
    heap->Shrink=0;
    heap->Base=0;heap->MappedBytes=0;heap->MaximumBytes=maximum_bytes;
    __atomic_store_n(&heap->Lock,0,__ATOMIC_RELAXED);
    return 0;
}

void bob64_heap_set_shrink(BOB64_HEAP *heap,BOB64_HEAP_SHRINK shrink) {
    if(heap) {
        u64 flags=heap_lock(heap);
        heap->Shrink=shrink;
        heap_unlock(heap,flags);
    }
}

static BOB64_HEAP_BLOCK *find_block(BOB64_HEAP *heap,usize size) {
    for(BOB64_HEAP_BLOCK *block=heap->First;block;block=block->Next)
        if(block->Magic==BOB64_HEAP_BLOCK_MAGIC&&block->Free&&block->Size>=size)
            return block;
    return 0;
}

static int grow_heap(BOB64_HEAP *heap) {
    void *region=0;
    usize bytes=0;
    if(heap->MappedBytes>=heap->MaximumBytes||!heap->Grow(heap->Context,&region,&bytes)||
       !region||!bytes||(bytes&(BOB64_HEAP_ALIGNMENT-1))||
       bytes>heap->MaximumBytes-heap->MappedBytes||
       ((uintptr_t)region&(BOB64_HEAP_ALIGNMENT-1))||
       heap->MappedBytes>~(uintptr_t)0-(uintptr_t)region)return -1;
    if(heap->Base&&(u8 *)region!=(u8 *)heap->Base+heap->MappedBytes)return -1;
    BOB64_HEAP_BLOCK *block=block_at(region,bytes);
    if(!block)return -1;
    if(!heap->Base)heap->Base=region;
    heap->MappedBytes+=bytes;
    if(heap->Last&&heap->Last->Free) {
        heap->Last->Size+=sizeof(*block)+block->Size;
        heap->Last->Next=0;
    } else {
        block->Previous=heap->Last;
        if(heap->Last)heap->Last->Next=block;
        else heap->First=block;
        heap->Last=block;
    }
    return 0;
}

static void shrink_heap_tail(BOB64_HEAP *heap) {
    usize bytes,maximum;
    void *region;
    if(!heap||!heap->Shrink||!heap->Last||!heap->Last->Free||
       heap->MappedBytes<=BOB64_HEAP_MINIMUM_REGION)return;
    bytes=heap->Last->Size&~(usize)(BOB64_HEAP_MINIMUM_REGION-1);
    maximum=(heap->MappedBytes-BOB64_HEAP_MINIMUM_REGION)&
            ~(usize)(BOB64_HEAP_MINIMUM_REGION-1);
    if(bytes>maximum)bytes=maximum;
    if(!bytes)return;
    region=(u8 *)heap->Base+heap->MappedBytes-bytes;
    if(heap->Shrink(heap->Context,region,bytes))return;
    heap->Last->Size-=bytes;
    heap->MappedBytes-=bytes;
}

void *bob64_heap_alloc(BOB64_HEAP *heap,usize size) {
    usize aligned;
    if(!heap||!heap->Grow||!size||!align_size(size,&aligned))return 0;
    u64 flags=heap_lock(heap);
    BOB64_HEAP_BLOCK *block;
    while(!(block=find_block(heap,aligned)))if(grow_heap(heap)) {
        heap_unlock(heap,flags);
        return 0;
    }
    if(block->Size>=sizeof(*block)+BOB64_HEAP_ALIGNMENT&&
       aligned<=block->Size-sizeof(*block)-BOB64_HEAP_ALIGNMENT) {
        BOB64_HEAP_BLOCK *remainder=(BOB64_HEAP_BLOCK *)((u8 *)(block+1)+aligned);
        remainder->Size=block->Size-aligned-sizeof(*remainder);
        remainder->Previous=block;remainder->Next=block->Next;
        remainder->Magic=BOB64_HEAP_BLOCK_MAGIC;remainder->Free=1;
        remainder->Reserved[0]=remainder->Reserved[1]=remainder->Reserved[2]=0;
        if(remainder->Next)remainder->Next->Previous=remainder;
        else heap->Last=remainder;
        block->Next=remainder;block->Size=aligned;
    }
    block->Free=0;
    heap_unlock(heap,flags);
    return block+1;
}

void *bob64_heap_calloc(BOB64_HEAP *heap,usize count,usize size) {
    usize bytes;
    if(!count||!size||size>~(usize)0/count)return 0;
    bytes=count*size;
    void *pointer=bob64_heap_alloc(heap,bytes);
    if(pointer) {
        u8 *destination=(u8 *)pointer;
        for(usize i=0;i<bytes;i++)destination[i]=0;
    }
    return pointer;
}

int bob64_heap_free(BOB64_HEAP *heap,void *pointer) {
    uintptr_t address=(uintptr_t)pointer,base,end;
    BOB64_HEAP_BLOCK *block;
    if(!heap||!pointer||(address&(BOB64_HEAP_ALIGNMENT-1)))return -1;
    u64 flags=heap_lock(heap);
    if(!heap->Base) {heap_unlock(heap,flags);return -1;}
    base=(uintptr_t)heap->Base;
    if(heap->MappedBytes>~(uintptr_t)0-base) {heap_unlock(heap,flags);return -1;}
    end=base+heap->MappedBytes;
    if(address<base+sizeof(*block)||address>=end) {heap_unlock(heap,flags);return -1;}
    block=(BOB64_HEAP_BLOCK *)pointer-1;
    if((uintptr_t)block<base||(uintptr_t)block+sizeof(*block)>end||
       block->Magic!=BOB64_HEAP_BLOCK_MAGIC||block->Free||
       block->Size>(usize)(end-(uintptr_t)(block+1))) {
        heap_unlock(heap,flags);
        return -1;
    }
    block->Free=1;
    if(block->Next&&block->Next->Magic==BOB64_HEAP_BLOCK_MAGIC&&block->Next->Free) {
        BOB64_HEAP_BLOCK *next=block->Next;
        block->Size+=sizeof(*next)+next->Size;block->Next=next->Next;
        if(block->Next)block->Next->Previous=block;
        else heap->Last=block;
        next->Magic=0;
    }
    if(block->Previous&&block->Previous->Magic==BOB64_HEAP_BLOCK_MAGIC&&block->Previous->Free) {
        BOB64_HEAP_BLOCK *previous=block->Previous;
        previous->Size+=sizeof(*block)+block->Size;previous->Next=block->Next;
        if(previous->Next)previous->Next->Previous=previous;
        else heap->Last=previous;
        block->Magic=0;
    }
    shrink_heap_tail(heap);
    heap_unlock(heap,flags);
    return 0;
}

usize bob64_heap_mapped_bytes(const BOB64_HEAP *heap) {
    usize result;
    if(!heap)return 0;
    u64 flags=heap_lock((BOB64_HEAP *)heap);
    result=heap->MappedBytes;
    heap_unlock((BOB64_HEAP *)heap,flags);
    return result;
}
