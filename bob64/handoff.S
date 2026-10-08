.intel_syntax noprefix
.text

.globl bob64_enter_kernel
bob64_enter_kernel:
    /* Microsoft x64 ABI: fifth argument is above the 32-byte shadow area. */
    mov r10,rcx
    mov r11,rdx
    mov r13,QWORD PTR [rsp+40]
    cli
    lgdt [r8]
    push 0x08
    lea rax,[rip+.Lbob64_reload_cs]
    push rax
    retfq
.Lbob64_reload_cs:
    mov ax,0x28
    ltr ax
    lidt [r9]
    mov ax,0x10
    mov ds,ax
    mov es,ax
    mov ss,ax
    cmp BYTE PTR [r13+81],0
    je .Lbob64_nx_ready
    mov ecx,0xc0000080
    rdmsr
    or eax,0x800
    wrmsr
.Lbob64_nx_ready:
    mov rax,cr0
    or rax,0x10000
    and rax,0xfffffffffffffffb
    or rax,0x2
    mov cr0,rax
    mov rax,cr4
    or rax,0x600
    mov cr4,rax
    mov cr3,r10
    mov rsp,r11
    and rsp,-16
    sub rsp,32
    mov rcx,r13
    call bob64_kernel_main
.Lbob64_unexpected_return:
    cli
    hlt
    jmp .Lbob64_unexpected_return
