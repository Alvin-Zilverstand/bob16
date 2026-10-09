#include "disk_store.h"

#define DISK_STORE_VERSION 1u
#define DISK_STORE_HEADER_SIZE 40u
#define DISK_STORE_HEADER_CRC_OFFSET 36u

typedef struct {
    u64 Generation,PayloadSize;
    u32 PayloadCrc;
    u8 Valid;
} DISK_STORE_SLOT;

static u8 disk_store_workspace[BOB64_BLOCK_MAX_TRANSFER_BYTES];

static u32 read32(const u8 *bytes) {
    return (u32)bytes[0]|((u32)bytes[1]<<8)|
           ((u32)bytes[2]<<16)|((u32)bytes[3]<<24);
}

static u64 read64(const u8 *bytes) {
    return (u64)read32(bytes)|((u64)read32(bytes+4)<<32);
}

static void write32(u8 *bytes,u32 value) {
    for(u32 i=0;i<4;i++)bytes[i]=(u8)(value>>(i*8));
}

static void write64(u8 *bytes,u64 value) {
    write32(bytes,(u32)value);write32(bytes+4,(u32)(value>>32));
}

static u32 crc32_step(u32 crc,const u8 *bytes,usize length) {
    for(usize i=0;i<length;i++) {
        crc^=bytes[i];
        for(u32 bit=0;bit<8;bit++)
            crc=(crc>>1)^(0xedb88320u&-(crc&1u));
    }
    return crc;
}

static u32 header_crc(const u8 *header) {
    u8 copy[DISK_STORE_HEADER_SIZE];
    for(u32 i=0;i<sizeof(copy);i++)
        copy[i]=(i>=DISK_STORE_HEADER_CRC_OFFSET&&
                 i<DISK_STORE_HEADER_CRC_OFFSET+4u)?0:header[i];
    return crc32_step(0xffffffffu,copy,sizeof(copy))^0xffffffffu;
}

static int valid_partition(BOB64_BLOCK_DEVICE *device,
        const BOB64_PARTITION *partition,u64 *slot_blocks) {
    if(!device||!partition||!slot_blocks||!device->BlockSize||
       device->BlockSize>sizeof(disk_store_workspace)||
       partition->BlockCount<4u||partition->FirstLba>=device->BlockCount||
       partition->BlockCount>device->BlockCount-partition->FirstLba||
       partition->BlockCount-1u>0xffffffffffffffffULL-partition->FirstLba)
        return -1;
    for(u32 i=0;i<BOB64_GPT_GUID_SIZE;i++)
        if(partition->TypeGuid[i]!=bob64_gpt_bob_data_type_guid[i])return -1;
    *slot_blocks=partition->BlockCount/2u;
    return *slot_blocks>=2u?0:-1;
}

static u64 slot_start(const BOB64_PARTITION *partition,u64 slot_blocks,
                      u32 slot) {
    return partition->FirstLba+(u64)slot*slot_blocks;
}

static int payload_crc(BOB64_BLOCK_DEVICE *device,u64 lba,u64 payload_size,
                       u32 *result_crc) {
    u32 crc=0xffffffffu;
    u64 remaining=payload_size;
    if(!device||!result_crc||!payload_size)return -1;
    while(remaining) {
        u64 blocks=(remaining+device->BlockSize-1u)/device->BlockSize;
        if(blocks>device->MaxTransferBlocks)blocks=device->MaxTransferBlocks;
        if(!blocks||blocks>0xffffffffu||
           bob64_block_read(device,lba,(u32)blocks,disk_store_workspace))return -1;
        u64 bytes=blocks*(u64)device->BlockSize;
        if(bytes>remaining)bytes=remaining;
        crc=crc32_step(crc,disk_store_workspace,(usize)bytes);
        lba+=blocks;remaining-=bytes;
    }
    *result_crc=crc^0xffffffffu;
    return 0;
}

