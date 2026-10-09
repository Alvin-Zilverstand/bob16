#ifndef BOB64_GFX_H
#define BOB64_GFX_H

#include "app.h"
#include "font.h"

typedef struct {
    u32 *Pixels;
    u32 Width,Height;
    usize Capacity;
} BOB64_GFX;

#define BOB64_GFX_MAX_WIDTH 1280u
#define BOB64_GFX_MAX_HEIGHT 800u

static inline int bob64_gfx_init(BOB64_GFX *graphics,u32 *pixels,usize capacity) {
    u32 width,height;
    if(!graphics||!pixels||bob64_app_get_display(&width,&height)||!width||!height||
       (u64)width*height>capacity||width>BOB64_GFX_MAX_WIDTH||
       height>BOB64_GFX_MAX_HEIGHT)return -1;
    graphics->Pixels=pixels;graphics->Width=width;graphics->Height=height;
    graphics->Capacity=capacity;
    return 0;
}

static inline void bob64_gfx_pixel(BOB64_GFX *graphics,s32 x,s32 y,u32 color) {
    if(graphics&&graphics->Pixels&&graphics->Width<=BOB64_GFX_MAX_WIDTH&&
       graphics->Height<=BOB64_GFX_MAX_HEIGHT&&x>=0&&y>=0&&
       (u32)x<graphics->Width&&(u32)y<graphics->Height) {
        usize index=(usize)(u32)y*graphics->Width+(u32)x;
        if((u64)graphics->Width*graphics->Height<=graphics->Capacity&&
           index<graphics->Capacity)
            graphics->Pixels[index]=color&0x00ffffffu;
    }
}

static inline void bob64_gfx_fill_rect(BOB64_GFX *graphics,s64 x,s64 y,
                                       u32 width,u32 height,u32 color) {
    s64 left=x,top=y,right=x+(s64)width,bottom=y+(s64)height;
    usize row_start,row_end;
    if(!graphics||!graphics->Pixels||!width||!height||
       graphics->Width>BOB64_GFX_MAX_WIDTH||
       graphics->Height>BOB64_GFX_MAX_HEIGHT||
       (u64)graphics->Width*graphics->Height>graphics->Capacity)return;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>graphics->Width)right=graphics->Width;
    if(bottom>graphics->Height)bottom=graphics->Height;
    if(left>=right||top>=bottom)return;
    row_start=(usize)top*graphics->Width+(usize)left;
    row_end=row_start+(usize)(right-left);
    for(s64 row=top;row<bottom;row++) {
        for(usize pixel=row_start;pixel<row_end;pixel++)
            graphics->Pixels[pixel]=color&0x00ffffffu;
        row_start+=graphics->Width;row_end+=graphics->Width;
    }
}

static inline u32 bob64_gfx_line_outcode(s64 x,s64 y,u32 width,u32 height) {
    return (x<0?1u:(x>=(s64)width?2u:0u))|
           (y<0?4u:(y>=(s64)height?8u:0u));
}

/* Interpolate only between endpoints on the segment; each magnitude is at most
 * UINT32_MAX, so the unsigned product fits without requiring a 128-bit type. */
static inline s64 bob64_gfx_line_interpolate(s64 start,s64 end,s64 numerator,
                                              s64 denominator) {
    s64 delta=end-start;
    u64 delta_magnitude=(u64)(delta<0?-delta:delta);
    u64 numerator_magnitude=(u64)(numerator<0?-numerator:numerator);
    u64 denominator_magnitude=(u64)(denominator<0?-denominator:denominator);
    u64 step=(delta_magnitude*numerator_magnitude)/denominator_magnitude;
    int negative=((delta<0)^(numerator<0)^(denominator<0));
    return start+(negative?-(s64)step:(s64)step);
}

