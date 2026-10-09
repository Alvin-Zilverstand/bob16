#include "block_device.h"

int bob64_block_device_init(BOB64_BLOCK_DEVICE *device,void *context,
        u64 block_count,u32 block_size,int read_only,BOB64_BLOCK_READ read,
        BOB64_BLOCK_WRITE write,BOB64_BLOCK_FLUSH flush) {
    u32 maximum;
    if(!device||!block_count||!block_size||
       block_size>BOB64_BLOCK_MAX_TRANSFER_BYTES||!read||
       (read_only!=0&&read_only!=1)||(!read_only&&!write))return -1;
    maximum=BOB64_BLOCK_MAX_TRANSFER_BYTES/block_size;
    if(!maximum)return -1;
    device->Context=context;device->Read=read;device->Write=write;
    device->Flush=flush;device->BlockCount=block_count;
    device->BlockSize=block_size;device->MaxTransferBlocks=maximum;
    device->ReadOnly=(u8)read_only;
    return 0;
}

static int valid_request(const BOB64_BLOCK_DEVICE *device,u64 lba,
                         u32 block_count,const void *buffer) {
    if(!device||!buffer||!block_count||!device->BlockCount||
       !device->BlockSize||!device->MaxTransferBlocks||
       block_count>device->MaxTransferBlocks||lba>=device->BlockCount||
       (u64)block_count>device->BlockCount-lba)return 0;
    return 1;
}

int bob64_block_read(BOB64_BLOCK_DEVICE *device,u64 lba,u32 block_count,
                     void *buffer) {
    if(!valid_request(device,lba,block_count,buffer)||!device->Read)return -1;
    return device->Read(device->Context,lba,block_count,buffer);
}

int bob64_block_write(BOB64_BLOCK_DEVICE *device,u64 lba,u32 block_count,
                      const void *buffer) {
    if(!valid_request(device,lba,block_count,buffer))return -1;
    if(device->ReadOnly||!device->Write)return -2;
    return device->Write(device->Context,lba,block_count,buffer);
}

int bob64_block_flush(BOB64_BLOCK_DEVICE *device) {
    if(!device)return -1;
    if(device->ReadOnly)return -2;
    return device->Flush?device->Flush(device->Context):-3;
}
