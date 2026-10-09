#include "kernel.h"
#include "interrupts.h"
#include "paging.h"
#include "console.h"
#include "heap.h"
#include "runtime.h"
#include "keyboard.h"
#include "mouse.h"
#include "shell.h"
#include "filesystem.h"
#include "exec.h"
#include "process.h"
#include "syscall.h"
#include "compiler.h"
#include "firmware_store.h"
#include "pci.h"
#include "xhci.h"
#include "usb.h"
#include "block_device.h"
#include "partition.h"
#include "disk_store.h"

#define BOB64_KERNEL_HEAP_BASE 0xffff900000000000ULL
#define BOB64_KERNEL_HEAP_LIMIT (64ULL*1024ULL*1024ULL)
#define BOB64_KERNEL_PRIVILEGE_STACK_SIZE (16ULL*1024ULL)
#define BOB64_XHCI_MMIO_MAP_SIZE (4ULL*BOB64_PAGE_SIZE)
#define BOB64_XHCI_RING_TRBS 256u
#define BOB64_XHCI_MAX_SCRATCHPADS 1023u
#define BOB64_XHCI_USBCMD_RUN 0x00000001u
#define BOB64_XHCI_USBSTS_HALTED 0x00000001u
#define BOB64_XHCI_USBSTS_NOT_READY 0x00000800u
#define BOB64_XHCI_TRB_TYPE_SHIFT 10u
#define BOB64_XHCI_TRB_TYPE_LINK 6u
#define BOB64_XHCI_TRB_TYPE_ENABLE_SLOT 9u
#define BOB64_XHCI_TRB_TYPE_DISABLE_SLOT 10u
#define BOB64_XHCI_TRB_TYPE_NOOP_COMMAND 23u
#define BOB64_XHCI_TRB_TYPE_COMMAND_COMPLETION 33u
#define BOB64_XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34u
#define BOB64_XHCI_TRB_TYPE_TRANSFER_EVENT 32u
#define BOB64_XHCI_TRB_COMPLETION_SUCCESS 1u
#define KERNEL_XHCI_STORAGE_DEVICE_LIMIT 8u

static BOB64_HEAP kernel_heap;
static BOB64_FILESYSTEM kernel_filesystem;
static u64 kernel_next_window_owner=1;
static BOB64_SHELL kernel_shell;
static BOB64_PCI_DEVICE kernel_xhci_device;
static int kernel_xhci_found;
#define KERNEL_XHCI_HID_DEVICE_LIMIT BOB64_USB_INPUT_DEVICE_LIMIT
static u8 kernel_xhci_connected_ports[KERNEL_XHCI_HID_DEVICE_LIMIT];
static u8 kernel_xhci_connected_speeds[KERNEL_XHCI_HID_DEVICE_LIMIT];
typedef struct {
    BOB64_FILESYSTEM *Filesystem;
    u8 NxSupported,PhysicalAddressBits;
} KERNEL_APP_CONTEXT;
static KERNEL_APP_CONTEXT kernel_app_context;
static usize kernel_heap_mapped;
static usize kernel_heap_high_water;
static void *kernel_privilege_stack;
static char kernel_user_output[1024];
static usize kernel_user_output_length;
static u8 kernel_user_output_truncated;
static u8 kernel_compiler_output[BOB64_COMPILER_IMAGE_LIMIT];
static void kernel_write(const char *text);
static void kernel_hex64(u64 value);

static int kernel_pci_read_config(void *context,u8 bus,u8 device,
        u8 function,u8 offset,u32 *value) {
    (void)context;
    return bob64_pci_read_config32(bus,device,function,offset,value);
}

typedef struct {
    volatile u8 *Base;
    usize Size;
    u8 CapabilityLength;
} KERNEL_XHCI_MMIO;

typedef struct {
    u64 Parameter;
    u32 Status;
    u32 Control;
} KERNEL_XHCI_TRB;

typedef struct {
    u64 RingBase;
    u32 RingSize;
    u32 Reserved;
} KERNEL_XHCI_ERST_ENTRY;

typedef struct {
    volatile KERNEL_XHCI_TRB *CommandRing;
    volatile KERNEL_XHCI_TRB *EventRing;
    volatile u64 *DeviceContextArray;
    u64 CommandPhysical,EventPhysical;
    u32 CommandIndex,EventIndex;
    u8 EventCycle;
    KERNEL_XHCI_TRB DeferredEvents[32];
    u8 DeferredHead,DeferredCount;
} KERNEL_XHCI_RINGS;

static KERNEL_XHCI_RINGS kernel_xhci_rings;
static KERNEL_XHCI_MMIO kernel_xhci_live_mmio;
static BOB64_XHCI_CAPABILITIES kernel_xhci_live_capabilities;
typedef struct {
    u32 RouteString;
    u8 RootPort,ParentSlot,ParentPort,Speed,Depth,TtHubSlot,TtPort;
} KERNEL_XHCI_TOPOLOGY;

static KERNEL_XHCI_TOPOLOGY kernel_xhci_hub_children[32];
static u8 kernel_xhci_hub_child_count;

typedef struct {
    volatile KERNEL_XHCI_TRB *Ring;
    volatile u8 *Report;
    u64 RingPhysical,ReportPhysical;
    u32 NextIndex,PendingIndex;
    u8 Slot,Dci,Kind,ReportLength,Cycle,Active,Pending,TransferErrors,RootPort;
    u8 LoggedReport,LoggedInput,LoggedWheel;
    BOB64_KEYBOARD_HID_STATE KeyboardState;
    KERNEL_XHCI_TOPOLOGY Topology;
} KERNEL_XHCI_HID_DEVICE;

typedef struct {
    volatile KERNEL_XHCI_TRB *Ring;
    volatile u8 *Report;
    volatile KERNEL_XHCI_TRB *ControlRing;
    volatile u8 *ControlBuffer;
    u64 RingPhysical,ReportPhysical;
    u64 ControlRingPhysical,ControlBufferPhysical;
    u32 NextIndex,PendingIndex,ControlNextIndex;
    u8 Slot,Dci,ReportLength,Cycle,Active,Pending,TransferErrors,RootPort;
    u8 LoggedChange,ControlCycle,PortCount;
    KERNEL_XHCI_TOPOLOGY Topology;
} KERNEL_XHCI_HUB_DEVICE;

typedef struct {
    volatile KERNEL_XHCI_TRB *BulkInRing,*BulkOutRing;
    volatile u8 *Scratch;
    u64 BulkInRingPhysical,BulkOutRingPhysical;
    u64 ScratchPhysical,BlockCount;
    u16 BulkInMaxPacketSize,BulkOutMaxPacketSize;
    u32 BulkInNext,BulkOutNext,NextTag,BlockSize;
    u8 Slot,BulkInDci,BulkOutDci,BulkInCycle,BulkOutCycle,Active;
    KERNEL_XHCI_TOPOLOGY Topology;
    BOB64_BLOCK_DEVICE BlockDevice;
    BOB64_PARTITION DataPartition;
    u8 HasDataPartition,DisconnectTestReadPending;
} KERNEL_XHCI_STORAGE_DEVICE;

static KERNEL_XHCI_HID_DEVICE kernel_xhci_hid_devices[KERNEL_XHCI_HID_DEVICE_LIMIT];
static u8 kernel_xhci_hid_device_count;
static KERNEL_XHCI_HUB_DEVICE kernel_xhci_hub_devices[KERNEL_XHCI_HID_DEVICE_LIMIT];
static u8 kernel_xhci_hub_device_count;
static KERNEL_XHCI_STORAGE_DEVICE
    kernel_xhci_storage_devices[KERNEL_XHCI_STORAGE_DEVICE_LIMIT];
static u8 kernel_xhci_storage_device_count;
static int kernel_xhci_storage_probe(KERNEL_XHCI_STORAGE_DEVICE *device);
static int kernel_xhci_hub_status_submit(KERNEL_XHCI_HUB_DEVICE *device);
static int kernel_xhci_delay_ticks(u64 ticks);
static void kernel_xhci_hub_process_changes(KERNEL_XHCI_HUB_DEVICE *hub);
static void kernel_xhci_address_hub_children(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u32 start_index);
static void kernel_xhci_hid_reconnect(u8 port);

_Static_assert(sizeof(KERNEL_XHCI_TRB)==16,"xHCI TRB is 16 bytes");
_Static_assert(sizeof(KERNEL_XHCI_ERST_ENTRY)==16,"xHCI ERST entry is 16 bytes");

static int kernel_xhci_read32(void *opaque,u32 offset,u32 *value) {
    KERNEL_XHCI_MMIO *mmio=(KERNEL_XHCI_MMIO *)opaque;
    if(!mmio||!value||(offset&3u)||offset>mmio->Size||
       mmio->Size-offset<sizeof(u32))return -1;
    *value=*(volatile u32 *)(volatile void *)(mmio->Base+offset);
    return 0;
}

static int kernel_xhci_write32(void *opaque,u32 offset,u32 value) {
    KERNEL_XHCI_MMIO *mmio=(KERNEL_XHCI_MMIO *)opaque;
    if(!mmio||(offset&3u)||offset>mmio->Size||
       mmio->Size-offset<sizeof(u32))return -1;
    *(volatile u32 *)(volatile void *)(mmio->Base+offset)=value;
    return 0;
}

static int kernel_xhci_dma_alloc(u64 pages,u64 *physical,volatile u8 **virtual) {
    u64 bytes;
    u64 base;
    u64 flags=BOB64_PAGE_WRITE;
    if(!pages||!physical||!virtual||pages>~(u64)0/BOB64_PAGE_SIZE)return -1;
    bytes=pages*BOB64_PAGE_SIZE;
    base=bob64_page_alloc(&bob64_boot_page_allocator,pages);
    if(!base||base>=0x0000800000000000ULL||
       bytes>0x0000800000000000ULL-base)return -1;
    if(bob64_bootstrap_space.NxSupported)flags|=BOB64_PAGE_NX;
    for(u64 offset=0;offset<bytes;offset+=BOB64_PAGE_SIZE) {
        u64 translated=0;
        int result=bob64_page_translate(&bob64_bootstrap_space.PageTable,
                                        base+offset,&translated,0);
        if(result==0)
            result=bob64_page_map(&bob64_bootstrap_space.PageTable,base+offset,
                                  base+offset,flags);
        else if(result==1&&translated==base+offset)
            result=bob64_page_protect(&bob64_bootstrap_space.PageTable,
                                      base+offset,flags);
        else return -1;
        if(result)return -1;
        __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)(base+offset)):
                         "memory");
    }
    volatile u8 *memory=(volatile u8 *)(uintptr_t)base;
    for(u64 i=0;i<bytes;i++)memory[i]=0;
    *physical=base;*virtual=memory;
    return 0;
}

static int kernel_xhci_write64(KERNEL_XHCI_MMIO *mmio,u32 offset,u64 value) {
    if(!mmio||(offset&7u)||offset>mmio->Size||
       mmio->Size-offset<sizeof(value))return -1;
    if(kernel_xhci_write32(mmio,offset,(u32)value)||
       kernel_xhci_write32(mmio,offset+4,(u32)(value>>32)))return -1;
    return 0;
}

static int kernel_xhci_advance_event(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities) {
    u32 runtime_offset;
    if(!mmio||!capabilities||kernel_xhci_rings.EventIndex>=
       BOB64_XHCI_RING_TRBS)return -1;
    kernel_xhci_rings.EventIndex++;
    if(kernel_xhci_rings.EventIndex==BOB64_XHCI_RING_TRBS) {
        kernel_xhci_rings.EventIndex=0;
        kernel_xhci_rings.EventCycle^=1u;
    }
    runtime_offset=capabilities->RuntimeOffset-mmio->CapabilityLength;
    return kernel_xhci_write64(mmio,runtime_offset+0x38,
        kernel_xhci_rings.EventPhysical+
        kernel_xhci_rings.EventIndex*sizeof(KERNEL_XHCI_TRB)+8u);
}

static int kernel_xhci_event_take(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u32 expected_type,
        KERNEL_XHCI_TRB *result) {
    if(!mmio||!capabilities||!result||!kernel_xhci_rings.EventRing)return -1;
    for(u32 offset=0;offset<kernel_xhci_rings.DeferredCount;offset++) {
        u32 index=(kernel_xhci_rings.DeferredHead+offset)%
                  (u32)(sizeof(kernel_xhci_rings.DeferredEvents)/
                        sizeof(kernel_xhci_rings.DeferredEvents[0]));
        KERNEL_XHCI_TRB *queued=&kernel_xhci_rings.DeferredEvents[index];
        u32 type=(queued->Control>>BOB64_XHCI_TRB_TYPE_SHIFT)&0x3fu;
        if(!expected_type||type==expected_type) {
            *result=*queued;
            for(u32 move=offset;move+1<kernel_xhci_rings.DeferredCount;move++) {
                u32 to=(kernel_xhci_rings.DeferredHead+move)%32u;
                u32 from=(kernel_xhci_rings.DeferredHead+move+1u)%32u;
                kernel_xhci_rings.DeferredEvents[to]=
                    kernel_xhci_rings.DeferredEvents[from];
            }
            kernel_xhci_rings.DeferredCount--;
            return 1;
        }
    }
    volatile KERNEL_XHCI_TRB *event=
        &kernel_xhci_rings.EventRing[kernel_xhci_rings.EventIndex];
    u32 control=event->Control;
    if((control&1u)!=kernel_xhci_rings.EventCycle)return 0;
    result->Parameter=event->Parameter;
    result->Status=event->Status;
    result->Control=control;
    if(kernel_xhci_advance_event(mmio,capabilities))return -2;
    u32 type=(control>>BOB64_XHCI_TRB_TYPE_SHIFT)&0x3fu;
    if(!expected_type||type==expected_type)return 1;
    if(kernel_xhci_rings.DeferredCount>=32u)return -3;
    u32 tail=(kernel_xhci_rings.DeferredHead+
        kernel_xhci_rings.DeferredCount)%32u;
    kernel_xhci_rings.DeferredEvents[tail]=*result;
    kernel_xhci_rings.DeferredCount++;
    return 0;
}

static int kernel_xhci_wait_event(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u32 expected_type,
        u32 poll_limit,KERNEL_XHCI_TRB *result) {
    if(!mmio||!capabilities||!expected_type||!poll_limit||!result||
       !kernel_xhci_rings.EventRing)return -1;
    for(u32 wait=0;wait<poll_limit;wait++) {
        int taken=kernel_xhci_event_take(mmio,capabilities,expected_type,result);
        if(taken<0)return taken;
        if(taken)return 0;
        __asm__ volatile("pause");
    }
    return -4;
}

static int kernel_xhci_wait_transfer(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u8 slot,u8 dci,
        u64 trb_physical,u32 poll_limit,KERNEL_XHCI_TRB *completion) {
    KERNEL_XHCI_TRB deferred[32];
    u32 deferred_count=0;
    int found=0;
    int storage_removed=0;
#ifdef BOB64_STORAGE_ACTIVE_DISCONNECT_TEST
    static u8 active_disconnect_test_armed;
#endif
    if(!mmio||!capabilities||!slot||dci<2u||dci>31u||!poll_limit||
       !completion||!kernel_xhci_rings.EventRing)return -1;
#ifdef BOB64_STORAGE_ACTIVE_DISCONNECT_TEST
    if(!active_disconnect_test_armed) {
        for(u32 i=0;i<kernel_xhci_storage_device_count;i++) {
            if(kernel_xhci_storage_devices[i].Active&&
               kernel_xhci_storage_devices[i].Slot==slot&&
               kernel_xhci_storage_devices[i].DisconnectTestReadPending) {
                active_disconnect_test_armed=1;
                kernel_write("bob64 kernel: USB MSC READ(10) data transfer in flight\r\n");
                (void)kernel_xhci_delay_ticks(150);
                break;
            }
        }
    }
#endif
    while(kernel_xhci_rings.DeferredCount&&deferred_count<32u) {
        deferred[deferred_count++]=kernel_xhci_rings.DeferredEvents[
            kernel_xhci_rings.DeferredHead];
        kernel_xhci_rings.DeferredHead=(u8)((kernel_xhci_rings.DeferredHead+1u)%32u);
        kernel_xhci_rings.DeferredCount--;
    }
    for(u32 wait=0;wait<poll_limit;wait++) {
        volatile KERNEL_XHCI_TRB *event=
            &kernel_xhci_rings.EventRing[kernel_xhci_rings.EventIndex];
        u32 control=event->Control;
        if((control&1u)!=kernel_xhci_rings.EventCycle) {
            __asm__ volatile("pause");
            continue;
        }
        KERNEL_XHCI_TRB current={event->Parameter,event->Status,control};
        if(kernel_xhci_advance_event(mmio,capabilities))break;
        u32 type=(control>>BOB64_XHCI_TRB_TYPE_SHIFT)&0x3fu;
        if(type==BOB64_XHCI_TRB_TYPE_TRANSFER_EVENT&&
           (u8)(control>>24)==slot&&((control>>16)&0x1fu)==dci&&
           current.Parameter==trb_physical) {
            *completion=current;
            found=1;
            break;
        }
        if(type==BOB64_XHCI_TRB_TYPE_PORT_STATUS_CHANGE) {
            u8 port=(u8)(current.Parameter>>24);
            u32 port_status=0;
            for(u32 i=0;i<kernel_xhci_storage_device_count;i++) {
                KERNEL_XHCI_STORAGE_DEVICE *device=
                    &kernel_xhci_storage_devices[i];
                if(device->Active&&device->Slot==slot&&
                   device->Topology.RootPort==port&&port&&
                   port<=capabilities->MaxPorts&&
                   !kernel_xhci_read32(mmio,0x400u+((u32)port-1u)*0x10u,
                                      &port_status)&&!(port_status&1u)) {
                    storage_removed=1;
                    break;
                }
            }
            if(storage_removed) {
                kernel_write("bob64 kernel: USB MSC transfer aborted after disconnect\r\n");
                if(deferred_count>=32u)break;
                deferred[deferred_count++]=current;
                break;
            }
        }
        if(deferred_count>=32u)break;
        deferred[deferred_count++]=current;
    }
    for(u32 i=0;i<deferred_count;i++) {
        if(kernel_xhci_rings.DeferredCount>=32u)return -2;
        u32 tail=(kernel_xhci_rings.DeferredHead+
                  kernel_xhci_rings.DeferredCount)%32u;
        kernel_xhci_rings.DeferredEvents[tail]=deferred[i];
        kernel_xhci_rings.DeferredCount++;
    }
    return found?0:(storage_removed?-4:-3);
}

