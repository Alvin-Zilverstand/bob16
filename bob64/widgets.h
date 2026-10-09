#ifndef BOB64_WIDGETS_H
#define BOB64_WIDGETS_H

#include "gfx.h"

typedef struct {
    s32 X,Y;
    u32 Width,Height;
} BOB64_RECT;

typedef struct {
    u8 Hovered,Pressed;
} BOB64_BUTTON_STATE;

typedef struct {
    char *Buffer;
    usize Capacity,Length,Cursor,Scroll;
    u8 Focused,Dirty;
} BOB64_TEXT_FIELD;

#define BOB64_BUTTON_LEFT 1u

static inline int bob64_rect_contains(const BOB64_RECT *rect,s32 x,s32 y) {
    return rect&&rect->Width&&rect->Height&&x>=rect->X&&y>=rect->Y&&
        (s64)x<(s64)rect->X+rect->Width&&
        (s64)y<(s64)rect->Y+rect->Height;
}

/* Returns one only when a left-button press is released inside the rectangle. */
static inline int bob64_button_event(const BOB64_RECT *rect,
        BOB64_BUTTON_STATE *state,const BOB64_EVENT *event) {
    int inside;
    if(!rect||!state||!event)return 0;
    inside=bob64_rect_contains(rect,event->X,event->Y);
    if(event->Type==BOB64_EVENT_MOUSE_MOVE)state->Hovered=(u8)inside;
    else if(event->Type==BOB64_EVENT_MOUSE_BUTTON) {
        state->Hovered=(u8)inside;
        if(event->Buttons&BOB64_BUTTON_LEFT)
            state->Pressed=(u8)inside;
        else {
            int clicked=state->Pressed&&inside;
            state->Pressed=0;
            return clicked;
        }
    }
    return 0;
}

static inline int bob64_button_draw(BOB64_GFX *graphics,const BOB64_RECT *rect,
        const char *label,const BOB64_BUTTON_STATE *state) {
    u32 background,border;
    usize length=0;
    s32 text_x,text_y;
    if(!graphics||!rect||!label||!state||rect->Width<8||rect->Height<10||
       rect->Width>0x7fffffffu||rect->Height>0x7fffffffu||
       rect->X>0x7fffffff-(s32)(rect->Width-1)||
       rect->Y>0x7fffffff-(s32)(rect->Height-1))return -1;
    background=state->Pressed&&state->Hovered?0x001f4067u:
        (state->Hovered?0x004b78a8u:0x00335d8cu);
    border=state->Hovered?0x00d6ecffu:0x007fa3c4u;
    bob64_gfx_fill_rect(graphics,rect->X,rect->Y,rect->Width,rect->Height,
                        background);
    bob64_gfx_line(graphics,rect->X,rect->Y,
        rect->X+(s32)rect->Width-1,rect->Y,border);
    bob64_gfx_line(graphics,rect->X,rect->Y+(s32)rect->Height-1,
        rect->X+(s32)rect->Width-1,rect->Y+(s32)rect->Height-1,border);
    bob64_gfx_line(graphics,rect->X,rect->Y,rect->X,
        rect->Y+(s32)rect->Height-1,border);
    bob64_gfx_line(graphics,rect->X+(s32)rect->Width-1,rect->Y,
        rect->X+(s32)rect->Width-1,rect->Y+(s32)rect->Height-1,border);
    while(label[length]&&length<(rect->Width-1)/6)length++;
    if(label[length])return -1;
    usize text_width=length?length*6-1:0;
    if(text_width>rect->Width-2)return -1;
    text_x=rect->X+((s32)rect->Width-(s32)text_width)/2;
    text_y=rect->Y+((s32)rect->Height-7)/2;
    bob64_gfx_text(graphics,text_x,text_y,label,0x00ffffffu,1);
    return 0;
}

static inline int bob64_text_field_sync(BOB64_TEXT_FIELD *field) {
    usize length=0;
    if(!field||!field->Buffer||!field->Capacity)return -1;
    while(length<field->Capacity&&field->Buffer[length])length++;
    if(length==field->Capacity)return -1;
    field->Length=length;field->Cursor=length;field->Scroll=0;field->Dirty=0;
    return 0;
}

static inline int bob64_text_field_init(BOB64_TEXT_FIELD *field,char *buffer,
                                        usize capacity) {
    if(!field)return -1;
    field->Buffer=buffer;field->Capacity=capacity;field->Focused=0;
    return bob64_text_field_sync(field);
}

static inline void bob64_text_field_reveal(BOB64_TEXT_FIELD *field,
                                           usize visible_columns) {
    if(!field)return;
    if(!visible_columns)visible_columns=1;
    if(field->Cursor<field->Scroll)field->Scroll=field->Cursor;
    else if(field->Cursor>=field->Scroll+visible_columns)
        field->Scroll=field->Cursor-visible_columns+1;
}

