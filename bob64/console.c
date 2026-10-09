#include "console.h"
#include "font.h"
#include "font8x16.h"

typedef struct {
    volatile u32 *Pixels;
    u64 SizeBytes;
    u32 Width,Height,Stride,Format,Column,Row;
    u8 Ready,CursorDrawn;
} BOB64_FRAMEBUFFER;

static BOB64_FRAMEBUFFER framebuffer;

static u32 color(u8 red,u8 green,u8 blue) {
    if(framebuffer.Format==0)return (u32)red|((u32)green<<8)|((u32)blue<<16);
    return (u32)blue|((u32)green<<8)|((u32)red<<16);
}

static void pixel(u32 x,u32 y,u32 value) {
    if(x<framebuffer.Width&&y<framebuffer.Height)
        framebuffer.Pixels[(u64)y*framebuffer.Stride+x]=value;
}

static void clear_row(u32 y) {
    u32 background=color(12,18,28);
    for(u32 x=0;x<framebuffer.Width;x++)pixel(x,y,background);
}

static u32 columns(void) {
    u32 result=framebuffer.Width/FONT_CELL_WIDTH;
    return result?result:1;
}

static u32 rows(void) {
    u32 result=framebuffer.Height/LINE_HEIGHT;
    return result?result:1;
}

static u32 row_origin(void) {
    return framebuffer.Row*LINE_HEIGHT+(LINE_HEIGHT-FONT_HEIGHT)/2;
}

static void erase_cursor(void) {
    if(!framebuffer.Ready)return;
    u32 x=framebuffer.Column*FONT_CELL_WIDTH;
    u32 y=framebuffer.Row*LINE_HEIGHT+LINE_HEIGHT-2;
    for(u32 offset=0;offset<FONT_WIDTH;offset++)
        pixel(x+offset,y,color(12,18,28));
    framebuffer.CursorDrawn=0;
}

static void draw_cursor(void) {
    if(!framebuffer.Ready)return;
    u32 x=framebuffer.Column*FONT_CELL_WIDTH;
    u32 y=framebuffer.Row*LINE_HEIGHT+LINE_HEIGHT-2;
    for(u32 offset=0;offset<FONT_WIDTH;offset++)
        pixel(x+offset,y,color(148,194,232));
    framebuffer.CursorDrawn=1;
}

void bob64_framebuffer_clear(void) {
    if(!framebuffer.Ready)return;
    for(u32 y=0;y<framebuffer.Height;y++)clear_row(y);
    framebuffer.Column=0;framebuffer.Row=0;framebuffer.CursorDrawn=0;
}

static void scroll(void) {
    u32 lines=framebuffer.Height>LINE_HEIGHT?
              framebuffer.Height-LINE_HEIGHT:0;
    for(u32 y=0;y<lines;y++) {
        volatile u32 *destination=framebuffer.Pixels+(u64)y*framebuffer.Stride;
        volatile u32 *source=destination+(u64)LINE_HEIGHT*framebuffer.Stride;
        for(u32 x=0;x<framebuffer.Width;x++)destination[x]=source[x];
    }
    for(u32 y=lines;y<framebuffer.Height;y++)clear_row(y);
    framebuffer.Row=rows()-1;
}

static u32 glyph_index(u8 value) {
    if(value<' '||value>'~')value='?';
    return (u32)(value-' ');
}

static void console_plot_pixel(void *context,s64 x,s64 y,u32 scale,
                               u32 value) {
    (void)context;
    for(u32 dy=0;dy<scale;dy++)for(u32 dx=0;dx<scale;dx++)
        if(x+(s64)dx>=0&&x+(s64)dx<framebuffer.Width&&
           y+(s64)dy>=0&&y+(s64)dy<framebuffer.Height)
            pixel((u32)(x+(s64)dx),(u32)(y+(s64)dy),value);
}

