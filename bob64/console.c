#include "console.h"
#include "font.h"

typedef struct {
    volatile u32 *Pixels;
    u64 SizeBytes;
    u32 Width,Height,Stride,Format,Column,Row;
    u8 Ready;
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

void bob64_framebuffer_clear(void) {
    if(!framebuffer.Ready)return;
    for(u32 y=0;y<framebuffer.Height;y++)clear_row(y);
    framebuffer.Column=0;framebuffer.Row=0;
}

static void scroll(void) {
    u32 lines=framebuffer.Height>8?framebuffer.Height-8:0;
    for(u32 y=0;y<lines;y++) {
        volatile u32 *destination=framebuffer.Pixels+(u64)y*framebuffer.Stride;
        volatile u32 *source=destination+(u64)8*framebuffer.Stride;
        for(u32 x=0;x<framebuffer.Width;x++)destination[x]=source[x];
    }
    for(u32 y=lines;y<framebuffer.Height;y++)clear_row(y);
    framebuffer.Row=framebuffer.Height/8?framebuffer.Height/8-1:0;
}

static u32 glyph_index(u8 value) {
    if(value>='a'&&value<='z')value=(u8)(value-'a'+'A');
    if(value==' ')return 0;
    if(value>='A'&&value<='Z')return 1u+(u32)(value-'A');
    if(value>='0'&&value<='9')return 27u+(u32)(value-'0');
    if(value=='!')return 38;
    if(value=='-')return 39;
    if(value=='.')return 40;
    if(value==':')return 41;
    if(value=='=')return 42;
    if(value=='>')return 43;
    return 37;
}

static void draw_character(u8 value) {
    u32 foreground=color(228,238,248),background=color(12,18,28);
    u32 glyph=glyph_index(value),origin_x=framebuffer.Column*6,origin_y=framebuffer.Row*8;
    for(u32 y=0;y<8;y++)for(u32 x=0;x<6;x++) {
        u32 on=x<5&&y<7&&(bob64_font5x7[glyph][x]&(1u<<y));
        pixel(origin_x+x,origin_y+y,on?foreground:background);
    }
    framebuffer.Column++;
    if(framebuffer.Column>=framebuffer.Width/6) {
        framebuffer.Column=0;
        framebuffer.Row++;
    }
    if(framebuffer.Row>=framebuffer.Height/8)scroll();
}

static void erase_previous_character(void) {
    u32 columns=framebuffer.Width/6;
    if(framebuffer.Column)framebuffer.Column--;
    else if(framebuffer.Row) {
        framebuffer.Row--;
        framebuffer.Column=columns-1;
    } else return;
    u32 origin_x=framebuffer.Column*6,origin_y=framebuffer.Row*8;
    for(u32 y=0;y<8;y++)for(u32 x=0;x<6;x++)
        pixel(origin_x+x,origin_y+y,color(12,18,28));
}

int bob64_framebuffer_init(u64 base,u64 size,u32 width,u32 height,
                           u32 pixels_per_scan_line,u32 pixel_format) {
    u64 pixels,required;
    if(!base||!size||width<6||height<8||pixels_per_scan_line<width||
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
    while(*text) {
        u8 value=(u8)*text++;
        if(value=='\r')framebuffer.Column=0;
        else if(value=='\n') {
            framebuffer.Column=0;framebuffer.Row++;
            if(framebuffer.Row>=framebuffer.Height/8)scroll();
        } else if(value=='\b')erase_previous_character();
        else draw_character(value);
    }
}
