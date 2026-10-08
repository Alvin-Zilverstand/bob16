#include "exec.h"
#include "paging.h"

static u16 read16(const u8 *bytes) {
    return (u16)((u16)bytes[0]|((u16)bytes[1]<<8));
}

static u32 read32(const u8 *bytes) {
    return (u32)bytes[0]|((u32)bytes[1]<<8)|((u32)bytes[2]<<16)|((u32)bytes[3]<<24);
}

static u64 read64(const u8 *bytes) {
    return (u64)read32(bytes)|((u64)read32(bytes+4)<<32);
}

static u32 payload_checksum(const u8 *bytes,usize length) {
    u32 value=0xffffffffu;
    for(usize i=0;i<length;i++) {
        value^=bytes[i];
        for(u32 bit=0;bit<8;bit++)value=(value>>1)^(0xedb88320u&-(value&1u));
    }
    return ~value;
}

int bob64_exec_parse(const void *file,usize file_size,BOB64_EXEC_IMAGE *image) {
    const u8 *bytes=(const u8 *)file;
    u64 payload_size,memory_size,entry_offset,code_size;
    if(!bytes||!image||file_size<BOB64_EXEC_HEADER_SIZE||
       bytes[0]!='B'||bytes[1]!='6'||bytes[2]!='4'||bytes[3]!='E'||
       read16(bytes+4)!=BOB64_EXEC_VERSION||
       read16(bytes+6)!=BOB64_EXEC_HEADER_SIZE||
       read32(bytes+8)!=BOB64_EXEC_ABI_VERSION||read32(bytes+12)!=0)
        return -1;
    payload_size=read64(bytes+16);memory_size=read64(bytes+24);
    entry_offset=read64(bytes+32);code_size=read64(bytes+40);
    if(payload_size!=(u64)(file_size-BOB64_EXEC_HEADER_SIZE)||!code_size||
       (code_size&(BOB64_PAGE_SIZE-1))||
       code_size>payload_size||memory_size<payload_size||entry_offset>=code_size||
       read32(bytes+48)!=payload_checksum(bytes+BOB64_EXEC_HEADER_SIZE,
                                          file_size-BOB64_EXEC_HEADER_SIZE))
        return -1;
    for(usize i=52;i<BOB64_EXEC_HEADER_SIZE;i++)if(bytes[i])return -1;
    image->Image=bytes+BOB64_EXEC_HEADER_SIZE;
    image->FileSize=payload_size;image->MemorySize=memory_size;
    image->EntryOffset=entry_offset;image->CodeSize=code_size;
    image->AbiVersion=BOB64_EXEC_ABI_VERSION;image->Flags=0;
    return 0;
}

int bob64_exec_load(const BOB64_EXEC_IMAGE *image,u64 load_address,
                    void *destination,usize destination_capacity,
                    u64 *entry_address) {
    u8 *output=(u8 *)destination;
    if(!image||!image->Image||!output||!entry_address||
       image->AbiVersion!=BOB64_EXEC_ABI_VERSION||image->Flags||
       !image->CodeSize||image->CodeSize>image->FileSize||
       image->FileSize>image->MemorySize||image->EntryOffset>=image->CodeSize||
       image->MemorySize>(u64)destination_capacity||
       image->MemorySize>~(u64)0-load_address||
       image->EntryOffset>~(u64)0-load_address)
        return -1;
    for(u64 i=0;i<image->FileSize;i++)output[(usize)i]=image->Image[(usize)i];
    for(u64 i=image->FileSize;i<image->MemorySize;i++)output[(usize)i]=0;
    *entry_address=load_address+image->EntryOffset;
    return 0;
}
