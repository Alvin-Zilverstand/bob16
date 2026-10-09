#include "usb.h"

static u16 usb_read_le16(const u8 *bytes) {
    return (u16)(bytes[0]|((u16)bytes[1]<<8));
}

static u32 usb_read_le32(const u8 *bytes) {
    return (u32)bytes[0]|((u32)bytes[1]<<8)|((u32)bytes[2]<<16)|
           ((u32)bytes[3]<<24);
}

static void usb_write_le32(u8 *bytes,u32 value) {
    bytes[0]=(u8)value;bytes[1]=(u8)(value>>8);
    bytes[2]=(u8)(value>>16);bytes[3]=(u8)(value>>24);
}

static u32 usb_read_be32(const u8 *bytes) {
    return ((u32)bytes[0]<<24)|((u32)bytes[1]<<16)|
           ((u32)bytes[2]<<8)|bytes[3];
}

static u64 usb_read_be64(const u8 *bytes) {
    return ((u64)usb_read_be32(bytes)<<32)|usb_read_be32(bytes+4);
}

static void usb_write_be32(u8 *bytes,u32 value) {
    bytes[0]=(u8)(value>>24);bytes[1]=(u8)(value>>16);
    bytes[2]=(u8)(value>>8);bytes[3]=(u8)value;
}

static void usb_write_be64(u8 *bytes,u64 value) {
    usb_write_be32(bytes,(u32)(value>>32));
    usb_write_be32(bytes+4,(u32)value);
}

int bob64_scsi_build_read_write_cdb(u64 lba,u32 blocks,int write,
        u8 cdb[16],u8 *cdb_length) {
    if(!cdb||!cdb_length||!blocks||(write!=0&&write!=1)||
       (u64)blocks-1u>0xffffffffffffffffULL-lba)return -1;
    for(u32 i=0;i<16;i++)cdb[i]=0;
    if(lba<=0xffffffffULL&&blocks<=0xffffu&&
       (u64)blocks<=0x100000000ULL-lba) {
        cdb[0]=write?0x2a:0x28;
        usb_write_be32(cdb+2,(u32)lba);
        cdb[7]=(u8)(blocks>>8);cdb[8]=(u8)blocks;
        *cdb_length=10;
    } else {
        cdb[0]=write?0x8a:0x88;
        usb_write_be64(cdb+2,lba);
        usb_write_be32(cdb+10,blocks);
        *cdb_length=16;
    }
    return 0;
}

int bob64_scsi_build_read_capacity16_cdb(u8 cdb[16]) {
    if(!cdb)return -1;
    for(u32 i=0;i<16;i++)cdb[i]=0;
    cdb[0]=0x9e;cdb[1]=0x10;
    usb_write_be32(cdb+10,32);
    return 0;
}

int bob64_scsi_parse_read_capacity10(const void *response,usize length,
        u64 *block_count,u32 *block_size,int *needs_capacity16) {
    const u8 *bytes=(const u8 *)response;
    if(!bytes||length<8||!block_count||!block_size||!needs_capacity16)
        return -1;
    u32 last=usb_read_be32(bytes),size=usb_read_be32(bytes+4);
    if(!size)return -1;
    *block_size=size;
    *needs_capacity16=last==0xffffffffu;
    *block_count=*needs_capacity16?0:(u64)last+1u;
    return 0;
}

int bob64_scsi_parse_read_capacity16(const void *response,usize length,
        u64 *block_count,u32 *block_size) {
    const u8 *bytes=(const u8 *)response;
    if(!bytes||length<32||!block_count||!block_size)return -1;
    u64 last=usb_read_be64(bytes);
    u32 size=usb_read_be32(bytes+8);
    if(last==0xffffffffffffffffULL||!size)return -1;
    *block_count=last+1u;*block_size=size;
    return 0;
}

u64 bob64_usb_control_setup(u8 request_type,u8 request,u16 value,u16 index,
        u16 length) {
    return (u64)request_type|((u64)request<<8)|((u64)value<<16)|
           ((u64)index<<32)|((u64)length<<48);
}

