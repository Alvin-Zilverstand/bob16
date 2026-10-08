.intel_syntax noprefix
#ifdef __MINGW32__
.section .rdata,"dr"
#else
.section .rodata
#endif
.balign 16
.globl bob64_embedded_app
.globl bob64_embedded_app_end
bob64_embedded_app:
    .incbin "build/bob64-app/smoke.b64e"
bob64_embedded_app_end:
.balign 16
.globl bob64_embedded_gui
.globl bob64_embedded_gui_end
bob64_embedded_gui:
    .incbin "build/bob64-app/gui.b64e"
bob64_embedded_gui_end:
.balign 16
.globl bob64_embedded_source
.globl bob64_embedded_source_end
bob64_embedded_source:
    .incbin "apps/bob64_demo.c"
bob64_embedded_source_end:

#ifndef __MINGW32__
.section .note.GNU-stack,"",@progbits
#endif
