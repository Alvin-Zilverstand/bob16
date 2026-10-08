.intel_syntax noprefix
.text

.macro BOB64_EXCEPTION_NO_ERROR number
    .globl bob64_exception_\number
bob64_exception_\number:
    push 0
    push \number
    jmp bob64_exception_common
.endm

.macro BOB64_EXCEPTION_ERROR number
    .globl bob64_exception_\number
bob64_exception_\number:
    push \number
    jmp bob64_exception_common
.endm

BOB64_EXCEPTION_NO_ERROR 0
BOB64_EXCEPTION_NO_ERROR 1
BOB64_EXCEPTION_NO_ERROR 2
BOB64_EXCEPTION_NO_ERROR 3
BOB64_EXCEPTION_NO_ERROR 4
BOB64_EXCEPTION_NO_ERROR 5
BOB64_EXCEPTION_NO_ERROR 6
BOB64_EXCEPTION_NO_ERROR 7
BOB64_EXCEPTION_ERROR    8
BOB64_EXCEPTION_NO_ERROR 9
BOB64_EXCEPTION_ERROR    10
BOB64_EXCEPTION_ERROR    11
BOB64_EXCEPTION_ERROR    12
BOB64_EXCEPTION_ERROR    13
BOB64_EXCEPTION_ERROR    14
BOB64_EXCEPTION_NO_ERROR 15
BOB64_EXCEPTION_NO_ERROR 16
BOB64_EXCEPTION_ERROR    17
BOB64_EXCEPTION_NO_ERROR 18
BOB64_EXCEPTION_NO_ERROR 19
BOB64_EXCEPTION_NO_ERROR 20
BOB64_EXCEPTION_ERROR    21
BOB64_EXCEPTION_NO_ERROR 22
BOB64_EXCEPTION_NO_ERROR 23
BOB64_EXCEPTION_NO_ERROR 24
BOB64_EXCEPTION_NO_ERROR 25
BOB64_EXCEPTION_NO_ERROR 26
BOB64_EXCEPTION_NO_ERROR 27
BOB64_EXCEPTION_NO_ERROR 28
BOB64_EXCEPTION_ERROR    29
BOB64_EXCEPTION_ERROR    30
BOB64_EXCEPTION_NO_ERROR 31

.globl bob64_irq0_entry
bob64_irq0_entry:
    push 0
    push 32
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12,rsp
    cld
    and rsp,-16
    sub rsp,32
    mov rcx,r12
    call bob64_irq_dispatch
    mov rsp,r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp,16
    iretq

.globl bob64_irq1_entry
bob64_irq1_entry:
    push 0
    push 33
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12,rsp
    cld
    and rsp,-16
    sub rsp,32
    mov rcx,r12
    call bob64_irq_dispatch
    mov rsp,r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp,16
    iretq

.globl bob64_irq12_entry
bob64_irq12_entry:
    push 0
    push 44
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12,rsp
    cld
    and rsp,-16
    sub rsp,32
    mov rcx,r12
    call bob64_irq_dispatch
    mov rsp,r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp,16
    iretq

.globl bob64_unhandled_interrupt
bob64_unhandled_interrupt:
    push 0
    push 255
    jmp bob64_exception_common

.globl bob64_exception_common
bob64_exception_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12,rsp
    cld
    and rsp,-16
    sub rsp,32
    mov rcx,r12
    call bob64_exception_dispatch
    test rax,rax
    jnz .Lbob64_user_abort_return
    mov rsp,r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp,16
    iretq

.globl bob64_enter_user
bob64_enter_user:
    /* Preserve the kernel ABI's nonvolatile registers across the privilege change. */
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    sub rsp,168
    movdqa XMMWORD PTR [rsp],xmm6
    movdqa XMMWORD PTR [rsp+16],xmm7
    movdqa XMMWORD PTR [rsp+32],xmm8
    movdqa XMMWORD PTR [rsp+48],xmm9
    movdqa XMMWORD PTR [rsp+64],xmm10
    movdqa XMMWORD PTR [rsp+80],xmm11
    movdqa XMMWORD PTR [rsp+96],xmm12
    movdqa XMMWORD PTR [rsp+112],xmm13
    movdqa XMMWORD PTR [rsp+128],xmm14
    movdqa XMMWORD PTR [rsp+144],xmm15
    mov r10,QWORD PTR [rip+bob64_user_depth]
    cmp r10,8
    jae .Lbob64_user_depth_full
    lea r11,[rip+bob64_user_return_frames]
    lea r12,[r10+r10*4]
    shl r12,3
    add r11,r12
    mov QWORD PTR [r11],rsp
    mov QWORD PTR [r11+8],0
    mov rax,QWORD PTR [rip+bob64_user_return_value]
    mov QWORD PTR [r11+16],rax
    pushfq
    pop rax
    and rax,0x200
    mov QWORD PTR [r11+24],rax
    mov rax,QWORD PTR [rip+bob64_user_active]
    mov QWORD PTR [r11+32],rax
    inc r10
    mov QWORD PTR [rip+bob64_user_depth],r10
    mov QWORD PTR [rip+bob64_user_return_value],0
    mov QWORD PTR [rip+bob64_user_active],1
    mov rax,rcx
    mov rcx,rdx
    /* User mode accepts IRQs; bob64_enter_user restores the caller's IF on return. */
    push 0x1b
    push r8
    push 0x202
    push 0x23
    push rax
    iretq