static int kernel_xhci_disable_slot(u8 slot_id) {
    if(!slot_id||!kernel_xhci_rings.CommandRing||
       kernel_xhci_rings.CommandIndex>=BOB64_XHCI_RING_TRBS-1)return -1;
    u32 command_index=kernel_xhci_rings.CommandIndex;
    volatile KERNEL_XHCI_TRB *command=
        &kernel_xhci_rings.CommandRing[command_index];
    KERNEL_XHCI_TRB completion;
    command->Parameter=0;
    command->Status=0;
    command->Control=(BOB64_XHCI_TRB_TYPE_DISABLE_SLOT<<
        BOB64_XHCI_TRB_TYPE_SHIFT)|((u32)slot_id<<24)|1u;
    __asm__ volatile("sfence" ::: "memory");
    u32 doorbell=kernel_xhci_live_capabilities.DoorbellOffset-
        kernel_xhci_live_mmio.CapabilityLength;
    if(kernel_xhci_write32(&kernel_xhci_live_mmio,doorbell,0))return -2;
    int result=kernel_xhci_wait_event(&kernel_xhci_live_mmio,
        &kernel_xhci_live_capabilities,
        BOB64_XHCI_TRB_TYPE_COMMAND_COMPLETION,10000000u,&completion);
    if(result)return -3+result;
    if(completion.Parameter!=kernel_xhci_rings.CommandPhysical+
          command_index*sizeof(KERNEL_XHCI_TRB)||
       (completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=slot_id)return -4;
    kernel_xhci_rings.CommandIndex++;
    if(kernel_xhci_rings.DeviceContextArray)
        kernel_xhci_rings.DeviceContextArray[slot_id]=0;
    return 0;
}

static int kernel_xhci_usb_hid_submit(KERNEL_XHCI_HID_DEVICE *device) {
    if(!device||!device->Active||device->Pending||!device->Ring)return -1;
    for(u32 i=0;i<device->ReportLength;i++)device->Report[i]=0;
    if(device->NextIndex==BOB64_XHCI_RING_TRBS-1) {
        volatile KERNEL_XHCI_TRB *link=
            &device->Ring[BOB64_XHCI_RING_TRBS-1];
        link->Parameter=device->RingPhysical;link->Status=0;
        link->Control=(BOB64_XHCI_TRB_TYPE_LINK<<BOB64_XHCI_TRB_TYPE_SHIFT)|
            2u|device->Cycle;
        device->NextIndex=0;device->Cycle^=1u;
    }
    u32 index=device->NextIndex++;
    volatile KERNEL_XHCI_TRB *transfer=&device->Ring[index];
    transfer->Parameter=device->ReportPhysical;
    transfer->Status=device->ReportLength;
    transfer->Control=(1u<<BOB64_XHCI_TRB_TYPE_SHIFT)|(1u<<5)|device->Cycle;
    __asm__ volatile("sfence":::"memory");
    u32 doorbell=kernel_xhci_live_capabilities.DoorbellOffset-
        kernel_xhci_live_mmio.CapabilityLength+(u32)device->Slot*4u;
    if(kernel_xhci_write32(&kernel_xhci_live_mmio,doorbell,device->Dci))return -2;
    device->PendingIndex=index;device->Pending=1;
    return 0;
}

static int kernel_xhci_hub_status_submit(KERNEL_XHCI_HUB_DEVICE *device) {
    if(!device||!device->Active||device->Pending||!device->Ring)return -1;
    for(u32 i=0;i<device->ReportLength;i++)device->Report[i]=0;
    if(device->NextIndex==BOB64_XHCI_RING_TRBS-1) {
        volatile KERNEL_XHCI_TRB *link=
            &device->Ring[BOB64_XHCI_RING_TRBS-1];
        link->Parameter=device->RingPhysical;link->Status=0;
        link->Control=(BOB64_XHCI_TRB_TYPE_LINK<<BOB64_XHCI_TRB_TYPE_SHIFT)|
            2u|device->Cycle;
        device->NextIndex=0;device->Cycle^=1u;
    }
    u32 index=device->NextIndex++;
    volatile KERNEL_XHCI_TRB *transfer=&device->Ring[index];
    transfer->Parameter=device->ReportPhysical;
    transfer->Status=device->ReportLength;
    transfer->Control=(1u<<BOB64_XHCI_TRB_TYPE_SHIFT)|(1u<<5)|device->Cycle;
    __asm__ volatile("sfence" ::: "memory");
    u32 doorbell=kernel_xhci_live_capabilities.DoorbellOffset-
        kernel_xhci_live_mmio.CapabilityLength+(u32)device->Slot*4u;
    if(kernel_xhci_write32(&kernel_xhci_live_mmio,doorbell,device->Dci))return -2;
    device->PendingIndex=index;device->Pending=1;
    return 0;
}

static void kernel_xhci_hid_disconnect(KERNEL_XHCI_HID_DEVICE *device,u32 index) {
    static const u8 released_report[8]={0};
    if(!device||!device->Active)return;
    device->Active=0;device->Pending=0;
    if(device->Kind==1)
        (void)bob64_keyboard_hid_report_device(&device->KeyboardState,
                                                released_report);
    else if(device->Kind==2)
        (void)bob64_mouse_usb_report_device(index,0,0,0,0);
    if(kernel_xhci_disable_slot(device->Slot))
        kernel_write("bob64 kernel: USB HID slot disable failed\r\n");
    kernel_write("bob64 kernel: USB HID transfer stopped; held input released\r\n");
}

static void kernel_xhci_usb_hid_poll(void) {
    if((!kernel_xhci_hid_device_count&&!kernel_xhci_hub_device_count&&
        !kernel_xhci_storage_device_count)||
       !kernel_xhci_rings.EventRing)return;
    for(u32 count=0;count<8;count++) {
        KERNEL_XHCI_TRB event;
        int taken=kernel_xhci_event_take(&kernel_xhci_live_mmio,
            &kernel_xhci_live_capabilities,0,&event);
        if(taken<=0)return;
        u32 type=(event.Control>>BOB64_XHCI_TRB_TYPE_SHIFT)&0x3fu;
        if(type==BOB64_XHCI_TRB_TYPE_PORT_STATUS_CHANGE) {
            u8 port=(u8)(event.Parameter>>24);
            u32 port_status=0;
            if(port&&port<=kernel_xhci_live_capabilities.MaxPorts&&
               !kernel_xhci_read32(&kernel_xhci_live_mmio,
                   0x400u+((u32)port-1u)*0x10u,&port_status)) {
                if(port_status&1u)kernel_xhci_hid_reconnect(port);
                else {
                    for(u32 i=0;i<kernel_xhci_hid_device_count;i++)
                        if(kernel_xhci_hid_devices[i].Active&&
                           kernel_xhci_hid_devices[i].RootPort==port)
                            kernel_xhci_hid_disconnect(&kernel_xhci_hid_devices[i],i);
                    for(u32 i=0;i<kernel_xhci_hub_device_count;i++) {
                        KERNEL_XHCI_HUB_DEVICE *hub=&kernel_xhci_hub_devices[i];
                        if(hub->Active&&hub->RootPort==port) {
                            hub->Active=0;hub->Pending=0;
                            (void)kernel_xhci_disable_slot(hub->Slot);
                        }
                    }
                    for(u32 i=0;i<kernel_xhci_storage_device_count;i++) {
                        KERNEL_XHCI_STORAGE_DEVICE *storage=
                            &kernel_xhci_storage_devices[i];
                        if(storage->Active&&storage->Topology.RootPort==port) {
                            storage->Active=0;
                            (void)kernel_xhci_disable_slot(storage->Slot);
                            kernel_write("bob64 kernel: USB mass-storage device disconnected\r\n");
                        }
                    }
                }
            }
        } else if(type==BOB64_XHCI_TRB_TYPE_TRANSFER_EVENT) {
            u32 completion=(event.Status>>24)&0xffu;
            for(u32 i=0;i<kernel_xhci_hid_device_count;i++) {
                KERNEL_XHCI_HID_DEVICE *device=&kernel_xhci_hid_devices[i];
                if(!device->Pending||
                   (u8)(event.Control>>24)!=device->Slot||
                   ((event.Control>>16)&0x1fu)!=device->Dci||
                   event.Parameter!=device->RingPhysical+
                       device->PendingIndex*sizeof(KERNEL_XHCI_TRB))continue;
                device->Pending=0;
                if(completion==BOB64_XHCI_TRB_COMPLETION_SUCCESS||completion==13u) {
                    device->TransferErrors=0;
                    u8 report[8]={0};
                    for(u32 j=0;j<device->ReportLength&&j<sizeof(report);j++)
                        report[j]=device->Report[j];
                    if(!device->LoggedReport) {
                        device->LoggedReport=1;
                        kernel_write("bob64 kernel: USB HID transfer report received\r\n");
                    }
                    if(device->Kind==1) {
                        int has_key=0;
                        (void)bob64_keyboard_hid_report_device(
                            &device->KeyboardState,report);
                        for(u32 j=2;j<sizeof(report);j++)if(report[j])has_key=1;
                        if(has_key&&!device->LoggedInput) {
                            device->LoggedInput=1;
                            kernel_write("bob64 kernel: USB HID keyboard input queued\r\n");
                        }
                    } else if(device->Kind==2) {
                        s8 wheel=device->ReportLength>=4?(s8)report[3]:0;
                        int mouse_result=bob64_mouse_usb_report_device(i,report[0],
                            (s8)report[1],(s8)report[2],wheel);
                        if(!mouse_result&&!device->LoggedInput) {
                            device->LoggedInput=1;
                            kernel_write("bob64 kernel: USB HID mouse input queued\r\n");
                        }
                        if(!mouse_result&&wheel&&!device->LoggedWheel) {
                            device->LoggedWheel=1;
                            kernel_write("bob64 kernel: USB HID mouse wheel input queued\r\n");
                        }
                    }
                    (void)kernel_xhci_usb_hid_submit(device);
                } else if(++device->TransferErrors>=3u) {
                    kernel_xhci_hid_disconnect(device,i);
                } else {
                    (void)kernel_xhci_usb_hid_submit(device);
                }
                break;
            }
            for(u32 i=0;i<kernel_xhci_hub_device_count;i++) {
                KERNEL_XHCI_HUB_DEVICE *hub=&kernel_xhci_hub_devices[i];
                if(!hub->Active||!hub->Pending||
                   (u8)(event.Control>>24)!=hub->Slot||
                   ((event.Control>>16)&0x1fu)!=hub->Dci||
                   event.Parameter!=hub->RingPhysical+
                       hub->PendingIndex*sizeof(KERNEL_XHCI_TRB))continue;
                hub->Pending=0;
                if(completion==BOB64_XHCI_TRB_COMPLETION_SUCCESS||completion==13u) {
                    int changed=0;
                    hub->TransferErrors=0;
                    for(u32 byte=0;byte<hub->ReportLength;byte++)
                        if(hub->Report[byte])changed=1;
                    if(changed&&!hub->LoggedChange) {
                        hub->LoggedChange=1;
                        kernel_write("bob64 kernel: USB hub downstream change report received slot=");
                        kernel_hex64(hub->Slot);kernel_write(" bitmap=");
                        for(u32 byte=0;byte<hub->ReportLength;byte++) {
                            kernel_hex64(hub->Report[byte]);kernel_write(" ");
                        }
                        kernel_write("\r\n");
                    }
                    if(changed)kernel_xhci_hub_process_changes(hub);
                    (void)kernel_xhci_hub_status_submit(hub);
                } else if(++hub->TransferErrors<3u)
                    (void)kernel_xhci_hub_status_submit(hub);
                else hub->Active=0;
                break;
            }
        }
    }
}

static int kernel_xhci_start_command_ring(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities) {
    u64 dcbaa_physical,command_physical,event_physical,erst_physical;
    u64 scratch_array_physical=0;
    u64 scratchpad_physical;
    volatile u8 *dcbaa_bytes,*command_bytes,*event_bytes,*erst_bytes;
    u64 scratch_array_pages;
    u32 status,pagesize,command;
    if(!mmio||!capabilities||capabilities->MaxScratchpadBuffers>
       BOB64_XHCI_MAX_SCRATCHPADS)return -1;
    if(kernel_xhci_read32(mmio,8,&pagesize)||!(pagesize&1u))return -2;
    if(kernel_xhci_dma_alloc(1,&dcbaa_physical,&dcbaa_bytes)||
       kernel_xhci_dma_alloc(1,&command_physical,&command_bytes)||
       kernel_xhci_dma_alloc(1,&event_physical,&event_bytes)||
       kernel_xhci_dma_alloc(1,&erst_physical,&erst_bytes))return -3;
    if(capabilities->MaxScratchpadBuffers) {
        scratch_array_pages=(capabilities->MaxScratchpadBuffers*sizeof(u64)+
                             BOB64_PAGE_SIZE-1)/BOB64_PAGE_SIZE;
        volatile u8 *scratch_array_bytes;
        if(kernel_xhci_dma_alloc(scratch_array_pages,&scratch_array_physical,
                                 &scratch_array_bytes))return -4;
        volatile u64 *scratch_array=(volatile u64 *)(volatile void *)scratch_array_bytes;
        for(u32 i=0;i<capabilities->MaxScratchpadBuffers;i++) {
            volatile u8 *scratch_bytes;
            if(kernel_xhci_dma_alloc(1,&scratchpad_physical,&scratch_bytes))
                return -5;
            scratch_array[i]=scratchpad_physical;
        }
        *(volatile u64 *)(volatile void *)dcbaa_bytes=scratch_array_physical;
    }
    volatile KERNEL_XHCI_TRB *command_ring=
        (volatile KERNEL_XHCI_TRB *)(volatile void *)command_bytes;
    volatile KERNEL_XHCI_TRB *event_ring=
        (volatile KERNEL_XHCI_TRB *)(volatile void *)event_bytes;
    volatile KERNEL_XHCI_ERST_ENTRY *erst=
        (volatile KERNEL_XHCI_ERST_ENTRY *)(volatile void *)erst_bytes;
    kernel_xhci_rings.CommandRing=command_ring;
    kernel_xhci_rings.EventRing=event_ring;
    kernel_xhci_rings.CommandPhysical=command_physical;
    kernel_xhci_rings.EventPhysical=event_physical;
    kernel_xhci_rings.DeviceContextArray=
        (volatile u64 *)(volatile void *)dcbaa_bytes;
    kernel_xhci_rings.CommandIndex=0;
    kernel_xhci_rings.EventIndex=0;
    kernel_xhci_rings.EventCycle=1;
    command_ring[BOB64_XHCI_RING_TRBS-1].Parameter=command_physical;
    command_ring[BOB64_XHCI_RING_TRBS-1].Status=0;
    command_ring[BOB64_XHCI_RING_TRBS-1].Control=
        (BOB64_XHCI_TRB_TYPE_LINK<<BOB64_XHCI_TRB_TYPE_SHIFT)|3u;
    command_ring[0].Parameter=0;
    command_ring[0].Status=0;
    command_ring[0].Control=
        (BOB64_XHCI_TRB_TYPE_NOOP_COMMAND<<BOB64_XHCI_TRB_TYPE_SHIFT)|1u;
    erst[0].RingBase=event_physical;
    erst[0].RingSize=BOB64_XHCI_RING_TRBS;
    if(kernel_xhci_write64(mmio,0x30,dcbaa_physical)||
       kernel_xhci_write64(mmio,0x18,command_physical|1u))return -6;
    if(kernel_xhci_write32(mmio,0x38,1u))return -7;
    u32 runtime_offset=capabilities->RuntimeOffset-mmio->CapabilityLength;
    if(runtime_offset>mmio->Size||mmio->Size-runtime_offset<0x40)return -8;
    if(kernel_xhci_write32(mmio,runtime_offset+0x28,1u)||
       kernel_xhci_write64(mmio,runtime_offset+0x30,erst_physical)||
       kernel_xhci_write64(mmio,runtime_offset+0x38,event_physical))return -9;
    __asm__ volatile("sfence" ::: "memory");
    if(kernel_xhci_read32(mmio,0,&command)||
       kernel_xhci_write32(mmio,0,command|BOB64_XHCI_USBCMD_RUN))return -10;
    for(u32 i=0;i<1000000u;i++) {
        if(kernel_xhci_read32(mmio,4,&status))return -11;
        if(!(status&BOB64_XHCI_USBSTS_NOT_READY)&&
           !(status&BOB64_XHCI_USBSTS_HALTED))break;
        if(i==999999u)return -12;
    }
    u32 doorbell_offset=capabilities->DoorbellOffset-mmio->CapabilityLength;
    if(kernel_xhci_write32(mmio,doorbell_offset,0))return -16;
    KERNEL_XHCI_TRB completion;
    if(kernel_xhci_wait_event(mmio,capabilities,
          BOB64_XHCI_TRB_TYPE_COMMAND_COMPLETION,1000000u,&completion))return -15;
    if(completion.Parameter!=command_physical||
       (completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS)return -13;
    kernel_xhci_rings.CommandIndex=1;
    return 0;
}

static int kernel_xhci_enable_slot(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u8 *slot_id) {
    u32 doorbell_offset;
    if(!mmio||!capabilities||!slot_id||
       !kernel_xhci_rings.CommandRing||!kernel_xhci_rings.EventRing||
       kernel_xhci_rings.CommandIndex>=BOB64_XHCI_RING_TRBS-1||
       kernel_xhci_rings.EventIndex>=BOB64_XHCI_RING_TRBS)return -1;
    u32 command_index=kernel_xhci_rings.CommandIndex;
    volatile KERNEL_XHCI_TRB *command=&kernel_xhci_rings.CommandRing[command_index];
    KERNEL_XHCI_TRB completion;
    command->Parameter=0;
    command->Status=0;
    command->Control=(BOB64_XHCI_TRB_TYPE_ENABLE_SLOT<<
                      BOB64_XHCI_TRB_TYPE_SHIFT)|1u;
    __asm__ volatile("sfence" ::: "memory");
    doorbell_offset=capabilities->DoorbellOffset-mmio->CapabilityLength;
    if(kernel_xhci_write32(mmio,doorbell_offset,0))return -2;
    int result=kernel_xhci_wait_event(mmio,capabilities,
        BOB64_XHCI_TRB_TYPE_COMMAND_COMPLETION,10000000u,&completion);
    if(result)return -5+result;
    u8 result_slot=(u8)(completion.Control>>24);
    if(completion.Parameter!=kernel_xhci_rings.CommandPhysical+
                              command_index*sizeof(KERNEL_XHCI_TRB)||
       (completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       !result_slot||result_slot>capabilities->MaxSlots)return -3;
    *slot_id=result_slot;
    kernel_xhci_rings.CommandIndex++;
    return 0;
}

static int kernel_xhci_control_in_data(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u8 slot_id,
        volatile KERNEL_XHCI_TRB *transfer_ring,u64 transfer_ring_physical,
        u64 buffer_physical,u8 request_type,u8 request,u16 value,u16 index,
        u16 length,u32 ring_index) {
    u32 doorbell_offset;
    KERNEL_XHCI_TRB completion;
    if(!mmio||!capabilities||!slot_id||!transfer_ring||!length||
       ring_index+2>=BOB64_XHCI_RING_TRBS-1)return -1;
    transfer_ring[ring_index].Parameter=bob64_usb_control_setup(
        request_type,request,value,index,length);
    transfer_ring[ring_index].Status=8;
    transfer_ring[ring_index].Control=(2u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
        (3u<<16)|(1u<<6)|(1u<<4)|1u;
    transfer_ring[ring_index+1].Parameter=buffer_physical;
    transfer_ring[ring_index+1].Status=length;
    transfer_ring[ring_index+1].Control=(3u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
        (1u<<16)|(1u<<4)|(1u<<2)|1u;
    transfer_ring[ring_index+2].Parameter=0;
    transfer_ring[ring_index+2].Status=0;
    transfer_ring[ring_index+2].Control=(4u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
        (1u<<5)|1u;
    __asm__ volatile("sfence" ::: "memory");
    doorbell_offset=capabilities->DoorbellOffset-mmio->CapabilityLength+
                    (u32)slot_id*4u;
    if(kernel_xhci_write32(mmio,doorbell_offset,1u))return -2;
    int result=kernel_xhci_wait_event(mmio,capabilities,
        BOB64_XHCI_TRB_TYPE_TRANSFER_EVENT,10000000u,&completion);
    if(result)return -3+result;
    if((completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=slot_id||
       ((completion.Control>>16)&0x1fu)!=1u||
       completion.Parameter!=transfer_ring_physical+
             (ring_index+2)*sizeof(KERNEL_XHCI_TRB))return -4;
    return 0;
}

static int kernel_xhci_get_descriptor(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u8 slot_id,
        volatile KERNEL_XHCI_TRB *transfer_ring,u64 transfer_ring_physical,
        u64 buffer_physical,u8 descriptor_type,u16 length,u32 ring_index) {
    return kernel_xhci_control_in_data(mmio,capabilities,slot_id,
        transfer_ring,transfer_ring_physical,buffer_physical,0x80,6,
        (u16)((u16)descriptor_type<<8),0,length,ring_index);
}

static int kernel_xhci_control_no_data(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u8 slot_id,
        volatile KERNEL_XHCI_TRB *transfer_ring,u64 transfer_ring_physical,
        u32 ring_index,u8 request_type,u8 request,u16 value,u16 index) {
    KERNEL_XHCI_TRB completion;
    if(!mmio||!capabilities||!slot_id||!transfer_ring||
       ring_index+1>=BOB64_XHCI_RING_TRBS-1)return -1;
    transfer_ring[ring_index].Parameter=bob64_usb_control_setup(
        request_type,request,value,index,0);
    transfer_ring[ring_index].Status=8;
    transfer_ring[ring_index].Control=(2u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
        (1u<<6)|(1u<<4)|1u;
    transfer_ring[ring_index+1].Parameter=0;
    transfer_ring[ring_index+1].Status=0;
    transfer_ring[ring_index+1].Control=(4u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
        (1u<<16)|(1u<<5)|1u;
    __asm__ volatile("sfence" ::: "memory");
    u32 doorbell_offset=capabilities->DoorbellOffset-mmio->CapabilityLength+
                        (u32)slot_id*4u;
    if(kernel_xhci_write32(mmio,doorbell_offset,1u))return -2;
    int result=kernel_xhci_wait_event(mmio,capabilities,
        BOB64_XHCI_TRB_TYPE_TRANSFER_EVENT,10000000u,&completion);
    if(result)return -3+result;
    if((completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=slot_id||
       ((completion.Control>>16)&0x1fu)!=1u||
       completion.Parameter!=transfer_ring_physical+
             (ring_index+1)*sizeof(KERNEL_XHCI_TRB))return -4;
    return 0;
}

static int kernel_xhci_delay_ticks(u64 ticks) {
    u64 start=bob64_timer_ticks();
    if(!ticks)return 0;
    for(u32 wait=0;wait<200000000u;wait++) {
        if(bob64_timer_ticks()-start>=ticks)return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static int kernel_xhci_update_hub_context(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u8 slot_id,
        volatile u8 *device_context_bytes,u32 context_size,u8 port_count,
        u8 tt_think_time) {
    u64 input_context_physical;
    volatile u8 *input_context_bytes;
    KERNEL_XHCI_TRB completion;
    u32 command_index=kernel_xhci_rings.CommandIndex;
    if(!mmio||!capabilities||!slot_id||!device_context_bytes||
       !context_size||!port_count||command_index>=BOB64_XHCI_RING_TRBS-1)
        return -1;
    if(kernel_xhci_dma_alloc(1,&input_context_physical,
                             &input_context_bytes))return -2;
    volatile u32 *input=(volatile u32 *)(volatile void *)input_context_bytes;
    volatile u32 *slot=(volatile u32 *)(volatile void *)(input_context_bytes+
                                                         context_size);
    volatile u32 *live_slot=(volatile u32 *)(volatile void *)device_context_bytes;
    input[1]=1u; /* Update the Slot Context. */
    for(u32 word=0;word<context_size/sizeof(u32);word++)slot[word]=live_slot[word];
    slot[0]|=1u<<26; /* Hub */
    slot[1]=(slot[1]&0x00ffffffu)|((u32)port_count<<24);
    u32 speed=(live_slot[0]>>20)&0xfu;
    slot[2]=(slot[2]&~(3u<<16))|
            (speed==3u?((u32)(tt_think_time&3u)<<16):0u);
    volatile KERNEL_XHCI_TRB *command=
        &kernel_xhci_rings.CommandRing[command_index];
    command->Parameter=input_context_physical;
    command->Status=0;
    command->Control=(12u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                     ((u32)slot_id<<24)|1u;
    __asm__ volatile("sfence" ::: "memory");
    u32 doorbell_offset=capabilities->DoorbellOffset-mmio->CapabilityLength;
    if(kernel_xhci_write32(mmio,doorbell_offset,0))return -3;
    int result=kernel_xhci_wait_event(mmio,capabilities,
        BOB64_XHCI_TRB_TYPE_COMMAND_COMPLETION,10000000u,&completion);
    if(result)return -4+result;
    if(completion.Parameter!=kernel_xhci_rings.CommandPhysical+
                              command_index*sizeof(KERNEL_XHCI_TRB)||
       (completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=slot_id)return -5;
    kernel_xhci_rings.CommandIndex++;
    return 0;
}

static int kernel_xhci_configure_usb2_hub(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u8 slot_id,
        const KERNEL_XHCI_TOPOLOGY *topology,
        volatile KERNEL_XHCI_TRB *transfer_ring,u64 transfer_ring_physical,
        u64 descriptor_physical,volatile u8 *descriptor_bytes,
        volatile u8 *device_context_bytes,u32 context_size,u8 configuration,
        u8 *port_count_out,u32 *control_index_out) {
    BOB64_USB_HUB_DESCRIPTOR hub;
    u32 connected=0,reset=0,control_index=20;
    if(!mmio||!capabilities||!slot_id||!topology||!transfer_ring||
       !port_count_out||!control_index_out||
       !descriptor_bytes)
        return -1;
    int result=kernel_xhci_control_no_data(mmio,capabilities,slot_id,
        transfer_ring,transfer_ring_physical,12,0x00,9,configuration,0);
    if(result)return -2+result;
    result=kernel_xhci_control_in_data(mmio,capabilities,slot_id,
        transfer_ring,transfer_ring_physical,descriptor_physical,0xa0,6,
        0x2900,0,7,14);
    if(result)return -6+result;
    if(descriptor_bytes[0]<7||descriptor_bytes[1]!=0x29)return -10;
    result=kernel_xhci_control_in_data(mmio,capabilities,slot_id,
        transfer_ring,transfer_ring_physical,descriptor_physical,0xa0,6,
        0x2900,0,descriptor_bytes[0],17);
    if(result)return -11+result;
    if(bob64_usb_parse_hub_descriptor((const void *)(uintptr_t)descriptor_bytes,
            descriptor_bytes[0],&hub)) {
        kernel_write("bob64 kernel: invalid USB hub descriptor len=");
        kernel_hex64(descriptor_bytes[0]);kernel_write(" type=");
        kernel_hex64(descriptor_bytes[1]);kernel_write(" ports=");
        kernel_hex64(descriptor_bytes[2]);kernel_write(" raw=");
        for(u32 i=0;i<12&&i<descriptor_bytes[0];i++) {
            kernel_hex64(descriptor_bytes[i]);kernel_write(" ");
        }
        kernel_write("\r\n");
        return -15;
    }
    /* Keep this one-page EP0 ring within its link TRB while scanning ports. */
    if(hub.PortCount>16)return -16;
    *port_count_out=hub.PortCount;
    result=kernel_xhci_update_hub_context(mmio,capabilities,slot_id,
        device_context_bytes,context_size,hub.PortCount,
        (u8)((hub.Characteristics>>5)&3u));
    if(result)return -20+result;
    if((hub.Characteristics&3u)<2u) {
        for(u32 port=1;port<=hub.PortCount;port++) {
            result=kernel_xhci_control_no_data(mmio,capabilities,slot_id,
                transfer_ring,transfer_ring_physical,control_index,0x23,3,8,
                (u16)port);
            if(result)return -25+result;
            control_index+=2;
        }
        u64 delay_ms=(u64)hub.PowerOnToGood*2u;
        u64 delay_ticks=(delay_ms+9u)/10u;
        if(kernel_xhci_delay_ticks(delay_ticks))return -26;
    }
    control_index=20u+((hub.Characteristics&3u)<2u?
                       (u32)hub.PortCount*2u:0u);
    for(u32 port=1;port<=hub.PortCount;port++) {
        result=kernel_xhci_control_in_data(mmio,capabilities,slot_id,
            transfer_ring,transfer_ring_physical,descriptor_physical,0xa3,0,
            0,(u16)port,4,control_index);
        if(result)return -30+result;
        u16 port_status=(u16)(descriptor_bytes[0]|((u16)descriptor_bytes[1]<<8));
        control_index+=3;
        if(port_status&1u) {
            u16 port_change=(u16)(descriptor_bytes[2]|((u16)descriptor_bytes[3]<<8));
            connected++;
            result=kernel_xhci_control_no_data(mmio,capabilities,slot_id,
                transfer_ring,transfer_ring_physical,control_index,0x23,3,4,
                (u16)port);
            if(result)return -31+result;
            control_index+=2;
            if(kernel_xhci_delay_ticks(2))return -32;
            result=kernel_xhci_control_in_data(mmio,capabilities,slot_id,
                transfer_ring,transfer_ring_physical,descriptor_physical,
                0xa3,0,0,(u16)port,4,control_index);
            if(result)return -33+result;
            port_status=(u16)(descriptor_bytes[0]|((u16)descriptor_bytes[1]<<8));
            port_change=(u16)(descriptor_bytes[2]|((u16)descriptor_bytes[3]<<8));
            control_index+=3;
            if((port_status&(1u<<4))||!(port_status&(1u<<1)))return -34;
            reset++;
            if(port_change&1u) {
                result=kernel_xhci_control_no_data(mmio,capabilities,slot_id,
                    transfer_ring,transfer_ring_physical,control_index,
                    0x23,1,16,(u16)port);
                if(result)return -35+result;
                control_index+=2;
            }
            if(port_change&(1u<<4)) {
                result=kernel_xhci_control_no_data(mmio,capabilities,slot_id,
                    transfer_ring,transfer_ring_physical,control_index,
                    0x23,1,20,(u16)port);
                if(result)return -36+result;
                control_index+=2;
            }
            if(kernel_xhci_hub_child_count<
                   sizeof(kernel_xhci_hub_children)/
                       sizeof(kernel_xhci_hub_children[0])&&
               topology->Depth<5u) {
                u32 route=0;
                u16 port_speed_bits=(u16)((port_status>>9)&3u);
                u8 child_speed=port_speed_bits==2u?3u:
                               port_speed_bits==1u?2u:1u;
                if(!bob64_usb_route_string_append(topology->RouteString,
                        topology->Depth,(u8)port,&route)) {
                    KERNEL_XHCI_TOPOLOGY *child=
                        &kernel_xhci_hub_children[
                            kernel_xhci_hub_child_count++];
                    child->RouteString=route;
                    child->RootPort=topology->RootPort;
                    child->ParentSlot=slot_id;
                    child->ParentPort=(u8)port;
                    child->Speed=child_speed;
                    child->Depth=(u8)(topology->Depth+1u);
                    child->TtHubSlot=topology->TtHubSlot;
                    child->TtPort=topology->TtPort;
                    if(topology->Speed==3u&&child_speed<3u) {
                        child->TtHubSlot=slot_id;
                        child->TtPort=(u8)port;
                    }
                }
            }
        }
    }
    kernel_write("bob64 kernel: USB 2 hub configured ports=");
    kernel_hex64(hub.PortCount);kernel_write(" connected=");
    kernel_hex64(connected);kernel_write(" reset=");kernel_hex64(reset);
    kernel_write("\r\n");
    *control_index_out=control_index;
    return 0;
}

static int kernel_xhci_configure_hub_status_endpoint(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u8 slot_id,
        const KERNEL_XHCI_TOPOLOGY *topology,
        const BOB64_USB_HUB_INTERFACE *hub_interface,u8 port_count,
        volatile u8 *device_context_bytes,u32 context_size,
        volatile KERNEL_XHCI_TRB *control_ring,u64 control_ring_physical,
        volatile u8 *control_buffer,u64 control_buffer_physical,
        u32 control_next_index) {
    KERNEL_XHCI_HUB_DEVICE *hub=0;
    u64 ring_physical,report_physical,input_physical;
    volatile u8 *ring_bytes,*report_bytes,*input_bytes;
    u32 dci,report_length,interval,command_index,doorbell_offset;
    KERNEL_XHCI_TRB completion;
    if(!mmio||!capabilities||!slot_id||!topology||!hub_interface||
       !port_count||!device_context_bytes||!context_size||!control_ring||
       !control_buffer||!control_buffer_physical||
       !(hub_interface->EndpointAddress&0x80u)||
       (hub_interface->EndpointAttributes&3u)!=3u)return -1;
    dci=((hub_interface->EndpointAddress&0x0fu)<<1)|1u;
    report_length=((u32)port_count+1u+7u)/8u;
    if(dci<2u||dci>31u||!report_length||report_length>16u||
       report_length>hub_interface->EndpointMaxPacketSize)return -2;
    for(u32 i=0;i<kernel_xhci_hub_device_count;i++)
        if(!kernel_xhci_hub_devices[i].Active) { hub=&kernel_xhci_hub_devices[i];break; }
    if(!hub&&kernel_xhci_hub_device_count<KERNEL_XHCI_HID_DEVICE_LIMIT)
        hub=&kernel_xhci_hub_devices[kernel_xhci_hub_device_count++];
    if(!hub)return -3;
    if(kernel_xhci_dma_alloc(1,&ring_physical,&ring_bytes)||
       kernel_xhci_dma_alloc(1,&report_physical,&report_bytes)||
       kernel_xhci_dma_alloc(1,&input_physical,&input_bytes))return -4;
    volatile KERNEL_XHCI_TRB *ring=
        (volatile KERNEL_XHCI_TRB *)(volatile void *)ring_bytes;
    ring[BOB64_XHCI_RING_TRBS-1].Parameter=ring_physical;
    ring[BOB64_XHCI_RING_TRBS-1].Status=0;
    ring[BOB64_XHCI_RING_TRBS-1].Control=
        (BOB64_XHCI_TRB_TYPE_LINK<<BOB64_XHCI_TRB_TYPE_SHIFT)|3u;
    volatile u32 *input=(volatile u32 *)(volatile void *)input_bytes;
    volatile u32 *slot=(volatile u32 *)(volatile void *)(input_bytes+context_size);
    volatile u32 *endpoint=(volatile u32 *)(volatile void *)(input_bytes+
                                                  ((dci+1u)*context_size));
    volatile u32 *live_slot=(volatile u32 *)(volatile void *)device_context_bytes;
    input[1]=1u|(1u<<dci);
    for(u32 word=0;word<context_size/sizeof(u32);word++)slot[word]=live_slot[word];
    slot[0]=(slot[0]&~(31u<<27))|(dci<<27);
    if(topology->Speed>=3u)
        interval=hub_interface->EndpointInterval?
            (u32)hub_interface->EndpointInterval-1u:0u;
    else {
        u32 period=hub_interface->EndpointInterval?
            hub_interface->EndpointInterval:1u;
        interval=3u;
        while(period>1u&&interval<15u) { period=(period+1u)>>1;interval++; }
    }
    endpoint[0]=interval<<16;
    endpoint[1]=(3u<<1)|(7u<<3)|
        ((u32)hub_interface->EndpointMaxPacketSize<<16);
    endpoint[2]=(u32)(ring_physical|1u);
    endpoint[3]=(u32)(ring_physical>>32);
    endpoint[4]=report_length|((u32)hub_interface->EndpointMaxPacketSize<<16);
    command_index=kernel_xhci_rings.CommandIndex;
    if(command_index>=BOB64_XHCI_RING_TRBS-1)return -5;
    volatile KERNEL_XHCI_TRB *command=
        &kernel_xhci_rings.CommandRing[command_index];
    command->Parameter=input_physical;command->Status=0;
    command->Control=(12u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                     ((u32)slot_id<<24)|1u;
    __asm__ volatile("sfence" ::: "memory");
    doorbell_offset=capabilities->DoorbellOffset-mmio->CapabilityLength;
    if(kernel_xhci_write32(mmio,doorbell_offset,0))return -6;
    int result=kernel_xhci_wait_event(mmio,capabilities,
        BOB64_XHCI_TRB_TYPE_COMMAND_COMPLETION,10000000u,&completion);
    if(result)return -7+result;
    if(completion.Parameter!=kernel_xhci_rings.CommandPhysical+
           command_index*sizeof(KERNEL_XHCI_TRB)||
       (completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=slot_id)return -8;
    kernel_xhci_rings.CommandIndex++;
    hub->Ring=ring;hub->RingPhysical=ring_physical;
    hub->Report=report_bytes;hub->ReportPhysical=report_physical;
    hub->NextIndex=0;hub->PendingIndex=0;hub->Slot=slot_id;hub->Dci=(u8)dci;
    hub->ReportLength=(u8)report_length;hub->Cycle=1;hub->Pending=0;
    hub->TransferErrors=0;hub->RootPort=topology->RootPort;
    hub->Topology=*topology;hub->LoggedChange=0;hub->Active=1;
    hub->PortCount=port_count;
    hub->ControlRing=control_ring;
    hub->ControlRingPhysical=control_ring_physical;
    hub->ControlBuffer=control_buffer;
    hub->ControlBufferPhysical=control_buffer_physical;
    hub->ControlNextIndex=control_next_index;
    hub->ControlCycle=1;
    kernel_write("bob64 kernel: USB hub status endpoint configured; monitoring port changes\r\n");
    return kernel_xhci_hub_status_submit(hub);
}

static int kernel_xhci_configure_storage_endpoints(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u8 slot_id,
        const KERNEL_XHCI_TOPOLOGY *topology,
        const BOB64_USB_MASS_STORAGE *storage,
        volatile u8 *device_context_bytes,u32 context_size) {
    KERNEL_XHCI_STORAGE_DEVICE *device=0;
    u64 input_physical,in_ring_physical,out_ring_physical,scratch_physical;
    volatile u8 *input_bytes,*in_ring_bytes,*out_ring_bytes,*scratch_bytes;
    KERNEL_XHCI_TRB completion;
    u8 in_dci,out_dci;
    u32 command_index,doorbell_offset;
    if(!mmio||!capabilities||!slot_id||!topology||!storage||
       !device_context_bytes||(context_size!=32u&&context_size!=64u)||
       !(storage->BulkInEndpoint&0x80u)||
       (storage->BulkOutEndpoint&0x80u)||
       !storage->BulkInMaxPacketSize||!storage->BulkOutMaxPacketSize)
        return -1;
    in_dci=(u8)(((storage->BulkInEndpoint&0x0fu)<<1)|1u);
    out_dci=(u8)((storage->BulkOutEndpoint&0x0fu)<<1);
    if(in_dci<2u||out_dci<2u||in_dci>31u||out_dci>31u||in_dci==out_dci)
        return -2;
    for(u32 i=0;i<kernel_xhci_storage_device_count;i++)
        if(!kernel_xhci_storage_devices[i].Active) {
            device=&kernel_xhci_storage_devices[i];break;
        }
    if(!device&&kernel_xhci_storage_device_count<
                KERNEL_XHCI_STORAGE_DEVICE_LIMIT)
        device=&kernel_xhci_storage_devices[kernel_xhci_storage_device_count++];
    if(!device)return -3;
    if(kernel_xhci_dma_alloc(1,&in_ring_physical,&in_ring_bytes)||
       kernel_xhci_dma_alloc(1,&out_ring_physical,&out_ring_bytes)||
       kernel_xhci_dma_alloc(1,&input_physical,&input_bytes)||
       kernel_xhci_dma_alloc(2,&scratch_physical,&scratch_bytes))return -4;
    volatile KERNEL_XHCI_TRB *in_ring=
        (volatile KERNEL_XHCI_TRB *)(volatile void *)in_ring_bytes;
    volatile KERNEL_XHCI_TRB *out_ring=
        (volatile KERNEL_XHCI_TRB *)(volatile void *)out_ring_bytes;
    in_ring[BOB64_XHCI_RING_TRBS-1].Parameter=in_ring_physical;
    in_ring[BOB64_XHCI_RING_TRBS-1].Control=
        (BOB64_XHCI_TRB_TYPE_LINK<<BOB64_XHCI_TRB_TYPE_SHIFT)|3u;
    out_ring[BOB64_XHCI_RING_TRBS-1].Parameter=out_ring_physical;
    out_ring[BOB64_XHCI_RING_TRBS-1].Control=
        (BOB64_XHCI_TRB_TYPE_LINK<<BOB64_XHCI_TRB_TYPE_SHIFT)|3u;
    volatile u32 *input=(volatile u32 *)(volatile void *)input_bytes;
    volatile u32 *slot=(volatile u32 *)(volatile void *)(input_bytes+
                                                         context_size);
    volatile u32 *live_slot=(volatile u32 *)(volatile void *)device_context_bytes;
    volatile u32 *in_endpoint=(volatile u32 *)(volatile void *)(input_bytes+
                                                   (in_dci+1u)*context_size);
    volatile u32 *out_endpoint=(volatile u32 *)(volatile void *)(input_bytes+
                                                   (out_dci+1u)*context_size);
    input[1]=1u|(1u<<in_dci)|(1u<<out_dci);
    for(u32 word=0;word<context_size/sizeof(u32);word++)
        slot[word]=live_slot[word];
    u32 max_dci=in_dci>out_dci?in_dci:out_dci;
    slot[0]=(slot[0]&~(31u<<27))|(max_dci<<27);
    in_endpoint[1]=(3u<<1)|(6u<<3)|
        ((u32)storage->BulkInMaxPacketSize<<16);
    in_endpoint[2]=(u32)(in_ring_physical|1u);
    in_endpoint[3]=(u32)(in_ring_physical>>32);
    in_endpoint[4]=1024u;
    out_endpoint[1]=(3u<<1)|(2u<<3)|
        ((u32)storage->BulkOutMaxPacketSize<<16);
    out_endpoint[2]=(u32)(out_ring_physical|1u);
    out_endpoint[3]=(u32)(out_ring_physical>>32);
    out_endpoint[4]=1024u;
    command_index=kernel_xhci_rings.CommandIndex;
    if(command_index>=BOB64_XHCI_RING_TRBS-1)return -5;
    volatile KERNEL_XHCI_TRB *command=
        &kernel_xhci_rings.CommandRing[command_index];
    command->Parameter=input_physical;command->Status=0;
    command->Control=(12u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                     ((u32)slot_id<<24)|1u;
    __asm__ volatile("sfence" ::: "memory");
    doorbell_offset=capabilities->DoorbellOffset-mmio->CapabilityLength;
    if(kernel_xhci_write32(mmio,doorbell_offset,0))return -6;
    int result=kernel_xhci_wait_event(mmio,capabilities,
        BOB64_XHCI_TRB_TYPE_COMMAND_COMPLETION,10000000u,&completion);
    if(result)return -7+result;
    if(completion.Parameter!=kernel_xhci_rings.CommandPhysical+
           command_index*sizeof(KERNEL_XHCI_TRB)||
       (completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=slot_id)return -8;
    kernel_xhci_rings.CommandIndex++;
    device->BulkInRing=in_ring;device->BulkOutRing=out_ring;
    device->BulkInRingPhysical=in_ring_physical;
    device->BulkOutRingPhysical=out_ring_physical;
    device->BulkInMaxPacketSize=storage->BulkInMaxPacketSize;
    device->BulkOutMaxPacketSize=storage->BulkOutMaxPacketSize;
    device->Slot=slot_id;device->BulkInDci=in_dci;device->BulkOutDci=out_dci;
    device->BulkInNext=0;device->BulkOutNext=0;
    device->BulkInCycle=1;device->BulkOutCycle=1;device->NextTag=0;
    device->Scratch=scratch_bytes;device->ScratchPhysical=scratch_physical;
    device->BlockSize=0;device->BlockCount=0;device->HasDataPartition=0;
    device->Topology=*topology;device->Active=1;
    kernel_write("bob64 kernel: USB mass-storage bulk endpoints configured slot=");
    kernel_hex64(slot_id);kernel_write(" in=0x");
    kernel_hex64(storage->BulkInEndpoint);kernel_write(" out=0x");
    kernel_hex64(storage->BulkOutEndpoint);kernel_write(" packet=");
    kernel_hex64(storage->BulkInMaxPacketSize);kernel_write("\r\n");
    int probe_result=kernel_xhci_storage_probe(device);
    if(probe_result) {
        kernel_write("bob64 kernel: USB MSC command probe failed result=");
        kernel_hex64((u64)(s64)probe_result);kernel_write("\r\n");
    }
    return 0;
}

static int kernel_xhci_storage_bulk(KERNEL_XHCI_STORAGE_DEVICE *device,
        int input,u64 buffer_physical,u32 length,u32 *actual_length) {
    volatile KERNEL_XHCI_TRB *ring;
    u64 ring_physical;
    u32 *next;
    u8 *cycle;
    u8 dci;
    if(!device||!device->Active||!length||length>BOB64_PAGE_SIZE||
       !actual_length)return -1;
    if(input) {
        ring=device->BulkInRing;ring_physical=device->BulkInRingPhysical;
        next=&device->BulkInNext;cycle=&device->BulkInCycle;dci=device->BulkInDci;
    } else {
        ring=device->BulkOutRing;ring_physical=device->BulkOutRingPhysical;
        next=&device->BulkOutNext;cycle=&device->BulkOutCycle;dci=device->BulkOutDci;
    }
    if(!ring||!ring_physical||*next>=BOB64_XHCI_RING_TRBS)return -2;
    if(*next==BOB64_XHCI_RING_TRBS-1u) {
        volatile KERNEL_XHCI_TRB *link=&ring[*next];
        link->Parameter=ring_physical;link->Status=0;
        link->Control=(BOB64_XHCI_TRB_TYPE_LINK<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                      2u|*cycle;
        *next=0;*cycle^=1u;
    }
    u32 index=(*next)++;
    volatile KERNEL_XHCI_TRB *transfer=&ring[index];
    transfer->Parameter=buffer_physical;
    transfer->Status=length;
    transfer->Control=(1u<<BOB64_XHCI_TRB_TYPE_SHIFT)|(1u<<5)|*cycle;
    __asm__ volatile("sfence" ::: "memory");
    u32 doorbell=kernel_xhci_live_capabilities.DoorbellOffset-
        kernel_xhci_live_mmio.CapabilityLength+(u32)device->Slot*4u;
    if(kernel_xhci_write32(&kernel_xhci_live_mmio,doorbell,dci))return -3;
    KERNEL_XHCI_TRB completion;
    int result=kernel_xhci_wait_transfer(&kernel_xhci_live_mmio,
        &kernel_xhci_live_capabilities,device->Slot,dci,
        ring_physical+(u64)index*sizeof(KERNEL_XHCI_TRB),10000000u,
        &completion);
    if(result)return -4+result;
    u32 code=completion.Status>>24;
    u32 residual=completion.Status&0x00ffffffu;
    if((code!=BOB64_XHCI_TRB_COMPLETION_SUCCESS&&code!=13u)||
       residual>length)return -8;
    *actual_length=length-residual;
    return 0;
}

static int kernel_xhci_storage_command_internal(
        KERNEL_XHCI_STORAGE_DEVICE *device,
        const u8 *cdb,u8 cdb_length,int device_to_host,u32 data_length,
        void *data,u32 *actual_data,int allow_check_condition_recovery);

static int kernel_xhci_storage_command_internal(
        KERNEL_XHCI_STORAGE_DEVICE *device,
        const u8 *cdb,u8 cdb_length,int device_to_host,u32 data_length,
        void *data,u32 *actual_data,int allow_check_condition_recovery) {
    if(!device||!device->Active||!device->Scratch||!device->ScratchPhysical||
       !cdb||!data||!actual_data||data_length>BOB64_PAGE_SIZE)return -1;
    volatile u8 *scratch=device->Scratch;
    u32 tag=++device->NextTag;
    if(!tag)tag=++device->NextTag;
    if(bob64_usb_msc_build_cbw((void *)(uintptr_t)scratch,
          BOB64_USB_MSC_CBW_SIZE,tag,data_length,device_to_host,0,cdb,cdb_length))
        return -2;
    u32 actual=0,data_actual=0;
    int result=kernel_xhci_storage_bulk(device,0,device->ScratchPhysical,
        BOB64_USB_MSC_CBW_SIZE,&actual);
    if(result||actual!=BOB64_USB_MSC_CBW_SIZE) {
        kernel_write("bob64 kernel: USB MSC CBW transfer failed result=");
        kernel_hex64((u64)(s64)result);kernel_write(" bytes=");
        kernel_hex64(actual);kernel_write("\r\n");
        return -3;
    }
    if(data_length) {
        if(!device_to_host)
            for(u32 i=0;i<data_length;i++)scratch[i]=((const u8 *)data)[i];
        actual=0;
#ifdef BOB64_STORAGE_ACTIVE_DISCONNECT_TEST
        if(device_to_host&&(cdb[0]==0x28u||cdb[0]==0x88u)&&
           data_length==device->BlockSize)
            device->DisconnectTestReadPending=1;
#endif
        result=kernel_xhci_storage_bulk(device,device_to_host,
            device->ScratchPhysical,data_length,&actual);
#ifdef BOB64_STORAGE_ACTIVE_DISCONNECT_TEST
        device->DisconnectTestReadPending=0;
#endif
        if(result) {
            kernel_write("bob64 kernel: USB MSC data transfer failed result=");
            kernel_hex64((u64)(s64)result);kernel_write(" input=");
            kernel_hex64((u64)(device_to_host!=0));kernel_write("\r\n");
            return -4;
        }
        data_actual=actual;
    }
    actual=0;
    result=kernel_xhci_storage_bulk(device,1,
        device->ScratchPhysical+BOB64_PAGE_SIZE,BOB64_USB_MSC_CSW_SIZE,&actual);
    if(result||actual!=BOB64_USB_MSC_CSW_SIZE) {
        kernel_write("bob64 kernel: USB MSC CSW transfer failed result=");
        kernel_hex64((u64)(s64)result);kernel_write(" bytes=");
        kernel_hex64(actual);kernel_write("\r\n");
        return -5;
    }
    u32 residue=0;
    u8 status=0xff;
    if(bob64_usb_msc_parse_csw((const void *)(uintptr_t)
          (scratch+BOB64_PAGE_SIZE),BOB64_USB_MSC_CSW_SIZE,tag,data_length,
          &residue,&status)) {
        kernel_write("bob64 kernel: USB MSC CSW validation failed tag=");
        kernel_hex64(tag);kernel_write(" status=");kernel_hex64(status);
        kernel_write(" residue=");kernel_hex64(residue);kernel_write("\r\n");
        return -6;
    }
    if(status!=0) {
        kernel_write("bob64 kernel: USB MSC SCSI command status=");
        kernel_hex64(status);kernel_write(" opcode=");
        kernel_hex64(cdb[0]);kernel_write(" residue=");
        kernel_hex64(residue);kernel_write("\r\n");
        if(status==1u&&allow_check_condition_recovery&&cdb[0]!=0x03u) {
            u8 sense_cdb[6]={0x03,0,0,0,18,0};
            u8 sense[18]={0};
            u32 sense_actual=0;
            int sense_result=kernel_xhci_storage_command_internal(device,
                sense_cdb,sizeof(sense_cdb),1,sizeof(sense),sense,
                &sense_actual,0);
            if(sense_result||sense_actual<14u) {
                kernel_write("bob64 kernel: USB MSC REQUEST SENSE failed result=");
                kernel_hex64((u64)(s64)sense_result);kernel_write(" bytes=");
                kernel_hex64(sense_actual);kernel_write("\r\n");
                return -6;
            }
            kernel_write("bob64 kernel: USB MSC sense key=");
            kernel_hex64(sense[2]&0x0fu);kernel_write(" asc=");
            kernel_hex64(sense[12]);kernel_write(" ascq=");
            kernel_hex64(sense[13]);kernel_write("; retrying once\r\n");
            return kernel_xhci_storage_command_internal(device,cdb,cdb_length,
                device_to_host,data_length,data,actual_data,0);
        }
        return -6;
    }
    if(data_length&&data_actual+residue!=data_length) {
        kernel_write("bob64 kernel: USB MSC data residue mismatch actual=");
        kernel_hex64(data_actual);kernel_write(" residue=");
        kernel_hex64(residue);kernel_write(" expected=");
        kernel_hex64(data_length);kernel_write("\r\n");
        return -7;
    }
    if(data_length&&device_to_host) {
        for(u32 i=0;i<data_actual;i++)((u8 *)data)[i]=scratch[i];
    }
    *actual_data=data_actual;
    return 0;
}

static int kernel_xhci_storage_command(KERNEL_XHCI_STORAGE_DEVICE *device,
        const u8 *cdb,u8 cdb_length,int device_to_host,u32 data_length,
        void *data,u32 *actual_data) {
    return kernel_xhci_storage_command_internal(device,cdb,cdb_length,
        device_to_host,data_length,data,actual_data,1);
}

static int kernel_xhci_storage_read_blocks_raw(KERNEL_XHCI_STORAGE_DEVICE *device,
        u64 lba,u32 block_count,void *buffer) {
    u8 cdb[16],cdb_length=0;
    u32 length,actual=0;
    if(!device||!device->BlockSize||!block_count||!buffer||
       (u64)block_count*device->BlockSize>BOB64_PAGE_SIZE||
       lba>=device->BlockCount||block_count>device->BlockCount-lba||
       bob64_scsi_build_read_write_cdb(lba,block_count,0,cdb,&cdb_length))
        return -1;
    length=(u32)block_count*device->BlockSize;
    int result=kernel_xhci_storage_command(device,cdb,cdb_length,1,
                                            length,buffer,&actual);
    return result?result:(actual==length?0:-2);
}

static int kernel_xhci_storage_write_blocks_raw(KERNEL_XHCI_STORAGE_DEVICE *device,
        u64 lba,u32 block_count,void *buffer) {
    u8 cdb[16],cdb_length=0;
    u32 length,actual=0;
    if(!device||!device->BlockSize||!block_count||!buffer||
       (u64)block_count*device->BlockSize>BOB64_PAGE_SIZE||
       lba>=device->BlockCount||block_count>device->BlockCount-lba||
       bob64_scsi_build_read_write_cdb(lba,block_count,1,cdb,&cdb_length))
        return -1;
    length=(u32)block_count*device->BlockSize;
    int result=kernel_xhci_storage_command(device,cdb,cdb_length,0,
                                            length,buffer,&actual);
    return result?result:(actual==length?0:-2);
}

static int kernel_xhci_storage_sync_raw(KERNEL_XHCI_STORAGE_DEVICE *device) {
    u8 cdb[10]={0x35};
    u8 no_data=0;
    u32 actual=0;
    if(!device||!device->Active)return -1;
    return kernel_xhci_storage_command(device,cdb,sizeof(cdb),1,0,
                                        &no_data,&actual);
}

static int kernel_xhci_storage_block_read(void *context,u64 lba,u32 count,
                                           void *buffer) {
    KERNEL_XHCI_STORAGE_DEVICE *device=(KERNEL_XHCI_STORAGE_DEVICE *)context;
    return kernel_xhci_storage_read_blocks_raw(device,lba,count,buffer);
}

static int kernel_xhci_storage_block_write(void *context,u64 lba,u32 count,
                                            const void *buffer) {
    KERNEL_XHCI_STORAGE_DEVICE *device=(KERNEL_XHCI_STORAGE_DEVICE *)context;
    return kernel_xhci_storage_write_blocks_raw(device,lba,count,(void *)buffer);
}

static int kernel_xhci_storage_block_flush(void *context) {
    return kernel_xhci_storage_sync_raw(
        (KERNEL_XHCI_STORAGE_DEVICE *)context);
}

static int kernel_xhci_storage_probe(KERNEL_XHCI_STORAGE_DEVICE *device) {
    u8 inquiry_cdb[6]={0x12,0,0,0,36,0};
    u8 capacity_cdb[16]={0x25};
    u8 inquiry[36],capacity[32];
    u64 block_count=0;
    u32 block_size=0;
    int needs_capacity16=0;
    u32 actual=0;
    if(!device||!device->Active||device->BlockSize)return -1;
    int result=kernel_xhci_storage_command(device,inquiry_cdb,sizeof(inquiry_cdb),
        1,sizeof(inquiry),inquiry,&actual);
    if(result||actual!=sizeof(inquiry))return -2;
    result=kernel_xhci_storage_command(device,capacity_cdb,10,1,8,capacity,
                                       &actual);
    if(result||actual!=8)return -3;
    if(bob64_scsi_parse_read_capacity10(capacity,8,&block_count,&block_size,
                                         &needs_capacity16))return -4;
    if(needs_capacity16) {
        bob64_scsi_build_read_capacity16_cdb(capacity_cdb);
        result=kernel_xhci_storage_command(device,capacity_cdb,16,1,32,
                                            capacity,&actual);
        if(result||actual!=32||bob64_scsi_parse_read_capacity16(capacity,32,
                                      &block_count,&block_size))return -3;
    }
    if(!block_size||block_size>BOB64_PAGE_SIZE)return -4;
    device->BlockSize=block_size;device->BlockCount=block_count;
#ifdef BOB64_STORAGE_WRITE_TEST
    int initial_read_only=0;
#else
    int initial_read_only=1;
#endif
    if(bob64_block_device_init(&device->BlockDevice,device,device->BlockCount,
          block_size,initial_read_only,kernel_xhci_storage_block_read,
          kernel_xhci_storage_block_write,kernel_xhci_storage_block_flush))
        return -4;
    kernel_write("bob64 kernel: USB block device registered sectors=");
    kernel_hex64(device->BlockCount);kernel_write(" sector-bytes=");
    kernel_hex64(device->BlockSize);kernel_write(" access=");
    kernel_write(device->BlockDevice.ReadOnly?"read-only\r\n":"read-write\r\n");
    BOB64_PARTITION partition;
    int partition_result=bob64_gpt_find_partition(&device->BlockDevice,
        bob64_gpt_bob_data_type_guid,&partition);
    if(!partition_result) {
        device->DataPartition=partition;device->HasDataPartition=1;
        if(bob64_block_device_init(&device->BlockDevice,device,
              device->BlockCount,block_size,0,kernel_xhci_storage_block_read,
              kernel_xhci_storage_block_write,kernel_xhci_storage_block_flush))
            return -4;
        kernel_write("bob64 kernel: dedicated data partition enables writes\r\n");
        kernel_write("bob64 kernel: GPT bob64 data partition found first-lba=");
        kernel_hex64(partition.FirstLba);kernel_write(" blocks=");
        kernel_hex64(partition.BlockCount);kernel_write("\r\n");
    } else if(partition_result==1) {
        kernel_write("bob64 kernel: GPT bob64 data partition absent\r\n");
    } else {
        kernel_write("bob64 kernel: GPT data partition table invalid result=");
        kernel_hex64((u64)(s64)partition_result);kernel_write("\r\n");
    }
    if(bob64_block_read(&device->BlockDevice,device->BlockCount,1,
          (void *)(uintptr_t)device->Scratch)!=-1)return -5;
    result=bob64_block_read(&device->BlockDevice,0,1,
        (void *)(uintptr_t)device->Scratch);
    if(result)return -6;
    actual=block_size;
    kernel_write("bob64 kernel: USB MSC READ CAPACITY passed blocks=");
    kernel_hex64(device->BlockCount);kernel_write(" sector-bytes=");
    kernel_hex64(device->BlockSize);kernel_write("\r\n");
    kernel_write("bob64 kernel: USB MSC sector-read passed lba=0 bytes=");
    kernel_hex64(actual);kernel_write("\r\n");
#ifdef BOB64_STORAGE_LBA64_TEST
    if(device->BlockCount<=0x100000000ULL)return -13;
    result=bob64_block_read(&device->BlockDevice,0x100000000ULL,1,
        (void *)(uintptr_t)device->Scratch);
    if(result)return -14;
    for(u32 i=0;i<block_size;i++)
        if(device->Scratch[i]!=(u8)(i*53u+0xa7u))return -15;
    kernel_write("bob64 kernel: USB MSC high-LBA READ(16) passed lba=0000000100000000\r\n");
#endif
#ifdef BOB64_STORAGE_WRITE_TEST
    if(device->BlockCount<2u||block_size>BOB64_PAGE_SIZE)return -7;
    volatile u8 *write_buffer=device->Scratch+BOB64_PAGE_SIZE;
    volatile u8 *read_buffer=device->Scratch;
    for(u32 i=0;i<block_size;i++)write_buffer[i]=(u8)(i*37u+0x5au);
    result=bob64_block_write(&device->BlockDevice,1,1,
        (void *)(uintptr_t)write_buffer);
    if(result)return -8;
    result=bob64_block_flush(&device->BlockDevice);
    if(result)return -12;
    if(bob64_block_write(&device->BlockDevice,device->BlockCount,1,
          (void *)(uintptr_t)write_buffer)!=-1)return -11;
    result=bob64_block_read(&device->BlockDevice,1,1,
        (void *)(uintptr_t)read_buffer);
    if(result)return -9;
    for(u32 i=0;i<block_size;i++)
        if(read_buffer[i]!=(u8)(i*37u+0x5au))return -10;
    kernel_write("bob64 kernel: USB MSC sector-write/flush/readback passed lba=1 bytes=");
    kernel_hex64(block_size);kernel_write("\r\n");
#endif
    return 0;
}

static u32 kernel_xhci_hub_control_reserve(KERNEL_XHCI_HUB_DEVICE *hub,
                                            u32 trb_count) {
    if(!hub||!hub->ControlRing||trb_count>3u||
       hub->ControlNextIndex>=BOB64_XHCI_RING_TRBS)return 0xffffffffu;
    if(hub->ControlNextIndex+trb_count>BOB64_XHCI_RING_TRBS-1u) {
        volatile KERNEL_XHCI_TRB *link=
            &hub->ControlRing[hub->ControlNextIndex];
        link->Parameter=hub->ControlRingPhysical;link->Status=0;
        link->Control=(BOB64_XHCI_TRB_TYPE_LINK<<BOB64_XHCI_TRB_TYPE_SHIFT)|
            2u|hub->ControlCycle;
        hub->ControlNextIndex=0;
        hub->ControlCycle^=1u;
    }
    u32 index=hub->ControlNextIndex;
    hub->ControlNextIndex+=trb_count;
    return index;
}

static int kernel_xhci_hub_control_in(KERNEL_XHCI_HUB_DEVICE *hub,
        u8 request_type,u8 request,u16 value,u16 index,u16 length) {
    KERNEL_XHCI_TRB completion;
    if(!hub||!hub->Active||!length||length>BOB64_PAGE_SIZE)return -1;
    u32 ring_index=kernel_xhci_hub_control_reserve(hub,3u);
    if(ring_index==0xffffffffu)return -2;
    u32 cycle=hub->ControlCycle;
    volatile KERNEL_XHCI_TRB *ring=hub->ControlRing;
    ring[ring_index].Parameter=bob64_usb_control_setup(request_type,request,
                                                       value,index,length);
    ring[ring_index].Status=8;
    ring[ring_index].Control=(2u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
        (3u<<16)|(1u<<6)|(1u<<4)|cycle;
    ring[ring_index+1].Parameter=hub->ControlBufferPhysical;
    ring[ring_index+1].Status=length;
    ring[ring_index+1].Control=(3u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
        (1u<<16)|(1u<<4)|(1u<<2)|cycle;
    ring[ring_index+2].Parameter=0;ring[ring_index+2].Status=0;
    ring[ring_index+2].Control=(4u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                               (1u<<5)|cycle;
    __asm__ volatile("sfence" ::: "memory");
    u32 doorbell=kernel_xhci_live_capabilities.DoorbellOffset-
        kernel_xhci_live_mmio.CapabilityLength+(u32)hub->Slot*4u;
    if(kernel_xhci_write32(&kernel_xhci_live_mmio,doorbell,1u))return -3;
    int result=kernel_xhci_wait_event(&kernel_xhci_live_mmio,
        &kernel_xhci_live_capabilities,BOB64_XHCI_TRB_TYPE_TRANSFER_EVENT,
        10000000u,&completion);
    if(result)return -4+result;
    if((completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=hub->Slot||
       ((completion.Control>>16)&0x1fu)!=1u||
       completion.Parameter!=hub->ControlRingPhysical+
               (ring_index+2u)*sizeof(KERNEL_XHCI_TRB))return -8;
    return 0;
}

static int kernel_xhci_hub_control_no_data(KERNEL_XHCI_HUB_DEVICE *hub,
        u8 request_type,u8 request,u16 value,u16 index) {
    KERNEL_XHCI_TRB completion;
    if(!hub||!hub->Active)return -1;
    u32 ring_index=kernel_xhci_hub_control_reserve(hub,2u);
    if(ring_index==0xffffffffu)return -2;
    u32 cycle=hub->ControlCycle;
    volatile KERNEL_XHCI_TRB *ring=hub->ControlRing;
    ring[ring_index].Parameter=bob64_usb_control_setup(request_type,request,
                                                       value,index,0);
    ring[ring_index].Status=8;
    ring[ring_index].Control=(2u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
        (1u<<6)|(1u<<4)|cycle;
    ring[ring_index+1].Parameter=0;ring[ring_index+1].Status=0;
    ring[ring_index+1].Control=(4u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
        (1u<<16)|(1u<<5)|cycle;
    __asm__ volatile("sfence" ::: "memory");
    u32 doorbell=kernel_xhci_live_capabilities.DoorbellOffset-
        kernel_xhci_live_mmio.CapabilityLength+(u32)hub->Slot*4u;
    if(kernel_xhci_write32(&kernel_xhci_live_mmio,doorbell,1u))return -3;
    int result=kernel_xhci_wait_event(&kernel_xhci_live_mmio,
        &kernel_xhci_live_capabilities,BOB64_XHCI_TRB_TYPE_TRANSFER_EVENT,
        10000000u,&completion);
    if(result)return -4+result;
    if((completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=hub->Slot||
       ((completion.Control>>16)&0x1fu)!=1u||
       completion.Parameter!=hub->ControlRingPhysical+
               (ring_index+1u)*sizeof(KERNEL_XHCI_TRB))return -8;
    return 0;
}

static int kernel_xhci_address_and_read_descriptor(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u8 slot_id,
        const KERNEL_XHCI_TOPOLOGY *topology) {
    u64 device_context_physical,input_context_physical;
    u64 transfer_ring_physical,descriptor_physical;
    volatile u8 *device_context_bytes,*input_context_bytes;
    volatile u8 *transfer_ring_bytes,*descriptor_bytes;
    KERNEL_XHCI_HID_DEVICE *hid_device;
    u32 context_size=(capabilities->HccParameters1&(1u<<2))?64u:32u;
    u32 command_index=kernel_xhci_rings.CommandIndex;
    u32 doorbell_offset;
    u16 max_packet;
    u8 hub_port_count=0;
    u32 hub_control_index=0;
    KERNEL_XHCI_TRB completion;
    if(!mmio||!capabilities||!slot_id||!topology||!topology->RootPort||
       topology->Speed<1||topology->Speed>5||
       !kernel_xhci_rings.DeviceContextArray||
       command_index>=BOB64_XHCI_RING_TRBS-1)return -1;
    if(topology->Speed==1)max_packet=8;
    else if(topology->Speed==2)max_packet=8;
    else if(topology->Speed==3)max_packet=64;
    else max_packet=512;
    if(kernel_xhci_dma_alloc(1,&device_context_physical,
                             &device_context_bytes)||
       kernel_xhci_dma_alloc(1,&input_context_physical,
                             &input_context_bytes)||
       kernel_xhci_dma_alloc(1,&transfer_ring_physical,
                             &transfer_ring_bytes)||
       kernel_xhci_dma_alloc(1,&descriptor_physical,&descriptor_bytes))return -2;
    kernel_xhci_rings.DeviceContextArray[slot_id]=device_context_physical;
    volatile u32 *input=(volatile u32 *)(volatile void *)input_context_bytes;
    volatile u32 *slot=(volatile u32 *)(volatile void *)(input_context_bytes+
                                                         context_size);
    volatile u32 *endpoint0=(volatile u32 *)(volatile void *)(input_context_bytes+
                                                      2u*context_size);
    input[1]=(1u<<0)|(1u<<1); /* Add the Slot and Endpoint 0 contexts. */
    slot[0]=(topology->RouteString&0x000fffffu)|
            ((u32)topology->Speed<<20)|(1u<<27);
    slot[1]=(u32)topology->RootPort<<16;
    if(topology->TtHubSlot&&topology->TtPort&&topology->Speed<3u) {
        slot[2]=(u32)topology->TtHubSlot|
                ((u32)topology->TtPort<<8);
    }
    endpoint0[1]=(3u<<1)|(4u<<3)|((u32)max_packet<<16);
    endpoint0[2]=(u32)(transfer_ring_physical|1u);
    endpoint0[3]=(u32)(transfer_ring_physical>>32);
    endpoint0[4]=8u;
    volatile KERNEL_XHCI_TRB *command=
        &kernel_xhci_rings.CommandRing[command_index];
    command->Parameter=input_context_physical;
    command->Status=0;
    command->Control=(11u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                     ((u32)slot_id<<24)|1u;
    __asm__ volatile("sfence" ::: "memory");
    doorbell_offset=capabilities->DoorbellOffset-mmio->CapabilityLength;
    if(kernel_xhci_write32(mmio,doorbell_offset,0))return -3;
    int result=kernel_xhci_wait_event(mmio,capabilities,
        BOB64_XHCI_TRB_TYPE_COMMAND_COMPLETION,10000000u,&completion);
    if(result)return -4+result;
    if(completion.Parameter!=kernel_xhci_rings.CommandPhysical+
                              command_index*sizeof(KERNEL_XHCI_TRB)||
       (completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=slot_id)return -5;
    kernel_xhci_rings.CommandIndex++;
    volatile KERNEL_XHCI_TRB *transfer_ring=
        (volatile KERNEL_XHCI_TRB *)(volatile void *)transfer_ring_bytes;
    transfer_ring[BOB64_XHCI_RING_TRBS-1].Parameter=transfer_ring_physical;
    transfer_ring[BOB64_XHCI_RING_TRBS-1].Status=0;
    transfer_ring[BOB64_XHCI_RING_TRBS-1].Control=
        (BOB64_XHCI_TRB_TYPE_LINK<<BOB64_XHCI_TRB_TYPE_SHIFT)|3u;
    /* GET_DESCRIPTOR(Device, 8 bytes) on the newly addressed EP0. */
    transfer_ring[0].Parameter=0x0008000001000680ULL;
    transfer_ring[0].Status=8u;
    transfer_ring[0].Control=(2u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                              (3u<<16)|(1u<<6)|(1u<<4)|1u;
    transfer_ring[1].Parameter=descriptor_physical;
    transfer_ring[1].Status=8u;
    transfer_ring[1].Control=(3u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                              (1u<<16)|(1u<<4)|(1u<<2)|1u;
    transfer_ring[2].Parameter=0;
    transfer_ring[2].Status=0;
    transfer_ring[2].Control=(4u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                              (1u<<5)|1u;
    __asm__ volatile("sfence" ::: "memory");
    u32 slot_doorbell=doorbell_offset+(u32)slot_id*4u;
    if(kernel_xhci_write32(mmio,slot_doorbell,1u))return -6;
    result=kernel_xhci_wait_event(mmio,capabilities,
                                  BOB64_XHCI_TRB_TYPE_TRANSFER_EVENT,10000000u,
                                  &completion);
    if(result)return -7+result;
    if((completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=slot_id||
       ((completion.Control>>16)&0x1fu)!=1u||
       completion.Parameter!=transfer_ring_physical+
                              2u*sizeof(KERNEL_XHCI_TRB))return -8;
    if(descriptor_bytes[0]<8||descriptor_bytes[1]!=1||
       descriptor_bytes[7]!=(topology->Speed>=4u?9u:max_packet))return -9;
    transfer_ring[3].Parameter=0x0012000001000680ULL;
    transfer_ring[3].Status=8u;
    transfer_ring[3].Control=(2u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                              (3u<<16)|(1u<<6)|(1u<<4)|1u;
    transfer_ring[4].Parameter=descriptor_physical;
    transfer_ring[4].Status=18u;
    transfer_ring[4].Control=(3u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                              (1u<<16)|(1u<<4)|(1u<<2)|1u;
    transfer_ring[5].Parameter=0;
    transfer_ring[5].Status=0;
    transfer_ring[5].Control=(4u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                              (1u<<5)|1u;
    __asm__ volatile("sfence" ::: "memory");
    if(kernel_xhci_write32(mmio,slot_doorbell,1u))return -10;
    result=kernel_xhci_wait_event(mmio,capabilities,
        BOB64_XHCI_TRB_TYPE_TRANSFER_EVENT,10000000u,&completion);
    if(result)return -11+result;
    if((completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=slot_id||
       ((completion.Control>>16)&0x1fu)!=1u||
       completion.Parameter!=transfer_ring_physical+
                              5u*sizeof(KERNEL_XHCI_TRB))return -12;
    if(descriptor_bytes[0]!=18||descriptor_bytes[1]!=1||
       descriptor_bytes[7]!=(topology->Speed>=4u?9u:max_packet))return -13;
    u16 vendor_id=(u16)(descriptor_bytes[8]|((u16)descriptor_bytes[9]<<8));
    u16 product_id=(u16)(descriptor_bytes[10]|((u16)descriptor_bytes[11]<<8));
    kernel_write("bob64 kernel: USB device addressed; descriptor type=");
    kernel_hex64(descriptor_bytes[1]);kernel_write(" EP0 packet=");
    kernel_hex64(descriptor_bytes[7]);kernel_write(" VID=0x");
    kernel_hex64(vendor_id);
    kernel_write(" PID=0x");
    kernel_hex64(product_id);
    kernel_write("\r\n");
    result=kernel_xhci_get_descriptor(mmio,capabilities,slot_id,transfer_ring,
        transfer_ring_physical,descriptor_physical,2,9,6);
    if(result)return -14+result;
    if(descriptor_bytes[0]<9||descriptor_bytes[1]!=2)return -18;
    u16 configuration_length=(u16)(descriptor_bytes[2]|((u16)descriptor_bytes[3]<<8));
    if(configuration_length<9||configuration_length>BOB64_PAGE_SIZE)return -19;
    result=kernel_xhci_get_descriptor(mmio,capabilities,slot_id,transfer_ring,
        transfer_ring_physical,descriptor_physical,2,configuration_length,9);
    if(result)return -20+result;
    BOB64_USB_HUB_INTERFACE hub_interface;
    result=bob64_usb_find_hub_interface(
        (const void *)(uintptr_t)descriptor_bytes,configuration_length,
        &hub_interface);
    if(result<0)return -21+result;
    if(!result) {
        u8 child_count_before=kernel_xhci_hub_child_count;
        result=kernel_xhci_configure_usb2_hub(mmio,capabilities,slot_id,
            topology,transfer_ring,transfer_ring_physical,descriptor_physical,
            descriptor_bytes,device_context_bytes,context_size,
            hub_interface.ConfigurationValue,&hub_port_count,
            &hub_control_index);
        if(result) {
            kernel_xhci_hub_child_count=child_count_before;
            kernel_write("bob64 kernel: USB hub initialization failed result=");
            kernel_hex64((u64)(s64)result);kernel_write("\r\n");
        } else {
            result=kernel_xhci_configure_hub_status_endpoint(mmio,capabilities,
                slot_id,topology,&hub_interface,hub_port_count,
                device_context_bytes,context_size,transfer_ring,
                transfer_ring_physical,descriptor_bytes,descriptor_physical,
                hub_control_index);
            if(result) {
                kernel_write("bob64 kernel: USB hub change monitoring unavailable result=");
                kernel_hex64((u64)(s64)result);kernel_write("\r\n");
            }
        }
        return 0;
    }
    BOB64_USB_MASS_STORAGE storage;
    result=bob64_usb_find_mass_storage(
        (const void *)(uintptr_t)descriptor_bytes,configuration_length,
        &storage);
    if(result<0)return -23+result;
    if(!result) {
        kernel_write("bob64 kernel: USB mass-storage BOT interface found config=");
        kernel_hex64(storage.ConfigurationValue);kernel_write(" interface=");
        kernel_hex64(storage.InterfaceNumber);kernel_write(" in=0x");
        kernel_hex64(storage.BulkInEndpoint);kernel_write(" out=0x");
        kernel_hex64(storage.BulkOutEndpoint);kernel_write("\r\n");
        result=kernel_xhci_control_no_data(mmio,capabilities,slot_id,
            transfer_ring,transfer_ring_physical,12,0x00,9,
            storage.ConfigurationValue,0);
        if(result) {
            kernel_write("bob64 kernel: USB storage SET_CONFIGURATION failed result=");
            kernel_hex64((u64)(s64)result);kernel_write("\r\n");
            return -24+result;
        }
        result=kernel_xhci_configure_storage_endpoints(mmio,capabilities,
            slot_id,topology,&storage,device_context_bytes,context_size);
        if(result) {
            kernel_write("bob64 kernel: USB storage endpoint setup failed result=");
            kernel_hex64((u64)(s64)result);kernel_write("\r\n");
            return -25+result;
        }
        return 0;
    }
    BOB64_USB_HID_BOOT_DEVICE hid;
    u8 endpoint_dci,hid_kind,report_length;
    result=bob64_usb_find_hid_boot_keyboard(
        (const void *)(uintptr_t)descriptor_bytes,configuration_length,&hid);
    if(result<0)return -25+result;
    if(!result) { hid_kind=1;report_length=8; }
    else {
        result=bob64_usb_find_hid_boot_mouse(
            (const void *)(uintptr_t)descriptor_bytes,configuration_length,&hid);
        if(result)return -27+result;
        hid_kind=2;
        report_length=hid.EndpointMaxPacketSize>=4?4:3;
    }
    u32 hid_device_index=kernel_xhci_hid_device_count;
    for(u32 i=0;i<kernel_xhci_hid_device_count;i++)
        if(!kernel_xhci_hid_devices[i].Active) { hid_device_index=i;break; }
    if(hid_device_index>=KERNEL_XHCI_HID_DEVICE_LIMIT)return -28;
    hid_device=&kernel_xhci_hid_devices[hid_device_index];
    endpoint_dci=(u8)(((hid.EndpointAddress&0x0fu)<<1)|
                      ((hid.EndpointAddress&0x80u)?1u:0u));
    if(endpoint_dci<2||endpoint_dci>31)return -26;
    kernel_write("bob64 kernel: USB HID boot ");
    kernel_write(hid_kind==1?"keyboard":"mouse");
    kernel_write(" endpoint found address=0x");
    kernel_hex64(hid.EndpointAddress);kernel_write(" packet=");
    kernel_hex64(hid.EndpointMaxPacketSize);kernel_write(" interval=");
    kernel_hex64(hid.EndpointInterval);kernel_write(" config=");
    kernel_hex64(hid.ConfigurationValue);kernel_write("\r\n");
    /* Select the USB configuration and ask the HID interface for boot reports. */
    result=kernel_xhci_control_no_data(mmio,capabilities,slot_id,transfer_ring,
        transfer_ring_physical,12,0x00,9,hid.ConfigurationValue,0);
    if(result) {
        kernel_write("bob64 kernel: USB SET_CONFIGURATION failed result=");
        kernel_hex64((u64)(s64)result);kernel_write("\r\n");
        return -30+result;
    }
    result=kernel_xhci_control_no_data(mmio,capabilities,slot_id,transfer_ring,
        transfer_ring_physical,14,0x21,0x0b,0,hid.InterfaceNumber);
    if(result) {
        kernel_write("bob64 kernel: USB HID SET_PROTOCOL failed result=");
        kernel_hex64((u64)(s64)result);kernel_write("\r\n");
        return -35+result;
    }

    /* Add the interrupt-IN endpoint (EP1 IN is DCI 3) to the live device. */
    u64 endpoint_ring_physical;
    volatile u8 *endpoint_ring_bytes;
    if(kernel_xhci_dma_alloc(1,&endpoint_ring_physical,&endpoint_ring_bytes))
        return -40;
    volatile KERNEL_XHCI_TRB *endpoint_ring=
        (volatile KERNEL_XHCI_TRB *)(volatile void *)endpoint_ring_bytes;
    endpoint_ring[BOB64_XHCI_RING_TRBS-1].Parameter=endpoint_ring_physical;
    endpoint_ring[BOB64_XHCI_RING_TRBS-1].Status=0;
    endpoint_ring[BOB64_XHCI_RING_TRBS-1].Control=
        (BOB64_XHCI_TRB_TYPE_LINK<<BOB64_XHCI_TRB_TYPE_SHIFT)|3u;
    if(kernel_xhci_dma_alloc(1,&input_context_physical,&input_context_bytes))
        return -41;
    input=(volatile u32 *)(volatile void *)input_context_bytes;
    slot=(volatile u32 *)(volatile void *)(input_context_bytes+context_size);
    volatile u32 *endpoint=(volatile u32 *)(volatile void *)(input_context_bytes+
                                                ((u32)endpoint_dci+1u)*context_size);
    input[1]=(1u<<0)|(1u<<endpoint_dci);
    volatile u32 *live_slot=(volatile u32 *)(volatile void *)device_context_bytes;
    for(u32 word=0;word<context_size/sizeof(u32);word++)slot[word]=live_slot[word];
    slot[0]=(slot[0]&~(31u<<27))|((u32)endpoint_dci<<27);
    u32 interval;
    if(topology->Speed>=3)interval=hid.EndpointInterval?
        (u32)hid.EndpointInterval-1u:0u;
    else {
        u32 period=hid.EndpointInterval?hid.EndpointInterval:1u;
        interval=3;
        while(period>1u&&interval<15u) { period=(period+1u)>>1;interval++; }
    }
    endpoint[0]=interval<<16;
    endpoint[1]=(3u<<1)|(7u<<3)|
        ((u32)hid.EndpointMaxPacketSize<<16);
    endpoint[2]=(u32)(endpoint_ring_physical|1u);
    endpoint[3]=(u32)(endpoint_ring_physical>>32);
    endpoint[4]=(u32)report_length|((u32)hid.EndpointMaxPacketSize<<16);
    command_index=kernel_xhci_rings.CommandIndex;
    if(command_index>=BOB64_XHCI_RING_TRBS-1)return -42;
    command=&kernel_xhci_rings.CommandRing[command_index];
    command->Parameter=input_context_physical;
    command->Status=0;
    command->Control=(12u<<BOB64_XHCI_TRB_TYPE_SHIFT)|
                     ((u32)slot_id<<24)|1u;
    __asm__ volatile("sfence" ::: "memory");
    doorbell_offset=capabilities->DoorbellOffset-mmio->CapabilityLength;
    if(kernel_xhci_write32(mmio,doorbell_offset,0))return -43;
    result=kernel_xhci_wait_event(mmio,capabilities,
        BOB64_XHCI_TRB_TYPE_COMMAND_COMPLETION,10000000u,&completion);
    if(result)return -44+result;
    if(completion.Parameter!=kernel_xhci_rings.CommandPhysical+
                              command_index*sizeof(KERNEL_XHCI_TRB)||
       (completion.Status>>24)!=BOB64_XHCI_TRB_COMPLETION_SUCCESS||
       (u8)(completion.Control>>24)!=slot_id)return -45;
    kernel_xhci_rings.CommandIndex++;
    u64 report_physical;
    volatile u8 *report_bytes;
    if(kernel_xhci_dma_alloc(1,&report_physical,&report_bytes))return -46;
    hid_device->Ring=endpoint_ring;
    hid_device->RingPhysical=endpoint_ring_physical;
    hid_device->Report=report_bytes;
    hid_device->ReportPhysical=report_physical;
    hid_device->Slot=slot_id;hid_device->Dci=endpoint_dci;
    hid_device->Kind=hid_kind;hid_device->ReportLength=report_length;
    hid_device->Cycle=1;hid_device->NextIndex=0;
    hid_device->Pending=0;hid_device->Active=1;
    hid_device->TransferErrors=0;
    hid_device->RootPort=topology->RootPort;
    hid_device->Topology=*topology;
    hid_device->LoggedReport=hid_device->LoggedInput=hid_device->LoggedWheel=0;
    hid_device->KeyboardState=(BOB64_KEYBOARD_HID_STATE){0};
    if(kernel_xhci_hid_device_count<=hid_device_index)
        kernel_xhci_hid_device_count=(u8)(hid_device_index+1u);
    kernel_write("bob64 kernel: USB HID boot ");
    kernel_write(hid_kind==1?"keyboard":"mouse");
    kernel_write(" endpoint configured; waiting for input report\r\n");
    return 0;
}

static void kernel_xhci_address_hub_children(
        KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u32 start_index) {
    u32 index=start_index,limit=capabilities->MaxSlots;
    if(limit>KERNEL_XHCI_HID_DEVICE_LIMIT)limit=KERNEL_XHCI_HID_DEVICE_LIMIT;
    while(index<kernel_xhci_hub_child_count&&index<limit) {
        KERNEL_XHCI_TOPOLOGY topology=kernel_xhci_hub_children[index++];
        if(!topology.RootPort)continue;
        u8 slot_id=0;
        int result=kernel_xhci_enable_slot(mmio,capabilities,&slot_id);
        if(result) {
            kernel_write("bob64 kernel: USB hub child slot allocation failed result=");
            kernel_hex64((u64)(s64)result);kernel_write("\r\n");
            break;
        }
        kernel_write("bob64 kernel: xHCI USB hub child slot enabled id=");
        kernel_hex64(slot_id);kernel_write(" route=0x");
        kernel_hex64(topology.RouteString);kernel_write(" speed=");
        kernel_hex64(topology.Speed);kernel_write("\r\n");
        result=kernel_xhci_address_and_read_descriptor(mmio,capabilities,
                                                       slot_id,&topology);
        if(result) {
            (void)kernel_xhci_disable_slot(slot_id);
            kernel_write("bob64 kernel: USB hub child unsupported root port ");
            kernel_hex64(topology.RootPort);kernel_write(" port ");
            kernel_hex64(topology.ParentPort);kernel_write(" result=");
            kernel_hex64((u64)(s64)result);kernel_write("\r\n");
        }
    }
    if(index<kernel_xhci_hub_child_count) {
        kernel_write("bob64 kernel: USB hub child limit reached count=");
        kernel_hex64(kernel_xhci_hub_child_count);kernel_write("\r\n");
    }
}

static int kernel_xhci_topology_in_subtree(
        const KERNEL_XHCI_TOPOLOGY *topology,u8 root_port,u32 route,
        u8 depth) {
    u32 mask;
    if(!topology||!depth||depth>5u||topology->RootPort!=root_port||
       topology->Depth<depth)return 0;
    mask=(1u<<((u32)depth*4u))-1u;
    return (topology->RouteString&mask)==route;
}

static void kernel_xhci_hub_remove_subtree(KERNEL_XHCI_HUB_DEVICE *parent,
        u32 route,u8 depth) {
    if(!parent)return;
    for(u32 i=0;i<kernel_xhci_hid_device_count;i++) {
        KERNEL_XHCI_HID_DEVICE *device=&kernel_xhci_hid_devices[i];
        if(device->Active&&kernel_xhci_topology_in_subtree(&device->Topology,
                parent->RootPort,route,depth))
            kernel_xhci_hid_disconnect(device,i);
    }
    for(u32 i=0;i<kernel_xhci_hub_device_count;i++) {
        KERNEL_XHCI_HUB_DEVICE *hub=&kernel_xhci_hub_devices[i];
        if(hub->Active&&kernel_xhci_topology_in_subtree(&hub->Topology,
                parent->RootPort,route,depth)) {
            hub->Active=0;hub->Pending=0;
            (void)kernel_xhci_disable_slot(hub->Slot);
        }
    }
    for(u32 i=0;i<kernel_xhci_hub_child_count;i++)
        if(kernel_xhci_topology_in_subtree(&kernel_xhci_hub_children[i],
                parent->RootPort,route,depth))
            kernel_xhci_hub_children[i].RootPort=0;
}

static void kernel_xhci_hub_process_changes(KERNEL_XHCI_HUB_DEVICE *hub) {
    if(!hub||!hub->Active)return;
    for(u32 port=1;port<=hub->PortCount;port++) {
        u32 byte=port>>3,bit=port&7u;
        if(byte>=hub->ReportLength||!(hub->Report[byte]&(1u<<bit)))continue;
        int result=kernel_xhci_hub_control_in(hub,0xa3,0,0,(u16)port,4);
        if(result) {
            kernel_write("bob64 kernel: USB hub port status read failed port=");
            kernel_hex64(port);kernel_write(" result=");
            kernel_hex64((u64)(s64)result);kernel_write("\r\n");
            continue;
        }
        u16 status=(u16)(hub->ControlBuffer[0]|((u16)hub->ControlBuffer[1]<<8));
        u16 change=(u16)(hub->ControlBuffer[2]|((u16)hub->ControlBuffer[3]<<8));
        if(change) {
            kernel_write("bob64 kernel: USB hub changed port=");kernel_hex64(port);
            kernel_write(" status=0x");kernel_hex64(status);
            kernel_write(" change=0x");kernel_hex64(change);kernel_write("\r\n");
        }
        u32 child_route=0;
        u8 child_depth=(u8)(hub->Topology.Depth+1u);
        if((change&1u)&&!bob64_usb_route_string_append(hub->Topology.RouteString,
                hub->Topology.Depth,(u8)port,&child_route)) {
            kernel_xhci_hub_remove_subtree(hub,child_route,child_depth);
            if(status&1u) {
                result=kernel_xhci_hub_control_no_data(hub,0x23,3,4,(u16)port);
                if(!result)result=kernel_xhci_delay_ticks(2);
                if(!result)result=kernel_xhci_hub_control_in(hub,
                    0xa3,0,0,(u16)port,4);
                if(result) {
                    kernel_write("bob64 kernel: USB hub port reset failed port=");
                    kernel_hex64(port);kernel_write(" result=");
                    kernel_hex64((u64)(s64)result);kernel_write("\r\n");
                } else {
                    status=(u16)(hub->ControlBuffer[0]|
                                 ((u16)hub->ControlBuffer[1]<<8));
                    change|=(u16)(hub->ControlBuffer[2]|
                                 ((u16)hub->ControlBuffer[3]<<8));
                    if(!(status&(1u<<1))) {
                        kernel_write("bob64 kernel: USB hub port did not enable after reset port=");
                        kernel_hex64(port);kernel_write("\r\n");
                    } else {
                        u16 speed_bits=(u16)((status>>9)&3u);
                        KERNEL_XHCI_TOPOLOGY child={0};
                        child.RootPort=hub->RootPort;
                        child.ParentSlot=hub->Slot;
                        child.ParentPort=(u8)port;
                        child.RouteString=child_route;
                        child.Depth=child_depth;
                        child.Speed=speed_bits==2u?3u:
                                    speed_bits==1u?2u:1u;
                        child.TtHubSlot=hub->Topology.TtHubSlot;
                        child.TtPort=hub->Topology.TtPort;
                        if(hub->Topology.Speed==3u&&child.Speed<3u) {
                            child.TtHubSlot=hub->Slot;
                            child.TtPort=(u8)port;
                        }
                        u32 queue_start=kernel_xhci_hub_child_count;
                        u8 slot_id=0;
                        result=kernel_xhci_enable_slot(&kernel_xhci_live_mmio,
                            &kernel_xhci_live_capabilities,&slot_id);
                        if(!result)result=kernel_xhci_address_and_read_descriptor(
                            &kernel_xhci_live_mmio,&kernel_xhci_live_capabilities,
                            slot_id,&child);
                        if(result) {
                            if(slot_id)(void)kernel_xhci_disable_slot(slot_id);
                            kernel_write("bob64 kernel: USB hub hotplug enumeration failed port=");
                            kernel_hex64(port);kernel_write(" result=");
                            kernel_hex64((u64)(s64)result);kernel_write("\r\n");
                        } else {
                            kernel_xhci_address_hub_children(
                                &kernel_xhci_live_mmio,
                                &kernel_xhci_live_capabilities,queue_start);
                            for(u32 i=0;i<kernel_xhci_hid_device_count;i++)
                                if(kernel_xhci_hid_devices[i].Active&&
                                   !kernel_xhci_hid_devices[i].Pending)
                                    (void)kernel_xhci_usb_hid_submit(
                                        &kernel_xhci_hid_devices[i]);
                            kernel_write("bob64 kernel: USB hub hotplug device enumerated port=");
                            kernel_hex64(port);kernel_write("\r\n");
                        }
                    }
                }
            }
        }
        /* Clear every reported change feature to prevent a persistent bitmap. */
        for(u32 change_bit=0;change_bit<5u;change_bit++)
            if(change&(1u<<change_bit))
                (void)kernel_xhci_hub_control_no_data(hub,0x23,1,
                    (u16)(16u+change_bit),(u16)port);
    }
}

static int kernel_xhci_scan_connected_ports(KERNEL_XHCI_MMIO *mmio,
        const BOB64_XHCI_CAPABILITIES *capabilities,u32 *connected_count) {
    const u32 port_power=1u<<9;
    const u32 port_enabled=1u<<1;
    const u32 port_reset=1u<<4;
    const u32 port_warm_reset=1u<<31;
    const u32 port_warm_reset_change=1u<<19;
    const u32 port_reset_change=1u<<21;
    const u32 port_change_mask=0x00fe0000u;
    u32 connected=0,reset=0;
    if(!mmio||!capabilities||!connected_count)return -1;
    for(u32 port=0;port<capabilities->MaxPorts;port++) {
        u32 port_status;
        u32 offset=0x400u+port*0x10u;
        if(kernel_xhci_read32(mmio,offset,&port_status))return -2;
        if(port_status&1u) {
            connected++;
            if(connected<=KERNEL_XHCI_HID_DEVICE_LIMIT) {
                kernel_xhci_connected_ports[connected-1]=(u8)(port+1);
                kernel_xhci_connected_speeds[connected-1]=
                    (u8)((port_status>>10)&0xfu);
            }
            kernel_write("bob64 kernel: xHCI connected root port ");
            kernel_hex64(port+1);kernel_write(" status=0x");
            kernel_hex64(port_status);kernel_write("\r\n");
            if(!(port_status&port_enabled)) {
                u32 speed=(port_status>>10)&0xfu;
                if(speed>=4u) {
                    if(port_status&port_warm_reset_change)
                        (void)kernel_xhci_write32(mmio,offset,
                            (port_status&port_power)|port_warm_reset_change);
                    if(kernel_xhci_write32(mmio,offset,
                           (port_status&port_power)|port_warm_reset))return -4;
                    int warm_completed=0;
                    for(u32 wait=0;wait<20000000u;wait++) {
                        if(kernel_xhci_read32(mmio,offset,&port_status))return -5;
                        if(port_status&port_warm_reset_change) {
                            if(!(port_status&port_enabled))return -6;
                            (void)kernel_xhci_write32(mmio,offset,
                                (port_status&port_power)|
                                (port_status&port_change_mask));
                            warm_completed=1;
                            break;
                        }
                        __asm__ volatile("pause");
                    }
                    if(!warm_completed)return -7;
                    reset++;
                    kernel_write("bob64 kernel: xHCI root port warm reset passed ");
                    kernel_hex64(port+1);kernel_write("\r\n");
                    continue;
                }
                if(kernel_xhci_write32(mmio,offset,
                       (port_status&port_power)|port_reset))return -8;
                int completed=0;
                for(u32 wait=0;wait<20000000u;wait++) {
                    if(kernel_xhci_read32(mmio,offset,&port_status))return -9;
                    if(!(port_status&port_reset)) {
                        if(!(port_status&port_enabled))return -10;
                        completed=1;
                        break;
                    }
                    __asm__ volatile("pause");
                }
                if(!completed)return -11;
                if(port_status&port_reset_change)
                    (void)kernel_xhci_write32(mmio,offset,
                        (port_status&port_power)|
                        (port_status&port_change_mask));
                reset++;
                kernel_write("bob64 kernel: xHCI root port reset passed ");
                kernel_hex64(port+1);kernel_write("\r\n");
            }
        }
    }
    *connected_count=connected;
    if(reset) {
        kernel_write("bob64 kernel: xHCI root-port resets=");
        kernel_hex64(reset);kernel_write("\r\n");
    }
    return 0;
}

static void kernel_xhci_hid_reconnect(u8 port) {
    u32 port_status;
    const u32 port_enabled=1u<<1;
    const u32 port_power=1u<<9;
    const u32 port_change_mask=0x00fe0000u;
    if(!port||port>kernel_xhci_live_capabilities.MaxPorts)return;
    for(u32 i=0;i<kernel_xhci_hid_device_count;i++)
        if(kernel_xhci_hid_devices[i].Active&&
           kernel_xhci_hid_devices[i].RootPort==port)return;
    for(u32 i=0;i<kernel_xhci_storage_device_count;i++)
        if(kernel_xhci_storage_devices[i].Active&&
           kernel_xhci_storage_devices[i].Topology.RootPort==port)return;
    if(kernel_xhci_read32(&kernel_xhci_live_mmio,
        0x400u+((u32)port-1u)*0x10u,&port_status)||!(port_status&1u))return;
    /* A replacement device can arrive while the root port still appears
       enabled for the removed device. Disable it so the scan performs the
       required USB2 reset or SuperSpeed warm reset before addressing. */
    if(port_status&port_enabled) {
        if(kernel_xhci_write32(&kernel_xhci_live_mmio,
            0x400u+((u32)port-1u)*0x10u,
            (port_status&port_power)|port_enabled|
            (port_status&port_change_mask))) {
            kernel_write("bob64 kernel: USB reconnect port disable failed\r\n");
            return;
        }
    }
    if(!kernel_xhci_read32(&kernel_xhci_live_mmio,
        0x400u+((u32)port-1u)*0x10u,&port_status)&&
       (port_status&port_enabled)) {
        u32 speed=(port_status>>10)&0xfu;
        u32 reset_request=speed>=4u?(1u<<31):(1u<<4);
        u32 reset_change=speed>=4u?(1u<<19):(1u<<21);
        if(kernel_xhci_write32(&kernel_xhci_live_mmio,
            0x400u+((u32)port-1u)*0x10u,
            (port_status&port_power)|reset_request|
            (port_status&port_change_mask))) {
            kernel_write("bob64 kernel: USB reconnect port reset request failed\r\n");
            return;
        }
        int reset_completed=0;
        for(u32 wait=0;wait<20000000u;wait++) {
            if(kernel_xhci_read32(&kernel_xhci_live_mmio,
                0x400u+((u32)port-1u)*0x10u,&port_status))return;
            if(port_status&reset_change) {
                if(!(port_status&port_enabled))break;
                (void)kernel_xhci_write32(&kernel_xhci_live_mmio,
                    0x400u+((u32)port-1u)*0x10u,
                    (port_status&port_power)|
                    (port_status&port_change_mask));
                reset_completed=1;
                break;
            }
        }
        if(!reset_completed) {
            kernel_write("bob64 kernel: USB reconnect port reset timed out\r\n");
            return;
        }
    }
    u32 connected=0;
    if(kernel_xhci_scan_connected_ports(&kernel_xhci_live_mmio,
        &kernel_xhci_live_capabilities,&connected)) {
        kernel_write("bob64 kernel: USB reconnect port reset failed\r\n");
        return;
    }
    if(kernel_xhci_read32(&kernel_xhci_live_mmio,
        0x400u+((u32)port-1u)*0x10u,&port_status)||!(port_status&1u))return;
    u8 speed=(u8)((port_status>>10)&0xfu),slot_id=0;
    if(speed<1u||speed>5u||kernel_xhci_enable_slot(
        &kernel_xhci_live_mmio,&kernel_xhci_live_capabilities,&slot_id)) {
        kernel_write("bob64 kernel: USB reconnect slot allocation failed\r\n");
        return;
    }
    KERNEL_XHCI_TOPOLOGY topology={0};
    topology.RootPort=port;topology.Speed=speed;
    int result=kernel_xhci_address_and_read_descriptor(
        &kernel_xhci_live_mmio,&kernel_xhci_live_capabilities,slot_id,
        &topology);
    if(result) {
        (void)kernel_xhci_disable_slot(slot_id);
        kernel_write("bob64 kernel: USB reconnect device unsupported result=");
        kernel_hex64((u64)(s64)result);kernel_write("\r\n");
        return;
    }
    kernel_xhci_hub_child_count=0;
    kernel_xhci_address_hub_children(&kernel_xhci_live_mmio,
                                     &kernel_xhci_live_capabilities,0);
    for(u32 i=0;i<kernel_xhci_hid_device_count;i++)
        if(kernel_xhci_hid_devices[i].Active&&
           kernel_xhci_hid_devices[i].RootPort==port) {
            if(kernel_xhci_usb_hid_submit(&kernel_xhci_hid_devices[i]))
                kernel_xhci_hid_disconnect(&kernel_xhci_hid_devices[i],i);
            break;
        }
    kernel_write("bob64 kernel: USB device reconnected on root port ");
    kernel_hex64(port);kernel_write("\r\n");
}

static void kernel_pci_report_usb(void *context,const BOB64_PCI_DEVICE *device) {
    (void)context;
    if(!bob64_pci_is_usb_controller(device))return;
    if(bob64_pci_is_xhci_controller(device))
        kernel_write("bob64 kernel: xHCI PCI controller found at ");
    else kernel_write("bob64 kernel: USB PCI controller found at ");
    kernel_hex64(((u64)device->Bus<<16)|((u64)device->Device<<8)|
                 device->Function);
    kernel_write(" vendor=0x");kernel_hex64(device->VendorId);
    kernel_write(" device=0x");kernel_hex64(device->DeviceId);
    kernel_write(" BAR0=0x");kernel_hex64(device->Bar[0]);
    kernel_write("\r\n");
    if(bob64_pci_is_xhci_controller(device)) {
        kernel_xhci_device=*device;kernel_xhci_found=1;
    }
}

static int kernel_xhci_probe_capabilities(const BOB64_KERNEL_BOOT_INFO *info) {
    const u64 virtual_address=0xffffa00000000000ULL;
    BOB64_XHCI_CAPABILITIES capabilities;
    KERNEL_XHCI_MMIO mmio;
    u64 physical;
    int result;
    if(!kernel_xhci_found||!info||!kernel_xhci_device.BarValid[0]||
       kernel_xhci_device.BarIsIo[0])return -10;
    if(bob64_pci_enable_memory(&kernel_xhci_device))return -12;
    physical=kernel_xhci_device.Bar[0];
    if(physical&(BOB64_PAGE_SIZE-1))return -11;
    result=bob64_page_map_range(&bob64_bootstrap_space.PageTable,virtual_address,
        physical,BOB64_XHCI_MMIO_MAP_SIZE,BOB64_PAGE_WRITE|BOB64_PAGE_PCD|
        BOB64_PAGE_PWT|(info->NxSupported?BOB64_PAGE_NX:0));
    if(result)return -100+result;
    for(u64 offset=0;offset<BOB64_XHCI_MMIO_MAP_SIZE;
        offset+=BOB64_PAGE_SIZE)
        __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)(virtual_address+offset)):
                         "memory");
    if(bob64_xhci_parse_capabilities((const volatile void *)(uintptr_t)virtual_address,
                                     BOB64_XHCI_MMIO_MAP_SIZE,&capabilities)) {
        kernel_write("bob64 kernel: xHCI caps invalid len=");
        kernel_hex64(capabilities.CapabilityLength);kernel_write(" version=");
        kernel_hex64(capabilities.Version);kernel_write(" slots=");
        kernel_hex64(capabilities.MaxSlots);kernel_write(" intr=");
        kernel_hex64(capabilities.MaxInterrupters);kernel_write(" ports=");
        kernel_hex64(capabilities.MaxPorts);kernel_write(" db=");
        kernel_hex64(capabilities.DoorbellOffset);kernel_write(" rt=");
        kernel_hex64(capabilities.RuntimeOffset);kernel_write("\r\n");
        for(u64 offset=0;offset<BOB64_XHCI_MMIO_MAP_SIZE;
            offset+=BOB64_PAGE_SIZE) {
            (void)bob64_page_unmap(&bob64_bootstrap_space.PageTable,
                                   virtual_address+offset,0,0);
            __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)(virtual_address+offset)):
                             "memory");
        }
        return -20;
    }
    kernel_write("bob64 kernel: xHCI capabilities mapped, version=0x");
    kernel_hex64(capabilities.Version);kernel_write(" slots=");
    kernel_hex64(capabilities.MaxSlots);kernel_write(" ports=");
    kernel_hex64(capabilities.MaxPorts);kernel_write(" scratchpads=");
    kernel_hex64(capabilities.MaxScratchpadBuffers);kernel_write("\r\n");
    mmio.Base=(volatile u8 *)(uintptr_t)(virtual_address+
                                        capabilities.CapabilityLength);
    mmio.Size=(usize)(BOB64_XHCI_MMIO_MAP_SIZE-capabilities.CapabilityLength);
    mmio.CapabilityLength=capabilities.CapabilityLength;
    kernel_xhci_live_mmio=mmio;
    kernel_xhci_live_capabilities=capabilities;
    result=bob64_xhci_halt_reset(kernel_xhci_read32,kernel_xhci_write32,
                                 &mmio,1000000u,0);
    if(result)return -30+result;
    kernel_write("bob64 kernel: xHCI controller halted and reset\r\n");
    result=kernel_xhci_start_command_ring(&mmio,&capabilities);
    if(result)return -40+result;
    kernel_write("bob64 kernel: xHCI command ring completion passed\r\n");
    u32 connected_ports=0,device_limit=capabilities.MaxSlots;
    if(device_limit>KERNEL_XHCI_HID_DEVICE_LIMIT)
        device_limit=KERNEL_XHCI_HID_DEVICE_LIMIT;
    if(!device_limit)return -59;
    result=kernel_xhci_scan_connected_ports(&mmio,&capabilities,
                                            &connected_ports);
    if(result)return -60+result;
    kernel_write("bob64 kernel: xHCI connected ports=");
    kernel_hex64(connected_ports);kernel_write("\r\n");
    kernel_xhci_hid_device_count=0;
    kernel_xhci_hub_device_count=0;
    kernel_xhci_hub_child_count=0;
    for(u32 port_index=0;port_index<connected_ports&&
        port_index<device_limit;port_index++) {
        u8 slot_id=0;
        result=kernel_xhci_enable_slot(&mmio,&capabilities,&slot_id);
        if(result)return -70+result;
        kernel_write("bob64 kernel: xHCI USB slot enabled id=");
        kernel_hex64(slot_id);kernel_write("\r\n");
        KERNEL_XHCI_TOPOLOGY topology={0};
        topology.RootPort=kernel_xhci_connected_ports[port_index];
        topology.Speed=kernel_xhci_connected_speeds[port_index];
        result=kernel_xhci_address_and_read_descriptor(&mmio,&capabilities,
                                                       slot_id,&topology);
        if(result) {
            kernel_write("bob64 kernel: USB HID device unsupported at port ");
            kernel_hex64(kernel_xhci_connected_ports[port_index]);
            kernel_write(" result=");kernel_hex64((u64)(s64)result);
            kernel_write("\r\n");
            continue;
        }
        kernel_write("bob64 kernel: USB device descriptor read passed\r\n");
    }
    kernel_xhci_address_hub_children(&mmio,&capabilities,0);
    for(u32 i=0;i<kernel_xhci_hid_device_count;i++) {
        if(kernel_xhci_usb_hid_submit(&kernel_xhci_hid_devices[i]))return -90;
    }
    return 0;
}

static int kernel_text_contains(const char *text,const char *needle) {
    if(!text||!needle)return 0;
    for(usize i=0;text[i];i++) {
        usize j=0;
        while(needle[j]&&text[i+j]&&text[i+j]==needle[j])j++;
        if(!needle[j])return 1;
    }
    return 0;
}

static int kernel_interrupts_enabled(void) {
    u64 flags;
    __asm__ volatile("pushfq; pop %0":"=r"(flags));
    return (flags&0x200)!=0;
}
static void kernel_user_write_character(void *context,u8 character);
static s64 kernel_run_application(void *context,const char *name,
                                  usize argument_count,
                                  const char *const *arguments,int *started);
static int kernel_app_run_application(void *context,const char *name,
                                      usize argument_count,
                                      const char *const *arguments,
                                      s64 *exit_status);
static int kernel_copy_user_bytes(void *context,u64 address,void *destination,
                                  usize length);
static int kernel_process_fault_smoke_test(BOB64_PROCESS *process,
    BOB64_PAGE_TABLE *process_table,const BOB64_PROCESS_OPERATIONS *operations,
    const u8 *executable,usize executable_size,u64 expected_vector,
    u64 expected_rip,u64 error_mask,u64 error_value,int diagnose,
    const char *success_message);
static int kernel_wait_for_input_event(void *context,BOB64_EVENT *event);
static int kernel_snapshot_save(void *context,const void *snapshot,
                                usize snapshot_size);
static int kernel_snapshot_restore(void *context,BOB64_FILESYSTEM *filesystem);
extern const u8 bob64_embedded_app[];
extern const u8 bob64_embedded_app_end[];
extern const u8 bob64_embedded_display[];
extern const u8 bob64_embedded_display_end[];
extern const u8 bob64_embedded_gui[];
extern const u8 bob64_embedded_gui_end[];
extern const u8 bob64_embedded_nested_app[];
extern const u8 bob64_embedded_nested_app_end[];
extern const u8 bob64_embedded_echo[];
extern const u8 bob64_embedded_echo_end[];
extern const u8 bob64_embedded_ls[];
extern const u8 bob64_embedded_ls_end[];
extern const u8 bob64_embedded_cat[];
extern const u8 bob64_embedded_cat_end[];
extern const u8 bob64_embedded_notes[];
extern const u8 bob64_embedded_notes_end[];
extern const u8 bob64_embedded_info[];
extern const u8 bob64_embedded_info_end[];
extern const u8 bob64_embedded_mouse_smoke[];
extern const u8 bob64_embedded_mouse_smoke_end[];
extern const u8 bob64_embedded_launcher[];
extern const u8 bob64_embedded_launcher_end[];
extern const u8 bob64_embedded_source[];
extern const u8 bob64_embedded_source_end[];

typedef struct {
    BOB64_PAGE_TABLE *PageTable;
} KERNEL_PROCESS_CONTEXT;

static u64 read_cr3(void);

static u64 process_allocate_page(void *context) {
    (void)context;
    return bob64_page_alloc(&bob64_boot_page_allocator,1);
}

static int process_free_page(void *context,u64 physical_address) {
    (void)context;
    return bob64_page_free(&bob64_boot_page_allocator,physical_address,1);
}

static int process_map_page(void *context,u64 virtual_address,u64 physical_address,
                            u64 flags,u8 **writable_address) {
    KERNEL_PROCESS_CONTEXT *process=(KERNEL_PROCESS_CONTEXT *)context;
    if(!process||!process->PageTable||!writable_address||
       bob64_page_map(process->PageTable,
       virtual_address,physical_address,flags))return -1;
    __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)virtual_address):"memory");
    *writable_address=(u8 *)(uintptr_t)virtual_address;
    return 0;
}

static int process_protect_page(void *context,u64 virtual_address,u64 flags) {
    KERNEL_PROCESS_CONTEXT *process=(KERNEL_PROCESS_CONTEXT *)context;
    if(!process||!process->PageTable||
       bob64_page_protect(process->PageTable,virtual_address,flags))return -1;
    __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)virtual_address):"memory");
    return 0;
}

static int process_unmap_page(void *context,u64 virtual_address) {
    KERNEL_PROCESS_CONTEXT *process=(KERNEL_PROCESS_CONTEXT *)context;
    if(!process||!process->PageTable||
       bob64_page_unmap(process->PageTable,virtual_address,0,0))return -1;
    __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)virtual_address):"memory");
    return 0;
}

static void kernel_write16(u8 *bytes,u16 value) {
    bytes[0]=(u8)value;bytes[1]=(u8)(value>>8);
}

static void kernel_write32(u8 *bytes,u32 value) {
    for(u32 i=0;i<4;i++)bytes[i]=(u8)(value>>(i*8));
}

static void kernel_write64(u8 *bytes,u64 value) {
    kernel_write32(bytes,(u32)value);kernel_write32(bytes+4,(u32)(value>>32));
}

static usize kernel_emit_test_syscall(u8 *code,usize offset,u32 number,u32 argument) {
    code[offset++]=0xb8;kernel_write32(code+offset,number);offset+=4;
    code[offset++]=0xb9;kernel_write32(code+offset,argument);offset+=4;
    code[offset++]=0xcd;code[offset++]=0x80;
    return offset;
}

static usize kernel_emit_test_buffer_syscall(u8 *code,usize offset,
                                             const char *text,usize length) {
    usize instruction_start=offset,lea_end,buffer_offset;
    code[offset++]=0xb8;kernel_write32(code+offset,BOB64_SYSCALL_WRITE_BUFFER);offset+=4;
    code[offset++]=0x48;code[offset++]=0x8d;code[offset++]=0x0d;
    lea_end=offset+4;
    buffer_offset=instruction_start+24;
    kernel_write32(code+offset,(u32)(buffer_offset-lea_end));offset+=4;
    code[offset++]=0xba;kernel_write32(code+offset,(u32)length);offset+=4;
    code[offset++]=0xcd;code[offset++]=0x80;
    code[offset++]=0xe9;
    kernel_write32(code+offset,(u32)length);offset+=4;
    for(usize i=0;i<length;i++)code[offset++]=(u8)text[i];
    return offset;
}

static u32 kernel_crc32(const u8 *bytes,usize length) {
    u32 value=0xffffffffu;
    for(usize i=0;i<length;i++) {
        value^=bytes[i];
        for(u32 bit=0;bit<8;bit++)value=(value>>1)^(0xedb88320u&-(value&1u));
    }
    return ~value;
}

static int kernel_bytes_equal(const char *left,const char *right) {
    while(*left&&*left==*right){left++;right++;}
    return *left==*right;
}

static void kernel_build_bob_application(u8 *executable) {
    static const char text[]="bob!";
    usize code_size=0;
    for(usize i=0;i<BOB64_EXEC_HEADER_SIZE+BOB64_PAGE_SIZE;i++)executable[i]=0;
    executable[0]='B';executable[1]='6';executable[2]='4';executable[3]='E';
    kernel_write16(executable+4,BOB64_EXEC_VERSION);
    kernel_write16(executable+6,BOB64_EXEC_HEADER_SIZE);
    kernel_write32(executable+8,BOB64_APP_ABI_VERSION);
    kernel_write64(executable+16,BOB64_PAGE_SIZE);
    kernel_write64(executable+24,2*BOB64_PAGE_SIZE);
    kernel_write64(executable+32,0);
    kernel_write64(executable+40,BOB64_PAGE_SIZE);
    for(usize i=0;i<BOB64_PAGE_SIZE;i++)
        executable[BOB64_EXEC_HEADER_SIZE+i]=0x90;
    code_size=kernel_emit_test_syscall(executable+BOB64_EXEC_HEADER_SIZE,
        code_size,BOB64_SYSCALL_QUERY_ABI,0);
    code_size=kernel_emit_test_buffer_syscall(executable+BOB64_EXEC_HEADER_SIZE,
        code_size,text,sizeof(text)-1);
    (void)kernel_emit_test_syscall(executable+BOB64_EXEC_HEADER_SIZE,
        code_size,BOB64_SYSCALL_EXIT,0);
    kernel_write32(executable+48,kernel_crc32(executable+BOB64_EXEC_HEADER_SIZE,
                                               (usize)BOB64_PAGE_SIZE));
}

static u64 kernel_build_exception_diagnostic_application(u8 *executable) {
    static const char text[]="bob!";
    u8 *code=executable+BOB64_EXEC_HEADER_SIZE;
    usize offset=0;
    kernel_build_bob_application(executable);
    offset=kernel_emit_test_syscall(code,offset,BOB64_SYSCALL_QUERY_ABI,0);
    offset=kernel_emit_test_buffer_syscall(code,offset,text,sizeof(text)-1);
    code[offset++]=0x49;code[offset++]=0xbf; /* mov r15, imm64 */
    kernel_write64(code+offset,0x1234567887654321ULL);offset+=8;
    u64 fault_rip=BOB64_PROCESS_IMAGE_BASE+offset;
    code[offset++]=0x0f;code[offset++]=0x0b; /* ud2 */
    kernel_write32(executable+48,kernel_crc32(executable+BOB64_EXEC_HEADER_SIZE,
                                               (usize)BOB64_PAGE_SIZE));
    return fault_rip;
}

static void kernel_build_code_write_fault_application(u8 *executable) {
    static const char text[]="bob!";
    u8 *code=executable+BOB64_EXEC_HEADER_SIZE;
    usize offset;
    kernel_build_bob_application(executable);
    offset=kernel_emit_test_buffer_syscall(code,0,text,sizeof(text)-1);
    code[offset++]=0xc6;code[offset++]=0x05;
    kernel_write32(code+offset,(u32)(0-(offset+5)));offset+=4;
    code[offset++]=0x90;
    (void)offset;
    kernel_write32(executable+48,kernel_crc32(executable+BOB64_EXEC_HEADER_SIZE,
                                               (usize)BOB64_PAGE_SIZE));
}

static void kernel_build_nx_fault_application(u8 *executable) {
    u8 *code=executable+BOB64_EXEC_HEADER_SIZE;
    usize offset;
    kernel_build_bob_application(executable);
    offset=kernel_emit_test_buffer_syscall(code,0,"bob!",4);
    code[offset++]=0xe9;
    kernel_write32(code+offset,(u32)((u64)BOB64_PAGE_SIZE-(offset+4)));offset+=4;
    executable[BOB64_EXEC_HEADER_SIZE+BOB64_PAGE_SIZE]=0x0f;
    executable[BOB64_EXEC_HEADER_SIZE+BOB64_PAGE_SIZE+1]=0x0b;
    kernel_write64(executable+16,2*BOB64_PAGE_SIZE);
    kernel_write64(executable+24,2*BOB64_PAGE_SIZE);
    kernel_write32(executable+48,kernel_crc32(executable+BOB64_EXEC_HEADER_SIZE,
                                               (usize)(2*BOB64_PAGE_SIZE)));
}

static int kernel_install_bob_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_app_end-bob64_embedded_app);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"bob.b64e",bob64_embedded_app,length);
}

static int kernel_install_display_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_display_end-bob64_embedded_display);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"display.b64e",bob64_embedded_display,length);
}

static int kernel_install_gui_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_gui_end-bob64_embedded_gui);
    if(!length)return -1;
    return bob64_fs_write(filesystem,"desktop.b64e",bob64_embedded_gui,length);
}

static int kernel_install_nested_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_nested_app_end-
                         bob64_embedded_nested_app);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"nested.b64e",bob64_embedded_nested_app,
                          length);
}

static int kernel_install_echo_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_echo_end-bob64_embedded_echo);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"echo.b64e",bob64_embedded_echo,length);
}

static int kernel_install_ls_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_ls_end-bob64_embedded_ls);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"ls.b64e",bob64_embedded_ls,length);
}

static int kernel_install_cat_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_cat_end-bob64_embedded_cat);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"cat.b64e",bob64_embedded_cat,length);
}

static int kernel_install_notes_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_notes_end-bob64_embedded_notes);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"notes.b64e",bob64_embedded_notes,length);
}

