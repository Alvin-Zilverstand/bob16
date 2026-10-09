#include "window_server.h"

/* The compositor runs on the single kernel CPU with interrupts disabled. */
static u32 server_composite_chunk[1024];

static int server_has_owner(const BOB64_WINDOW_SERVER *server,u64 owner) {
    return server&&server->Active&&owner&&owner==server->Owner;
}

static int server_slot(const BOB64_WINDOW_SERVER *server,
                       BOB64_WINDOW_HANDLE handle) {
    if(!server||!handle)return -1;
    for(u32 i=0;i<BOB64_WINDOW_SERVER_CAPACITY;i++)
        if(server->Windows[i].Handle==handle)return (int)i;
    return -1;
}

static void server_damage(BOB64_WINDOW_SERVER *server,s64 left,s64 top,
                          s64 right,s64 bottom) {
    if(!server||left>=right||top>=bottom)return;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>server->Manager.ScreenWidth)right=server->Manager.ScreenWidth;
    if(bottom>server->Manager.ScreenHeight)bottom=server->Manager.ScreenHeight;
    if(left>=right||top>=bottom)return;
    if(!server->Damaged) {
        server->DamageLeft=(u32)left;server->DamageTop=(u32)top;
        server->DamageRight=(u32)right;server->DamageBottom=(u32)bottom;
        server->Damaged=1;
    } else {
        if((u32)left<server->DamageLeft)server->DamageLeft=(u32)left;
        if((u32)top<server->DamageTop)server->DamageTop=(u32)top;
        if((u32)right>server->DamageRight)server->DamageRight=(u32)right;
        if((u32)bottom>server->DamageBottom)server->DamageBottom=(u32)bottom;
    }
}

static void server_damage_window(BOB64_WINDOW_SERVER *server,
                                 const BOB64_WINDOW *window) {
    if(!window)return;
    server_damage(server,window->X,window->Y,
                  (s64)window->X+window->Width,
                  (s64)window->Y+window->Height);
}

static u32 server_pixel(const BOB64_WINDOW_SERVER *server,u32 x,u32 y) {
    for(u32 i=server->Manager.Count;i>0;i--) {
        BOB64_WINDOW_HANDLE handle=server->Manager.ZOrder[i-1];
        BOB64_WINDOW *window=bob64_wm_find((BOB64_WINDOW_MANAGER *)&server->Manager,
                                           handle);
        if(window&&(window->Flags&BOB64_WINDOW_VISIBLE)&&x>=(u32)window->X&&
           y>=(u32)window->Y&&x-(u32)window->X<window->Width&&
           y-(u32)window->Y<window->Height) {
            const u32 *surface=(const u32 *)window->Context;
            return surface[(usize)(y-(u32)window->Y)*window->Width+
                           (x-(u32)window->X)];
        }
    }
    return 0x000d1723u;
}

static int server_composite(BOB64_WINDOW_SERVER *server) {
    u32 width,height;
    if(!server||!server->Damaged||bob64_framebuffer_resolution(&width,&height))
        return server&&!server->Damaged?0:-1;
    if(width!=server->Manager.ScreenWidth||height!=server->Manager.ScreenHeight)
        return -1;
    for(u32 y=server->DamageTop;y<server->DamageBottom;y++) {
        for(u32 x=server->DamageLeft;x<server->DamageRight;) {
            u32 count=server->DamageRight-x;
            if(count>sizeof(server_composite_chunk)/sizeof(server_composite_chunk[0]))
                count=sizeof(server_composite_chunk)/sizeof(server_composite_chunk[0]);
            for(u32 i=0;i<count;i++)
                server_composite_chunk[i]=server_pixel(server,x+i,y);
            if(bob64_framebuffer_write_pixels((u64)y*width+x,
                                               server_composite_chunk,count))return -1;
            x+=count;
        }
    }
    server->Damaged=0;
    server->DamageLeft=server->DamageTop=0;
    server->DamageRight=server->DamageBottom=0;
    return 0;
}