.Lbob64_user_depth_full:
    mov rax,-1
    movdqa xmm6,XMMWORD PTR [rsp]
    movdqa xmm7,XMMWORD PTR [rsp+16]
    movdqa xmm8,XMMWORD PTR [rsp+32]
    movdqa xmm9,XMMWORD PTR [rsp+48]
    movdqa xmm10,XMMWORD PTR [rsp+64]
    movdqa xmm11,XMMWORD PTR [rsp+80]
    movdqa xmm12,XMMWORD PTR [rsp+96]
    movdqa xmm13,XMMWORD PTR [rsp+112]
    movdqa xmm14,XMMWORD PTR [rsp+128]
    movdqa xmm15,XMMWORD PTR [rsp+144]
    add rsp,168
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    ret

.globl bob64_syscall_entry
bob64_syscall_entry:
    push 0
    push 128
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12,rsp
    cld
    and rsp,-16
    sub rsp,32
    mov rcx,r12
    call bob64_syscall_dispatch
    test rax,rax
    jnz .Lbob64_user_abort_return
    mov rsp,r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp,16
    iretq
.Lbob64_user_abort_return:
    mov r10,QWORD PTR [rip+bob64_user_depth]
    test r10,r10
    jz .Lbob64_user_return_corrupt
    dec r10
    lea r11,[rip+bob64_user_return_frames]
    lea r12,[r10+r10*4]
    shl r12,3
    add r11,r12
    mov rsp,QWORD PTR [r11]
    mov rax,QWORD PTR [r11+8]
    mov r9,QWORD PTR [r11+24]
    mov r8,QWORD PTR [r11+32]
    mov rdx,QWORD PTR [r11+16]
    movdqa xmm6,XMMWORD PTR [rsp]
    movdqa xmm7,XMMWORD PTR [rsp+16]
    movdqa xmm8,XMMWORD PTR [rsp+32]
    movdqa xmm9,XMMWORD PTR [rsp+48]
    movdqa xmm10,XMMWORD PTR [rsp+64]
    movdqa xmm11,XMMWORD PTR [rsp+80]
    movdqa xmm12,XMMWORD PTR [rsp+96]
    movdqa xmm13,XMMWORD PTR [rsp+112]
    movdqa xmm14,XMMWORD PTR [rsp+128]
    movdqa xmm15,XMMWORD PTR [rsp+144]
    add rsp,168
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    mov QWORD PTR [rip+bob64_user_depth],r10
    mov QWORD PTR [rip+bob64_user_active],r8
    mov QWORD PTR [rip+bob64_user_return_value],rdx
    test r9,r9
    jz .Lbob64_return_without_if
    sti
.Lbob64_return_without_if:
    ret
.Lbob64_user_return_corrupt:
    cli
.Lbob64_user_return_halt:
    hlt
    jmp .Lbob64_user_return_halt

#ifdef __MINGW32__
.section .rdata,"dr"
#else
.section .rodata
#endif
.align 8
.globl bob64_exception_stub_table
bob64_exception_stub_table:
    .quad bob64_exception_0,bob64_exception_1,bob64_exception_2,bob64_exception_3
    .quad bob64_exception_4,bob64_exception_5,bob64_exception_6,bob64_exception_7
    .quad bob64_exception_8,bob64_exception_9,bob64_exception_10,bob64_exception_11
    .quad bob64_exception_12,bob64_exception_13,bob64_exception_14,bob64_exception_15
    .quad bob64_exception_16,bob64_exception_17,bob64_exception_18,bob64_exception_19
    .quad bob64_exception_20,bob64_exception_21,bob64_exception_22,bob64_exception_23
    .quad bob64_exception_24,bob64_exception_25,bob64_exception_26,bob64_exception_27
    .quad bob64_exception_28,bob64_exception_29,bob64_exception_30,bob64_exception_31

#ifndef __MINGW32__
.section .note.GNU-stack,"",@progbits
#endif