static int usb_find_hid_boot_device(const void *configuration,usize length,
        u8 protocol,u16 minimum_packet,BOB64_USB_HID_BOOT_DEVICE *device) {
    const u8 *bytes=(const u8 *)configuration;
    usize total,offset;
    int matching_interface=0;
    BOB64_USB_HID_BOOT_DEVICE candidate={0};
    if(!bytes||!device||length<9||bytes[0]<9||bytes[1]!=2)return -1;
    total=usb_read_le16(bytes+2);
    if(total<bytes[0]||total>length)return -1;
    candidate.ConfigurationValue=bytes[5];
    for(offset=bytes[0];offset<total;) {
        u8 descriptor_length,descriptor_type;
        if(total-offset<2)return -1;
        descriptor_length=bytes[offset];
        descriptor_type=bytes[offset+1];
        if(descriptor_length<2||descriptor_length>total-offset)return -1;
        if(descriptor_type==4) {
            matching_interface=descriptor_length>=9&&
                bytes[offset+3]==0&&bytes[offset+5]==3&&
                bytes[offset+6]==1&&bytes[offset+7]==protocol;
            if(matching_interface) {
                candidate.InterfaceNumber=bytes[offset+2];
                candidate.AlternateSetting=bytes[offset+3];
                candidate.InterfaceProtocol=bytes[offset+7];
            }
        } else if(descriptor_type==5&&matching_interface) {
            u8 address,attributes;
            u16 packet_size;
            if(descriptor_length<7)return -1;
            address=bytes[offset+2];
            attributes=bytes[offset+3];
            packet_size=(u16)(usb_read_le16(bytes+offset+4)&0x07ffu);
            if((address&0x80u)&&(address&0x0fu)&&
               (attributes&3u)==3u&&packet_size>=minimum_packet) {
                candidate.EndpointAddress=address;
                candidate.EndpointAttributes=attributes;
                candidate.EndpointMaxPacketSize=packet_size;
                candidate.EndpointInterval=bytes[offset+6];
                *device=candidate;
                return 0;
            }
        }
        offset+=descriptor_length;
    }
    return 1;
}

int bob64_usb_find_hid_boot_keyboard(const void *configuration,usize length,
        BOB64_USB_HID_BOOT_KEYBOARD *keyboard) {
    return usb_find_hid_boot_device(configuration,length,1,8,keyboard);
}

int bob64_usb_find_hid_boot_mouse(const void *configuration,usize length,
        BOB64_USB_HID_BOOT_DEVICE *mouse) {
    return usb_find_hid_boot_device(configuration,length,2,3,mouse);
}

int bob64_usb_find_mass_storage(const void *configuration,usize length,
        BOB64_USB_MASS_STORAGE *storage) {
    const u8 *bytes=(const u8 *)configuration;
    BOB64_USB_MASS_STORAGE candidate={0};
    usize total,offset;
    int matching_interface=0;
    if(!bytes||!storage||length<9||bytes[0]<9||bytes[1]!=2)return -1;
    total=usb_read_le16(bytes+2);
    if(total<bytes[0]||total>length)return -1;
    candidate.ConfigurationValue=bytes[5];
    for(offset=bytes[0];offset<total;) {
        u8 descriptor_length,descriptor_type;
        if(total-offset<2)return -1;
        descriptor_length=bytes[offset];descriptor_type=bytes[offset+1];
        if(descriptor_length<2||descriptor_length>total-offset)return -1;
        if(descriptor_type==4) {
            if(matching_interface&&candidate.BulkInEndpoint&&
               candidate.BulkOutEndpoint) {
                *storage=candidate;
                return 0;
            }
            matching_interface=descriptor_length>=9&&
                bytes[offset+5]==8&&bytes[offset+6]==6&&
                bytes[offset+7]==0x50;
            candidate.BulkInEndpoint=0;
            candidate.BulkOutEndpoint=0;
            candidate.BulkInMaxPacketSize=0;
            candidate.BulkOutMaxPacketSize=0;
            if(matching_interface) {
                candidate.InterfaceNumber=bytes[offset+2];
                candidate.AlternateSetting=bytes[offset+3];
            }
        } else if(descriptor_type==5&&matching_interface) {
            u8 address,attributes;
            u16 packet_size;
            if(descriptor_length<7)return -1;
            address=bytes[offset+2];attributes=bytes[offset+3];
            packet_size=(u16)(usb_read_le16(bytes+offset+4)&0x07ffu);
            if(!(address&0x70u)&&(address&0x0fu)!=0&&
               (attributes&3u)==2u&&packet_size) {
                if(address&0x80u) {
                    candidate.BulkInEndpoint=address;
                    candidate.BulkInMaxPacketSize=packet_size;
                } else {
                    candidate.BulkOutEndpoint=address;
                    candidate.BulkOutMaxPacketSize=packet_size;
                }
            }
        }
        offset+=descriptor_length;
    }
    if(matching_interface&&candidate.BulkInEndpoint&&candidate.BulkOutEndpoint) {
        *storage=candidate;
        return 0;
    }
    return 1;
}

int bob64_usb_msc_build_cbw(void *buffer,usize capacity,u32 tag,
        u32 transfer_length,int device_to_host,u8 lun,const void *cdb,
        u8 cdb_length) {
    u8 *bytes=(u8 *)buffer;
    const u8 *command=(const u8 *)cdb;
    if(!bytes||capacity<BOB64_USB_MSC_CBW_SIZE||!command||
       !cdb_length||cdb_length>16||lun>15||
       (device_to_host!=0&&device_to_host!=1))return -1;
    for(usize i=0;i<BOB64_USB_MSC_CBW_SIZE;i++)bytes[i]=0;
    usb_write_le32(bytes,0x43425355u);
    usb_write_le32(bytes+4,tag);
    usb_write_le32(bytes+8,transfer_length);
    bytes[12]=device_to_host?0x80u:0;
    bytes[13]=lun;
    bytes[14]=cdb_length;
    for(u8 i=0;i<cdb_length;i++)bytes[15u+i]=command[i];
    return 0;
}