int bob64_window_server_init(BOB64_WINDOW_SERVER *server,BOB64_HEAP *heap,
                             u64 owner,u32 screen_width,u32 screen_height) {
    if(!server||!heap||!owner||!bob64_framebuffer_available())return -1;
    for(usize i=0;i<sizeof(*server);i++)((u8 *)server)[i]=0;
    if(bob64_wm_init(&server->Manager,server->Windows,
        BOB64_WINDOW_SERVER_CAPACITY,server->ZOrder,screen_width,screen_height))return -1;
    server->Heap=heap;server->Owner=owner;server->Active=1;
    server_damage(server,0,0,screen_width,screen_height);
    return server_composite(server);
}

void bob64_window_server_close(BOB64_WINDOW_SERVER *server) {
    if(!server||!server->Active)return;
    while(server->Manager.Count) {
        BOB64_WINDOW_HANDLE handle=server->Manager.ZOrder[server->Manager.Count-1];
        if(bob64_window_server_destroy(server,server->Owner,handle))break;
    }
    server->Active=0;server->Owner=0;server->BytesAllocated=0;
}

BOB64_WINDOW_HANDLE bob64_window_server_create(BOB64_WINDOW_SERVER *server,
        u64 owner,s32 x,s32 y,u32 width,u32 height) {
    u64 pixels,bytes;
    u32 *surface;
    BOB64_WINDOW_HANDLE handle;
    int slot;
    if(!server_has_owner(server,owner)||!width||!height||
       (u64)width>~(u64)0/height)return 0;
    pixels=(u64)width*height;
    if(pixels>~(u64)0/sizeof(u32))return 0;
    bytes=pixels*sizeof(u32);
    if(bytes>BOB64_WINDOW_SERVER_SURFACE_LIMIT-server->BytesAllocated||
       bytes>(u64)~(usize)0)return 0;
    surface=(u32 *)bob64_heap_calloc(server->Heap,1,(usize)bytes);
    if(!surface)return 0;
    handle=bob64_wm_create(&server->Manager,x,y,width,height,surface);
    slot=server_slot(server,handle);
    if(!handle||slot<0) {
        bob64_heap_free(server->Heap,surface);return 0;
    }
    server_damage_window(server,&server->Windows[slot]);
    server->Owners[slot]=owner;server->SurfaceBytes[slot]=(usize)bytes;
    server->BytesAllocated+=bytes;
    if(server_composite(server)) {
        bob64_window_server_destroy(server,owner,handle);return 0;
    }
    return handle;
}

int bob64_window_server_destroy(BOB64_WINDOW_SERVER *server,u64 owner,
                                BOB64_WINDOW_HANDLE handle) {
    int slot;
    u32 *surface;
    if(!server_has_owner(server,owner)||(slot=server_slot(server,handle))<0||
       server->Owners[slot]!=owner)return -1;
    server_damage_window(server,&server->Windows[slot]);
    surface=(u32 *)server->Windows[slot].Context;
    if(bob64_wm_destroy(&server->Manager,handle))return -1;
    if(surface) {
        server->BytesAllocated-=server->SurfaceBytes[slot];
        if(bob64_heap_free(server->Heap,surface))return -1;
    }
    server->Owners[slot]=0;server->SurfaceBytes[slot]=0;
    return server_composite(server);
}

int bob64_window_server_move(BOB64_WINDOW_SERVER *server,u64 owner,
        BOB64_WINDOW_HANDLE handle,s32 x,s32 y) {
    int slot=server_slot(server,handle);
    if(!server_has_owner(server,owner)||slot<0||server->Owners[slot]!=owner||
       !server->Windows[slot].Handle)return -1;
    BOB64_WINDOW *window=&server->Windows[slot];
    s32 old_x=window->X,old_y=window->Y;
    if(bob64_wm_move(&server->Manager,handle,x,y))return -1;
    server_damage(server,old_x,old_y,(s64)old_x+window->Width,
                  (s64)old_y+window->Height);
    server_damage_window(server,window);
    return server_composite(server);
}

int bob64_window_server_focus(BOB64_WINDOW_SERVER *server,u64 owner,
        BOB64_WINDOW_HANDLE handle) {
    int slot=server_slot(server,handle);
    if(!server_has_owner(server,owner)||slot<0||server->Owners[slot]!=owner||
       bob64_wm_focus(&server->Manager,handle))return -1;
    server_damage_window(server,&server->Windows[slot]);
    return server_composite(server);
}

