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
    u64 total,offset=0;
    if(!server||bob64_framebuffer_resolution(&width,&height))return -1;
    total=(u64)width*height;
    while(offset<total) {
        u32 count=(u32)(total-offset>
            sizeof(server_composite_chunk)/sizeof(server_composite_chunk[0])?
            sizeof(server_composite_chunk)/sizeof(server_composite_chunk[0]):total-offset);
        for(u32 i=0;i<count;i++) {
            u64 index=offset+i;
            server_composite_chunk[i]=server_pixel(server,(u32)(index%width),
                                                    (u32)(index/width));
        }
        if(bob64_framebuffer_write_pixels(offset,server_composite_chunk,count))return -1;
        offset+=count;
    }
    return 0;
}

int bob64_window_server_init(BOB64_WINDOW_SERVER *server,BOB64_HEAP *heap,
                             u64 owner,u32 screen_width,u32 screen_height) {
    if(!server||!heap||!owner||!bob64_framebuffer_available())return -1;
    for(usize i=0;i<sizeof(*server);i++)((u8 *)server)[i]=0;
    if(bob64_wm_init(&server->Manager,server->Windows,
        BOB64_WINDOW_SERVER_CAPACITY,server->ZOrder,screen_width,screen_height))return -1;
    server->Heap=heap;server->Owner=owner;server->Active=1;
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
       bob64_wm_move(&server->Manager,handle,x,y))return -1;
    return server_composite(server);
}

int bob64_window_server_focus(BOB64_WINDOW_SERVER *server,u64 owner,
        BOB64_WINDOW_HANDLE handle) {
    int slot=server_slot(server,handle);
    if(!server_has_owner(server,owner)||slot<0||server->Owners[slot]!=owner||
       bob64_wm_focus(&server->Manager,handle))return -1;
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
    if(!server_has_owner(server,owner)||!event||!routed)return -1;
    target=bob64_wm_dispatch(&server->Manager,event,routed);
    if(target<0)return target;
    routed->WindowHandle=(u64)target;
    routed->ScreenX=event->X;routed->ScreenY=event->Y;
    if(event->Type==BOB64_EVENT_MOUSE_MOVE||event->Type==BOB64_EVENT_MOUSE_BUTTON||
       event->Type==BOB64_EVENT_MOUSE_WHEEL)
        if(server_composite(server))return -1;
    return target;
}
