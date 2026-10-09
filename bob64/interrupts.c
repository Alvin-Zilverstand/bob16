#include "interrupts.h"
#include "console.h"
#include "syscall.h"
#include "keyboard.h"
#include "mouse.h"

#define BOB64_COM1 0x3f8u
#define BOB64_SERIAL_INPUT_QUEUE_SIZE 128u
static int serial_available;
static volatile u8 serial_irq_active;
static u8 serial_input_queue[BOB64_SERIAL_INPUT_QUEUE_SIZE];
static volatile u32 serial_input_head,serial_input_tail;
static u8 pic_ready;
static u8 pic_master_mask=0xff,pic_slave_mask=0xff;
static volatile u64 timer_tick_count;
volatile u64 bob64_user_exception_vector=~(u64)0;
volatile u64 bob64_user_exception_error=~(u64)0;
volatile u64 bob64_user_exception_rip=~(u64)0;
volatile u64 bob64_user_expected_exception_vector=~(u64)0;
volatile u64 bob64_user_expected_exception_error_mask;
volatile u64 bob64_user_expected_exception_error_value;

static void port_out8(u16 port,u8 value) {
    __asm__ volatile("outb %0, %1"::"a"(value),"Nd"(port));
}

static u8 port_in8(u16 port) {
    u8 value;
    __asm__ volatile("inb %1, %0":"=a"(value):"Nd"(port));
    return value;
}

static void serial_init(void) {
    port_out8(BOB64_COM1+1,0x00);
    port_out8(BOB64_COM1+3,0x80);
    port_out8(BOB64_COM1+0,0x01);
    port_out8(BOB64_COM1+1,0x00);
    port_out8(BOB64_COM1+3,0x03);
    port_out8(BOB64_COM1+2,0xc7);
    port_out8(BOB64_COM1+4,0x0b);
    serial_available=port_in8(BOB64_COM1+5)!=0xff;
}

static void serial_char(char value) {
    if(!serial_available)return;
    for(u32 timeout=0;timeout<100000;timeout++) {
        if(port_in8(BOB64_COM1+5)&0x20) {
            port_out8(BOB64_COM1,(u8)value);
            return;
        }
    }
}

static void serial_text(const char *text) {
    while(*text)serial_char(*text++);
}

static void diagnostic_text(const char *text) {
    serial_text(text);
    bob64_framebuffer_write(text);
}

static void serial_hex64(u64 value) {
    static const char digits[]="0123456789abcdef";
    for(int shift=60;shift>=0;shift-=4)serial_char(digits[(value>>shift)&15]);
}

void bob64_early_console_init(void) {
    serial_init();
}

void bob64_early_console_write(const char *text) {
    if(text)serial_text(text);
}

void bob64_early_console_hex64(u64 value) {
    serial_hex64(value);
}

void bob64_serial_irq_reset(void) {
    serial_input_head=serial_input_tail=0;
}

int bob64_serial_irq_capture(u8 character) {
    u32 head=serial_input_head,tail=serial_input_tail;
    if((u32)(head-tail)>=BOB64_SERIAL_INPUT_QUEUE_SIZE)return -1;
    serial_input_queue[head&(BOB64_SERIAL_INPUT_QUEUE_SIZE-1)]=character;
    __asm__ volatile("":::"memory");
    serial_input_head=head+1;
    return 0;
}

int bob64_serial_irq_pop(u8 *character) {
    u32 tail=serial_input_tail,head=serial_input_head;
    if(!character||tail==head)return -1;
    __asm__ volatile("":::"memory");
    *character=serial_input_queue[tail&(BOB64_SERIAL_INPUT_QUEUE_SIZE-1)];
    __asm__ volatile("":::"memory");
    serial_input_tail=tail+1;
    return 0;
}

int bob64_early_console_try_read(void) {
    u8 character;
    if(serial_irq_active)return bob64_serial_irq_pop(&character)?-1:character;
    if(!serial_available||(port_in8(BOB64_COM1+5)&1)==0)return -1;
    return port_in8(BOB64_COM1);
}

static void diagnostic_register(const char *name,u64 value) {
    static const char digits[]="0123456789abcdef";
    char line[17];
    diagnostic_text(name);diagnostic_text("=0x");
    for(int i=0;i<16;i++)line[i]=digits[(value>>(60-i*4))&15];
    line[16]=0;
    diagnostic_text(line);diagnostic_text("\r\n");
}