int bob64_usb_msc_parse_csw(const void *buffer,usize length,u32 expected_tag,
        u32 expected_transfer_length,u32 *residue,u8 *status) {
    const u8 *bytes=(const u8 *)buffer;
    u32 parsed_tag,parsed_residue;
    u8 parsed_status;
    if(!bytes||length<BOB64_USB_MSC_CSW_SIZE||!residue||!status)return -1;
    if(usb_read_le32(bytes)!=0x53425355u)return -1;
    parsed_tag=usb_read_le32(bytes+4);
    parsed_residue=usb_read_le32(bytes+8);
    parsed_status=bytes[12];
    if(parsed_tag!=expected_tag||parsed_residue>expected_transfer_length||
       parsed_status>2u)return -1;
    *residue=parsed_residue;
    *status=parsed_status;
    return 0;
}

int bob64_usb_parse_hub_descriptor(const void *descriptor,usize length,
        BOB64_USB_HUB_DESCRIPTOR *hub) {
    const u8 *bytes=(const u8 *)descriptor;
    BOB64_USB_HUB_DESCRIPTOR candidate={0};
    usize removable_bytes,power_mask_bytes,required;
    if(!bytes||!hub||length<7||bytes[0]<7||bytes[1]!=0x29||
       bytes[0]>length)return -1;
    candidate.PortCount=bytes[2];
    if(!candidate.PortCount||candidate.PortCount>BOB64_USB_HUB_MAX_PORTS)
        return -1;
    removable_bytes=((usize)candidate.PortCount+1u+7u)/8u;
    power_mask_bytes=((usize)candidate.PortCount+7u)/8u;
    required=7u+removable_bytes+power_mask_bytes;
    if(bytes[0]<required)return -1;
    candidate.Characteristics=usb_read_le16(bytes+3);
    candidate.PowerOnToGood=bytes[5];
    candidate.ControllerCurrent=bytes[6];
    for(usize i=0;i<removable_bytes;i++)
        candidate.DeviceRemovable[i]=bytes[7u+i];
    for(usize i=0;i<power_mask_bytes;i++)
        candidate.PortPowerControlMask[i]=bytes[7u+removable_bytes+i];
    *hub=candidate;
    return 0;
}

int bob64_usb_find_hub_interface(const void *configuration,usize length,
        BOB64_USB_HUB_INTERFACE *hub) {
    const u8 *bytes=(const u8 *)configuration;
    BOB64_USB_HUB_INTERFACE candidate={0};
    usize total,offset;
    int matching_interface=0;
    if(!bytes||!hub||length<9||bytes[0]<9||bytes[1]!=2)return -1;
    total=usb_read_le16(bytes+2);
    if(total<bytes[0]||total>length)return -1;
    candidate.ConfigurationValue=bytes[5];
    for(offset=bytes[0];offset<total;) {
        u8 descriptor_length,descriptor_type;
        if(total-offset<2)return -1;
        descriptor_length=bytes[offset];descriptor_type=bytes[offset+1];
        if(descriptor_length<2||descriptor_length>total-offset)return -1;
        if(descriptor_type==4) {
            matching_interface=descriptor_length>=9&&
                bytes[offset+5]==9&&bytes[offset+6]==0;
            if(matching_interface) {
                candidate.InterfaceNumber=bytes[offset+2];
                candidate.AlternateSetting=bytes[offset+3];
            }
        } else if(descriptor_type==5&&matching_interface) {
            u8 address,attributes;
            u16 packet_size;
            if(descriptor_length<7)return -1;
            address=bytes[offset+2];attributes=bytes[offset+3];
            packet_size=(u16)(usb_read_le16(bytes+offset+4)&0x07ffu);
            if((address&0x80u)&&(address&0x0fu)&&
               (attributes&3u)==3u&&packet_size) {
                candidate.EndpointAddress=address;
                candidate.EndpointAttributes=attributes;
                candidate.EndpointMaxPacketSize=packet_size;
                candidate.EndpointInterval=bytes[offset+6];
                *hub=candidate;
                return 0;
            }
        }
        offset+=descriptor_length;
    }
    return 1;
}

int bob64_usb_route_string_append(u32 route,u8 depth,u8 port,u32 *result) {
    u32 preceding_mask,shift;
    u8 route_port;
    if(!result||!port||depth>=5)return -1;
    shift=(u32)depth*4u;
    preceding_mask=shift?(1u<<shift)-1u:0;
    if((route&~preceding_mask)||route>0x000fffffu)return -1;
    route_port=port>14u?15u:port;
    *result=route|((u32)route_port<<shift);
    return 0;
}
