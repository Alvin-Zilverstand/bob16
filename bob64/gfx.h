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
    if(graphics&&x>=0&&y>=0&&(u32)x<graphics->Width&&(u32)y<graphics->Height)
        graphics->Pixels[(usize)(u32)y*graphics->Width+(u32)x]=color&0x00ffffffu;
}

static inline void bob64_gfx_fill_rect(BOB64_GFX *graphics,s32 x,s32 y,
                                       u32 width,u32 height,u32 color) {
    if(!graphics)return;
    for(u32 row=0;row<height;row++)for(u32 column=0;column<width;column++)
        bob64_gfx_pixel(graphics,x+(s32)column,y+(s32)row,color);
}

static inline void bob64_gfx_line(BOB64_GFX *graphics,s32 x0,s32 y0,s32 x1,s32 y1,
                                  u32 color) {
    s32 dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1;
    s32 dy=y1>y0?y0-y1:y1-y0,sy=y0<y1?1:-1;
    s32 error=dx+dy;
    for(;;) {
        bob64_gfx_pixel(graphics,x0,y0,color);
        if(x0==x1&&y0==y1)break;
        s32 twice=error*2;
        if(twice>=dy){error+=dy;x0+=sx;}
        if(twice<=dx){error+=dx;y0+=sy;}
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

static inline void bob64_gfx_text(BOB64_GFX *graphics,s32 x,s32 y,
                                  const char *text,u32 color,u32 scale) {
    if(!graphics||!text||!scale)return;
    while(*text) {
        u32 glyph=bob64_gfx_glyph((u8)*text++);
        for(u32 column=0;column<5;column++)for(u32 row=0;row<7;row++)
            if(bob64_font5x7[glyph][column]&(1u<<row))
                bob64_gfx_fill_rect(graphics,x+(s32)(column*scale),
                    y+(s32)(row*scale),scale,scale,color);
        x+=(s32)(6*scale);
    }
}

static inline int bob64_gfx_present(BOB64_GFX *graphics) {
    if(!graphics||!graphics->Pixels)return -1;
    return bob64_app_present(graphics->Pixels,graphics->Width,graphics->Height)==
           (s64)((u64)graphics->Width*graphics->Height)?0:-1;
}

#endif