int bob64_interrupts_build_idt(BOB64_IDT_GATE entries[BOB64_IDT_ENTRIES],u16 selector) {
    if(!entries||bob64_idt_init(entries,(u64)(uintptr_t)bob64_unhandled_interrupt,
                                selector,0))return -1;
    for(usize vector=0;vector<32;vector++) {
        u64 handler=(u64)(uintptr_t)bob64_exception_stub_table[vector];
        if(bob64_idt_set_gate(&entries[vector],handler,selector,0,0,
                              BOB64_IDT_INTERRUPT_GATE))return -1;
    }
    if(bob64_idt_set_gate(&entries[0x20],(u64)(uintptr_t)bob64_irq0_entry,
       selector,0,0,BOB64_IDT_INTERRUPT_GATE))return -1;
    if(bob64_idt_set_gate(&entries[0x24],(u64)(uintptr_t)bob64_irq4_entry,
       selector,0,0,BOB64_IDT_INTERRUPT_GATE))return -1;
    if(bob64_idt_set_gate(&entries[0x80],(u64)(uintptr_t)bob64_syscall_entry,
       selector,0,3,BOB64_IDT_INTERRUPT_GATE))return -1;
    if(bob64_idt_set_gate(&entries[0x21],(u64)(uintptr_t)bob64_irq1_entry,
       selector,0,0,BOB64_IDT_INTERRUPT_GATE))return -1;
    if(bob64_idt_set_gate(&entries[0x2c],(u64)(uintptr_t)bob64_irq12_entry,
       selector,0,0,BOB64_IDT_INTERRUPT_GATE))return -1;
    return 0;
}

static int pic_initialize(void) {
    if(pic_ready)return 0;
    pic_master_mask=pic_slave_mask=0xff;
    port_out8(0x21,pic_master_mask);port_out8(0xa1,pic_slave_mask);
    port_out8(0x20,0x11);port_out8(0x80,0);
    port_out8(0xa0,0x11);port_out8(0x80,0);
    port_out8(0x21,0x20);port_out8(0x80,0);
    port_out8(0xa1,0x28);port_out8(0x80,0);
    port_out8(0x21,0x04);port_out8(0x80,0);
    port_out8(0xa1,0x02);port_out8(0x80,0);
    port_out8(0x21,0x01);port_out8(0x80,0);
    port_out8(0xa1,0x01);port_out8(0x80,0);
    pic_ready=1;
    return 0;
}

static void pic_unmask(u8 irq) {
    if(irq<8)pic_master_mask=(u8)(pic_master_mask&~(1u<<irq));
    else {
        pic_slave_mask=(u8)(pic_slave_mask&~(1u<<(irq-8)));
        pic_master_mask=(u8)(pic_master_mask&~(1u<<2));
    }
    port_out8(0xa1,pic_slave_mask);port_out8(0x21,pic_master_mask);
}

int bob64_interrupts_enable_serial(void) {
    u64 saved_flags;
    if(!serial_available)return -1;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(saved_flags)::"memory");
    if(pic_initialize())goto failed;
    bob64_serial_irq_reset();
    while(port_in8(BOB64_COM1+5)&1u)(void)port_in8(BOB64_COM1);
    port_out8(BOB64_COM1+1,0x01); /* receive-data interrupt only */
    serial_irq_active=1;
    pic_unmask(4);
    if(saved_flags&0x200)__asm__ volatile("sti":::"memory");
    return 0;
failed:
    if(saved_flags&0x200)__asm__ volatile("sti":::"memory");
    return -1;
}

void bob64_timer_irq_tick(void) { timer_tick_count++; }
u64 bob64_timer_ticks(void) { return timer_tick_count; }

int bob64_interrupts_enable_timer(void) {
    u16 divisor=(u16)(1193182u/100u);
    if(pic_initialize())return -1;
    timer_tick_count=0;
    port_out8(0x43,0x36);
    port_out8(0x40,(u8)divisor);port_out8(0x40,(u8)(divisor>>8));
    pic_unmask(0);
    __asm__ volatile("sti":::"memory");
    return 0;
}

static int wait_keyboard_controller(u8 mask,u8 expected) {
    for(u32 i=0;i<100000;i++) {
        u8 status=port_in8(0x64);
        if(status==0xff)return -1;
        if((status&mask)==expected)return 0;
    }
    return -1;
}

int bob64_interrupts_enable_keyboard(void) {
    u8 command;
    if(pic_initialize())return -1;
    for(u32 i=0;i<32;i++) {
        u8 status=port_in8(0x64);
        if(status==0xff)return -1;
        if(!(status&1))break;
        if(status&0x20)return -1;
        (void)port_in8(0x60);
    }
    if(wait_keyboard_controller(0,0))return -1;
    port_out8(0x64,0x20);
    if(wait_keyboard_controller(1,1))return -1;
    if(port_in8(0x64)&0x20)return -1;
    command=port_in8(0x60);
    command=(u8)((command|1)&~0x10u); /* enable keyboard IRQ and clock */
    if(wait_keyboard_controller(2,0))return -1;
    port_out8(0x64,0x60);
    if(wait_keyboard_controller(2,0))return -1;
    port_out8(0x60,command);
    bob64_keyboard_reset();
    bob64_keyboard_irq_set_active(1);
    pic_unmask(1);
    __asm__ volatile("sti":::"memory");
    return 0;
}

