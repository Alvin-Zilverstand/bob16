#ifndef BOB64_WINDOW_H
#define BOB64_WINDOW_H

#include "event.h"

typedef u64 BOB64_WINDOW_HANDLE;

#define BOB64_WINDOW_VISIBLE 1u
#define BOB64_WINDOW_DIRTY 2u
#define BOB64_WINDOW_TITLE_BAR_HEIGHT 24u

/* App-owned state. Context is a full-width app pointer and is never a kernel pointer. */
typedef struct {
    BOB64_WINDOW_HANDLE Handle;
    void *Context;
    s32 X,Y;
    u32 Width,Height,Flags;
} BOB64_WINDOW;

typedef struct {
    BOB64_WINDOW *Windows;
    BOB64_WINDOW_HANDLE *ZOrder;
    u32 Capacity,Count,ScreenWidth,ScreenHeight,LastButtons;
    BOB64_WINDOW_HANDLE Focused,Dragging,NextHandle;
    s32 DragOffsetX,DragOffsetY;
    u32 FullDirty,DrawIndex;
} BOB64_WINDOW_MANAGER;

static inline BOB64_WINDOW *bob64_wm_find(BOB64_WINDOW_MANAGER *manager,
                                           BOB64_WINDOW_HANDLE handle) {
    if(!manager||!handle)return 0;
    for(u32 i=0;i<manager->Capacity;i++)
        if(manager->Windows[i].Handle==handle)return &manager->Windows[i];
    return 0;
}

static inline int bob64_wm_intersects(const BOB64_WINDOW *a,const BOB64_WINDOW *b) {
    return (s64)a->X+a->Width>b->X&&(s64)b->X+b->Width>a->X&&
           (s64)a->Y+a->Height>b->Y&&(s64)b->Y+b->Height>a->Y;
}

static inline void bob64_wm_clamp(BOB64_WINDOW_MANAGER *manager,BOB64_WINDOW *window) {
    s32 max_x=(s32)(manager->ScreenWidth-window->Width);
    s32 max_y=(s32)(manager->ScreenHeight-window->Height);
    if(window->X<0)window->X=0;
    if(window->Y<0)window->Y=0;
    if(window->X>max_x)window->X=max_x;
    if(window->Y>max_y)window->Y=max_y;
}

static inline s32 bob64_wm_saturate_s32(s64 value) {
    if(value<(-2147483647LL-1))return (-2147483647-1);
    if(value>2147483647LL)return 2147483647;
    return (s32)value;
}

static inline void bob64_wm_dirty_all(BOB64_WINDOW_MANAGER *manager) {
    for(u32 i=0;i<manager->Count;i++) {
        BOB64_WINDOW *window=bob64_wm_find(manager,manager->ZOrder[i]);
        if(window&&(window->Flags&BOB64_WINDOW_VISIBLE))window->Flags|=BOB64_WINDOW_DIRTY;
    }
    manager->FullDirty=1;
}

static inline int bob64_wm_init(BOB64_WINDOW_MANAGER *manager,
                                BOB64_WINDOW *windows,u32 capacity,
                                BOB64_WINDOW_HANDLE *z_order,
                                u32 screen_width,u32 screen_height) {
    if(!manager||!windows||!z_order||!capacity||capacity>64||!screen_width||
       !screen_height||screen_width>0x7fffffffu||screen_height>0x7fffffffu)return -1;
    manager->Windows=windows;manager->ZOrder=z_order;
    manager->Capacity=capacity;manager->Count=0;
    manager->ScreenWidth=screen_width;manager->ScreenHeight=screen_height;
    manager->LastButtons=0;manager->Focused=0;manager->Dragging=0;
    manager->NextHandle=1;manager->DragOffsetX=0;manager->DragOffsetY=0;
    manager->FullDirty=1;manager->DrawIndex=0;
    for(u32 i=0;i<capacity;i++) {
        windows[i].Handle=0;windows[i].Context=0;
        windows[i].X=windows[i].Y=0;windows[i].Width=windows[i].Height=0;
        windows[i].Flags=0;z_order[i]=0;
    }
    return 0;
}

static inline int bob64_wm_raise(BOB64_WINDOW_MANAGER *manager,
                                BOB64_WINDOW_HANDLE handle) {
    u32 index=manager?manager->Count:0;
    if(!manager||!bob64_wm_find(manager,handle))return -1;
    for(u32 i=0;i<manager->Count;i++)if(manager->ZOrder[i]==handle){index=i;break;}
    if(index==manager->Count)return -1;
    if(index+1==manager->Count)return 0;
    for(u32 i=index;i+1<manager->Count;i++)manager->ZOrder[i]=manager->ZOrder[i+1];
    manager->ZOrder[manager->Count-1]=handle;
    bob64_wm_dirty_all(manager);
    return 1;
}

