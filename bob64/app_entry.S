.intel_syntax noprefix
.text
.globl bob64_app_entry
bob64_app_entry:
    /* RSP enters at 8 mod 16; reserve shadow space and align before the call. */
    sub rsp,40
    call bob64_app_main
    mov rcx,rax
    mov eax,3                 /* BOB64_SYSCALL_EXIT in ABI v1 */
    int 0x80
    ud2

#ifndef __MINGW32__
.section .note.GNU-stack,"",@progbits
#endif
