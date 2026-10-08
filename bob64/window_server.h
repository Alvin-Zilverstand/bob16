#ifndef BOB64_WINDOW_SERVER_H
#define BOB64_WINDOW_SERVER_H

#include "window.h"
#include "heap.h"
#include "console.h"

#define BOB64_WINDOW_SERVER_CAPACITY 16u
#define BOB64_WINDOW_SERVER_SURFACE_LIMIT (16u*1024u*1024u)

typedef struct {
    BOB64_HEAP *Heap;
    BOB64_WINDOW_MANAGER Manager;
    BOB64_WINDOW Windows[BOB64_WINDOW_SERVER_CAPACITY];
    BOB64_WINDOW_HANDLE ZOrder[BOB64_WINDOW_SERVER_CAPACITY];
    u64 Owners[BOB64_WINDOW_SERVER_CAPACITY];
    usize SurfaceBytes[BOB64_WINDOW_SERVER_CAPACITY];
    u64 Owner;
    u64 BytesAllocated;
    u8 Active;
} BOB64_WINDOW_SERVER;

int bob64_window_server_init(BOB64_WINDOW_SERVER *server,BOB64_HEAP *heap,
                             u64 owner,u32 screen_width,u32 screen_height);
void bob64_window_server_close(BOB64_WINDOW_SERVER *server);
BOB64_WINDOW_HANDLE bob64_window_server_create(BOB64_WINDOW_SERVER *server,
        u64 owner,s32 x,s32 y,u32 width,u32 height);
int bob64_window_server_destroy(BOB64_WINDOW_SERVER *server,u64 owner,
                                BOB64_WINDOW_HANDLE handle);
int bob64_window_server_move(BOB64_WINDOW_SERVER *server,u64 owner,
        BOB64_WINDOW_HANDLE handle,s32 x,s32 y);
int bob64_window_server_focus(BOB64_WINDOW_SERVER *server,u64 owner,
        BOB64_WINDOW_HANDLE handle);
int bob64_window_server_write_pixels(BOB64_WINDOW_SERVER *server,u64 owner,
        BOB64_WINDOW_HANDLE handle,u64 first_pixel,const u32 *pixels,u32 count);
int bob64_window_server_surface_size(const BOB64_WINDOW_SERVER *server,u64 owner,
        BOB64_WINDOW_HANDLE handle,u32 *width,u32 *height);
int bob64_window_server_present(BOB64_WINDOW_SERVER *server,u64 owner,
                                BOB64_WINDOW_HANDLE handle);
s64 bob64_window_server_route_event(BOB64_WINDOW_SERVER *server,u64 owner,
        const BOB64_EVENT *event,BOB64_EVENT *routed);

#endif