static inline int bob64_wm_focus(BOB64_WINDOW_MANAGER *manager,
                                BOB64_WINDOW_HANDLE handle) {
    BOB64_WINDOW *window=bob64_wm_find(manager,handle);
    if(!window||!(window->Flags&BOB64_WINDOW_VISIBLE))return -1;
    bob64_wm_raise(manager,handle);
    manager->Focused=handle;window->Flags|=BOB64_WINDOW_DIRTY;
    return 0;
}

static inline BOB64_WINDOW_HANDLE bob64_wm_create(BOB64_WINDOW_MANAGER *manager,
        s32 x,s32 y,u32 width,u32 height,void *context) {
    BOB64_WINDOW *window=0;
    if(!manager||!width||!height||width>manager->ScreenWidth||
       height>manager->ScreenHeight||manager->Count>=manager->Capacity||
       !manager->NextHandle||manager->NextHandle>0x7fffffffffffffffULL)return 0;
    for(u32 i=0;i<manager->Capacity;i++)if(!manager->Windows[i].Handle) {
        window=&manager->Windows[i];break;
    }
    if(!window)return 0;
    window->Handle=manager->NextHandle++;
    window->Context=context;window->X=x;window->Y=y;
    window->Width=width;window->Height=height;
    window->Flags=BOB64_WINDOW_VISIBLE|BOB64_WINDOW_DIRTY;
    bob64_wm_clamp(manager,window);
    manager->ZOrder[manager->Count++]=window->Handle;
    manager->Focused=window->Handle;manager->Dragging=0;
    bob64_wm_dirty_all(manager);
    return window->Handle;
}

static inline int bob64_wm_move(BOB64_WINDOW_MANAGER *manager,
        BOB64_WINDOW_HANDLE handle,s32 x,s32 y) {
    BOB64_WINDOW *window=bob64_wm_find(manager,handle);
    if(!window)return -1;
    window->X=x;window->Y=y;bob64_wm_clamp(manager,window);
    bob64_wm_dirty_all(manager);
    return 0;
}

static inline int bob64_wm_show(BOB64_WINDOW_MANAGER *manager,
        BOB64_WINDOW_HANDLE handle,int visible) {
    BOB64_WINDOW *window=bob64_wm_find(manager,handle);
    if(!window)return -1;
    visible=visible!=0;
    if(((window->Flags&BOB64_WINDOW_VISIBLE)!=0)==visible)return 0;
    if(visible) {
        window->Flags|=BOB64_WINDOW_VISIBLE;
        bob64_wm_focus(manager,handle);
    } else {
        window->Flags&=~BOB64_WINDOW_VISIBLE;
        if(manager->Dragging==handle)manager->Dragging=0;
        if(manager->Focused==handle) {
            manager->Focused=0;
            for(u32 i=manager->Count;i>0;i--) {
                BOB64_WINDOW *candidate=bob64_wm_find(manager,manager->ZOrder[i-1]);
                if(candidate&&(candidate->Flags&BOB64_WINDOW_VISIBLE)) {
                    manager->Focused=candidate->Handle;break;
                }
            }
        }
    }
    bob64_wm_dirty_all(manager);
    return 1;
}

static inline int bob64_wm_destroy(BOB64_WINDOW_MANAGER *manager,
                                   BOB64_WINDOW_HANDLE handle) {
    BOB64_WINDOW *window=bob64_wm_find(manager,handle);
    u32 index;
    if(!window)return -1;
    for(index=0;index<manager->Count;index++)if(manager->ZOrder[index]==handle)break;
    if(index==manager->Count)return -1;
    for(u32 i=index;i+1<manager->Count;i++)manager->ZOrder[i]=manager->ZOrder[i+1];
    manager->ZOrder[--manager->Count]=0;
    if(manager->Focused==handle)manager->Focused=0;
    if(manager->Dragging==handle)manager->Dragging=0;
    window->Handle=0;window->Context=0;window->Flags=0;
    window->X=window->Y=0;window->Width=window->Height=0;
    if(!manager->Focused)for(u32 i=manager->Count;i>0;i--) {
        BOB64_WINDOW *candidate=bob64_wm_find(manager,manager->ZOrder[i-1]);
        if(candidate&&(candidate->Flags&BOB64_WINDOW_VISIBLE)) {
            manager->Focused=candidate->Handle;break;
        }
    }
    bob64_wm_dirty_all(manager);
    return 0;
}

static inline void bob64_wm_invalidate(BOB64_WINDOW_MANAGER *manager,
        BOB64_WINDOW_HANDLE handle) {
    BOB64_WINDOW *window=bob64_wm_find(manager,handle);
    if(!window)return;
    window->Flags|=BOB64_WINDOW_DIRTY;
    for(u32 i=0;i<manager->Count;i++) {
        BOB64_WINDOW *other=bob64_wm_find(manager,manager->ZOrder[i]);
        if(other&&other!=window&&(other->Flags&BOB64_WINDOW_VISIBLE)&&
           bob64_wm_intersects(window,other))other->Flags|=BOB64_WINDOW_DIRTY;
    }
}

