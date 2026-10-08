#ifndef BOB64_DESCRIPTORS_H
#define BOB64_DESCRIPTORS_H

#include "types.h"

#define BOB64_GDT_KERNEL_CODE_SELECTOR 0x08u
#define BOB64_GDT_KERNEL_DATA_SELECTOR 0x10u
#define BOB64_GDT_USER_DATA_SELECTOR 0x1bu
#define BOB64_GDT_USER_CODE_SELECTOR 0x23u
#define BOB64_GDT_TSS_SELECTOR 0x28u
#define BOB64_GDT_ENTRY_COUNT 7u
#define BOB64_IDT_ENTRIES 256
#define BOB64_IDT_INTERRUPT_GATE 0x0eu
#define BOB64_IDT_TRAP_GATE 0x0fu

typedef struct __attribute__((packed)) {
    u16 Limit;
    u64 Base;
} BOB64_DESCRIPTOR_TABLE_POINTER;

typedef struct __attribute__((packed)) {
    u16 OffsetLow;
    u16 Selector;
    u8 Ist;
    u8 TypeAttributes;
    u16 OffsetMiddle;
    u32 OffsetHigh;
    u32 Reserved;
} BOB64_IDT_GATE;

typedef struct __attribute__((packed)) {
    u32 Reserved0;
    u64 Rsp0,Rsp1,Rsp2;
    u64 Reserved1;
    u64 Ist1,Ist2,Ist3,Ist4,Ist5,Ist6,Ist7;
    u64 Reserved2;
    u16 Reserved3;
    u16 IoMapBase;
} BOB64_TSS;

_Static_assert(sizeof(BOB64_DESCRIPTOR_TABLE_POINTER)==10,"x86-64 GDTR/IDTR layout");
_Static_assert(sizeof(BOB64_IDT_GATE)==16,"x86-64 IDT gate layout");
_Static_assert(sizeof(BOB64_TSS)==104,"x86-64 TSS layout");
_Static_assert(__builtin_offsetof(BOB64_TSS,Rsp0)==4,"x86-64 TSS RSP0 offset");
_Static_assert(__builtin_offsetof(BOB64_TSS,Ist1)==36,"x86-64 TSS IST offset");
_Static_assert(__builtin_offsetof(BOB64_TSS,IoMapBase)==102,"x86-64 TSS I/O map offset");

int bob64_tss_init(BOB64_TSS *tss,u64 ring0_stack_top);
int bob64_tss_set_rsp0(BOB64_TSS *tss,u64 ring0_stack_top);
void bob64_gdt_init(u64 entries[BOB64_GDT_ENTRY_COUNT],
                    BOB64_DESCRIPTOR_TABLE_POINTER *pointer,const BOB64_TSS *tss);
int bob64_idt_set_gate(BOB64_IDT_GATE *gate,u64 handler,u16 selector,u8 ist,
                       u8 dpl,u8 gate_type);
int bob64_idt_init(BOB64_IDT_GATE entries[BOB64_IDT_ENTRIES],u64 default_handler,
                   u16 selector,u8 ist);
void bob64_idt_pointer(BOB64_DESCRIPTOR_TABLE_POINTER *pointer,
                       const BOB64_IDT_GATE entries[BOB64_IDT_ENTRIES]);

#endif