static int kernel_install_info_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_info_end-bob64_embedded_info);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"info.b64e",bob64_embedded_info,length);
}

static int kernel_install_mouse_smoke_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_mouse_smoke_end-
                         bob64_embedded_mouse_smoke);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"mousesmoke.b64e",
                          bob64_embedded_mouse_smoke,length);
}

static int kernel_install_launcher_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_launcher_end-
                         bob64_embedded_launcher);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"launcher.b64e",bob64_embedded_launcher,
                          length);
}

static int kernel_install_bob_source(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_source_end-bob64_embedded_source);
    return bob64_fs_write(filesystem,"bob.c",bob64_embedded_source,length);
}

static int kernel_process_smoke_test(const BOB64_KERNEL_BOOT_INFO *info) {
    static const char *arguments[]={"bob!"};
    static u8 executable[BOB64_EXEC_HEADER_SIZE+BOB64_PAGE_SIZE];
    static u8 nx_executable[BOB64_EXEC_HEADER_SIZE+2*BOB64_PAGE_SIZE];
    BOB64_PAGE_TABLE process_table;
    KERNEL_PROCESS_CONTEXT process_context={&process_table};
    BOB64_PROCESS_OPERATIONS operations={process_allocate_page,process_free_page,
        process_map_page,process_protect_page,process_unmap_page,&process_context,0};
    BOB64_PROCESS process;
    BOB64_PROCESS_PAGE *pages;
    BOB64_APP_STARTUP *startup;
    u64 physical,flags,kernel_root,table_checkpoint;
    usize allocated_before,heap_checkpoint;
    if(!info->NxSupported)return 1;
    pages=(BOB64_PROCESS_PAGE *)bob64_heap_calloc(&kernel_heap,
          (usize)BOB64_PROCESS_PAGE_LIMIT,sizeof(*pages));
    if(!pages)return -1;
    kernel_root=read_cr3();
    table_checkpoint=bob64_bootstrap_space.TablePoolUsed;
    heap_checkpoint=kernel_heap_high_water;
    allocated_before=bob64_boot_page_allocator.AllocatedCount;
    operations.NxSupported=info->NxSupported;
    if(bob64_process_init(&process,pages,(usize)BOB64_PROCESS_PAGE_LIMIT,&operations)) {
        bob64_heap_free(&kernel_heap,pages);return -1;
    }
    if(bob64_page_table_init(&process_table,info->PhysicalAddressBits,
       bob64_bootstrap_space.PageTable.Allocate,bob64_bootstrap_space.PageTable.Access,
       bob64_bootstrap_space.PageTable.Context)||
       bob64_page_table_clone_isolated(&process_table,&bob64_bootstrap_space.PageTable,
       BOB64_PROCESS_IMAGE_BASE,BOB64_PROCESS_ISOLATED_SIZE)) {
        if(kernel_heap_high_water==heap_checkpoint)
            bob64_bootstrap_space.TablePoolUsed=table_checkpoint;
        bob64_heap_free(&kernel_heap,pages);return -1;
    }
    __asm__ volatile("mov %0,%%cr3"::"r"(process_table.RootPhysical):"memory");
    kernel_build_bob_application(executable);
    int result=bob64_process_load(&process,executable,sizeof(executable),1,arguments);
    if(result) {
        bob64_process_unload(&process);
        __asm__ volatile("mov %0,%%cr3"::"r"(kernel_root):"memory");
        if(kernel_heap_high_water==heap_checkpoint)
            bob64_bootstrap_space.TablePoolUsed=table_checkpoint;
        bob64_heap_free(&kernel_heap,pages);return -1;
    }
    startup=(BOB64_APP_STARTUP *)(uintptr_t)process.StartupAddress;
    result=process.EntryAddress==BOB64_PROCESS_IMAGE_BASE&&
           startup->AbiVersion==BOB64_APP_ABI_VERSION&&startup->ArgumentCount==1&&
           startup->Arguments&&kernel_bytes_equal(startup->Arguments[0],"bob!")&&
           bob64_page_translate(&process_table,
             BOB64_PROCESS_IMAGE_BASE,&physical,&flags)==1&&physical&&
           (flags&BOB64_PAGE_USER)&&!(flags&BOB64_PAGE_WRITE)&&!(flags&BOB64_PAGE_NX)&&
           bob64_page_translate(&process_table,
             BOB64_PROCESS_IMAGE_BASE+BOB64_PAGE_SIZE,&physical,&flags)==1&&
           (flags&BOB64_PAGE_USER)&&(flags&BOB64_PAGE_WRITE)&&(flags&BOB64_PAGE_NX)&&
           bob64_page_translate(&process_table,
             BOB64_PROCESS_STACK_GUARD,&physical,&flags)==0;
    if(result) {
        kernel_user_output_length=0;
        kernel_user_output_truncated=0;
        bob64_syscall_set_address_space(&process_table);
        bob64_syscall_set_read_user(kernel_copy_user_bytes,0);
        bob64_syscall_set_write(kernel_user_write_character,0);
        bob64_syscall_set_wait_event(kernel_wait_for_input_event,0);
        s64 app_status=bob64_enter_user(process.EntryAddress,process.StartupAddress,
                                         process.InitialStackPointer);
        bob64_syscall_set_wait_event(0,0);
        bob64_syscall_set_write(0,0);
        bob64_syscall_set_read_user(0,0);
        bob64_syscall_set_address_space(0);
        kernel_user_output[kernel_user_output_length]=0;
        if(app_status||kernel_user_output_truncated||
           kernel_user_output_length!=4||
           !kernel_bytes_equal(kernel_user_output,"bob!"))result=0;
        else kernel_write("\r\n");
    }
    int unload_result=bob64_process_unload(&process);
    if(unload_result||process.PageCount||process.Loaded||
       bob64_boot_page_allocator.AllocatedCount!=allocated_before)result=-1;
    else result=result?0:-1;
    if(!result) {
        u64 exception_rip=kernel_build_exception_diagnostic_application(executable);
        if(kernel_process_fault_smoke_test(&process,&process_table,&operations,
           executable,sizeof(executable),6,exception_rip,0,0,1,
           "bob64 kernel: full-width user exception diagnostics passed\r\n"))
            result=-1;
    }
    if(!result) {
        kernel_build_code_write_fault_application(executable);
        if(kernel_process_fault_smoke_test(&process,&process_table,&operations,
           executable,sizeof(executable),14,BOB64_PROCESS_IMAGE_BASE+28,0x7,0x7,1,
           "bob64 kernel: read-only code protection passed\r\n"))result=-1;
    }
    if(!result) {
        kernel_build_nx_fault_application(nx_executable);
        if(kernel_process_fault_smoke_test(&process,&process_table,&operations,
           nx_executable,sizeof(nx_executable),14,
           BOB64_PROCESS_IMAGE_BASE+BOB64_PAGE_SIZE,0x15,0x15,0,
           "bob64 kernel: non-executable data protection passed\r\n"))result=-1;
    }
    __asm__ volatile("mov %0,%%cr3"::"r"(kernel_root):"memory");
    if(kernel_heap_high_water==heap_checkpoint)
        bob64_bootstrap_space.TablePoolUsed=table_checkpoint;
    if(bob64_boot_page_allocator.AllocatedCount!=allocated_before)result=-1;
    if(bob64_heap_free(&kernel_heap,pages))result=-1;
    return result;
}