static inline int bob64_wm_begin_draw(BOB64_WINDOW_MANAGER *manager) {
    if(!manager)return -1;
    if(manager->FullDirty)bob64_wm_dirty_all(manager);
    manager->FullDirty=0;manager->DrawIndex=0;
    return 0;
}

static inline BOB64_WINDOW_HANDLE bob64_wm_next_dirty(BOB64_WINDOW_MANAGER *manager) {
    if(!manager)return 0;
    while(manager->DrawIndex<manager->Count) {
        BOB64_WINDOW_HANDLE handle=manager->ZOrder[manager->DrawIndex++];
        BOB64_WINDOW *window=bob64_wm_find(manager,handle);
        if(window&&(window->Flags&BOB64_WINDOW_VISIBLE)&&
           (window->Flags&BOB64_WINDOW_DIRTY)) {
            window->Flags&=~BOB64_WINDOW_DIRTY;return handle;
        }
    }
    return 0;
}

static inline BOB64_WINDOW_HANDLE bob64_wm_hit_test(const BOB64_WINDOW_MANAGER *manager,
                                                    s32 x,s32 y) {
    if(!manager)return 0;
    for(u32 i=manager->Count;i>0;i--) {
        BOB64_WINDOW *window=bob64_wm_find((BOB64_WINDOW_MANAGER *)manager,
                                          manager->ZOrder[i-1]);
        if(window&&(window->Flags&BOB64_WINDOW_VISIBLE)&&x>=window->X&&y>=window->Y&&
           (s64)x<=(s64)window->X+window->Width-1&&
           (s64)y<=(s64)window->Y+window->Height-1)return window->Handle;
    }
    return 0;
}

/* Returns the target handle, zero for unhandled input, or -1 for invalid arguments. */
static inline s64 bob64_wm_dispatch(BOB64_WINDOW_MANAGER *manager,
        const BOB64_EVENT *event,BOB64_EVENT *routed) {
    BOB64_WINDOW_HANDLE target=0;
    u32 pressed,released;
    if(!manager||!event||!routed)return -1;
    *routed=*event;
    pressed=event->Buttons&~manager->LastButtons;
    released=manager->LastButtons&~event->Buttons;
    if(event->Type==BOB64_EVENT_KEY_DOWN||event->Type==BOB64_EVENT_KEY_UP) {
        BOB64_WINDOW *focused=bob64_wm_find(manager,manager->Focused);
        if(focused&&(focused->Flags&BOB64_WINDOW_VISIBLE))target=focused->Handle;
    } else if(event->Type==BOB64_EVENT_MOUSE_MOVE||
              event->Type==BOB64_EVENT_MOUSE_BUTTON||
              event->Type==BOB64_EVENT_MOUSE_WHEEL) {
        if(manager->Dragging)target=manager->Dragging;
        else target=bob64_wm_hit_test(manager,event->X,event->Y);
        BOB64_WINDOW *window=bob64_wm_find(manager,target);
        if(window) {
            if(event->Type==BOB64_EVENT_MOUSE_BUTTON&&pressed) {
                bob64_wm_focus(manager,target);window=bob64_wm_find(manager,target);
                if((pressed&1u)&&event->Y>=window->Y&&
                   (u32)(event->Y-window->Y)<BOB64_WINDOW_TITLE_BAR_HEIGHT) {
                    manager->Dragging=target;
                    manager->DragOffsetX=event->X-window->X;
                    manager->DragOffsetY=event->Y-window->Y;
                }
            }
            if(manager->Dragging&&
               (event->Type==BOB64_EVENT_MOUSE_MOVE||event->Type==BOB64_EVENT_MOUSE_BUTTON)) {
                if((event->Type==BOB64_EVENT_MOUSE_MOVE&&(event->Buttons&1u))||
                   event->Type==BOB64_EVENT_MOUSE_BUTTON)
                    bob64_wm_move(manager,target,
                        bob64_wm_saturate_s32((s64)event->X-manager->DragOffsetX),
                        bob64_wm_saturate_s32((s64)event->Y-manager->DragOffsetY));
                if(released&1u)manager->Dragging=0;
                window=bob64_wm_find(manager,target);
            }
            routed->X=bob64_wm_saturate_s32((s64)event->X-window->X);
            routed->Y=bob64_wm_saturate_s32((s64)event->Y-window->Y);
        }
    }
    if(event->Type==BOB64_EVENT_MOUSE_MOVE||event->Type==BOB64_EVENT_MOUSE_BUTTON||
       event->Type==BOB64_EVENT_MOUSE_WHEEL)manager->LastButtons=event->Buttons;
    return (s64)target;
}

#endif
