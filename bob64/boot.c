#include "efi.h"
#include "memory.h"
#include "cpu.h"
#include "bootstrap.h"
#include "descriptors.h"
#include "interrupts.h"
#include "kernel.h"

#define BOB64_MEMORY_MAP_CAPACITY 131072
#define BOB64_BOOT_STACK_PAGES 8
#define BOB64_TABLE_POOL_PAGES 128
#define BOB64_FRAMEBUFFER_MAP_LIMIT (64ULL*1024ULL*1024ULL)

static u8 memory_map[BOB64_MEMORY_MAP_CAPACITY] __attribute__((aligned(16)));
BOB64_PAGE_EXTENT bob64_boot_page_extents[BOB64_PAGE_EXTENT_LIMIT];
BOB64_PAGE_ALLOCATOR bob64_boot_page_allocator;
BOB64_BOOTSTRAP_SPACE bob64_bootstrap_space;
static u64 kernel_gdt[BOB64_GDT_ENTRY_COUNT];
BOB64_TSS bob64_kernel_tss;
static BOB64_IDT_GATE kernel_idt[BOB64_IDT_ENTRIES];
static BOB64_DESCRIPTOR_TABLE_POINTER kernel_gdtr,kernel_idtr;
#ifdef BOB64_ENABLE_HANDOFF
static BOB64_KERNEL_BOOT_INFO kernel_boot_info;
#endif
static const EFI_GUID loaded_image_guid={0x5b1b31a1u,0x9562u,0x11d2u,
                                         {0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
#ifdef BOB64_ENABLE_HANDOFF
static const EFI_GUID graphics_output_guid={0x9042a9deu,0x23dcu,0x4a38u,
                                            {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
#endif

static u64 *physical_identity_access(void *context,u64 physical_address) {
    (void)context;
    return (u64 *)(uintptr_t)physical_address;
}

static EFI_STATUS efi_print(EFI_SYSTEM_TABLE *system,const CHAR16 *text) {
    if(!system || !system->ConOut || !system->ConOut->OutputString)
        return EFI_UNSUPPORTED;
    return system->ConOut->OutputString(system->ConOut,text);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *system) {
    static const CHAR16 active[]={'b','o','b','6','4',':',' ','l','o','n','g',' ' ,
        'm','o','d','e',' ','a','c','t','i','v','e','\r','\n',0};
    static const CHAR16 unsupported[]={'b','o','b','6','4',':',' ','x','8','6','-','6','4',
        ' ','l','o','n','g',' ','m','o','d','e',' ','r','e','q','u','i','r','e','d','\r','\n',0};
#ifndef BOB64_ENABLE_HANDOFF
    static const CHAR16 prepared[]={'b','o','b','6','4',':',' ','b','o','o','t','s','t','r','a','p',
        ' ','p','a','g','i','n','g',' ','p','r','e','p','a','r','e','d','\r','\n',0};
#endif
#ifdef BOB64_ENABLE_HANDOFF
    static const CHAR16 handoff[]={'b','o','b','6','4',':',' ','e','x','i','t','i','n','g',' ' ,
        'U','E','F','I',' ','b','o','o','t',' ','s','e','r','v','i','c','e','s','\r','\n',0};
#endif
    static const CHAR16 marker[]={'b','o','b','6','4','!','\r','\n',0};
    /* This image-base-relative pointer must be repaired if firmware relocates the PE image. */
    static const CHAR16 * volatile relocated_marker=marker;
    UINTN map_capacity=sizeof(memory_map),map_size=sizeof(memory_map),map_key=0,descriptor_size=0;
    u32 descriptor_version=0;
    EFI_STATUS status;
    EFI_MEMORY_DESCRIPTOR *map=(EFI_MEMORY_DESCRIPTOR *)memory_map;
    BOB64_CPU_FEATURES cpu;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
    void *loaded_image_interface=0;
#ifdef BOB64_ENABLE_HANDOFF
    EFI_GRAPHICS_OUTPUT_PROTOCOL *graphics=0;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *graphics_mode=0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *graphics_info=0;
    int framebuffer_valid=0;
    u64 framebuffer_map_size=0;
#endif
    u64 stack_base,table_pool_base,translated;
    u64 probe_page;
    BOB64_PAGE_EXTENT *active_page_extents=bob64_boot_page_extents;
    BOB64_PAGE_ALLOCATOR *active_page_allocator=&bob64_boot_page_allocator;
    BOB64_BOOTSTRAP_SPACE *active_bootstrap_space=&bob64_bootstrap_space;
    if(!system)return EFI_UNSUPPORTED;
    if(bob64_cpu_detect(&cpu)||!cpu.PAE||!cpu.LongMode||
       !bob64_cpu_long_mode_active(&cpu)) {
        efi_print(system,unsupported);
        return EFI_UNSUPPORTED;
    }
#ifdef BOB64_ENABLE_HANDOFF
    if(!bob64_cpu_four_level_paging_active()) {
        efi_print(system,unsupported);
        return EFI_UNSUPPORTED;
    }
#endif
    if(EFI_ERROR(efi_print(system,active)))return EFI_UNSUPPORTED;
    if(EFI_ERROR(efi_print(system,relocated_marker)))return EFI_UNSUPPORTED;
    if(!system->BootServices||!system->BootServices->GetMemoryMap||
       !system->BootServices->HandleProtocol)return EFI_UNSUPPORTED;
    status=system->BootServices->HandleProtocol(image,&loaded_image_guid,&loaded_image_interface);
    if(EFI_ERROR(status))return status;
    if(!loaded_image_interface)return EFI_UNSUPPORTED;
    loaded_image=(EFI_LOADED_IMAGE_PROTOCOL *)loaded_image_interface;
    if(!loaded_image->ImageBase||!loaded_image->ImageSize)return EFI_UNSUPPORTED;
#ifdef BOB64_ENABLE_HANDOFF
    if(system->BootServices->LocateProtocol&&
       !EFI_ERROR(system->BootServices->LocateProtocol(&graphics_output_guid,0,
                                                        (void **)&graphics))&&
       graphics&&graphics->Mode&&(graphics_mode=graphics->Mode)&&
       graphics_mode->Info&&(graphics_info=graphics_mode->Info)&&
       (graphics_info->PixelFormat==0||graphics_info->PixelFormat==1)&&
       graphics_mode->FrameBufferBase&&graphics_mode->FrameBufferSize&&
       graphics_info->HorizontalResolution>=6&&graphics_info->VerticalResolution>=8&&
       graphics_info->PixelsPerScanLine>=graphics_info->HorizontalResolution&&
       (u64)graphics_info->PixelsPerScanLine<=~(u64)0/graphics_info->VerticalResolution) {
        u64 pixels=(u64)graphics_info->PixelsPerScanLine*graphics_info->VerticalResolution;
        if(pixels<=~(u64)0/4) {
            framebuffer_map_size=pixels*4;
            framebuffer_valid=framebuffer_map_size<=graphics_mode->FrameBufferSize&&
                              framebuffer_map_size<=BOB64_FRAMEBUFFER_MAP_LIMIT;
        }
    }
#endif
    status=system->BootServices->GetMemoryMap(&map_size,map,&map_key,&descriptor_size,&descriptor_version);
    if(status==EFI_BUFFER_TOO_SMALL) {
        if(!system->BootServices->AllocatePool)return status;
        UINTN slack=(descriptor_size?descriptor_size:sizeof(EFI_MEMORY_DESCRIPTOR))*2;
        if(map_size>(~(UINTN)0)-slack)return EFI_UNSUPPORTED;
        UINTN required=map_size+slack;
        status=system->BootServices->AllocatePool(EFI_MEMORY_LOADER_DATA,required,(void **)&map);
        if(EFI_ERROR(status))return status;
        map_capacity=required;map_size=required;
        status=system->BootServices->GetMemoryMap(&map_size,map,&map_key,&descriptor_size,&descriptor_version);
    }
    if(EFI_ERROR(status))return status;
    if(descriptor_version!=1||bob64_page_allocator_init(active_page_allocator,active_page_extents,
       BOB64_PAGE_EXTENT_LIMIT,cpu.PhysicalAddressBits,map,(usize)map_size,
       (usize)descriptor_size)<0)
        return EFI_UNSUPPORTED;
    probe_page=bob64_page_alloc(active_page_allocator,1);
    if(!probe_page||bob64_page_free(active_page_allocator,probe_page,1))return EFI_UNSUPPORTED;
    stack_base=bob64_page_alloc(active_page_allocator,BOB64_BOOT_STACK_PAGES);
    table_pool_base=bob64_page_alloc(active_page_allocator,BOB64_TABLE_POOL_PAGES);
    if(!stack_base||!table_pool_base)return EFI_UNSUPPORTED;
    volatile u64 *stack_words=(volatile u64 *)(uintptr_t)stack_base;
    for(usize i=0;i<BOB64_BOOT_STACK_PAGES*EFI_PAGE_SIZE/sizeof(u64);i++)stack_words[i]=0;
    if(bob64_tss_init(&bob64_kernel_tss,
       stack_base+BOB64_BOOT_STACK_PAGES*EFI_PAGE_SIZE))
        return EFI_UNSUPPORTED;
    bob64_gdt_init(kernel_gdt,&kernel_gdtr,&bob64_kernel_tss);
    bob64_idt_pointer(&kernel_idtr,kernel_idt);
    if(bob64_interrupts_build_idt(kernel_idt,BOB64_GDT_KERNEL_CODE_SELECTOR))
        return EFI_UNSUPPORTED;
    bob64_idt_pointer(&kernel_idtr,kernel_idt);
    if(bob64_bootstrap_space_init(active_bootstrap_space,cpu.PhysicalAddressBits,
       physical_identity_access,0,table_pool_base,BOB64_TABLE_POOL_PAGES,
       (u64)(uintptr_t)loaded_image->ImageBase,loaded_image->ImageSize,
       stack_base,BOB64_BOOT_STACK_PAGES*EFI_PAGE_SIZE,cpu.NX)<0)
        return EFI_UNSUPPORTED;
    u64 image_base=(u64)(uintptr_t)loaded_image->ImageBase;
    u64 map_base=(u64)(uintptr_t)map;
    int map_is_in_image=map_base>=image_base&&map_base-image_base<=loaded_image->ImageSize&&
       (u64)map_capacity<=loaded_image->ImageSize-(map_base-image_base);
    if(!map_is_in_image&&bob64_page_map_range(&active_bootstrap_space->PageTable,map_base,map_base,
       (u64)map_capacity,BOB64_PAGE_WRITE|(cpu.NX?BOB64_PAGE_NX:0)))
        return EFI_UNSUPPORTED;
#ifdef BOB64_ENABLE_HANDOFF
    if(framebuffer_valid&&bob64_page_map_range(&active_bootstrap_space->PageTable,
       graphics_mode->FrameBufferBase,graphics_mode->FrameBufferBase,
       framebuffer_map_size,BOB64_PAGE_WRITE|BOB64_PAGE_PCD|BOB64_PAGE_PWT|
       (cpu.NX?BOB64_PAGE_NX:0)))framebuffer_valid=0;
#endif
    if(bob64_page_translate(&active_bootstrap_space->PageTable,
       (u64)(uintptr_t)loaded_image->ImageBase,&translated,0)!=1||
       translated!=(u64)(uintptr_t)loaded_image->ImageBase||
       bob64_page_translate(&active_bootstrap_space->PageTable,
       stack_base+BOB64_BOOT_STACK_PAGES*EFI_PAGE_SIZE-1,&translated,0)!=1||
       translated!=stack_base+BOB64_BOOT_STACK_PAGES*EFI_PAGE_SIZE-1)
        return EFI_UNSUPPORTED;
#ifdef BOB64_ENABLE_HANDOFF
    if(!system->BootServices->ExitBootServices)return EFI_UNSUPPORTED;
    if(EFI_ERROR(efi_print(system,handoff)))return EFI_UNSUPPORTED;
    kernel_boot_info.PageTableRoot=active_bootstrap_space->PageTable.RootPhysical;
    kernel_boot_info.MemoryMapAddress=map_base;
    kernel_boot_info.MemoryDescriptorSize=descriptor_size;
    kernel_boot_info.StackBase=stack_base;
    kernel_boot_info.StackSize=BOB64_BOOT_STACK_PAGES*EFI_PAGE_SIZE;
    kernel_boot_info.PhysicalAddressBits=cpu.PhysicalAddressBits;
    kernel_boot_info.NxSupported=cpu.NX;
    if(framebuffer_valid) {
        kernel_boot_info.FramebufferBase=graphics_mode->FrameBufferBase;
        kernel_boot_info.FramebufferSize=framebuffer_map_size;
        kernel_boot_info.FramebufferWidth=graphics_info->HorizontalResolution;
        kernel_boot_info.FramebufferHeight=graphics_info->VerticalResolution;
        kernel_boot_info.FramebufferPixelsPerScanLine=graphics_info->PixelsPerScanLine;
        kernel_boot_info.FramebufferPixelFormat=graphics_info->PixelFormat;
        kernel_boot_info.HasFramebuffer=1;
    }
    for(u32 attempt=0;attempt<3;attempt++) {
        map_size=map_capacity;
        status=system->BootServices->GetMemoryMap(&map_size,map,&map_key,&descriptor_size,
                                                   &descriptor_version);
        if(EFI_ERROR(status))return status;
        if(descriptor_version!=1||!descriptor_size||map_size>map_capacity)return EFI_UNSUPPORTED;
        kernel_boot_info.MemoryMapSize=map_size;
        kernel_boot_info.MemoryDescriptorSize=descriptor_size;
        status=system->BootServices->ExitBootServices(image,map_key);
        if(!EFI_ERROR(status))
            bob64_enter_kernel(kernel_boot_info.PageTableRoot,
                stack_base+BOB64_BOOT_STACK_PAGES*EFI_PAGE_SIZE,&kernel_gdtr,&kernel_idtr,
                &kernel_boot_info);
        if(status!=EFI_INVALID_PARAMETER)return status;
    }
    return status;
#else
    if(EFI_ERROR(efi_print(system,prepared)))return EFI_UNSUPPORTED;
    return EFI_SUCCESS;
#endif
}