static int read_slot(BOB64_BLOCK_DEVICE *device,
        const BOB64_PARTITION *partition,u64 slot_blocks,u32 index,
        DISK_STORE_SLOT *slot) {
    u64 start=slot_start(partition,slot_blocks,index);
    u64 maximum_payload=(slot_blocks-1u)*(u64)device->BlockSize;
    u32 actual_crc;
    if(!slot||bob64_block_read(device,start,1,disk_store_workspace))return -1;
    slot->Valid=0;slot->Generation=0;slot->PayloadSize=0;slot->PayloadCrc=0;
    if(disk_store_workspace[0]!='B'||disk_store_workspace[1]!='6'||
       disk_store_workspace[2]!='4'||disk_store_workspace[3]!='D'||
       read32(disk_store_workspace+4)!=DISK_STORE_VERSION||
       read32(disk_store_workspace+8)!=DISK_STORE_HEADER_SIZE||
       read32(disk_store_workspace+12)!=0||
       read32(disk_store_workspace+DISK_STORE_HEADER_CRC_OFFSET)!=
           header_crc(disk_store_workspace))return 1;
    slot->Generation=read64(disk_store_workspace+16);
    slot->PayloadSize=read64(disk_store_workspace+24);
    slot->PayloadCrc=read32(disk_store_workspace+32);
    if(!slot->Generation||!slot->PayloadSize||
       slot->PayloadSize>maximum_payload)return 1;
    if(payload_crc(device,start+1u,slot->PayloadSize,&actual_crc))return -1;
    if(actual_crc!=slot->PayloadCrc)return 1;
    slot->Valid=1;
    return 0;
}

static int write_payload(BOB64_BLOCK_DEVICE *device,u64 lba,
        const u8 *payload,usize payload_size) {
    u64 remaining=payload_size;
    usize offset=0;
    while(remaining) {
        u64 blocks=(remaining+device->BlockSize-1u)/device->BlockSize;
        if(blocks>device->MaxTransferBlocks)blocks=device->MaxTransferBlocks;
        if(!blocks||blocks>0xffffffffu)return -1;
        u64 capacity=blocks*(u64)device->BlockSize;
        u64 bytes=remaining<capacity?remaining:capacity;
        for(usize i=0;i<(usize)capacity;i++)
            disk_store_workspace[i]=i<(usize)bytes?payload[offset+i]:0;
        if(bob64_block_write(device,lba,(u32)blocks,disk_store_workspace))return -1;
        lba+=blocks;offset+=(usize)bytes;remaining-=bytes;
    }
    return 0;
}

static int read_payload(BOB64_BLOCK_DEVICE *device,u64 lba,
        u8 *payload,usize payload_size) {
    u64 remaining=payload_size;
    usize offset=0;
    while(remaining) {
        u64 blocks=(remaining+device->BlockSize-1u)/device->BlockSize;
        if(blocks>device->MaxTransferBlocks)blocks=device->MaxTransferBlocks;
        if(!blocks||blocks>0xffffffffu||
           bob64_block_read(device,lba,(u32)blocks,disk_store_workspace))return -1;
        u64 capacity=blocks*(u64)device->BlockSize;
        u64 bytes=remaining<capacity?remaining:capacity;
        for(usize i=0;i<(usize)bytes;i++)payload[offset+i]=disk_store_workspace[i];
        lba+=blocks;offset+=(usize)bytes;remaining-=bytes;
    }
    return 0;
}

static int inspect_slots(BOB64_BLOCK_DEVICE *device,
        const BOB64_PARTITION *partition,u64 slot_blocks,
        DISK_STORE_SLOT slots[2]) {
    for(u32 i=0;i<2;i++) {
        int result=read_slot(device,partition,slot_blocks,i,&slots[i]);
        if(result<0)return result;
    }
    return 0;
}