static u64 read_cr3(void) {
    u64 value;
    __asm__ volatile("mov %%cr3,%0":"=r"(value));
    return value&BOB64_PAGE_ADDRESS_MASK;
}

static __attribute__((noreturn)) void kernel_halt(void) {
    __asm__ volatile("cli");
    for(;;)__asm__ volatile("hlt");
}

static void kernel_write(const char *text) {
    bob64_early_console_write(text);
    bob64_framebuffer_write(text);
}

static void kernel_user_write_character(void *context,u8 character) {
    char text[2]={(char)character,0};
    (void)context;
    if(kernel_user_output_length+1<sizeof(kernel_user_output))
        kernel_user_output[kernel_user_output_length++]=(char)character;
    else kernel_user_output_truncated=1;
    kernel_write(text);
}

static int kernel_copy_user_bytes(void *context,u64 address,void *destination,
                                  usize length) {
    const volatile u8 *source=(const volatile u8 *)(uintptr_t)address;
    u8 *target=(u8 *)destination;
    (void)context;
    if((!source&&length)||(!target&&length)||
       length>BOB64_SYSCALL_COPY_CHUNK_BYTES)
        return -1;
    for(usize i=0;i<length;i++)target[i]=source[i];
    return 0;
}

static int kernel_process_fault_smoke_test(BOB64_PROCESS *process,
    BOB64_PAGE_TABLE *process_table,const BOB64_PROCESS_OPERATIONS *operations,
    const u8 *executable,usize executable_size,u64 expected_vector,
    u64 expected_rip,u64 error_mask,u64 error_value,int diagnose,
    const char *success_message) {
    static const char *arguments[]={"bob!"};
    s64 status;
    int passed;
    if(bob64_process_init(process,process->Pages,process->PageCapacity,operations)||
       bob64_process_load(process,executable,executable_size,1,arguments)) {
        (void)bob64_process_unload(process);
        return -1;
    }
    kernel_user_output_length=0;
    kernel_user_output_truncated=0;
    bob64_user_exception_vector=~(u64)0;
    bob64_user_exception_error=~(u64)0;
    bob64_user_exception_rip=~(u64)0;
    bob64_user_expected_exception_vector=diagnose?~(u64)0:expected_vector;
    bob64_user_expected_exception_error_mask=error_mask;
    bob64_user_expected_exception_error_value=error_value;
    bob64_syscall_set_address_space(process_table);
    bob64_syscall_set_read_user(kernel_copy_user_bytes,0);
    bob64_syscall_set_write(kernel_user_write_character,0);
    status=bob64_enter_user(process->EntryAddress,process->StartupAddress,
                            process->InitialStackPointer);
    bob64_user_expected_exception_vector=~(u64)0;
    bob64_syscall_set_write(0,0);
    bob64_syscall_set_read_user(0,0);
    bob64_syscall_set_address_space(0);
    kernel_user_output[kernel_user_output_length]=0;
    passed=status==-(s64)(128+expected_vector)&&
        bob64_user_exception_vector==expected_vector&&
        bob64_user_exception_rip==expected_rip&&
        (bob64_user_exception_error&error_mask)==error_value&&
        !kernel_user_output_truncated&&kernel_user_output_length==4&&
        kernel_bytes_equal(kernel_user_output,"bob!");
    if(bob64_process_unload(process))return -1;
    if(!passed)return -1;
    kernel_write(success_message);
    return 0;
}

