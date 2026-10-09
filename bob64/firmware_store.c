#include "firmware_store.h"
#include "heap.h"
#include "lz4.h"
#include "snapshot.h"

#define BOB64_FIRMWARE_SNAPSHOT_VERSION 2u
#define BOB64_FIRMWARE_LEGACY_VERSION 1u
#define BOB64_FIRMWARE_SNAPSHOT_ATTRIBUTES (EFI_VARIABLE_NON_VOLATILE|\
    EFI_VARIABLE_BOOTSERVICE_ACCESS|EFI_VARIABLE_RUNTIME_ACCESS)
#define BOB64_FIRMWARE_SNAPSHOT_RAW_LIMIT (32u*1024u*1024u)
#define BOB64_FIRMWARE_CHUNK_SIZE 30720u
#define BOB64_FIRMWARE_CHUNK_COUNT 6u
#define BOB64_FIRMWARE_MANIFEST_SIZE 32u
#define BOB64_FIRMWARE_ACTIVE_SIZE 16u

static const EFI_GUID firmware_snapshot_guid={0x8b7a4c21u,0x6c34u,0x4f09u,
                                                {0x92,0x6d,0x42,0x6f,0x62,0x36,0x34,0x01}};
static const CHAR16 firmware_legacy_name[]={
    'B','o','b','6','4','S','n','a','p','s','h','o','t',0
};
static const CHAR16 firmware_manifest_names[2][14]={
    {'B','o','b','6','4','S','l','o','t','A',0},
    {'B','o','b','6','4','S','l','o','t','B',0}
};
static const CHAR16 firmware_chunk_names[2][BOB64_FIRMWARE_CHUNK_COUNT][16]={
    {{'B','o','b','6','4','D','a','t','a','A','0',0},
     {'B','o','b','6','4','D','a','t','a','A','1',0},
     {'B','o','b','6','4','D','a','t','a','A','2',0},
     {'B','o','b','6','4','D','a','t','a','A','3',0},
     {'B','o','b','6','4','D','a','t','a','A','4',0},
     {'B','o','b','6','4','D','a','t','a','A','5',0}},
    {{'B','o','b','6','4','D','a','t','a','B','0',0},
     {'B','o','b','6','4','D','a','t','a','B','1',0},
     {'B','o','b','6','4','D','a','t','a','B','2',0},
     {'B','o','b','6','4','D','a','t','a','B','3',0},
     {'B','o','b','6','4','D','a','t','a','B','4',0},
     {'B','o','b','6','4','D','a','t','a','B','5',0}}
};
static const CHAR16 firmware_active_name[]={
    'B','o','b','6','4','A','c','t','i','v','e',0
};

static void write32(u8 *bytes,u32 value) {
    for(u32 i=0;i<4;i++)bytes[i]=(u8)(value>>(i*8));
}

static void write64(u8 *bytes,u64 value) {
    write32(bytes,(u32)value);write32(bytes+4,(u32)(value>>32));
}

static u32 read32(const u8 *bytes) {
    return (u32)bytes[0]|((u32)bytes[1]<<8)|((u32)bytes[2]<<16)|
           ((u32)bytes[3]<<24);
}

static u64 read64(const u8 *bytes) {
    return (u64)read32(bytes)|((u64)read32(bytes+4)<<32);
}

static u32 crc32(const u8 *bytes,usize length) {
    u32 value=0xffffffffu;
    for(usize i=0;i<length;i++) {
        value^=bytes[i];
        for(u32 bit=0;bit<8;bit++)
            value=(value>>1)^(0xedb88320u&-(value&1u));
    }
    return ~value;
}

static EFI_STATUS call_set_variable(EFI_RUNTIME_SERVICES *services,
        const CHAR16 *name,const void *data,usize size,u32 attributes) {
    EFI_STATUS status;
#ifdef BOB64_UEFI_ABI
    u64 flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
#endif
    status=services->SetVariable(name,&firmware_snapshot_guid,attributes,
                                  (UINTN)size,data);
#ifdef BOB64_UEFI_ABI
    if(flags&(1ull<<9))__asm__ volatile("sti" ::: "memory");
#endif
    return status;
}

static EFI_STATUS call_get_variable(EFI_RUNTIME_SERVICES *services,
        const CHAR16 *name,UINTN *size,void *data,u32 *attributes) {
    EFI_STATUS status;
#ifdef BOB64_UEFI_ABI
    u64 flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
#endif
    status=services->GetVariable(name,&firmware_snapshot_guid,attributes,size,data);
#ifdef BOB64_UEFI_ABI
    if(flags&(1ull<<9))__asm__ volatile("sti" ::: "memory");
#endif
    return status;
}

