#ifndef BOB64_INTERRUPTS_H
#define BOB64_INTERRUPTS_H

#include "descriptors.h"
#include "abi.h"

typedef struct {
    u64 R15,R14,R13,R12,R11,R10,R9,R8;
    u64 RDI,RSI,RBP,RDX,RCX,RBX,RAX;
    u64 Vector,ErrorCode,RIP,CS,RFLAGS;
    /* Present in the hardware frame only when the exception changed CPL. */
    u64 UserRSP,UserSS;
} BOB64_INTERRUPT_FRAME;

_Static_assert(__builtin_offsetof(BOB64_INTERRUPT_FRAME,Vector)==120,
               "interrupt stub register save layout");
_Static_assert(__builtin_offsetof(BOB64_INTERRUPT_FRAME,RIP)==136,
               "interrupt hardware frame layout");
_Static_assert(__builtin_offsetof(BOB64_INTERRUPT_FRAME,UserRSP)==160,
               "privilege-change stack pointer frame offset");
_Static_assert(__builtin_offsetof(BOB64_INTERRUPT_FRAME,UserSS)==168,
               "privilege-change stack selector frame offset");
_Static_assert(sizeof(BOB64_INTERRUPT_FRAME)==176,
               "interrupt frame maximum size");

static inline u64 bob64_interrupt_frame_rsp(const BOB64_INTERRUPT_FRAME *frame) {
    if(!frame)return 0;
    if((frame->CS&3u)==3u)return frame->UserRSP;
    return (u64)(uintptr_t)frame+
           __builtin_offsetof(BOB64_INTERRUPT_FRAME,UserRSP);
}

extern void (*bob64_exception_stub_table[32])(void);
extern void bob64_irq0_entry(void);
extern void bob64_irq4_entry(void);
extern void bob64_syscall_entry(void);
extern void bob64_irq1_entry(void);
extern void bob64_irq12_entry(void);
extern volatile u64 bob64_user_exception_vector;
extern volatile u64 bob64_user_exception_error;
extern volatile u64 bob64_user_exception_rip;
extern volatile u64 bob64_user_expected_exception_vector;
extern volatile u64 bob64_user_expected_exception_error_mask;
extern volatile u64 bob64_user_expected_exception_error_value;
void bob64_unhandled_interrupt(void);
int bob64_interrupts_build_idt(BOB64_IDT_GATE entries[BOB64_IDT_ENTRIES],u16 selector);
void bob64_early_console_init(void);
int bob64_interrupts_enable_keyboard(void);
int bob64_interrupts_enable_serial(void);
int bob64_interrupts_enable_mouse(void);
int bob64_interrupts_enable_timer(void);
void bob64_timer_irq_tick(void);
u64 bob64_timer_ticks(void);
void bob64_early_console_write(const char *text);
void bob64_early_console_hex64(u64 value);
int bob64_early_console_try_read(void);
void bob64_serial_irq_reset(void);
int bob64_serial_irq_capture(u8 character);
int bob64_serial_irq_pop(u8 *character);
u64 BOB64_MS_ABI bob64_exception_dispatch(const BOB64_INTERRUPT_FRAME *frame);
void BOB64_MS_ABI bob64_irq_dispatch(const BOB64_INTERRUPT_FRAME *frame);

#endif