static void draw_character(u8 value) {
    u32 foreground=color(228,238,248),background=color(12,18,28);
    u32 glyph=glyph_index(value),origin_x=framebuffer.Column*FONT_CELL_WIDTH;
    u32 origin_y=row_origin();
    for(u32 y=0;y<LINE_HEIGHT;y++)
        for(u32 x=0;x<FONT_CELL_WIDTH;x++)pixel(origin_x+x,
            framebuffer.Row*LINE_HEIGHT+y,background);
    bob64_font_draw_glyph(&bob64_font8x16,glyph,origin_x,origin_y,1,
                          console_plot_pixel,0,foreground);
    framebuffer.Column++;
    if(framebuffer.Column>=columns()) {
        framebuffer.Column=0;
        framebuffer.Row++;
    }
    if(framebuffer.Row>=rows())scroll();
}

static void erase_previous_character(void) {
    u32 column_count=columns();
    if(framebuffer.Column)framebuffer.Column--;
    else if(framebuffer.Row) {
        framebuffer.Row--;
        framebuffer.Column=column_count-1;
    } else return;
    u32 origin_x=framebuffer.Column*FONT_CELL_WIDTH;
    u32 origin_y=framebuffer.Row*LINE_HEIGHT;
    for(u32 y=0;y<LINE_HEIGHT;y++)for(u32 x=0;x<FONT_CELL_WIDTH;x++)
        pixel(origin_x+x,origin_y+y,color(12,18,28));
}

int bob64_framebuffer_init(u64 base,u64 size,u32 width,u32 height,
                           u32 pixels_per_scan_line,u32 pixel_format) {
    u64 pixels,required;
    if(!base||!size||width<FONT_CELL_WIDTH||height<FONT_HEIGHT||
       pixels_per_scan_line<width||
       (pixel_format!=0&&pixel_format!=1)||
       (u64)pixels_per_scan_line>~(u64)0/height)return -1;
    pixels=(u64)pixels_per_scan_line*height;
    if(pixels>~(u64)0/sizeof(u32))return -1;
    required=pixels*sizeof(u32);
    if(required>size)return -1;
    framebuffer.Pixels=(volatile u32 *)(uintptr_t)base;
    framebuffer.SizeBytes=size;
    framebuffer.Width=width;framebuffer.Height=height;
    framebuffer.Stride=pixels_per_scan_line;framebuffer.Format=pixel_format;
    framebuffer.Column=0;framebuffer.Row=0;framebuffer.Ready=1;
    for(u32 y=0;y<height;y++)clear_row(y);
    return 0;
}

int bob64_framebuffer_available(void) {
    return framebuffer.Ready!=0;
}

int bob64_framebuffer_resolution(u32 *width,u32 *height) {
    if(!framebuffer.Ready||!width||!height)return -1;
    *width=framebuffer.Width;*height=framebuffer.Height;
    return 0;
}

int bob64_framebuffer_write_pixels(u64 first_pixel,const u32 *pixels,u32 count) {
    u64 total;
    if(!framebuffer.Ready||(!pixels&&count))return -1;
    if(count&&framebuffer.CursorDrawn)erase_cursor();
    total=(u64)framebuffer.Width*framebuffer.Height;
    if(first_pixel>total||(u64)count>total-first_pixel)return -1;
    for(u32 i=0;i<count;i++) {
        u64 index=first_pixel+i;
        u32 red=(pixels[i]>>16)&0xff,green=(pixels[i]>>8)&0xff,blue=pixels[i]&0xff;
        pixel((u32)(index%framebuffer.Width),(u32)(index/framebuffer.Width),
              color((u8)red,(u8)green,(u8)blue));
    }
    return 0;
}

void bob64_framebuffer_write(const char *text) {
    if(!framebuffer.Ready||!text)return;
    if(framebuffer.CursorDrawn)erase_cursor();
    while(*text) {
        u8 value=(u8)*text++;
        if(value=='\r')framebuffer.Column=0;
        else if(value=='\n') {
            framebuffer.Column=0;framebuffer.Row++;
            if(framebuffer.Row>=rows())scroll();
        } else if(value=='\b')erase_previous_character();
        else draw_character(value);
    }
    draw_cursor();
}
