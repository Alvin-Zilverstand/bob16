#include "partition.h"

#define GPT_HEADER_MIN_SIZE 92u
#define GPT_ENTRY_MIN_SIZE 128u
#define GPT_ENTRY_MAX_COUNT 128u
#define GPT_ENTRY_MAX_SIZE 512u

const u8 bob64_gpt_bob_data_type_guid[BOB64_GPT_GUID_SIZE]=
    BOB64_GPT_BOB_DATA_TYPE_GUID_INITIALIZER;
/* The current kernel enumerates storage synchronously on one CPU. */
static u8 gpt_sector_workspace[BOB64_BLOCK_MAX_TRANSFER_BYTES];

static u32 read32(const u8 *bytes) {
    return (u32)bytes[0]|((u32)bytes[1]<<8)|
           ((u32)bytes[2]<<16)|((u32)bytes[3]<<24);
}

static u64 read64(const u8 *bytes) {
    return (u64)read32(bytes)|((u64)read32(bytes+4)<<32);
}

static u32 crc32_step(u32 crc,const u8 *bytes,usize length) {
    for(usize i=0;i<length;i++) {
        crc^=bytes[i];
        for(u32 bit=0;bit<8;bit++)
            crc=(crc>>1)^(0xedb88320u&-(crc&1u));
    }
    return crc;
}

static int guid_is_zero(const u8 *guid) {
    for(u32 i=0;i<BOB64_GPT_GUID_SIZE;i++)if(guid[i])return 0;
    return 1;
}

static int guid_equal(const u8 *left,const u8 *right) {
    for(u32 i=0;i<BOB64_GPT_GUID_SIZE;i++)if(left[i]!=right[i])return 0;
    return 1;
}

int bob64_gpt_find_partition(BOB64_BLOCK_DEVICE *device,
        const u8 type_guid[BOB64_GPT_GUID_SIZE],BOB64_PARTITION *partition) {
    u8 *sector=gpt_sector_workspace;
    u64 current_lba,backup_lba,first_usable,last_usable,entries_lba;
    u64 table_bytes,table_blocks;
    u64 range_first[GPT_ENTRY_MAX_COUNT],range_last[GPT_ENTRY_MAX_COUNT];
    u32 header_size,stored_header_crc,entry_count,entry_size,stored_entries_crc;
    u32 crc=0xffffffffu,matches=0,range_count=0;
    BOB64_PARTITION candidate={0};
    if(!device||!type_guid||!partition||device->BlockSize<512u||
       device->BlockSize>BOB64_BLOCK_MAX_TRANSFER_BYTES)return -1;
    if(bob64_block_read(device,1,1,sector))return -2;
    if(sector[0]!='E'||sector[1]!='F'||sector[2]!='I'||sector[3]!=' '||
       sector[4]!='P'||sector[5]!='A'||sector[6]!='R'||sector[7]!='T')return 1;
    if(read32(sector+8)<0x00010000u)return -3;
    header_size=read32(sector+12);
    if(header_size<GPT_HEADER_MIN_SIZE||header_size>device->BlockSize||
       read32(sector+20)!=0)return -4;
    stored_header_crc=read32(sector+16);
    sector[16]=sector[17]=sector[18]=sector[19]=0;
    crc=crc32_step(0xffffffffu,sector,header_size)^0xffffffffu;
    sector[16]=(u8)stored_header_crc;sector[17]=(u8)(stored_header_crc>>8);
    sector[18]=(u8)(stored_header_crc>>16);sector[19]=(u8)(stored_header_crc>>24);
    if(crc!=stored_header_crc)return -5;
    current_lba=read64(sector+24);backup_lba=read64(sector+32);
    first_usable=read64(sector+40);last_usable=read64(sector+48);
    entries_lba=read64(sector+72);entry_count=read32(sector+80);
    entry_size=read32(sector+84);stored_entries_crc=read32(sector+88);
    if(current_lba!=1||backup_lba>=device->BlockCount||backup_lba<=last_usable||
       first_usable<2||first_usable>last_usable||last_usable>=device->BlockCount||
       entries_lba<2||!entry_count||entry_count>GPT_ENTRY_MAX_COUNT||
       entry_size<GPT_ENTRY_MIN_SIZE||entry_size>GPT_ENTRY_MAX_SIZE||
       entry_size>device->BlockSize||device->BlockSize%entry_size)return -6;
    if((u64)entry_count>(~(u64)0)/entry_size)return -7;
    table_bytes=(u64)entry_count*entry_size;
    table_blocks=(table_bytes+device->BlockSize-1u)/device->BlockSize;
    if(entries_lba>=device->BlockCount||table_blocks>device->BlockCount-entries_lba||
       entries_lba+table_blocks>first_usable)return -8;
    crc=0xffffffffu;
    for(u64 block=0;block<table_blocks;block++) {
        u64 lba=entries_lba+block;
        usize valid=(usize)(table_bytes-block*(u64)device->BlockSize);
        if(valid>device->BlockSize)valid=device->BlockSize;
        if(bob64_block_read(device,lba,1,sector))return -9;
        crc=crc32_step(crc,sector,valid);
        for(usize offset=0;offset+entry_size<=valid;offset+=entry_size) {
            const u8 *entry=sector+offset;
            if(guid_is_zero(entry))continue;
            u64 first=read64(entry+32),last=read64(entry+40);
            if(first<first_usable||first>last||last>last_usable)return -10;
            if(guid_is_zero(entry+16))return -12;
            for(u32 previous=0;previous<range_count;previous++)
                if(first<=range_last[previous]&&last>=range_first[previous])
                    return -15;
            range_first[range_count]=first;range_last[range_count]=last;
            range_count++;
            if(!guid_equal(entry,type_guid))continue;
            if(matches++)return -11;
            candidate.FirstLba=first;candidate.BlockCount=last-first+1u;
            for(u32 i=0;i<BOB64_GPT_GUID_SIZE;i++) {
                candidate.TypeGuid[i]=entry[i];
                candidate.UniqueGuid[i]=entry[16u+i];
            }
        }
        if(valid%entry_size)return -13;
    }
    if((crc^0xffffffffu)!=stored_entries_crc)return -14;
    if(!matches)return 1;
    *partition=candidate;
    return 0;
}
