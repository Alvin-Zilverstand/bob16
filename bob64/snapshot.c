#include "snapshot.h"

#define BOB64_SNAPSHOT_RECORD_HEADER_SIZE 12u
#define BOB64_SNAPSHOT_CHECKSUM_OFFSET 20u

static u16 read16(const u8 *bytes) {
    return (u16)((u16)bytes[0]|((u16)bytes[1]<<8));
}

static u32 read32(const u8 *bytes) {
    return (u32)bytes[0]|((u32)bytes[1]<<8)|((u32)bytes[2]<<16)|((u32)bytes[3]<<24);
}

static u64 read64(const u8 *bytes) {
    return (u64)read32(bytes)|((u64)read32(bytes+4)<<32);
}

static void write16(u8 *bytes,u16 value) {
    bytes[0]=(u8)value;bytes[1]=(u8)(value>>8);
}

static void write32(u8 *bytes,u32 value) {
    for(u32 i=0;i<4;i++)bytes[i]=(u8)(value>>(i*8));
}

static void write64(u8 *bytes,u64 value) {
    write32(bytes,(u32)value);write32(bytes+4,(u32)(value>>32));
}

static usize name_length(const char *name) {
    usize length=0;
    while(name[length])length++;
    return length;
}

static u32 snapshot_checksum(const u8 *bytes,usize length) {
    u32 value=0xffffffffu;
    for(usize i=0;i<length;i++) {
        u8 byte=(i>=BOB64_SNAPSHOT_CHECKSUM_OFFSET&&
                 i<BOB64_SNAPSHOT_CHECKSUM_OFFSET+4)?0:bytes[i];
        value^=byte;
        for(u32 bit=0;bit<8;bit++)value=(value>>1)^(0xedb88320u&-(value&1u));
    }
    return ~value;
}

int bob64_fs_snapshot_size(const BOB64_FILESYSTEM *filesystem,usize *size) {
    usize total=BOB64_SNAPSHOT_HEADER_SIZE,count=0;
    if(!filesystem||!size)return -1;
    for(BOB64_FS_FILE *file=filesystem->First;file;file=file->Next) {
        usize name_size=name_length(file->Name);
        if(!name_size||name_size>=BOB64_FS_NAME_CAPACITY||
           total>~(usize)0-BOB64_SNAPSHOT_RECORD_HEADER_SIZE-name_size||
           file->Length>~(usize)0-total-BOB64_SNAPSHOT_RECORD_HEADER_SIZE-name_size)
            return -1;
        total+=BOB64_SNAPSHOT_RECORD_HEADER_SIZE+name_size+file->Length;
        if(count==0xffffffffu)return -1;
        count++;
    }
    if(count!=filesystem->FileCount)return -1;
    *size=total;
    return 0;
}

int bob64_fs_snapshot_write(const BOB64_FILESYSTEM *filesystem,
                            void *output,usize capacity,usize *written) {
    usize required,offset=BOB64_SNAPSHOT_HEADER_SIZE;
    u8 *bytes=(u8 *)output;
    if(!output||!written||bob64_fs_snapshot_size(filesystem,&required)||capacity<required)
        return -1;
    bytes[0]='B';bytes[1]='6';bytes[2]='4';bytes[3]='S';
    write16(bytes+4,BOB64_SNAPSHOT_VERSION);write16(bytes+6,BOB64_SNAPSHOT_HEADER_SIZE);
    write64(bytes+8,(u64)required);write32(bytes+16,(u32)filesystem->FileCount);
    write32(bytes+BOB64_SNAPSHOT_CHECKSUM_OFFSET,0);
    for(BOB64_FS_FILE *file=filesystem->First;file;file=file->Next) {
        usize name_size=name_length(file->Name);
        write16(bytes+offset,(u16)name_size);write16(bytes+offset+2,0);
        write64(bytes+offset+4,(u64)file->Length);offset+=BOB64_SNAPSHOT_RECORD_HEADER_SIZE;
        for(usize i=0;i<name_size;i++)bytes[offset+i]=(u8)file->Name[i];
        offset+=name_size;
        for(usize i=0;i<file->Length;i++)bytes[offset+i]=(u8)file->Data[i];
        offset+=file->Length;
    }
    if(offset!=required)return -1;
    write32(bytes+BOB64_SNAPSHOT_CHECKSUM_OFFSET,snapshot_checksum(bytes,required));
    *written=required;
    return 0;
}

static int clear_filesystem(BOB64_FILESYSTEM *filesystem) {
    while(filesystem->First) {
        char name[BOB64_FS_NAME_CAPACITY];
        usize length=name_length(filesystem->First->Name);
        if(!length||length>=sizeof(name))return -1;
        for(usize i=0;i<length;i++)name[i]=filesystem->First->Name[i];
        name[length]=0;
        if(bob64_fs_delete(filesystem,name))return -1;
    }
    return 0;
}

int bob64_fs_snapshot_restore(BOB64_FILESYSTEM *filesystem,
                              const void *snapshot,usize snapshot_size) {
    const u8 *bytes=(const u8 *)snapshot;
    BOB64_FILESYSTEM replacement,old;
    usize offset=BOB64_SNAPSHOT_HEADER_SIZE;
    u32 file_count;
    if(!filesystem||!filesystem->Heap||!bytes||snapshot_size<BOB64_SNAPSHOT_HEADER_SIZE||
       bytes[0]!='B'||bytes[1]!='6'||bytes[2]!='4'||bytes[3]!='S'||
       read16(bytes+4)!=BOB64_SNAPSHOT_VERSION||
       read16(bytes+6)!=BOB64_SNAPSHOT_HEADER_SIZE||
       read64(bytes+8)!=(u64)snapshot_size||
       read32(bytes+BOB64_SNAPSHOT_CHECKSUM_OFFSET)!=snapshot_checksum(bytes,snapshot_size))
        return -1;
    file_count=read32(bytes+16);
    if((u64)file_count>((u64)snapshot_size-BOB64_SNAPSHOT_HEADER_SIZE)/
       BOB64_SNAPSHOT_RECORD_HEADER_SIZE||bob64_fs_init(&replacement,filesystem->Heap))
        return -1;
    for(u32 i=0;i<file_count;i++) {
        u16 name_size;
        u64 data_size;
        char name[BOB64_FS_NAME_CAPACITY];
        if(offset>snapshot_size||snapshot_size-offset<BOB64_SNAPSHOT_RECORD_HEADER_SIZE)
            goto invalid;
        name_size=read16(bytes+offset);
        if(read16(bytes+offset+2)!=0||!name_size||name_size>=sizeof(name))goto invalid;
        data_size=read64(bytes+offset+4);offset+=BOB64_SNAPSHOT_RECORD_HEADER_SIZE;
        if((u64)name_size>(u64)snapshot_size-offset)goto invalid;
        for(usize n=0;n<name_size;n++) {
            if(!bytes[offset+n])goto invalid;
            name[n]=(char)bytes[offset+n];
        }
        name[name_size]=0;offset+=name_size;
        if(data_size>(u64)snapshot_size-offset)goto invalid;
        usize previous_count=replacement.FileCount;
        if(bob64_fs_write(&replacement,name,bytes+offset,(usize)data_size)||
           replacement.FileCount!=previous_count+1)goto invalid;
        offset+=(usize)data_size;
    }
    if(offset!=snapshot_size)goto invalid;
    old=*filesystem;*filesystem=replacement;
    if(clear_filesystem(&old))return -1;
    return 0;
invalid:
    clear_filesystem(&replacement);
    return -1;
}
