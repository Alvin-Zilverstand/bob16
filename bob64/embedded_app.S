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
.globl bob64_embedded_display
.globl bob64_embedded_display_end
bob64_embedded_display:
    .incbin "build/bob64-app/display.b64e"
bob64_embedded_display_end:
.balign 16
.globl bob64_embedded_gui
.globl bob64_embedded_gui_end
bob64_embedded_gui:
    .incbin "build/bob64-app/gui.b64e"
bob64_embedded_gui_end:
.balign 16
.globl bob64_embedded_nested_app
.globl bob64_embedded_nested_app_end
bob64_embedded_nested_app:
    .incbin "build/bob64-app/nested-smoke.b64e"
bob64_embedded_nested_app_end:
.balign 16
.globl bob64_embedded_echo
.globl bob64_embedded_echo_end
bob64_embedded_echo:
    .incbin "build/bob64-app/echo.b64e"
bob64_embedded_echo_end:
.balign 16
.globl bob64_embedded_ls
.globl bob64_embedded_ls_end
bob64_embedded_ls:
    .incbin "build/bob64-app/ls.b64e"
bob64_embedded_ls_end:
.balign 16
.globl bob64_embedded_cat
.globl bob64_embedded_cat_end
bob64_embedded_cat:
    .incbin "build/bob64-app/cat.b64e"
bob64_embedded_cat_end:
.balign 16
.globl bob64_embedded_notes
.globl bob64_embedded_notes_end
bob64_embedded_notes:
    .incbin "build/bob64-app/notes.b64e"
bob64_embedded_notes_end:
.balign 16
.globl bob64_embedded_info
.globl bob64_embedded_info_end
bob64_embedded_info:
    .incbin "build/bob64-app/info.b64e"
bob64_embedded_info_end:
.balign 16
.globl bob64_embedded_mouse_smoke
.globl bob64_embedded_mouse_smoke_end
bob64_embedded_mouse_smoke:
    .incbin "build/bob64-app/mouse-smoke.b64e"
bob64_embedded_mouse_smoke_end:
.balign 16
.globl bob64_embedded_launcher
.globl bob64_embedded_launcher_end
bob64_embedded_launcher:
    .incbin "build/bob64-app/launcher.b64e"
bob64_embedded_launcher_end:
.balign 16
.globl bob64_embedded_source
.globl bob64_embedded_source_end
bob64_embedded_source:
    .incbin "apps/bob64_demo.c"
bob64_embedded_source_end:

#ifndef __MINGW32__
.section .note.GNU-stack,"",@progbits
#endif