static inline void bob64_gfx_line(BOB64_GFX *graphics,s32 x0,s32 y0,s32 x1,s32 y1,
                                  u32 color) {
    s64 ax=x0,ay=y0,bx=x1,by=y1;
    u32 width,height;
    if(!graphics||!graphics->Pixels||!graphics->Width||!graphics->Height||
       graphics->Width>BOB64_GFX_MAX_WIDTH||
       graphics->Height>BOB64_GFX_MAX_HEIGHT||
       (u64)graphics->Width*graphics->Height>graphics->Capacity)return;
    width=graphics->Width;height=graphics->Height;
    for(;;) {
        u32 a=bob64_gfx_line_outcode(ax,ay,width,height);
        u32 b=bob64_gfx_line_outcode(bx,by,width,height);
        u32 outside;
        s64 x,y;
        if(!(a|b))break;
        if(a&b)return;
        outside=a?a:b;
        if(outside&4u) {
            y=0;x=bob64_gfx_line_interpolate(ax,bx,-ay,by-ay);
        } else if(outside&8u) {
            y=(s64)height-1;
            x=bob64_gfx_line_interpolate(ax,bx,y-ay,by-ay);
        } else if(outside&1u) {
            x=0;y=bob64_gfx_line_interpolate(ay,by,-ax,bx-ax);
        } else {
            x=(s64)width-1;
            y=bob64_gfx_line_interpolate(ay,by,x-ax,bx-ax);
        }
        if(outside==a)ax=x,ay=y;
        else bx=x,by=y;
    }
    s64 dx=bx>ax?bx-ax:ax-bx,sx=ax<bx?1:-1;
    s64 dy=by>ay?ay-by:by-ay,sy=ay<by?1:-1;
    s64 error=dx+dy;
    for(;;) {
        bob64_gfx_pixel(graphics,(s32)ax,(s32)ay,color);
        if(ax==bx&&ay==by)break;
        s64 twice=error*2;
        if(twice>=dy){error+=dy;ax+=sx;}
        if(twice<=dx){error+=dx;ay+=sy;}
    }
}

static inline u32 bob64_gfx_glyph(u8 value) {
    if(value>='a'&&value<='z')value=(u8)(value-'a'+'A');
    if(value==' ')return 0;
    if(value>='A'&&value<='Z')return 1u+(u32)(value-'A');
    if(value>='0'&&value<='9')return 27u+(u32)(value-'0');
    if(value=='?')return 37;
    if(value=='!')return 38;
    if(value=='-')return 39;
    if(value=='.')return 40;
    if(value==':')return 41;
    if(value=='=')return 42;
    if(value=='>')return 43;
    return 37;
}

typedef struct {
    BOB64_GFX *Graphics;
} BOB64_GFX_FONT_CONTEXT;

static inline void bob64_gfx_font_plot_pixel(void *opaque,s64 x,s64 y,
        u32 scale,u32 color) {
    BOB64_GFX_FONT_CONTEXT *context=(BOB64_GFX_FONT_CONTEXT *)opaque;
    bob64_gfx_fill_rect(context->Graphics,x,y,scale,scale,color);
}

static inline void bob64_gfx_text(BOB64_GFX *graphics,s32 x,s32 y,
                                  const char *text,u32 color,u32 scale) {
    s64 cursor_x=x,origin_y=y;
    BOB64_GFX_FONT_CONTEXT context={graphics};
    if(!graphics||!text||!scale)return;
    while(*text) {
        u32 glyph=bob64_gfx_glyph((u8)*text++);
        bob64_font_draw_glyph(&bob64_font5x7,glyph,cursor_x,origin_y,scale,
                              bob64_gfx_font_plot_pixel,&context,color);
        cursor_x+=(s64)6*scale;
    }
}

static inline int bob64_gfx_present(BOB64_GFX *graphics) {
    if(!graphics||!graphics->Pixels)return -1;
    return bob64_app_present(graphics->Pixels,graphics->Width,graphics->Height)==
           (s64)((u64)graphics->Width*graphics->Height)?0:-1;
}

#endif