static EFI_STATUS query_variable_capacity(EFI_RUNTIME_SERVICES *services,
        UINTN required,UINTN *maximum_variable_size,
        UINTN *remaining_storage_size) {
    UINTN maximum_storage=0,remaining_storage=0,maximum_variable=0;
    EFI_STATUS status;
    if(maximum_variable_size)*maximum_variable_size=0;
    if(remaining_storage_size)*remaining_storage_size=0;
    if(!services->QueryVariableInfo)return EFI_SUCCESS;
#ifdef BOB64_UEFI_ABI
    u64 flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
#endif
    status=services->QueryVariableInfo(BOB64_FIRMWARE_SNAPSHOT_ATTRIBUTES,
          &maximum_storage,&remaining_storage,&maximum_variable);
#ifdef BOB64_UEFI_ABI
    if(flags&(1ull<<9))__asm__ volatile("sti" ::: "memory");
#endif
    if(maximum_variable_size)*maximum_variable_size=maximum_variable;
    if(remaining_storage_size)*remaining_storage_size=remaining_storage;
    (void)maximum_storage;
    if(status==EFI_UNSUPPORTED)return EFI_SUCCESS;
    if(EFI_ERROR(status))return status;
    return required<=maximum_variable?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;
}

static int load_variable(EFI_RUNTIME_SERVICES *services,const CHAR16 *name,
        BOB64_HEAP *heap,u8 **data,usize *size) {
    UINTN variable_size=0,actual_size;
    u32 attributes=0;
    u8 *buffer;
    EFI_STATUS status;
    if(data)*data=0;
    if(size)*size=0;
    if(!services||!services->GetVariable||!heap||!data||!size)return -1;
    status=call_get_variable(services,name,&variable_size,0,&attributes);
    if(status==EFI_NOT_FOUND)return BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND;
    if(status!=EFI_BUFFER_TOO_SMALL||!variable_size||
       variable_size>BOB64_FIRMWARE_SNAPSHOT_LIMIT)return -1;
    buffer=(u8 *)bob64_heap_alloc(heap,(usize)variable_size);
    if(!buffer)return -1;
    actual_size=variable_size;
    status=call_get_variable(services,name,&actual_size,buffer,&attributes);
    if(EFI_ERROR(status)||actual_size!=variable_size||
       (attributes&BOB64_FIRMWARE_SNAPSHOT_ATTRIBUTES)!=
          BOB64_FIRMWARE_SNAPSHOT_ATTRIBUTES) {
        bob64_heap_free(heap,buffer);return -1;
    }
    *data=buffer;*size=(usize)variable_size;
    return 0;
}

static EFI_STATUS delete_variable(EFI_RUNTIME_SERVICES *services,
                                  const CHAR16 *name) {
    EFI_STATUS status=call_set_variable(services,name,0,0,0);
    return status==EFI_NOT_FOUND?EFI_SUCCESS:status;
}

static int variable_present(EFI_RUNTIME_SERVICES *services,const CHAR16 *name,
                            BOB64_HEAP *heap) {
    u8 *data=0;usize size=0;
    int result=load_variable(services,name,heap,&data,&size);
    if(data)bob64_heap_free(heap,data);
    return result!=BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND;
}

static int read_active_slot(EFI_RUNTIME_SERVICES *services,BOB64_HEAP *heap,
                            u32 *slot) {
    u8 *data=0;usize size=0;
    int result=load_variable(services,firmware_active_name,heap,&data,&size);
    if(result==BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND)return 0;
    if(result||size!=BOB64_FIRMWARE_ACTIVE_SIZE||data[0]!='B'||data[1]!='6'||
       data[2]!='4'||data[3]!='A'||read32(data+4)!=1||
       read32(data+8)>1||read32(data+12)!=crc32(data,12))result=-1;
    else {*slot=read32(data+8);result=1;}
    if(data)bob64_heap_free(heap,data);
    return result;
}

static int restore_compressed(BOB64_FILESYSTEM *filesystem,const u8 *compressed,
        usize compressed_size,u64 raw_size) {
    u8 *raw;
    int result=-1;
    if(!raw_size||raw_size>BOB64_FIRMWARE_SNAPSHOT_RAW_LIMIT||
       raw_size>~(usize)0||compressed_size>~(usize)0)return -1;
    raw=(u8 *)bob64_heap_alloc(filesystem->Heap,(usize)raw_size);
    if(!raw)return -1;
    if(!bob64_lz4_decompress(compressed,compressed_size,raw,(usize)raw_size,
                             (usize)raw_size)&&
       !bob64_fs_snapshot_restore(filesystem,raw,(usize)raw_size))result=0;
    bob64_heap_free(filesystem->Heap,raw);
    return result;
}