static int kernel_wait_for_input_event(void *context,BOB64_EVENT *event) {
    (void)context;
    if(!event)return -1;
    /* int 0x80 enters through an interrupt gate, which clears IF. */
    __asm__ volatile("sti":::"memory");
    for(;;) {
        kernel_xhci_usb_hid_poll();
        if(!bob64_mouse_poll_event(event)||!bob64_keyboard_poll_event(event))break;
        __asm__ volatile("pause");
    }
    /* Keep the syscall handler's state until iretq restores the user flags. */
    __asm__ volatile("cli":::"memory");
    return 0;
}

static int kernel_write_user_bytes(void *context,u64 address,const void *source,
                                   usize length) {
    volatile u8 *target=(volatile u8 *)(uintptr_t)address;
    const u8 *bytes=(const u8 *)source;
    (void)context;
    if((!target&&length)||(!bytes&&length)||length>BOB64_SYSCALL_MAX_BUFFER)
        return -1;
    for(usize i=0;i<length;i++)target[i]=bytes[i];
    return 0;
}

static s64 kernel_app_read_file(void *context,const char *name,u8 *buffer,
                                usize capacity) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    const char *data;
    usize length;
    if(!app||!app->Filesystem||!name||(!buffer&&capacity))return -22;
    if(bob64_fs_read(app->Filesystem,name,&data,&length))return -2;
    if(length>capacity)return -28;
    for(usize i=0;i<length;i++)buffer[i]=(u8)data[i];
    return (s64)length;
}

