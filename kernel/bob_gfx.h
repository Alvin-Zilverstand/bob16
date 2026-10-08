#ifndef BOB_GFX_H
#define BOB_GFX_H

#include "bob.h"

/* Version 1 is an 80x25 character-cell canvas; one cell is one pixel. */
#define BOB_GFX_VERSION 1
#define BOB_GFX_REQUEST(operation) ((BOB_GFX_VERSION << 16) | (operation))

int bob_gfx_enter(void) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(16);
    request[1]=0;request[2]=0;request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

int bob_gfx_resolution(int *values) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(17);request[1]=(int)values;request[2]=2;
    request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

int bob_gfx_pixel(int x,int y,int character,int color) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(18);request[1]=x;request[2]=y;
    request[3]=character;request[4]=color;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

int bob_gfx_fill_rect(int x,int y,int width,int height,int character,int color) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(19);request[1]=x;request[2]=y;
    request[3]=width;request[4]=height;request[5]=character;request[6]=color;
    return bob_os_service(request);
}

int bob_gfx_line(int x0,int y0,int x1,int y1,int character,int color) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(20);request[1]=x0;request[2]=y0;
    request[3]=x1;request[4]=y1;request[5]=character;request[6]=color;
    return bob_os_service(request);
}

int bob_gfx_text(int x,int y,const char *text,int color) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(21);request[1]=x;request[2]=y;
    request[3]=(int)text;request[4]=color;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

/* Bitmap cells are zero-transparent ASCII characters, row-major. */
int bob_gfx_blit(int x,int y,int width,int height,const char *bitmap,int color) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(22);request[1]=x;request[2]=y;
    request[3]=width;request[4]=height;request[5]=(int)bitmap;request[6]=color;
    return bob_os_service(request);
}

/* Each word stores character in bits 0..7 and color in bits 8..11; zero is transparent. */
int bob_gfx_blit_color(int x,int y,int width,int height,const int *bitmap) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(27);request[1]=x;request[2]=y;
    request[3]=width;request[4]=height;request[5]=(int)bitmap;request[6]=0;
    return bob_os_service(request);
}

int bob_gfx_clear(int character,int color) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(23);request[1]=character;request[2]=color;
    request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

int bob_gfx_present(void) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(24);
    request[1]=0;request[2]=0;request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

int bob_gfx_leave(void) {
    int request[7];
    request[0]=BOB_GFX_REQUEST(25);
    request[1]=0;request[2]=0;request[3]=0;request[4]=0;request[5]=0;request[6]=0;
    return bob_os_service(request);
}

#endif