static int restore_slot(EFI_RUNTIME_SERVICES *services,
        BOB64_FILESYSTEM *filesystem,u32 slot) {
    BOB64_HEAP *heap=filesystem->Heap;
    u8 *manifest=0,*compressed=0;
    usize manifest_size=0;
    u64 raw_size,compressed_size;
    u32 chunks,checksum;
    usize offset=0;
    int loaded=load_variable(services,firmware_manifest_names[slot],heap,
                             &manifest,&manifest_size);
    if(loaded)return loaded;
    loaded=-1;
    if(manifest_size!=BOB64_FIRMWARE_MANIFEST_SIZE||manifest[0]!='B'||
       manifest[1]!='6'||manifest[2]!='4'||manifest[3]!='C'||
       read32(manifest+4)!=BOB64_FIRMWARE_SNAPSHOT_VERSION)goto done;
    raw_size=read64(manifest+8);compressed_size=read64(manifest+16);
    checksum=read32(manifest+24);chunks=read32(manifest+28);
    if(!raw_size||raw_size>BOB64_FIRMWARE_SNAPSHOT_RAW_LIMIT||
       !compressed_size||compressed_size>BOB64_FIRMWARE_SNAPSHOT_LIMIT||
       chunks!=(compressed_size+BOB64_FIRMWARE_CHUNK_SIZE-1)/
                   BOB64_FIRMWARE_CHUNK_SIZE||
       !chunks||chunks>BOB64_FIRMWARE_CHUNK_COUNT||
       compressed_size>~(usize)0)goto done;
    compressed=(u8 *)bob64_heap_alloc(heap,(usize)compressed_size);
    if(!compressed)goto done;
    for(u32 i=0;i<chunks;i++) {
        u8 *chunk=0;usize chunk_size=0;
        usize expected=(usize)compressed_size-offset;
        if(expected>BOB64_FIRMWARE_CHUNK_SIZE)
            expected=BOB64_FIRMWARE_CHUNK_SIZE;
        if(load_variable(services,firmware_chunk_names[slot][i],heap,
                         &chunk,&chunk_size)||chunk_size!=expected) {
            if(chunk)bob64_heap_free(heap,chunk);
            goto done;
        }
        for(usize j=0;j<chunk_size;j++)compressed[offset+j]=chunk[j];
        offset+=chunk_size;
        bob64_heap_free(heap,chunk);
    }
    if(offset!=(usize)compressed_size||crc32(compressed,offset)!=checksum)goto done;
    loaded=restore_compressed(filesystem,compressed,offset,raw_size);
done:
    if(compressed)bob64_heap_free(heap,compressed);
    if(manifest)bob64_heap_free(heap,manifest);
    return loaded;
}

static int restore_legacy(EFI_RUNTIME_SERVICES *services,
        BOB64_FILESYSTEM *filesystem) {
    BOB64_HEAP *heap=filesystem->Heap;
    u8 *blob=0;
    usize blob_size=0;
    u64 raw_size,compressed_size;
    int result=load_variable(services,firmware_legacy_name,heap,&blob,&blob_size);
    if(result)return result;
    if(blob_size<24||blob[0]!='B'||blob[1]!='6'||blob[2]!='4'||blob[3]!='C'||
       read32(blob+4)!=BOB64_FIRMWARE_LEGACY_VERSION)result=-1;
    else {
        raw_size=read64(blob+8);compressed_size=read64(blob+16);
        if(compressed_size!=blob_size-24||
           compressed_size>~(usize)0)result=-1;
        else result=restore_compressed(filesystem,blob+24,
                                        (usize)compressed_size,raw_size);
    }
    bob64_heap_free(heap,blob);
    return result;
}