static inline int bob64_text_field_event(BOB64_TEXT_FIELD *field,
        const BOB64_RECT *rect,const BOB64_EVENT *event) {
    usize visible_columns;
    if(!field||!field->Buffer||!rect||!event)return 0;
    visible_columns=rect->Width>6?(rect->Width-6)/6:1;
    if(event->Type==BOB64_EVENT_MOUSE_BUTTON&&
       (event->Buttons&BOB64_BUTTON_LEFT)) {
        field->Focused=(u8)bob64_rect_contains(rect,event->X,event->Y);
        if(field->Focused) {
            usize column=event->X>rect->X+3?
                (usize)(event->X-rect->X-3)/6:0;
            field->Cursor=field->Scroll+column;
            if(field->Cursor>field->Length)field->Cursor=field->Length;
            return 0;
        }
        return 0;
    }
    if(event->Type!=BOB64_EVENT_KEY_DOWN||!field->Focused)return 0;
    if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x4b)) {
        if(field->Cursor)field->Cursor--;
    } else if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x4d)) {
        if(field->Cursor<field->Length)field->Cursor++;
    } else if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x47))field->Cursor=0;
    else if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x4f))field->Cursor=field->Length;
    else if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x53)) {
        if(field->Cursor>=field->Length)return 0;
        for(usize i=field->Cursor;i<field->Length;i++)
            field->Buffer[i]=field->Buffer[i+1];
        field->Length--;field->Dirty=1;
    } else if(event->Character=='\b') {
        if(!field->Cursor)return 0;
        for(usize i=field->Cursor-1;i<field->Length;i++)
            field->Buffer[i]=field->Buffer[i+1];
        field->Cursor--;field->Length--;field->Dirty=1;
    } else if(event->Character>=32&&event->Character<=126) {
        if(field->Length+1>=field->Capacity)return 0;
        for(usize i=field->Length+1;i>field->Cursor;i--)
            field->Buffer[i]=field->Buffer[i-1];
        field->Buffer[field->Cursor++]=(char)event->Character;
        field->Length++;field->Dirty=1;
    } else return 0;
    field->Buffer[field->Length]=0;
    bob64_text_field_reveal(field,visible_columns);
    return 1;
}

static inline int bob64_text_field_draw(BOB64_GFX *graphics,
        const BOB64_RECT *rect,const BOB64_TEXT_FIELD *field) {
    char visible[256];
    usize columns,length=0;
    u32 border;
    if(!graphics||!rect||!field||!field->Buffer||rect->Width<12||
       rect->Height<10||rect->Width>sizeof(visible)*6u+6u||
       rect->Width>0x7fffffffu||rect->Height>0x7fffffffu||
       rect->X>0x7fffffff-(s32)(rect->Width-1)||
       rect->Y>0x7fffffff-(s32)(rect->Height-1))return -1;
    columns=(rect->Width-6)/6;
    if(columns>=sizeof(visible))columns=sizeof(visible)-1;
    while(length<columns&&field->Scroll+length<field->Length) {
        visible[length]=field->Buffer[field->Scroll+length];length++;
    }
    visible[length]=0;
    border=field->Focused?0x00d6ecffu:0x007fa3c4u;
    bob64_gfx_fill_rect(graphics,rect->X,rect->Y,rect->Width,rect->Height,
                        0x001a2a3cu);
    bob64_gfx_line(graphics,rect->X,rect->Y,
        rect->X+(s32)rect->Width-1,rect->Y,border);
    bob64_gfx_line(graphics,rect->X,rect->Y+(s32)rect->Height-1,
        rect->X+(s32)rect->Width-1,rect->Y+(s32)rect->Height-1,border);
    bob64_gfx_line(graphics,rect->X,rect->Y,rect->X,
        rect->Y+(s32)rect->Height-1,border);
    bob64_gfx_line(graphics,rect->X+(s32)rect->Width-1,rect->Y,
        rect->X+(s32)rect->Width-1,rect->Y+(s32)rect->Height-1,border);
    bob64_gfx_text(graphics,rect->X+3,rect->Y+((s32)rect->Height-7)/2,
                   visible,0x00e2ebf5u,1);
    if(field->Focused&&field->Cursor>=field->Scroll&&
       field->Cursor-field->Scroll<=columns) {
        s32 cursor_x=rect->X+3+(s32)((field->Cursor-field->Scroll)*6);
        bob64_gfx_line(graphics,cursor_x,rect->Y+3,cursor_x,
                       rect->Y+(s32)rect->Height-4,0x00ffffffu);
    }
    return 0;
}

#endif
