#ifndef BOB64_CONSOLE_H
#define BOB64_CONSOLE_H

#include "types.h"

/* UEFI GOP pixel formats 0 and 1; returns nonzero when unsupported/invalid. */
int bob64_framebuffer_init(u64 base,u64 size,u32 width,u32 height,
                           u32 pixels_per_scan_line,u32 pixel_format);
int bob64_framebuffer_available(void);
int bob64_framebuffer_resolution(u32 *width,u32 *height);
/* App surfaces use packed 0x00RRGGBB pixels in row-major order. */
int bob64_framebuffer_write_pixels(u64 first_pixel,const u32 *pixels,u32 count);
void bob64_framebuffer_clear(void);
void bob64_framebuffer_write(const char *text);

#endif