static s64 kernel_app_write_file(void *context,const char *name,const u8 *buffer,
                                 usize length) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    if(!app||!app->Filesystem||!name||(!buffer&&length))return -22;
    if(bob64_fs_write(app->Filesystem,name,buffer,length))return -28;
    return (s64)length;
}

static s64 kernel_app_open_stream(void *context,const char *name,u32 flags) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    const char *data;usize length;
    if(!app||!app->Filesystem||!name)return -22;
    if(bob64_fs_read(app->Filesystem,name,&data,&length)) {
        if(!(flags&BOB64_FILE_OPEN_CREATE))return -2;
        if(bob64_fs_write(app->Filesystem,name,0,0))return -28;
        length=0;
    } else if(flags&BOB64_FILE_OPEN_TRUNCATE) {
        if(bob64_fs_write(app->Filesystem,name,0,0))return -28;
        length=0;
    }
    if(length>0x7fffffffffffffffULL)return -75;
    return (s64)length;
}

static s64 kernel_app_read_stream(void *context,const char *name,u64 offset,
                                  u8 *buffer,usize capacity) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    usize count=0;
    if(!app||!app->Filesystem||!name||(!buffer&&capacity))return -22;
    if(bob64_fs_read_at(app->Filesystem,name,offset,buffer,capacity,&count))return -2;
    return (s64)count;
}

