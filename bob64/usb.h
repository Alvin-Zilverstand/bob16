#ifndef BOB64_USB_H
#define BOB64_USB_H

#include "types.h"

typedef struct {
    u8 ConfigurationValue;
    u8 InterfaceNumber,AlternateSetting;
    u8 InterfaceProtocol;
    u8 EndpointAddress,EndpointAttributes,EndpointInterval;
    u16 EndpointMaxPacketSize;
} BOB64_USB_HID_BOOT_DEVICE;

typedef BOB64_USB_HID_BOOT_DEVICE BOB64_USB_HID_BOOT_KEYBOARD;

/* USB Mass Storage, SCSI transparent command set, Bulk-Only Transport. */
typedef struct {
    u8 ConfigurationValue;
    u8 InterfaceNumber,AlternateSetting;
    u8 BulkInEndpoint,BulkOutEndpoint;
    u16 BulkInMaxPacketSize,BulkOutMaxPacketSize;
} BOB64_USB_MASS_STORAGE;

#define BOB64_USB_HUB_MAX_PORTS 127u
#define BOB64_USB_HUB_BITMAP_BYTES 16u
#define BOB64_USB_MSC_CBW_SIZE 31u
#define BOB64_USB_MSC_CSW_SIZE 13u

/* Parsed USB 2 hub descriptor, including port zero in the bitmap. */
typedef struct {
    u8 PortCount;
    u16 Characteristics;
    u8 PowerOnToGood;
    u8 ControllerCurrent;
    u8 DeviceRemovable[BOB64_USB_HUB_BITMAP_BYTES];
    u8 PortPowerControlMask[BOB64_USB_HUB_BITMAP_BYTES];
} BOB64_USB_HUB_DESCRIPTOR;

typedef struct {
    u8 ConfigurationValue;
    u8 InterfaceNumber,AlternateSetting;
    u8 EndpointAddress,EndpointAttributes,EndpointInterval;
    u16 EndpointMaxPacketSize;
} BOB64_USB_HUB_INTERFACE;

/* Packs the eight-byte USB setup packet as a little-endian u64 TRB value. */
u64 bob64_usb_control_setup(u8 request_type,u8 request,u16 value,u16 index,
        u16 length);

/* Finds the first HID boot-keyboard interface and interrupt-IN endpoint. */
int bob64_usb_find_hid_boot_keyboard(const void *configuration,usize length,
        BOB64_USB_HID_BOOT_KEYBOARD *keyboard);
/* Finds a boot-protocol HID mouse interface with an interrupt-IN endpoint. */
int bob64_usb_find_hid_boot_mouse(const void *configuration,usize length,
        BOB64_USB_HID_BOOT_DEVICE *mouse);
/* Finds a SCSI-transparent Bulk-Only Transport interface with both bulk endpoints. */
int bob64_usb_find_mass_storage(const void *configuration,usize length,
        BOB64_USB_MASS_STORAGE *storage);
/* Build/validate the fixed-width BOT command and status wrappers. */
int bob64_usb_msc_build_cbw(void *buffer,usize capacity,u32 tag,
        u32 transfer_length,int device_to_host,u8 lun,const void *cdb,
        u8 cdb_length);
int bob64_usb_msc_parse_csw(const void *buffer,usize length,u32 expected_tag,
        u32 expected_transfer_length,u32 *residue,u8 *status);
/* Build SCSI disk commands, using 10-byte forms where the LBA fits. */
int bob64_scsi_build_read_write_cdb(u64 lba,u32 blocks,int write,
        u8 cdb[16],u8 *cdb_length);
int bob64_scsi_build_read_capacity16_cdb(u8 cdb[16]);
int bob64_scsi_parse_read_capacity10(const void *response,usize length,
        u64 *block_count,u32 *block_size,int *needs_capacity16);
int bob64_scsi_parse_read_capacity16(const void *response,usize length,
        u64 *block_count,u32 *block_size);
/* Parses a USB 2 hub descriptor (type 0x29) and validates both port bitmaps. */
int bob64_usb_parse_hub_descriptor(const void *descriptor,usize length,
        BOB64_USB_HUB_DESCRIPTOR *hub);
/* Finds a USB hub-class interface and its interrupt-IN status endpoint. */
int bob64_usb_find_hub_interface(const void *configuration,usize length,
        BOB64_USB_HUB_INTERFACE *hub);
/* Appends a hub-port nibble to the five-level xHCI route string. */
int bob64_usb_route_string_append(u32 route,u8 depth,u8 port,u32 *result);

#endif
