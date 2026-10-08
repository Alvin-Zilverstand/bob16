#include "descriptors.h"

#define BOB64_GDT_CODE 0x00af9a000000ffffULL
#define BOB64_GDT_DATA 0x00cf92000000ffffULL
#define BOB64_GDT_USER_CODE 0x00affa000000ffffULL
#define BOB64_GDT_USER_DATA 0x00cff2000000ffffULL
#define BOB64_GDT_TSS_ACCESS 0x89u
#define BOB64_IDT_PRESENT 0x80u

int bob64_tss_init(BOB64_TSS *tss,u64 ring0_stack_top) {
    if(!tss||!ring0_stack_top||(ring0_stack_top&15))return -1;
    for(usize i=0;i<sizeof(*tss);i++)((u8 *)tss)[i]=0;
    tss->Rsp0=ring0_stack_top;
    tss->IoMapBase=(u16)sizeof(*tss);
    return 0;
}

int bob64_tss_set_rsp0(BOB64_TSS *tss,u64 ring0_stack_top) {
    if(!tss||!ring0_stack_top||(ring0_stack_top&15))return -1;
    tss->Rsp0=ring0_stack_top;
    return 0;
}

void bob64_gdt_init(u64 entries[BOB64_GDT_ENTRY_COUNT],
                    BOB64_DESCRIPTOR_TABLE_POINTER *pointer,const BOB64_TSS *tss) {
    u64 base,limit,descriptor;
    if(!entries||!pointer||!tss)return;
    base=(u64)(uintptr_t)tss;limit=sizeof(*tss)-1;
    entries[0]=0;
    entries[1]=BOB64_GDT_CODE;
    entries[2]=BOB64_GDT_DATA;
    entries[3]=BOB64_GDT_USER_DATA;
    entries[4]=BOB64_GDT_USER_CODE;
    descriptor=(limit&0xffffULL)|((base&0xffffffULL)<<16)|
               ((u64)BOB64_GDT_TSS_ACCESS<<40)|
               ((limit&0xf0000ULL)<<32)|((base&0xff000000ULL)<<32);
    entries[5]=descriptor;entries[6]=base>>32;
    pointer->Limit=(u16)(BOB64_GDT_ENTRY_COUNT*sizeof(entries[0])-1);
    pointer->Base=(u64)(uintptr_t)entries;
}

int bob64_idt_set_gate(BOB64_IDT_GATE *gate,u64 handler,u16 selector,u8 ist,
                       u8 dpl,u8 gate_type) {
    if(!gate||!handler||!selector||ist>7||dpl>3||
       (gate_type!=BOB64_IDT_INTERRUPT_GATE&&gate_type!=BOB64_IDT_TRAP_GATE))
        return -1;
    gate->OffsetLow=(u16)handler;
    gate->Selector=selector;
    gate->Ist=ist;
    gate->TypeAttributes=(u8)(BOB64_IDT_PRESENT|(dpl<<5)|gate_type);
    gate->OffsetMiddle=(u16)(handler>>16);
    gate->OffsetHigh=(u32)(handler>>32);
    gate->Reserved=0;
    return 0;
}

int bob64_idt_init(BOB64_IDT_GATE entries[BOB64_IDT_ENTRIES],u64 default_handler,
                   u16 selector,u8 ist) {
    BOB64_IDT_GATE default_gate;
    if(!entries||bob64_idt_set_gate(&default_gate,default_handler,selector,ist,0,
                                    BOB64_IDT_INTERRUPT_GATE))return -1;
    for(usize i=0;i<BOB64_IDT_ENTRIES;i++)entries[i]=default_gate;
    return 0;
}

void bob64_idt_pointer(BOB64_DESCRIPTOR_TABLE_POINTER *pointer,
                       const BOB64_IDT_GATE entries[BOB64_IDT_ENTRIES]) {
    if(!pointer||!entries)return;
    pointer->Limit=(u16)(BOB64_IDT_ENTRIES*sizeof(entries[0])-1);
    pointer->Base=(u64)(uintptr_t)entries;
}