static s64 kernel_app_write_stream(void *context,const char *name,u64 offset,
                                   const u8 *buffer,usize length) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    if(!app||!app->Filesystem||!name||(!buffer&&length))return -22;
    if(bob64_fs_write_at(app->Filesystem,name,offset,buffer,length))return -28;
    return (s64)length;
}

typedef struct {
    BOB64_FILE_INFO *Entries;
    usize Capacity,Count;
    int Full;
} KERNEL_FILE_LIST_CONTEXT;

static int kernel_app_list_file(void *context,const char *name,usize size) {
    KERNEL_FILE_LIST_CONTEXT *list=(KERNEL_FILE_LIST_CONTEXT *)context;
    usize length=0;
    BOB64_FILE_INFO *entry;
    while(name[length]&&length<sizeof(list->Entries[0].Name)-1)length++;
    if(name[length]||list->Count>=list->Capacity) {
        list->Full=1;return 1;
    }
    entry=&list->Entries[list->Count++];
    for(usize i=0;i<sizeof(*entry);i++)((u8 *)entry)[i]=0;
    for(usize i=0;i<length;i++)entry->Name[i]=name[i];
    entry->Size=(u64)size;
    return 0;
}

static s64 kernel_app_list_files(void *context,BOB64_FILE_INFO *entries,
                                 usize capacity) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    KERNEL_FILE_LIST_CONTEXT list={entries,capacity,0,0};
    if(!app||!app->Filesystem||!entries||!capacity)return -22;
    bob64_fs_list(app->Filesystem,kernel_app_list_file,&list);
    return list.Full?-28:(s64)list.Count;
}

static s64 kernel_app_delete_file(void *context,const char *name) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    if(!app||!app->Filesystem||!name)return -22;
    return bob64_fs_delete(app->Filesystem,name)?-2:0;
}

static s64 kernel_run_application(void *context,const char *name,usize argument_count,
                                  const char *const *arguments,int *started) {
    BOB64_PAGE_TABLE process_table;
    KERNEL_PROCESS_CONTEXT process_context={&process_table};
    BOB64_PROCESS_OPERATIONS operations;
    BOB64_PROCESS process;
    BOB64_PROCESS_PAGE *pages;
    const char *image;
    usize image_size;
    u64 kernel_root,table_checkpoint;
    usize heap_checkpoint;
    s64 status=-1;
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    void *application_privilege_stack=0;
    BOB64_WINDOW_SERVER *window_server=0;
    u64 previous_privilege_stack=bob64_kernel_tss.Rsp0;
    int entered=0,cleanup_error=0,syscall_context_saved=0;
    int window_server_ready=0;
    u64 window_owner=0;
    if(started)*started=0;
    if(!app||!app->Filesystem||!app->NxSupported||!started||
       !app->PhysicalAddressBits||
       bob64_user_depth>=BOB64_USER_ENTRY_MAX_DEPTH||
       bob64_fs_read(app->Filesystem,name,&image,&image_size))return -1;
    operations=(BOB64_PROCESS_OPERATIONS){process_allocate_page,process_free_page,
        process_map_page,process_protect_page,process_unmap_page,&process_context,
        app->NxSupported};
    pages=(BOB64_PROCESS_PAGE *)bob64_heap_calloc(&kernel_heap,
          (usize)BOB64_PROCESS_PAGE_LIMIT,sizeof(*pages));
    if(!pages)return -1;
    kernel_root=read_cr3();
    table_checkpoint=bob64_bootstrap_space.TablePoolUsed;
    heap_checkpoint=kernel_heap_high_water;
    if(bob64_process_init(&process,pages,(usize)BOB64_PROCESS_PAGE_LIMIT,&operations))
        goto done;
    if(bob64_page_table_init(&process_table,app->PhysicalAddressBits,
       bob64_bootstrap_space.PageTable.Allocate,bob64_bootstrap_space.PageTable.Access,
       bob64_bootstrap_space.PageTable.Context))goto done;
    if(bob64_page_table_clone_isolated(&process_table,&bob64_bootstrap_space.PageTable,
       BOB64_PROCESS_IMAGE_BASE,BOB64_PROCESS_ISOLATED_SIZE))goto done;
    __asm__ volatile("mov %0,%%cr3"::"r"(process_table.RootPhysical):"memory");
    if(bob64_process_load(&process,image,image_size,argument_count,arguments))goto restore;
    application_privilege_stack=bob64_heap_alloc(&kernel_heap,
        (usize)BOB64_KERNEL_PRIVILEGE_STACK_SIZE);
    if(!application_privilege_stack) {
        if(bob64_process_unload(&process))cleanup_error=1;
        goto restore;
    }
    for(usize i=0;i<(usize)BOB64_KERNEL_PRIVILEGE_STACK_SIZE;i++)
        ((u8 *)application_privilege_stack)[i]=0;
    u64 application_stack_top=(u64)(uintptr_t)application_privilege_stack+
                               BOB64_KERNEL_PRIVILEGE_STACK_SIZE;
    if(bob64_tss_set_rsp0(&bob64_kernel_tss,application_stack_top)||
       bob64_kernel_tss.Rsp0!=application_stack_top) {
        if(bob64_process_unload(&process))cleanup_error=1;
        goto restore;
    }
    if(bob64_syscall_context_push()) {
        if(bob64_process_unload(&process))cleanup_error=1;
        goto restore;
    }
    syscall_context_saved=1;
    kernel_user_output_length=0;
    kernel_user_output_truncated=0;
    u32 display_width,display_height;
    if(!bob64_framebuffer_resolution(&display_width,&display_height)) {
        window_owner=kernel_next_window_owner++;
        if(!window_owner)window_owner=kernel_next_window_owner++;
        window_server=(BOB64_WINDOW_SERVER *)bob64_heap_calloc(&kernel_heap,1,
                                                               sizeof(*window_server));
        if(window_server&&!bob64_window_server_init(window_server,&kernel_heap,
              window_owner,display_width,display_height))window_server_ready=1;
        else if(window_server) {
            bob64_window_server_close(window_server);
            if(bob64_heap_free(&kernel_heap,window_server))cleanup_error=1;
            window_server=0;
        }
    }
    bob64_syscall_set_address_space(&process_table);
    bob64_syscall_set_read_user(kernel_copy_user_bytes,0);
    bob64_syscall_set_write_user(kernel_write_user_bytes,0);
    bob64_syscall_set_filesystem(kernel_app_read_file,kernel_app_write_file,app);
    bob64_syscall_set_file_stream(kernel_app_open_stream,kernel_app_read_stream,
                                  kernel_app_write_stream,app);
    bob64_syscall_set_file_manager(kernel_app_list_files,kernel_app_delete_file,app);
    bob64_syscall_set_app_runner(kernel_app_run_application,app);
    bob64_syscall_set_write(kernel_user_write_character,0);
    bob64_syscall_set_wait_event(kernel_wait_for_input_event,0);
    bob64_syscall_set_window_server(window_server_ready?window_server:0,
                                    window_owner);
    *started=1;entered=1;
    status=bob64_enter_user(process.EntryAddress,process.StartupAddress,
                             process.InitialStackPointer);
    kernel_user_output[kernel_user_output_length]=0;
    if(bob64_tss_set_rsp0(&bob64_kernel_tss,previous_privilege_stack)||
       bob64_kernel_tss.Rsp0!=previous_privilege_stack)cleanup_error=1;
    if(bob64_heap_free(&kernel_heap,application_privilege_stack))cleanup_error=1;
    application_privilege_stack=0;
    if(window_server_ready)bob64_window_server_close(window_server);
    if(window_server&&bob64_heap_free(&kernel_heap,window_server))cleanup_error=1;
    if(syscall_context_saved&&bob64_syscall_context_pop())cleanup_error=1;
    kernel_write("\r\n");
    if(bob64_process_unload(&process))cleanup_error=1;
restore:
    if(application_privilege_stack) {
        if(bob64_tss_set_rsp0(&bob64_kernel_tss,previous_privilege_stack))
            cleanup_error=1;
        if(bob64_heap_free(&kernel_heap,application_privilege_stack))cleanup_error=1;
        application_privilege_stack=0;
    }
    __asm__ volatile("mov %0,%%cr3"::"r"(kernel_root):"memory");
done:
    /* A heap peak can leave shared page-table levels mapped after the heap
       later shrinks. Preserve those table pages across this address space. */
    if(kernel_heap_high_water==heap_checkpoint)
        bob64_bootstrap_space.TablePoolUsed=table_checkpoint;
    if(bob64_heap_free(&kernel_heap,pages)||cleanup_error)status=-1;
    if(!entered)*started=0;
    return status;
}

static int kernel_app_run_application(void *context,const char *name,
                                      usize argument_count,
                                      const char *const *arguments,
                                      s64 *exit_status) {
    int started=0;
    s64 status;
    if(!exit_status)return -22;
    status=kernel_run_application(context,name,argument_count,arguments,&started);
    if(!started)return -2;
    *exit_status=status;
    return 0;
}

static int kernel_compile_application(void *context,const char *source_name,
                                      const char *output_name,usize *error_offset) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    const char *source;
    usize source_length,output_length;
    int result;
    if(!app||!app->Filesystem||!source_name||!output_name||
       bob64_fs_read(app->Filesystem,source_name,&source,&source_length))return -1;
    result=bob64_compile_c(source,source_length,kernel_compiler_output,
                           sizeof(kernel_compiler_output),&output_length,error_offset);
    if(result)return result;
    if(bob64_fs_write(app->Filesystem,output_name,kernel_compiler_output,output_length))
        return -1;
    return 0;
}

static void kernel_hex64(u64 value) {
    static const char digits[]="0123456789abcdef";
    char text[17];
    for(u32 i=0;i<16;i++)text[i]=digits[(value>>(60-i*4))&15];
    text[16]=0;
    kernel_write(text);
}

static int kernel_heap_grow(void *context,void **region,usize *region_size) {
    const BOB64_KERNEL_BOOT_INFO *info=(const BOB64_KERNEL_BOOT_INFO *)context;
    u64 physical,virtual_address;
    int map_result;
    if(!info||!region||!region_size||kernel_heap_mapped>=BOB64_KERNEL_HEAP_LIMIT)
        return 0;
    physical=bob64_page_alloc(&bob64_boot_page_allocator,1);
    if(!physical) {
        kernel_write("bob64 kernel: heap physical page allocation failed\r\n");
        return 0;
    }
    virtual_address=BOB64_KERNEL_HEAP_BASE+kernel_heap_mapped;
    map_result=bob64_page_map(&bob64_bootstrap_space.PageTable,virtual_address,
       physical,BOB64_PAGE_WRITE|(info->NxSupported?BOB64_PAGE_NX:0));
    if(map_result) {
        kernel_write("bob64 kernel: heap page-table mapping failed (result=0x");
        kernel_hex64((u64)(s64)map_result);
        kernel_write(" tables=0x");kernel_hex64(bob64_bootstrap_space.TablePoolUsed);
        kernel_write("/0x");kernel_hex64(bob64_bootstrap_space.TablePoolPages);
        kernel_write(")\r\n");
        bob64_page_free(&bob64_boot_page_allocator,physical,1);
        return 0;
    }
    __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)virtual_address):"memory");
    volatile u64 *words=(volatile u64 *)(uintptr_t)virtual_address;
    for(usize i=0;i<EFI_PAGE_SIZE/sizeof(u64);i++)words[i]=0;
    kernel_heap_mapped+=(usize)EFI_PAGE_SIZE;
    if(kernel_heap_mapped>kernel_heap_high_water)
        kernel_heap_high_water=kernel_heap_mapped;
    *region=(void *)(uintptr_t)virtual_address;
    *region_size=(usize)EFI_PAGE_SIZE;
    return 1;
}