int bob64_interrupts_enable_mouse(void) {
    u64 saved_flags;
    u8 command;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(saved_flags)::"memory");
    for(u32 i=0;i<32;i++) {
        u8 status=port_in8(0x64);
        if(status==0xff)goto failed;
        if(!(status&1))break;
        if(!(status&0x20))goto failed;
        (void)port_in8(0x60);
    }
    if(wait_keyboard_controller(0,0))goto failed;
    port_out8(0x64,0x20);
    if(wait_keyboard_controller(1,1)||(port_in8(0x64)&0x20))goto failed;
    command=port_in8(0x60);
    command=(u8)((command|2)&~0x20u); /* enable mouse IRQ and clock */
    if(wait_keyboard_controller(2,0))goto failed;
    port_out8(0x64,0x60);
    if(wait_keyboard_controller(2,0))goto failed;
    port_out8(0x60,command);
    bob64_mouse_irq_set_active(1);
    pic_unmask(12);
    __asm__ volatile("sti":::"memory");
    return 0;
failed:
    if(saved_flags&0x200)__asm__ volatile("sti":::"memory");
    return -1;
}

void BOB64_MS_ABI bob64_irq_dispatch(const BOB64_INTERRUPT_FRAME *frame) {
    if(frame&&frame->Vector==0x20) bob64_timer_irq_tick();
    else if(frame&&frame->Vector==0x21) (void)bob64_keyboard_irq_service();
    else if(frame&&frame->Vector==0x24) {
        for(u32 pending=0;pending<16;pending++) {
            u8 interrupt_id=port_in8(BOB64_COM1+2);
            u8 reason=(u8)((interrupt_id>>1)&7u);
            if(interrupt_id&1u)break;
            if(reason==2||reason==6||reason==3) {
                u8 line_status=port_in8(BOB64_COM1+5);
                while(line_status&1u) {
                    (void)bob64_serial_irq_capture(port_in8(BOB64_COM1));
                    line_status=port_in8(BOB64_COM1+5);
                }
            } else if(reason==0)(void)port_in8(BOB64_COM1+6);
            else break;
        }
    }
    else if(frame&&frame->Vector==0x2c) {
        (void)bob64_mouse_irq_service();
        port_out8(0xa0,0x20);
    }
    /* IRQ1 is on the master; IRQ12 also needs the slave and cascade EOIs. */
    port_out8(0x20,0x20);
}

u64 BOB64_MS_ABI bob64_exception_dispatch(const BOB64_INTERRUPT_FRAME *frame) {
    __asm__ volatile("cli");
    bob64_early_console_init();
    int from_user=frame&&(frame->CS&3)==3&&bob64_user_active;
    int expected_user_fault=from_user&&
        frame->Vector==bob64_user_expected_exception_vector&&
        (frame->ErrorCode&bob64_user_expected_exception_error_mask)==
            bob64_user_expected_exception_error_value;
    if(!expected_user_fault)
        diagnostic_text(from_user?"bob64 user exception\r\n":"bob64 exception\r\n");
    if(frame&&!expected_user_fault) {
        diagnostic_register("vector",frame->Vector);
        diagnostic_register("error",frame->ErrorCode);
        diagnostic_register("rip",frame->RIP);
        diagnostic_register("rsp",bob64_interrupt_frame_rsp(frame));
        diagnostic_register("cs",frame->CS);
        if(from_user)diagnostic_register("ss",frame->UserSS);
        diagnostic_register("rflags",frame->RFLAGS);
        diagnostic_register("rax",frame->RAX);diagnostic_register("rbx",frame->RBX);
        diagnostic_register("rcx",frame->RCX);diagnostic_register("rdx",frame->RDX);
        diagnostic_register("rbp",frame->RBP);diagnostic_register("rsi",frame->RSI);
        diagnostic_register("rdi",frame->RDI);diagnostic_register("r8",frame->R8);
        diagnostic_register("r9",frame->R9);diagnostic_register("r10",frame->R10);
        diagnostic_register("r11",frame->R11);diagnostic_register("r12",frame->R12);
        diagnostic_register("r13",frame->R13);diagnostic_register("r14",frame->R14);
        diagnostic_register("r15",frame->R15);
        if(frame->Vector==14) {
            u64 cr2;
            __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
            diagnostic_register("cr2",cr2);
        }
    }
    if(from_user) {
        bob64_user_exception_vector=frame->Vector;
        bob64_user_exception_error=frame->ErrorCode;
        bob64_user_exception_rip=frame->RIP;
        bob64_user_expected_exception_vector=~(u64)0;
        bob64_user_request_return(-(s64)(128+frame->Vector));
        return 1;
    }
    diagnostic_text("system halted\r\n");
    for(;;)__asm__ volatile("cli\n\thlt");
}