int bob64_disk_store_save(BOB64_BLOCK_DEVICE *device,
        const BOB64_PARTITION *partition,BOB64_FILESYSTEM *filesystem,
        BOB64_HEAP *heap) {
    DISK_STORE_SLOT slots[2]={{0}};
    usize snapshot_size=0,written=0;
    u8 *snapshot;
    u64 slot_blocks,maximum_payload,generation,start;
    u32 target;
    if(!device||!filesystem||!heap||device->ReadOnly||!device->Write||!device->Flush||
       valid_partition(device,partition,&slot_blocks)||
       bob64_fs_snapshot_size(filesystem,&snapshot_size)||!snapshot_size)
        return -1;
    maximum_payload=(slot_blocks-1u)*(u64)device->BlockSize;
    if((u64)snapshot_size>maximum_payload)return -2;
    if(inspect_slots(device,partition,slot_blocks,slots))return -3;
    if(!slots[0].Valid)target=0;
    else if(!slots[1].Valid)target=1;
    else target=slots[0].Generation<=slots[1].Generation?0u:1u;
    generation=1;
    if(slots[0].Valid&&slots[0].Generation>=generation) {
        if(slots[0].Generation==~(u64)0)return -4;
        generation=slots[0].Generation+1u;
    }
    if(slots[1].Valid&&slots[1].Generation>=generation) {
        if(slots[1].Generation==~(u64)0)return -4;
        generation=slots[1].Generation+1u;
    }
    snapshot=(u8 *)bob64_heap_alloc(heap,snapshot_size);
    if(!snapshot)return -5;
    if(bob64_fs_snapshot_write(filesystem,snapshot,snapshot_size,&written)||
       written!=snapshot_size) {
        bob64_heap_free(heap,snapshot);return -6;
    }
    start=slot_start(partition,slot_blocks,target);
    if(write_payload(device,start+1u,snapshot,snapshot_size)||
       bob64_block_flush(device)) {
        bob64_heap_free(heap,snapshot);return -7;
    }
    u32 checksum=crc32_step(0xffffffffu,snapshot,snapshot_size)^0xffffffffu;
    bob64_heap_free(heap,snapshot);
    for(u32 i=0;i<device->BlockSize;i++)disk_store_workspace[i]=0;
    disk_store_workspace[0]='B';disk_store_workspace[1]='6';
    disk_store_workspace[2]='4';disk_store_workspace[3]='D';
    write32(disk_store_workspace+4,DISK_STORE_VERSION);
    write32(disk_store_workspace+8,DISK_STORE_HEADER_SIZE);
    write64(disk_store_workspace+16,generation);
    write64(disk_store_workspace+24,snapshot_size);
    write32(disk_store_workspace+32,checksum);
    write32(disk_store_workspace+DISK_STORE_HEADER_CRC_OFFSET,
            header_crc(disk_store_workspace));
    if(bob64_block_write(device,start,1,disk_store_workspace)||
       bob64_block_flush(device))return -8;
    return 0;
}

int bob64_disk_store_restore(BOB64_BLOCK_DEVICE *device,
        const BOB64_PARTITION *partition,BOB64_FILESYSTEM *filesystem,
        BOB64_HEAP *heap) {
    DISK_STORE_SLOT slots[2]={{0}};
    u64 slot_blocks;
    int order[2],valid_count=0;
    if(!filesystem||!heap||valid_partition(device,partition,&slot_blocks))
        return -1;
    if(inspect_slots(device,partition,slot_blocks,slots))return -2;
    for(int i=0;i<2;i++)if(slots[i].Valid)order[valid_count++]=i;
    if(!valid_count)return BOB64_DISK_STORE_NOT_FOUND;
    if(valid_count==2&&slots[order[1]].Generation>slots[order[0]].Generation) {
        int swap=order[0];order[0]=order[1];order[1]=swap;
    }
    for(int attempt=0;attempt<valid_count;attempt++) {
        int index=order[attempt];
        if(slots[index].PayloadSize>~(usize)0)continue;
        usize size=(usize)slots[index].PayloadSize;
        u8 *snapshot=(u8 *)bob64_heap_alloc(heap,size);
        if(!snapshot)return -3;
        int loaded=read_payload(device,slot_start(partition,slot_blocks,
            (u32)index)+1u,snapshot,size);
        int restored=loaded?loaded:bob64_fs_snapshot_restore(filesystem,
            snapshot,size);
        bob64_heap_free(heap,snapshot);
        if(!restored)return 0;
    }
    return -4;
}
