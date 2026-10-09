#ifndef BOB64_PCI_H
#define BOB64_PCI_H

#include "types.h"

typedef struct {
    u8 Bus,Device,Function;
    u8 Revision,ProgrammingInterface,Subclass,ClassCode,HeaderType;
    u16 VendorId,DeviceId;
    u64 Bar[6];
    u8 BarValid[6],BarIsIo[6];
} BOB64_PCI_DEVICE;

typedef struct {
    u8 CapabilityLength,MaxSlots,MaxPorts;
    u16 Version,MaxInterrupters,MaxScratchpadBuffers;
    u32 DoorbellOffset,RuntimeOffset,HccParameters1;
    u32 HcsParameters2;
} BOB64_XHCI_CAPABILITIES;

typedef int (*BOB64_PCI_READ_CONFIG)(void *context,u8 bus,u8 device,
                                     u8 function,u8 offset,u32 *value);
typedef void (*BOB64_PCI_DEVICE_VISITOR)(void *context,
                                        const BOB64_PCI_DEVICE *device);

/* Enumerates bus 0 and the secondary buses of PCI-to-PCI bridges it finds. */
int bob64_pci_enumerate(BOB64_PCI_READ_CONFIG read_config,void *read_context,
                        BOB64_PCI_DEVICE_VISITOR visit,void *visit_context,
                        u32 *device_count);
int bob64_pci_is_usb_controller(const BOB64_PCI_DEVICE *device);
int bob64_pci_is_xhci_controller(const BOB64_PCI_DEVICE *device);
int bob64_pci_enable_memory(const BOB64_PCI_DEVICE *device);
int bob64_xhci_parse_capabilities(const volatile void *registers,
        usize mapped_size,BOB64_XHCI_CAPABILITIES *capabilities);
int bob64_pci_read_config32(u8 bus,u8 device,u8 function,u8 offset,
                            u32 *value);

#endif