int bob64_window_server_write_pixels(BOB64_WINDOW_SERVER *server,u64 owner,
        BOB64_WINDOW_HANDLE handle,u64 first_pixel,const u32 *pixels,u32 count) {
    int slot=server_slot(server,handle);
    u64 total;
    u32 *surface;
    if(!server_has_owner(server,owner)||slot<0||server->Owners[slot]!=owner||
       (!pixels&&count))return -1;
    BOB64_WINDOW *window=&server->Windows[slot];
    total=(u64)window->Width*window->Height;
    if(first_pixel>total||(u64)count>total-first_pixel)return -1;
    surface=(u32 *)window->Context;
    for(u32 i=0;i<count;i++)surface[first_pixel+i]=pixels[i]&0x00ffffffu;
    if(count) {
        u64 last=first_pixel+(u64)count-1;
        u32 first_y=(u32)(first_pixel/window->Width);
        u32 last_y=(u32)(last/window->Width);
        if(first_y==last_y)
            server_damage(server,(s64)window->X+first_pixel%window->Width,
                (s64)window->Y+first_y,
                (s64)window->X+last%window->Width+1,
                (s64)window->Y+first_y+1);
        else
            server_damage(server,window->X,(s64)window->Y+first_y,
                (s64)window->X+window->Width,
                (s64)window->Y+last_y+1);
    }
    return 0;
}

int bob64_window_server_surface_size(const BOB64_WINDOW_SERVER *server,u64 owner,
        BOB64_WINDOW_HANDLE handle,u32 *width,u32 *height) {
    int slot=server_slot(server,handle);
    if(!server_has_owner(server,owner)||slot<0||server->Owners[slot]!=owner||
       !width||!height)return -1;
    *width=server->Windows[slot].Width;*height=server->Windows[slot].Height;
    return 0;
}

int bob64_window_server_present(BOB64_WINDOW_SERVER *server,u64 owner,
                                BOB64_WINDOW_HANDLE handle) {
    int slot=server_slot(server,handle);
    if(!server_has_owner(server,owner)||slot<0||server->Owners[slot]!=owner)return -1;
    return server_composite(server);
}

s64 bob64_window_server_route_event(BOB64_WINDOW_SERVER *server,u64 owner,
        const BOB64_EVENT *event,BOB64_EVENT *routed) {
    s64 target;
    BOB64_WINDOW_HANDLE previous_focus;
    BOB64_WINDOW *window;
    s32 old_x=0,old_y=0;
    int had_window=0;
    if(!server_has_owner(server,owner)||!event||!routed)return -1;
    previous_focus=server->Manager.Focused;
    if(event->Type==BOB64_EVENT_MOUSE_MOVE||
       event->Type==BOB64_EVENT_MOUSE_BUTTON||
       event->Type==BOB64_EVENT_MOUSE_WHEEL) {
        if(server->Manager.Dragging)target=(s64)server->Manager.Dragging;
        else target=(s64)bob64_wm_hit_test(&server->Manager,event->X,event->Y);
        window=bob64_wm_find(&server->Manager,(BOB64_WINDOW_HANDLE)target);
        if(window) {old_x=window->X;old_y=window->Y;had_window=1;}
    }
    target=bob64_wm_dispatch(&server->Manager,event,routed);
    if(target<0)return target;
    window=bob64_wm_find(&server->Manager,(BOB64_WINDOW_HANDLE)target);
    if(had_window&&window&&(old_x!=window->X||old_y!=window->Y))
        server_damage(server,old_x,old_y,(s64)old_x+window->Width,
                      (s64)old_y+window->Height);
    if(window&&previous_focus!=window->Handle)server_damage_window(server,window);
    if(window&&had_window&&(old_x!=window->X||old_y!=window->Y))
        server_damage_window(server,window);
    routed->WindowHandle=(u64)target;
    routed->ScreenX=event->X;routed->ScreenY=event->Y;
    if(server_composite(server))return -1;
    return target;
}