int bob64_firmware_snapshot_save(EFI_RUNTIME_SERVICES *services,
        BOB64_HEAP *heap,const void *snapshot,usize snapshot_size,
        EFI_STATUS *firmware_status,UINTN *maximum_variable_size,
        UINTN *remaining_storage_size) {
    usize compressed_capacity,compressed_size=0,offset=0;
    const u8 *raw=(const u8 *)snapshot;
    u8 *compressed=0,*workspace=0;
    u8 manifest[BOB64_FIRMWARE_MANIFEST_SIZE];
    u8 active[BOB64_FIRMWARE_ACTIVE_SIZE]={0};
    EFI_STATUS status=EFI_INVALID_PARAMETER;
    u32 active_slot=0,target_slot,chunks;
    int result=-1;
    if(firmware_status)*firmware_status=status;
    if(maximum_variable_size)*maximum_variable_size=0;
    if(remaining_storage_size)*remaining_storage_size=0;
    if(!services||!services->SetVariable||!heap||!raw||!snapshot_size||
       snapshot_size>BOB64_FIRMWARE_SNAPSHOT_RAW_LIMIT)return -1;
    compressed_capacity=bob64_lz4_compress_bound(snapshot_size);
    if(!compressed_capacity)return -1;
    status=EFI_OUT_OF_RESOURCES;
    compressed=(u8 *)bob64_heap_alloc(heap,compressed_capacity);
    workspace=(u8 *)bob64_heap_alloc(heap,BOB64_LZ4_WORKSPACE_SIZE);
    if(!compressed||!workspace||bob64_lz4_compress(raw,snapshot_size,
        compressed,compressed_capacity,&compressed_size,workspace,
        BOB64_LZ4_WORKSPACE_SIZE))goto done;
    if(!compressed_size||compressed_size>BOB64_FIRMWARE_SNAPSHOT_LIMIT)goto done;
    chunks=(u32)((compressed_size+BOB64_FIRMWARE_CHUNK_SIZE-1)/
                 BOB64_FIRMWARE_CHUNK_SIZE);
    if(!chunks||chunks>BOB64_FIRMWARE_CHUNK_COUNT)goto done;
    usize largest_chunk=compressed_size>BOB64_FIRMWARE_CHUNK_SIZE?
                         BOB64_FIRMWARE_CHUNK_SIZE:compressed_size;
    status=query_variable_capacity(services,(UINTN)largest_chunk,
        maximum_variable_size,remaining_storage_size);
    if(EFI_ERROR(status))goto done;
    int active_result=read_active_slot(services,heap,&active_slot);
    if(active_result==1)target_slot=active_slot^1u;
    else target_slot=variable_present(services,firmware_manifest_names[0],heap)?1u:0u;
    status=delete_variable(services,firmware_manifest_names[target_slot]);
    if(EFI_ERROR(status))goto done;
    for(u32 i=0;i<BOB64_FIRMWARE_CHUNK_COUNT;i++) {
        status=delete_variable(services,firmware_chunk_names[target_slot][i]);
        if(EFI_ERROR(status))goto done;
    }
    for(u32 i=0;i<chunks;i++) {
        usize chunk_size=compressed_size-offset;
        if(chunk_size>BOB64_FIRMWARE_CHUNK_SIZE)
            chunk_size=BOB64_FIRMWARE_CHUNK_SIZE;
        status=call_set_variable(services,firmware_chunk_names[target_slot][i],
            compressed+offset,chunk_size,BOB64_FIRMWARE_SNAPSHOT_ATTRIBUTES);
        if(EFI_ERROR(status))goto done;
        offset+=chunk_size;
    }
    manifest[0]='B';manifest[1]='6';manifest[2]='4';manifest[3]='C';
    write32(manifest+4,BOB64_FIRMWARE_SNAPSHOT_VERSION);
    write64(manifest+8,snapshot_size);write64(manifest+16,compressed_size);
    write32(manifest+24,crc32(compressed,compressed_size));
    write32(manifest+28,chunks);
    status=call_set_variable(services,firmware_manifest_names[target_slot],
        manifest,sizeof(manifest),BOB64_FIRMWARE_SNAPSHOT_ATTRIBUTES);
    if(EFI_ERROR(status))goto done;
    active[0]='B';active[1]='6';active[2]='4';active[3]='A';
    write32(active+4,1);write32(active+8,target_slot);
    write32(active+12,crc32(active,12));
    status=call_set_variable(services,firmware_active_name,active,sizeof(active),
                             BOB64_FIRMWARE_SNAPSHOT_ATTRIBUTES);
    if(EFI_ERROR(status))goto done;
    /* The new slot is committed; the obsolete v1 variable can now be reclaimed. */
    (void)delete_variable(services,firmware_legacy_name);
    result=0;
done:
    if(firmware_status)*firmware_status=status;
    if(workspace)bob64_heap_free(heap,workspace);
    if(compressed)bob64_heap_free(heap,compressed);
    return result;
}

int bob64_firmware_snapshot_restore(EFI_RUNTIME_SERVICES *services,
                                    BOB64_FILESYSTEM *filesystem) {
    u32 active_slot=0;
    int active_result,slot_result,present=0;
    if(!services||!services->GetVariable)
        return BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND;
    if(!filesystem||!filesystem->Heap)return -1;
    active_result=read_active_slot(services,filesystem->Heap,&active_slot);
    for(u32 i=0;i<2;i++) {
        u32 slot=active_result==1?(i?active_slot^1u:active_slot):i;
        slot_result=restore_slot(services,filesystem,slot);
        if(!slot_result)return 0;
        if(slot_result!=BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND)present=1;
    }
    slot_result=restore_legacy(services,filesystem);
    if(!slot_result)return 0;
    if(slot_result!=BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND)present=1;
    return present?-1:BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND;
}