static int kernel_heap_shrink(void *context,void *region,usize region_size) {
    const BOB64_KERNEL_BOOT_INFO *info=(const BOB64_KERNEL_BOOT_INFO *)context;
    u64 expected;
    if(!info||!region||!region_size||
       (region_size&(usize)(EFI_PAGE_SIZE-1))||
       region_size>kernel_heap_mapped||
       region_size>~(u64)0-(u64)(uintptr_t)BOB64_KERNEL_HEAP_BASE)
        return -1;
    expected=BOB64_KERNEL_HEAP_BASE+kernel_heap_mapped-region_size;
    if((u64)(uintptr_t)region!=expected)return -1;
    for(usize offset=0;offset<region_size;offset+=(usize)EFI_PAGE_SIZE) {
        u64 translated,flags;
        if(bob64_page_translate(&bob64_bootstrap_space.PageTable,
             expected+offset,&translated,&flags)!=1||
           (translated&(EFI_PAGE_SIZE-1))||
           (flags&(BOB64_PAGE_PRESENT|BOB64_PAGE_WRITE))!=
             (BOB64_PAGE_PRESENT|BOB64_PAGE_WRITE))return -1;
    }
    for(usize offset=0;offset<region_size;offset+=(usize)EFI_PAGE_SIZE) {
        u64 unmapped_physical;
        if(bob64_page_unmap(&bob64_bootstrap_space.PageTable,expected+offset,
                            &unmapped_physical,0))return -1;
        __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)(expected+offset)):
                         "memory");
        if(bob64_page_free(&bob64_boot_page_allocator,unmapped_physical,1))
            return -1;
    }
    kernel_heap_mapped-=region_size;
    return 0;
}

static int kernel_heap_smoke_test(const BOB64_KERNEL_BOOT_INFO *info) {
    char *first,*large,*zero;
    if(bob64_heap_init(&kernel_heap,(usize)BOB64_KERNEL_HEAP_LIMIT,
                       kernel_heap_grow,(void *)info))return -1;
    bob64_heap_set_shrink(&kernel_heap,kernel_heap_shrink);
    first=(char *)bob64_heap_alloc(&kernel_heap,64);
    large=(char *)bob64_heap_alloc(&kernel_heap,5000);
    zero=(char *)bob64_heap_calloc(&kernel_heap,32,2);
    if(!first||!large||!zero||((uintptr_t)first&15)||((uintptr_t)large&15)||
       ((uintptr_t)zero&15)||!((uintptr_t)first>=BOB64_KERNEL_HEAP_BASE))return -1;
    first[0]='b';first[1]='o';first[2]='b';first[3]=0;
    large[0]='6';large[1]='4';large[4999]='!';
    for(usize i=0;i<64;i++)if(zero[i])return -1;
    if(first[0]!='b'||first[1]!='o'||first[2]!='b'||large[0]!='6'||
       large[1]!='4'||large[4999]!='!')return -1;
    if(bob64_heap_free(&kernel_heap,large)||bob64_heap_free(&kernel_heap,first)||
       bob64_heap_free(&kernel_heap,zero)||bob64_heap_free(&kernel_heap,zero)!=-1)
        return -1;
    if(bob64_heap_mapped_bytes(&kernel_heap)!=EFI_PAGE_SIZE||
       kernel_heap_mapped!=EFI_PAGE_SIZE)return -1;
    return 0;
}

static int kernel_table_pool_smoke_test(const BOB64_KERNEL_BOOT_INFO *info) {
    BOB64_PAGE_TABLE table;
    u64 checkpoint,initial_pages,physical;
    if(!info||!bob64_bootstrap_space.TablePoolPages)return -1;
    checkpoint=bob64_bootstrap_space.TablePoolUsed;
    initial_pages=bob64_bootstrap_space.TablePoolPages;
    if(bob64_page_table_init(&table,info->PhysicalAddressBits,
       bob64_bootstrap_space.PageTable.Allocate,
       bob64_bootstrap_space.PageTable.Access,
       bob64_bootstrap_space.PageTable.Context))return -1;
    for(u64 i=0;i<140;i++) {
        u64 virtual_address=0x0000001000000000ULL+i*(2ULL<<20);
        u64 physical_address=0x0000000010000000ULL+i*BOB64_PAGE_SIZE;
        if(bob64_page_map(&table,virtual_address,physical_address,
                          BOB64_PAGE_WRITE|BOB64_PAGE_NX)) {
            bob64_bootstrap_space.TablePoolUsed=checkpoint;
            return -1;
        }
    }
    int valid=bob64_bootstrap_space.TablePoolPages>initial_pages&&
        bob64_bootstrap_space.TablePoolUsed>checkpoint&&
        bob64_page_translate(&table,0x0000001000000000ULL+139*(2ULL<<20),
                             &physical,0)==1&&
        physical==0x0000000010000000ULL+139*BOB64_PAGE_SIZE;
    bob64_bootstrap_space.TablePoolUsed=checkpoint;
    return valid?0:-1;
}

static int kernel_privilege_stack_init(void) {
    kernel_privilege_stack=bob64_heap_alloc(&kernel_heap,
                                            (usize)BOB64_KERNEL_PRIVILEGE_STACK_SIZE);
    if(!kernel_privilege_stack)return -1;
    for(usize i=0;i<(usize)BOB64_KERNEL_PRIVILEGE_STACK_SIZE;i++)
        ((u8 *)kernel_privilege_stack)[i]=0;
    return bob64_tss_set_rsp0(&bob64_kernel_tss,
        (u64)(uintptr_t)kernel_privilege_stack+
        BOB64_KERNEL_PRIVILEGE_STACK_SIZE);
}

static void shell_write(void *context,const char *text) {
    (void)context;
    kernel_write(text);
}

static void shell_clear(void *context) {
    (void)context;
    bob64_framebuffer_clear();
    bob64_early_console_write("\x1b[2J\x1b[H");
}

static EFI_RUNTIME_SERVICES *kernel_runtime_services(
        const BOB64_KERNEL_BOOT_INFO *info) {
    return info&&info->RuntimeServices?
        (EFI_RUNTIME_SERVICES *)(uintptr_t)info->RuntimeServices:0;
}

static int kernel_snapshot_save(void *context,const void *snapshot,
                                usize snapshot_size) {
    const BOB64_KERNEL_BOOT_INFO *info=(const BOB64_KERNEL_BOOT_INFO *)context;
    EFI_STATUS firmware_status=EFI_SUCCESS;
    UINTN maximum_variable_size=0,remaining_storage_size=0;
    int result=bob64_firmware_snapshot_save(kernel_runtime_services(info),
        &kernel_heap,snapshot,snapshot_size,&firmware_status,
        &maximum_variable_size,&remaining_storage_size);
    if(result) {
        kernel_write("firmware snapshot save failed (EFI 0x");
        kernel_hex64(firmware_status);kernel_write(" max=0x");
        kernel_hex64(maximum_variable_size);kernel_write(" free=0x");
        kernel_hex64(remaining_storage_size);kernel_write(")\r\n");
    }
    for(u32 i=0;i<kernel_xhci_storage_device_count;i++) {
        KERNEL_XHCI_STORAGE_DEVICE *device=&kernel_xhci_storage_devices[i];
        if(!device->HasDataPartition||device->BlockDevice.ReadOnly)continue;
        int disk_result=bob64_disk_store_save(&device->BlockDevice,
            &device->DataPartition,&kernel_filesystem,&kernel_heap);
        if(!disk_result) {
            kernel_write("bob64 kernel: disk B64S snapshot saved\r\n");
            return 0;
        }
        kernel_write("bob64 kernel: disk B64S snapshot save failed result=");
        kernel_hex64((u64)(s64)disk_result);kernel_write("\r\n");
        return result?disk_result:0;
    }
    return result;
}

static int kernel_snapshot_restore(void *context,BOB64_FILESYSTEM *filesystem) {
    const BOB64_KERNEL_BOOT_INFO *info=(const BOB64_KERNEL_BOOT_INFO *)context;
    EFI_RUNTIME_SERVICES *services=kernel_runtime_services(info);
    int disk_error=0;
    for(u32 i=0;i<kernel_xhci_storage_device_count;i++) {
        KERNEL_XHCI_STORAGE_DEVICE *device=&kernel_xhci_storage_devices[i];
        if(!device->HasDataPartition)continue;
        int disk_result=bob64_disk_store_restore(&device->BlockDevice,
            &device->DataPartition,filesystem,&kernel_heap);
        if(!disk_result) {
            kernel_write("bob64 kernel: disk B64S snapshot restored\r\n");
            return 0;
        }
        if(disk_result<0)disk_error=disk_result;
    }
    int firmware_result=services?bob64_firmware_snapshot_restore(services,
        filesystem):BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND;
    if(!firmware_result)return 0;
    return disk_error?disk_error:firmware_result;
}

__attribute__((noreturn)) void bob64_kernel_main(const BOB64_KERNEL_BOOT_INFO *info) {
    bob64_early_console_init();
    if(info&&info->HasFramebuffer&&bob64_framebuffer_init(info->FramebufferBase,
       info->FramebufferSize,info->FramebufferWidth,info->FramebufferHeight,
       info->FramebufferPixelsPerScanLine,info->FramebufferPixelFormat))
        bob64_early_console_write("bob64 kernel: framebuffer rejected\r\n");
    kernel_write("bob64!\r\nbob64 kernel: long mode entry\r\n");
    if(!info||sizeof(void *)!=8||read_cr3()!=info->PageTableRoot||
       !info->MemoryMapAddress||!info->MemoryMapSize||
       info->MemoryDescriptorSize<40||!info->StackBase||!info->StackSize||
       info->PhysicalAddressBits<36||info->PhysicalAddressBits>52||
       info->NxSupported>1||info->HasFramebuffer>1) {
        kernel_write("bob64 kernel: invalid boot information\r\n");
        kernel_halt();
    }
    if(kernel_heap_smoke_test(info)) {
        kernel_write("bob64 kernel: heap initialization failed\r\n");
        kernel_halt();
    }
    if(kernel_table_pool_smoke_test(info)) {
        kernel_write("bob64 kernel: page-table pool growth failed\r\n");
        kernel_halt();
    }
    if(bob64_interrupts_enable_timer()) {
        kernel_write("bob64 kernel: timer IRQ unavailable\r\n");
        kernel_halt();
    }
    kernel_write("bob64 kernel: timer IRQ enabled\r\n");
    u64 initial_timer_ticks=bob64_timer_ticks();
    for(volatile u32 timer_wait=0;
        timer_wait<100000000u&&bob64_timer_ticks()==initial_timer_ticks;
        timer_wait++)
        __asm__ volatile("pause");
    if(bob64_timer_ticks()==initial_timer_ticks) {
        kernel_write("bob64 kernel: timer tick test failed\r\n");
        kernel_halt();
    }
    kernel_write("bob64 kernel: timer tick passed\r\n");
    u32 pci_device_count=0;
    if(!bob64_pci_enumerate(kernel_pci_read_config,0,
                            kernel_pci_report_usb,0,&pci_device_count)) {
        kernel_write("bob64 kernel: PCI enumeration passed, devices=");
        kernel_hex64(pci_device_count);kernel_write("\r\n");
        if(kernel_xhci_found) {
            int xhci_result=kernel_xhci_probe_capabilities(info);
            if(xhci_result) {
                kernel_write("bob64 kernel: xHCI capability probe failed, result=0x");
                kernel_hex64((u64)(s64)xhci_result);kernel_write(" phys-bits=");
                kernel_hex64(info->PhysicalAddressBits);kernel_write("\r\n");
            }
        }
    } else kernel_write("bob64 kernel: PCI enumeration unavailable\r\n");
    kernel_write("bob64 kernel: dynamic page-table pool passed\r\n");
    if(kernel_privilege_stack_init()) {
        kernel_write("bob64 kernel: privilege stack initialization failed\r\n");
        kernel_halt();
    }
    int process_test=kernel_process_smoke_test(info);
    if(process_test<0) {
        kernel_write("bob64 kernel: application loader smoke test failed\r\n");
        kernel_halt();
    }
    if(!process_test)kernel_write("bob64 kernel: ring-3 bob! app passed\r\n");
    else kernel_write("bob64 kernel: app test skipped (NX unavailable)\r\n");
    if(bob64_fs_init(&kernel_filesystem,&kernel_heap)) {
        kernel_write("bob64 kernel: filesystem initialization failed\r\n");
        kernel_halt();
    }
    if(info->NxSupported&&kernel_install_bob_application(&kernel_filesystem))
        kernel_write("bob64 kernel: demo app install failed\r\n");
    if(info->NxSupported&&kernel_install_gui_application(&kernel_filesystem))
        kernel_write("bob64 kernel: desktop app install failed\r\n");
    if(info->NxSupported&&kernel_install_nested_application(&kernel_filesystem))
        kernel_write("bob64 kernel: nested smoke app install failed\r\n");
    if(info->NxSupported&&kernel_install_echo_application(&kernel_filesystem))
        kernel_write("bob64 kernel: echo app install failed\r\n");
    if(kernel_install_bob_source(&kernel_filesystem))
        kernel_write("bob64 kernel: C demo source install failed\r\n");
    if(info->NxSupported&&kernel_install_cat_application(&kernel_filesystem))
        kernel_write("bob64 kernel: cat app install failed\r\n");
    if(info->NxSupported&&kernel_install_ls_application(&kernel_filesystem))
        kernel_write("bob64 kernel: ls app install failed\r\n");
    if(info->NxSupported&&kernel_install_notes_application(&kernel_filesystem))
        kernel_write("bob64 kernel: notes app install failed\r\n");
    if(info->NxSupported&&kernel_install_info_application(&kernel_filesystem))
        kernel_write("bob64 kernel: info app install failed\r\n");
    if(info->NxSupported&&kernel_install_mouse_smoke_application(&kernel_filesystem))
        kernel_write("bob64 kernel: mouse smoke app install failed\r\n");
    if(info->NxSupported&&kernel_install_launcher_application(&kernel_filesystem))
        kernel_write("bob64 kernel: launcher app install failed\r\n");
    if(info->NxSupported&&kernel_install_display_application(&kernel_filesystem))
        kernel_write("bob64 kernel: graphics demo install failed\r\n");
    kernel_app_context.Filesystem=&kernel_filesystem;
    kernel_app_context.NxSupported=info->NxSupported;
    kernel_app_context.PhysicalAddressBits=info->PhysicalAddressBits;
    kernel_write("CR3=0x");kernel_hex64(info->PageTableRoot);
    kernel_write(" memory-map=0x");kernel_hex64(info->MemoryMapAddress);
    kernel_write(" bytes=0x");kernel_hex64(info->MemoryMapSize);
    kernel_write(" heap=0x");kernel_hex64(BOB64_KERNEL_HEAP_BASE);
    kernel_write(" mapped=0x");kernel_hex64((u64)bob64_heap_mapped_bytes(&kernel_heap));
    kernel_write("\r\n");
    kernel_write("bob64 kernel: memory checks passed\r\n");
    if(!bob64_interrupts_enable_serial())
        kernel_write("bob64 kernel: serial RX IRQ enabled\r\n");
    else
        kernel_write("bob64 kernel: serial RX polling fallback\r\n");
    bob64_keyboard_reset();
    int keyboard_irq_enabled=bob64_interrupts_enable_keyboard()==0;
    if(!keyboard_irq_enabled)
        kernel_write("bob64 kernel: keyboard IRQ unavailable; using polling\r\n");
    else kernel_write("bob64 kernel: keyboard IRQ enabled\r\n");
    if(info->NxSupported) {
        int started=0;
        s64 status=kernel_run_application(&kernel_app_context,"bob.b64e",0,0,&started);
        if(!started||status||(keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native streaming app test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native streaming app passed\r\n");
        started=0;
        status=kernel_run_application(&kernel_app_context,"nested.b64e",0,0,
                                      &started);
        if(!started||status||(keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: nested app return test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: nested app return passed\r\n");
        static const char *echo_arguments[]={"echo.b64e","bob!"};
        started=0;
        status=kernel_run_application(&kernel_app_context,"echo.b64e",2,
                                      echo_arguments,&started);
        if(!started||status||kernel_user_output_length!=5||
           !kernel_bytes_equal(kernel_user_output,"bob!\n")||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: echo argument test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native echo arguments passed\r\n");
        static const char *cat_arguments[]={"cat.b64e","cat-smoke.txt"};
        static const char cat_fixture[]="bob!";
        if(bob64_fs_write(&kernel_filesystem,"cat-smoke.txt",cat_fixture,
                          sizeof(cat_fixture)-1)) {
            kernel_write("bob64 kernel: cat fixture setup failed\r\n");
            kernel_halt();
        }
        started=0;
        status=kernel_run_application(&kernel_app_context,"cat.b64e",2,
                                      cat_arguments,&started);
        int cat_output_ok=kernel_user_output_length==5&&
                          kernel_bytes_equal(kernel_user_output,"bob!\n");
        if(bob64_fs_delete(&kernel_filesystem,"cat-smoke.txt")) {
            kernel_write("bob64 kernel: cat fixture cleanup failed\r\n");
            kernel_halt();
        }
        if(!started||status||!cat_output_ok||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native file-read app test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native file-read app passed\r\n");
        started=0;
        status=kernel_run_application(&kernel_app_context,"ls.b64e",0,0,
                                      &started);
        if(!started||status||kernel_user_output_truncated||
           !kernel_text_contains(kernel_user_output,"bob.b64e  ")||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native file-list app test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native file-list app passed\r\n");
        static const char *notes_put_arguments[]={"notes.b64e","put",
                                                   "notes-smoke.txt","bob!"};
        static const char *notes_show_arguments[]={"notes.b64e","show",
                                                    "notes-smoke.txt"};
        static const char *notes_delete_arguments[]={"notes.b64e","delete",
                                                      "notes-smoke.txt"};
        static const char *notes_copy_arguments[]={"notes.b64e","copy",
            "notes-large-source.txt","notes-large-copy.txt"};
        static u8 note_fixture_check[4];
        started=0;
        status=kernel_run_application(&kernel_app_context,"notes.b64e",4,
                                      notes_put_arguments,&started);
        s64 note_read_result=kernel_app_read_file(&kernel_app_context,
            "notes-smoke.txt",note_fixture_check,sizeof(note_fixture_check));
        if(!started||status||note_read_result!=4||
           note_fixture_check[0]!='b'||note_fixture_check[1]!='o'||
           note_fixture_check[2]!='b'||note_fixture_check[3]!='!'||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native notes write test failed\r\n");
            kernel_halt();
        }
        started=0;
        status=kernel_run_application(&kernel_app_context,"notes.b64e",3,
                                      notes_show_arguments,&started);
        if(!started||status||kernel_user_output_length!=5||
           !kernel_bytes_equal(kernel_user_output,"bob!\n")||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native notes show test failed\r\n");
            kernel_halt();
        }
        started=0;
        status=kernel_run_application(&kernel_app_context,"notes.b64e",3,
                                      notes_delete_arguments,&started);
        if(!started||status||kernel_app_read_file(&kernel_app_context,
            "notes-smoke.txt",note_fixture_check,sizeof(note_fixture_check))!=-2||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native notes delete test failed\r\n");
            kernel_halt();
        }
        static char large_note_fixture[12288];
        const char *large_note_copy=0;
        usize large_note_copy_length=0;
        for(usize i=0;i<sizeof(large_note_fixture);i++)
            large_note_fixture[i]=(i%80==79)?'\n':(char)('a'+i%26);
        if(bob64_fs_write(&kernel_filesystem,"notes-large-source.txt",
                          large_note_fixture,sizeof(large_note_fixture))) {
            kernel_write("bob64 kernel: large notes fixture setup failed\r\n");
            kernel_halt();
        }
        started=0;
        status=kernel_run_application(&kernel_app_context,"notes.b64e",4,
                                      notes_copy_arguments,&started);
        int large_note_match=!bob64_fs_read(&kernel_filesystem,
            "notes-large-copy.txt",&large_note_copy,&large_note_copy_length)&&
            large_note_copy_length==sizeof(large_note_fixture);
        for(usize i=0;large_note_match&&i<sizeof(large_note_fixture);i++)
            if(large_note_copy[i]!=large_note_fixture[i])large_note_match=0;
        if(!started||status||kernel_user_output_length!=8||
           !kernel_bytes_equal(kernel_user_output,"Copied.\n")||
           kernel_user_output_truncated||!large_note_match||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: large notes streaming test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: large notes streaming copy passed\r\n");
        if(bob64_fs_delete(&kernel_filesystem,"notes-large-source.txt")||
           bob64_fs_delete(&kernel_filesystem,"notes-large-copy.txt")) {
            kernel_write("bob64 kernel: large notes fixture cleanup failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native notes app passed\r\n");
        started=0;
        status=kernel_run_application(&kernel_app_context,"info.b64e",0,0,
                                      &started);
        if(!started||status||kernel_user_output_truncated||
           !kernel_text_contains(kernel_user_output,
                                 "bob64 system information\n")||
           !kernel_text_contains(kernel_user_output,"System call ABI: 12\n")||
           !kernel_text_contains(kernel_user_output,"Uptime: ")||
           !kernel_text_contains(kernel_user_output,"RAM files: ")||
           !kernel_text_contains(kernel_user_output,"RAM file data: ")||
           kernel_user_output_length<5||
           !kernel_bytes_equal(kernel_user_output+
               kernel_user_output_length-5,"bob!\n")||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native info app test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native info app passed\r\n");
    }
    int snapshot_restore=kernel_snapshot_restore((void *)info,
                                                  &kernel_filesystem);
    if(!snapshot_restore) {
        kernel_write("bob64 kernel: persistent B64S snapshot restored\r\n");
        const char *persisted_contents;
        usize persisted_length;
        if(!bob64_fs_read(&kernel_filesystem,"persist.txt",&persisted_contents,
                          &persisted_length)&&persisted_length==8&&
           kernel_bytes_equal(persisted_contents,"survived"))
            kernel_write("bob64 kernel: persistent B64S smoke file passed\r\n");
    } else if(snapshot_restore<0)
        kernel_write("bob64 kernel: persistent B64S snapshot rejected\r\n");
    u32 display_width,display_height;
    if(!bob64_framebuffer_resolution(&display_width,&display_height)) {
        if(bob64_mouse_init(display_width,display_height))
            kernel_write("bob64 kernel: PS/2 mouse unavailable\r\n");
        else if(bob64_interrupts_enable_mouse())
            kernel_write("bob64 kernel: mouse IRQ unavailable; using polling\r\n");
        else kernel_write("bob64 kernel: PS/2 mouse IRQ enabled\r\n");
    }
    if(bob64_shell_init(&kernel_shell,&bob64_boot_page_allocator,&kernel_heap,
                        &kernel_filesystem,
                        shell_write,shell_clear,&kernel_app_context)) {
        kernel_write("bob64 kernel: shell initialization failed\r\n");
        kernel_halt();
    }
    bob64_shell_set_runner(&kernel_shell,kernel_run_application);
    bob64_shell_set_compiler(&kernel_shell,kernel_compile_application);
    bob64_shell_set_snapshot_storage(&kernel_shell,kernel_snapshot_save,
                                     kernel_snapshot_restore,(void *)info);
    for(;;) {
        kernel_xhci_usb_hid_poll();
        int character=bob64_keyboard_poll();
        if(character<0)character=bob64_early_console_try_read();
        if(character>=0)bob64_shell_input(&kernel_shell,character);
        else __asm__ volatile("pause");
    }
}
