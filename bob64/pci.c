#include "pci.h"

#define PCI_CONFIG_ADDRESS 0x0cf8u
#define PCI_CONFIG_DATA    0x0cfcu
#define PCI_DEVICE_LIMIT   32u
#define PCI_FUNCTION_LIMIT 8u

static void pci_out32(u16 port,u32 value) {
    __asm__ volatile("outl %0,%1"::"a"(value),"Nd"(port));
}

static u32 pci_in32(u16 port) {
    u32 value;
    __asm__ volatile("inl %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static void pci_out16(u16 port,u16 value) {
    __asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port));
}

int bob64_pci_read_config32(u8 bus,u8 device,u8 function,u8 offset,
                            u32 *value) {
    u32 address;
    if(!value||device>=PCI_DEVICE_LIMIT||function>=PCI_FUNCTION_LIMIT||
       (offset&3u))return -1;
    address=0x80000000u|((u32)bus<<16)|((u32)device<<11)|
            ((u32)function<<8)|offset;
    pci_out32(PCI_CONFIG_ADDRESS,address);
    *value=pci_in32(PCI_CONFIG_DATA);
    return 0;
}

static int pci_visit_function(BOB64_PCI_READ_CONFIG read_config,
        void *read_context,u8 bus,u8 device,u8 function,
        BOB64_PCI_DEVICE_VISITOR visit,void *visit_context,
        BOB64_PCI_DEVICE *result) {
    u32 id,class_revision,header,bars[6];
    if(read_config(read_context,bus,device,function,0,&id))return -2;
    if((u16)id==0xffffu)return -1;
    if(read_config(read_context,bus,device,function,8,&class_revision)||
       read_config(read_context,bus,device,function,0x0c,&header))return -2;
    result->Bus=bus;result->Device=device;result->Function=function;
    result->VendorId=(u16)id;result->DeviceId=(u16)(id>>16);
    result->Revision=(u8)class_revision;
    result->ProgrammingInterface=(u8)(class_revision>>8);
    result->Subclass=(u8)(class_revision>>16);
    result->ClassCode=(u8)(class_revision>>24);
    result->HeaderType=(u8)(header>>16)&0x7fu;
    for(u32 i=0;i<6;i++) {
        result->Bar[i]=0;result->BarValid[i]=0;result->BarIsIo[i]=0;
        bars[i]=0;
    }
    if(result->HeaderType==0) {
        for(u32 i=0;i<6;i++)
            if(read_config(read_context,bus,device,function,
                           (u8)(0x10+i*4),&bars[i]))return -2;
        for(u32 i=0;i<6;i++) {
            u32 raw=bars[i];
            if(raw==0||raw==0xffffffffu)continue;
            result->BarValid[i]=1;
            if(raw&1u) {
                result->BarIsIo[i]=1;
                result->Bar[i]=(u64)(raw&~3u);
            } else if(((raw>>1)&3u)==2u) {
                u32 upper;
                if(i==5||read_config(read_context,bus,device,function,
                                     (u8)(0x10+(i+1)*4),&upper))return -2;
                result->Bar[i]=((u64)upper<<32)|(u64)(raw&~15u);
                result->BarValid[i+1]=0;
                i++;
            } else result->Bar[i]=(u64)(raw&~15u);
        }
    }
    if(visit)visit(visit_context,result);
    return 0;
}

int bob64_pci_enumerate(BOB64_PCI_READ_CONFIG read_config,void *read_context,
                        BOB64_PCI_DEVICE_VISITOR visit,void *visit_context,
                        u32 *device_count) {
    u8 buses[256]={0};
    u8 bus_order[256];
    u32 bus_count=1,processed=0,count=0;
    if(!read_config||!device_count)return -1;
    bus_order[0]=0;buses[0]=1;
    while(processed<bus_count) {
        u8 bus=bus_order[processed++];
        for(u32 dev=0;dev<PCI_DEVICE_LIMIT;dev++) {
            u32 id,header;
            if(read_config(read_context,bus,(u8)dev,0,0,&id))return -2;
            if((u16)id==0xffffu)continue;
            if(read_config(read_context,bus,(u8)dev,0,0x0c,&header))return -2;
            u32 functions=((header>>16)&0x80u)?PCI_FUNCTION_LIMIT:1u;
            for(u32 fn=0;fn<functions;fn++) {
                BOB64_PCI_DEVICE found;
                int status=pci_visit_function(read_config,read_context,bus,
                    (u8)dev,(u8)fn,visit,visit_context,&found);
                if(status==-1)continue;
                if(status)return -2;
                if(count==0xffffffffu)return -3;
                count++;
                if(found.ClassCode==0x06&&found.Subclass==0x04) {
                    u32 buses_value;
                    if(read_config(read_context,bus,(u8)dev,(u8)fn,
                                   0x18,&buses_value))return -2;
                    u8 secondary=(u8)(buses_value>>8);
                    if(secondary&&!buses[secondary]) {
                        buses[secondary]=1;bus_order[bus_count++]=secondary;
                    }
                }
            }
        }
    }
    *device_count=count;
    return 0;
}

int bob64_pci_is_usb_controller(const BOB64_PCI_DEVICE *device) {
    return device&&device->ClassCode==0x0c&&device->Subclass==0x03;
}

int bob64_pci_is_xhci_controller(const BOB64_PCI_DEVICE *device) {
    return bob64_pci_is_usb_controller(device)&&
           device->ProgrammingInterface==0x30;
}

int bob64_pci_enable_memory(const BOB64_PCI_DEVICE *device) {
    u32 address,command_status;
    u16 command;
    if(!device||device->Device>=PCI_DEVICE_LIMIT||
       device->Function>=PCI_FUNCTION_LIMIT)return -1;
    if(bob64_pci_read_config32(device->Bus,device->Device,device->Function,
                               4,&command_status))return -1;
    command=(u16)command_status;
    if(command&2u)return 0;
    address=0x80000000u|((u32)device->Bus<<16)|
            ((u32)device->Device<<11)|((u32)device->Function<<8)|4u;
    pci_out32(PCI_CONFIG_ADDRESS,address);
    /* A 16-bit write avoids clearing write-one-to-clear status bits. */
    pci_out16(PCI_CONFIG_DATA,command|2u);
    return 0;
}

static u32 xhci_read32(const volatile u8 *bytes,usize offset) {
    const volatile u32 *registers=(const volatile u32 *)(const volatile void *)bytes;
    return registers[offset/sizeof(u32)];
}

int bob64_xhci_parse_capabilities(const volatile void *registers,
        usize mapped_size,BOB64_XHCI_CAPABILITIES *capabilities) {
    const volatile u8 *bytes=(const volatile u8 *)registers;
    u32 structural,doorbell,runtime,hcs_parameters2;
    if(!bytes||!capabilities||mapped_size<0x20||
       ((uintptr_t)registers&(sizeof(u32)-1)))return -1;
    u32 capability_header=xhci_read32(bytes,0);
    capabilities->CapabilityLength=(u8)capability_header;
    capabilities->Version=(u16)(capability_header>>16);
    structural=xhci_read32(bytes,4);
    capabilities->MaxSlots=(u8)structural;
    capabilities->MaxInterrupters=(u16)((structural>>8)&0x7ffu);
    capabilities->MaxPorts=(u8)(structural>>24);
    hcs_parameters2=xhci_read32(bytes,0x08);
    capabilities->HcsParameters2=hcs_parameters2;
    capabilities->MaxScratchpadBuffers=(u16)(
        (((hcs_parameters2>>27)&0x1fu)<<5)|((hcs_parameters2>>21)&0x1fu));
    capabilities->HccParameters1=xhci_read32(bytes,0x10);
    doorbell=xhci_read32(bytes,0x14);
    runtime=xhci_read32(bytes,0x18);
    capabilities->DoorbellOffset=doorbell&~3u;
    capabilities->RuntimeOffset=runtime&~31u;
    if(capabilities->CapabilityLength<0x20||
       capabilities->CapabilityLength>mapped_size||
       capabilities->Version<0x0100||!capabilities->MaxSlots||
       !capabilities->MaxInterrupters||!capabilities->MaxPorts||
       capabilities->DoorbellOffset<capabilities->CapabilityLength||
       capabilities->RuntimeOffset<capabilities->CapabilityLength)
        return -1;
    return 0;
}
