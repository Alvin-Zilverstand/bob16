#include "syscall.h"
#include "process.h"
#include "console.h"

volatile u64 bob64_user_active;
volatile u64 bob64_user_depth;
BOB64_USER_RETURN_FRAME bob64_user_return_frames[BOB64_USER_ENTRY_MAX_DEPTH];
volatile s64 bob64_user_return_value;
#define BOB64_SYSCALL_OPEN_HANDLES 16
#define BOB64_SYSCALL_CONTEXT_DEPTH BOB64_SYSCALL_CONTEXT_MAX_DEPTH
typedef struct {
    u64 Id,Position;
    u32 Flags;
    char Name[BOB64_SYSCALL_MAX_FILENAME+1];
} BOB64_SYSCALL_HANDLE;
typedef struct {
    BOB64_SYSCALL_WRITE WriteCharacter;
    void *WriteContext;
    BOB64_SYSCALL_READ_USER ReadUser;
    void *ReadUserContext;
    BOB64_SYSCALL_WRITE_USER WriteUser;
    void *WriteUserContext;
    BOB64_SYSCALL_FILE_READ FileRead;
    BOB64_SYSCALL_FILE_WRITE FileWrite;
    void *FilesystemContext;
    BOB64_SYSCALL_FILE_LIST FileList;
    BOB64_SYSCALL_FILE_DELETE FileDelete;
    void *FileManagerContext;
    BOB64_SYSCALL_FILE_OPEN FileOpen;
    BOB64_SYSCALL_FILE_READ_AT FileReadAt;
    BOB64_SYSCALL_FILE_WRITE_AT FileWriteAt;
    void *FileStreamContext;
    BOB64_SYSCALL_EVENT_WAIT WaitEvent;
    void *WaitEventContext;
    BOB64_SYSCALL_APP_RUN RunApplication;
    void *AppRunnerContext;
    BOB64_WINDOW_SERVER *WindowServer;
    u64 WindowOwner;
    const BOB64_PAGE_TABLE *PageTable;
    u32 SurfaceChunk[BOB64_SYSCALL_COPY_CHUNK_BYTES/sizeof(u32)];
    u8 Buffer[BOB64_SYSCALL_MAX_BUFFER];
    char ArgumentStorage[BOB64_SYSCALL_MAX_ARGUMENT_BYTES];
    char *Arguments[BOB64_SYSCALL_MAX_ARGUMENTS];
    BOB64_FILE_INFO FileEntries[BOB64_SYSCALL_MAX_FILES];
    BOB64_SYSCALL_HANDLE Handles[BOB64_SYSCALL_OPEN_HANDLES];
    u64 NextHandle;
} BOB64_SYSCALL_CONTEXT;

/* Keep per-app dispatch state, scratch buffers, and streaming handles together. */
static BOB64_SYSCALL_CONTEXT syscall_context={.NextHandle=1};
static BOB64_SYSCALL_CONTEXT syscall_context_stack[BOB64_SYSCALL_CONTEXT_DEPTH];
static u32 syscall_context_depth;
#define syscall_write_character syscall_context.WriteCharacter
#define syscall_write_context syscall_context.WriteContext
#define syscall_read_user syscall_context.ReadUser
#define syscall_read_user_context syscall_context.ReadUserContext
#define syscall_write_user syscall_context.WriteUser
#define syscall_write_user_context syscall_context.WriteUserContext
#define syscall_file_read syscall_context.FileRead
#define syscall_file_write syscall_context.FileWrite
#define syscall_filesystem_context syscall_context.FilesystemContext
#define syscall_file_list syscall_context.FileList
#define syscall_file_delete syscall_context.FileDelete
#define syscall_file_manager_context syscall_context.FileManagerContext
#define syscall_file_open syscall_context.FileOpen
#define syscall_file_read_at syscall_context.FileReadAt
#define syscall_file_write_at syscall_context.FileWriteAt
#define syscall_file_stream_context syscall_context.FileStreamContext
#define syscall_wait_event syscall_context.WaitEvent
#define syscall_wait_event_context syscall_context.WaitEventContext
#define syscall_run_application syscall_context.RunApplication
#define syscall_app_runner_context syscall_context.AppRunnerContext
#define syscall_window_server syscall_context.WindowServer
#define syscall_window_owner syscall_context.WindowOwner
#define syscall_page_table syscall_context.PageTable
#define syscall_surface_chunk syscall_context.SurfaceChunk
#define syscall_buffer syscall_context.Buffer
#define syscall_argument_storage syscall_context.ArgumentStorage
#define syscall_arguments syscall_context.Arguments
#define syscall_file_entries syscall_context.FileEntries
#define syscall_handles syscall_context.Handles
#define syscall_next_handle syscall_context.NextHandle

static void copy_context(BOB64_SYSCALL_CONTEXT *destination,
                         const BOB64_SYSCALL_CONTEXT *source) {
    u8 *out=(u8 *)destination;
    const u8 *in=(const u8 *)source;
    for(usize i=0;i<sizeof(*destination);i++)out[i]=in[i];
}

int bob64_syscall_context_push(void) {
    if(syscall_context_depth>=BOB64_SYSCALL_CONTEXT_DEPTH)return -1;
    copy_context(&syscall_context_stack[syscall_context_depth],&syscall_context);
    syscall_context_depth++;
    return 0;
}

int bob64_syscall_context_pop(void) {
    if(!syscall_context_depth)return -1;
    syscall_context_depth--;
    copy_context(&syscall_context,
                 &syscall_context_stack[syscall_context_depth]);
    return 0;
}

void bob64_user_request_return(s64 value) {
    if(bob64_user_depth&&bob64_user_depth<=BOB64_USER_ENTRY_MAX_DEPTH)
        bob64_user_return_frames[bob64_user_depth-1].ReturnValue=value;
    bob64_user_return_value=value;
    bob64_user_active=0;
}

static BOB64_SYSCALL_HANDLE *find_handle(u64 id) {
    if(!id)return 0;
    for(usize i=0;i<BOB64_SYSCALL_OPEN_HANDLES;i++)
        if(syscall_handles[i].Id==id)return &syscall_handles[i];
    return 0;
}

void bob64_syscall_set_write(BOB64_SYSCALL_WRITE write_character,void *context) {
    syscall_write_character=write_character;
    syscall_write_context=context;
}

void bob64_syscall_set_read_user(BOB64_SYSCALL_READ_USER read_user,void *context) {
    syscall_read_user=read_user;
    syscall_read_user_context=context;
}

void bob64_syscall_set_write_user(BOB64_SYSCALL_WRITE_USER write_user,void *context) {
    syscall_write_user=write_user;
    syscall_write_user_context=context;
}

void bob64_syscall_set_filesystem(BOB64_SYSCALL_FILE_READ read_file,
                                  BOB64_SYSCALL_FILE_WRITE write_file,void *context) {
    syscall_file_read=read_file;
    syscall_file_write=write_file;
    syscall_filesystem_context=context;
}

void bob64_syscall_set_file_manager(BOB64_SYSCALL_FILE_LIST list_files,
                                    BOB64_SYSCALL_FILE_DELETE delete_file,
                                    void *context) {
    syscall_file_list=list_files;
    syscall_file_delete=delete_file;
    syscall_file_manager_context=context;
}

void bob64_syscall_set_file_stream(BOB64_SYSCALL_FILE_OPEN open_file,
                                   BOB64_SYSCALL_FILE_READ_AT read_at,
                                   BOB64_SYSCALL_FILE_WRITE_AT write_at,
                                   void *context) {
    syscall_file_open=open_file;syscall_file_read_at=read_at;
    syscall_file_write_at=write_at;syscall_file_stream_context=context;
    for(usize i=0;i<BOB64_SYSCALL_OPEN_HANDLES;i++)syscall_handles[i].Id=0;
}

void bob64_syscall_set_wait_event(BOB64_SYSCALL_EVENT_WAIT wait_event,void *context) {
    syscall_wait_event=wait_event;
    syscall_wait_event_context=context;
}

void bob64_syscall_set_app_runner(BOB64_SYSCALL_APP_RUN run_application,
                                  void *context) {
    syscall_run_application=run_application;
    syscall_app_runner_context=context;
}

void bob64_syscall_set_window_server(BOB64_WINDOW_SERVER *server,u64 owner) {
    syscall_window_server=server;
    syscall_window_owner=server?owner:0;
}

void bob64_syscall_set_address_space(const BOB64_PAGE_TABLE *page_table) {
    syscall_page_table=page_table;
}

static int syscall_validate_user_range(const BOB64_PAGE_TABLE *page_table,
                                       u64 address,u64 length,u64 required_flags,
                                       u64 maximum_length) {
    u64 arena_end=BOB64_PROCESS_IMAGE_BASE+BOB64_PROCESS_ISOLATED_SIZE;
    u64 end,first_page,last_page,physical,flags;
    if(!page_table||length>maximum_length||
       address<BOB64_PROCESS_IMAGE_BASE||address>=arena_end||
       length>arena_end-address)return -1;
    if(!length)return 0;
    end=address+length-1;
    first_page=address&~(BOB64_PAGE_SIZE-1);
    last_page=end&~(BOB64_PAGE_SIZE-1);
    for(u64 page=first_page;;page+=BOB64_PAGE_SIZE) {
        if(bob64_page_translate(page_table,page,&physical,&flags)!=1||
           (flags&required_flags)!=required_flags)return -1;
        if(page==last_page)break;
    }
    return 0;
}

int bob64_syscall_validate_user_range(const BOB64_PAGE_TABLE *page_table,
                                      u64 address,u64 length) {
    return syscall_validate_user_range(page_table,address,length,BOB64_PAGE_USER,
                                       BOB64_SYSCALL_MAX_BUFFER);
}

u64 BOB64_MS_ABI bob64_syscall_dispatch(BOB64_INTERRUPT_FRAME *frame) {
    if(!frame)return 0;
    if((frame->CS&3)!=3||!bob64_user_active) {
        frame->RAX=(u64)-1;
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_QUERY_ABI) {
        frame->RAX=BOB64_SYSCALL_ABI_VERSION;
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_GET_TICKS) {
        frame->RAX=bob64_timer_ticks();
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_RUN_APPLICATION) {
        char name[BOB64_SYSCALL_MAX_FILENAME+1];
        u64 name_address=frame->RCX,name_length=frame->RDX;
        u64 status_address=frame->R8;
        u64 argument_count=frame->RSI,argument_vector=frame->R9;
        usize argument_bytes=0;
        s64 exit_status=0;
        if(!syscall_run_application||!syscall_read_user||!syscall_write_user) {
            frame->RAX=(u64)-38;return 0;
        }
        if(!name_length||name_length>BOB64_SYSCALL_MAX_FILENAME) {
            frame->RAX=(u64)-22;return 0;
        }
        if(argument_count>BOB64_SYSCALL_MAX_ARGUMENTS||
           (argument_count&&!argument_vector)) {frame->RAX=(u64)-22;return 0;}
        if(syscall_validate_user_range(syscall_page_table,name_address,name_length,
               BOB64_PAGE_USER,BOB64_SYSCALL_MAX_BUFFER)||
           syscall_validate_user_range(syscall_page_table,status_address,
               sizeof(exit_status),BOB64_PAGE_USER|BOB64_PAGE_WRITE,
               sizeof(exit_status))||
           syscall_read_user(syscall_read_user_context,name_address,name,
                             (usize)name_length)) {
            frame->RAX=(u64)-14;return 0;
        }
        name[name_length]=0;
        if(argument_count) {
            u64 vector_bytes=argument_count*sizeof(u64);
            if(syscall_validate_user_range(syscall_page_table,argument_vector,
                    vector_bytes,BOB64_PAGE_USER,BOB64_SYSCALL_MAX_ARGUMENT_BYTES)) {
                frame->RAX=(u64)-14;return 0;
            }
            for(usize i=0;i<(usize)argument_count;i++) {
                u64 string_address=0;
                int terminated=0;
                if(syscall_read_user(syscall_read_user_context,
                       argument_vector+i*sizeof(u64),&string_address,sizeof(u64))||
                   !string_address) {frame->RAX=(u64)-14;return 0;}
                syscall_arguments[i]=syscall_argument_storage+argument_bytes;
                while(argument_bytes<sizeof(syscall_context.ArgumentStorage)) {
                    char character;
                    if(syscall_validate_user_range(syscall_page_table,
                           string_address,1,BOB64_PAGE_USER,
                           BOB64_SYSCALL_MAX_ARGUMENT_BYTES)||
                       syscall_read_user(syscall_read_user_context,string_address,
                           &character,1)) {frame->RAX=(u64)-14;return 0;}
                    string_address++;
                    syscall_argument_storage[argument_bytes++]=character;
                    if(!character){terminated=1;break;}
                }
                if(!terminated) {frame->RAX=(u64)-7;return 0;}
            }
        }
        int result=syscall_run_application(syscall_app_runner_context,name,
                    (usize)argument_count,
                    (const char *const *)syscall_arguments,
                    &exit_status);
        if(result) {frame->RAX=(u64)(result<0?result:-5);return 0;}
        if(syscall_write_user(syscall_write_user_context,status_address,
                              &exit_status,sizeof(exit_status))) {
            frame->RAX=(u64)-14;return 0;
        }
        frame->RAX=0;return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_WRITE_CHAR) {
        if(!syscall_write_character) {
            frame->RAX=(u64)-1;
            return 0;
        }
        syscall_write_character(syscall_write_context,(u8)frame->RCX);
        frame->RAX=0;
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_WRITE_BUFFER) {
        u64 address=frame->RCX,length=frame->RDX;
        if(!syscall_write_character||!syscall_read_user||
           bob64_syscall_validate_user_range(syscall_page_table,address,length)||
           syscall_read_user(syscall_read_user_context,address,syscall_buffer,(usize)length)) {
            frame->RAX=(u64)-14;
            return 0;
        }
        for(u64 i=0;i<length;i++)
            syscall_write_character(syscall_write_context,syscall_buffer[(usize)i]);
        frame->RAX=length;
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_GET_DISPLAY) {
        u32 width,height;
        if(bob64_framebuffer_resolution(&width,&height)) {
            frame->RAX=(u64)-19;frame->RDX=0;
        } else {
            frame->RAX=width;frame->RDX=height;
        }
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_PRESENT) {
        u64 address=frame->RCX,width=frame->RDX,height=frame->R8;
        u32 display_width,display_height;
        u64 pixels,bytes,offset=0;
        if(bob64_framebuffer_resolution(&display_width,&display_height)) {
            frame->RAX=(u64)-19;return 0;
        }
        if(width!=display_width||height!=display_height||!height||
           width>~(u64)0/height||
           (pixels=width*height)>~(u64)0/sizeof(u32)||
           (bytes=pixels*sizeof(u32))>BOB64_SYSCALL_MAX_SURFACE_BYTES||
           syscall_validate_user_range(syscall_page_table,address,bytes,
               BOB64_PAGE_USER,BOB64_SYSCALL_MAX_SURFACE_BYTES)||!syscall_read_user) {
            frame->RAX=(u64)-14;return 0;
        }
        while(offset<pixels) {
            u32 count=(u32)(pixels-offset>
                 sizeof(syscall_surface_chunk)/sizeof(syscall_surface_chunk[0])?
                 sizeof(syscall_surface_chunk)/sizeof(syscall_surface_chunk[0]):
                 pixels-offset);
            if(syscall_read_user(syscall_read_user_context,address+offset*sizeof(u32),
                                 syscall_surface_chunk,(usize)count*sizeof(u32))||
               bob64_framebuffer_write_pixels(offset,syscall_surface_chunk,count)) {
                frame->RAX=(u64)-14;return 0;
            }
            offset+=count;
        }
        frame->RAX=pixels;return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_WAIT_EVENT) {
        BOB64_EVENT event;
        BOB64_EVENT routed;
        u64 address=frame->RCX;
        if(!syscall_wait_event||!syscall_write_user||
           syscall_validate_user_range(syscall_page_table,address,sizeof(event),
               BOB64_PAGE_USER|BOB64_PAGE_WRITE,sizeof(event))||
           syscall_wait_event(syscall_wait_event_context,&event)) {
            frame->RAX=(u64)-14;
            return 0;
        }
        if(syscall_window_server) {
            if(bob64_window_server_route_event(syscall_window_server,
                syscall_window_owner,&event,&routed)<0) {
                frame->RAX=(u64)-14;return 0;
            }
            event=routed;
        }
        if(syscall_write_user(syscall_write_user_context,address,&event,sizeof(event)))
            frame->RAX=(u64)-14;
        else frame->RAX=0;
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_LIST_FILES) {
        u64 address=frame->RCX,capacity=frame->RDX;
        u64 bytes;
        s64 count;
        if(!capacity||capacity>BOB64_SYSCALL_MAX_FILES||
           capacity>~(u64)0/sizeof(BOB64_FILE_INFO)) {
            frame->RAX=(u64)-22;return 0;
        }
        bytes=capacity*sizeof(BOB64_FILE_INFO);
        if(syscall_validate_user_range(syscall_page_table,address,bytes,
            BOB64_PAGE_USER|BOB64_PAGE_WRITE,BOB64_SYSCALL_MAX_FILES*sizeof(BOB64_FILE_INFO))) {
            frame->RAX=(u64)-14;return 0;
        }
        if(!syscall_file_list||!syscall_write_user) {
            frame->RAX=(u64)-38;return 0;
        }
        for(usize i=0;i<(usize)capacity;i++)
            for(usize j=0;j<sizeof(syscall_file_entries[i]);j++)
                ((u8 *)&syscall_file_entries[i])[j]=0;
        count=syscall_file_list(syscall_file_manager_context,syscall_file_entries,
                                (usize)capacity);
        if(count<0||(u64)count>capacity) {
            frame->RAX=(u64)(count<0?count:-5);return 0;
        }
        bytes=(u64)count*sizeof(BOB64_FILE_INFO);
        if(bytes&&syscall_write_user(syscall_write_user_context,address,
                                     syscall_file_entries,(usize)bytes)) {
            frame->RAX=(u64)-14;return 0;
        }
        frame->RAX=(u64)count;return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_DELETE_FILE) {
        u64 name_address=frame->RCX,name_length=frame->RDX;
        char name[BOB64_SYSCALL_MAX_FILENAME+1];
        if(!name_length||name_length>BOB64_SYSCALL_MAX_FILENAME) {
            frame->RAX=(u64)-22;return 0;
        }
        if(bob64_syscall_validate_user_range(syscall_page_table,name_address,name_length)||
           !syscall_read_user||syscall_read_user(syscall_read_user_context,name_address,
                                                 name,(usize)name_length)) {
            frame->RAX=(u64)-14;return 0;
        }
        for(u64 i=0;i<name_length;i++)if(!name[i]) {
            frame->RAX=(u64)-22;return 0;
        }
        name[name_length]=0;
        if(!syscall_file_delete)frame->RAX=(u64)-38;
        else {
            s64 result=syscall_file_delete(syscall_file_manager_context,name);
            if(!result)for(usize i=0;i<BOB64_SYSCALL_OPEN_HANDLES;i++)
                if(syscall_handles[i].Id) {
                    usize j=0;while(syscall_handles[i].Name[j]&&syscall_handles[i].Name[j]==name[j])j++;
                    if(!syscall_handles[i].Name[j]&&!name[j])syscall_handles[i].Id=0;
                }
            frame->RAX=(u64)result;
        }
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_CREATE_WINDOW) {
        s64 x=(s64)frame->RCX,y=(s64)frame->RDX;
        if(!syscall_window_server||x<(-2147483647LL-1)||x>2147483647LL||
           y<(-2147483647LL-1)||y>2147483647LL||
           frame->R8>0xffffffffULL||frame->R9>0xffffffffULL||!frame->R8||!frame->R9) {
            frame->RAX=(u64)-22;return 0;
        }
        BOB64_WINDOW_HANDLE handle=bob64_window_server_create(syscall_window_server,
            syscall_window_owner,(s32)x,(s32)y,(u32)frame->R8,(u32)frame->R9);
        frame->RAX=handle?handle:(u64)-12;
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_DESTROY_WINDOW) {
        if(!syscall_window_server||bob64_window_server_destroy(syscall_window_server,
           syscall_window_owner,frame->RCX))frame->RAX=(u64)-22;
        else frame->RAX=0;
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_FOCUS_WINDOW) {
        if(!syscall_window_server||bob64_window_server_focus(syscall_window_server,
           syscall_window_owner,frame->RCX))frame->RAX=(u64)-22;
        else frame->RAX=0;
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_PRESENT_WINDOW) {
        u64 handle=frame->RCX,address=frame->RDX,width=frame->R8,height=frame->R9;
        u32 expected_width,expected_height;
        u64 pixels,bytes,offset=0;
        if(!syscall_window_server||width>0xffffffffULL||height>0xffffffffULL||
           !width||!height||width>~(u64)0/height||
           (pixels=width*height)>~(u64)0/sizeof(u32)||
           (bytes=pixels*sizeof(u32))>BOB64_WINDOW_SERVER_SURFACE_LIMIT||
           bob64_window_server_surface_size(syscall_window_server,syscall_window_owner,
               handle,&expected_width,&expected_height)||
           width!=expected_width||height!=expected_height||
           syscall_validate_user_range(syscall_page_table,address,bytes,
               BOB64_PAGE_USER,BOB64_WINDOW_SERVER_SURFACE_LIMIT)||!syscall_read_user) {
            frame->RAX=(u64)-14;return 0;
        }
        while(offset<pixels) {
            u32 count=(u32)(pixels-offset>
                sizeof(syscall_surface_chunk)/sizeof(syscall_surface_chunk[0])?
                sizeof(syscall_surface_chunk)/sizeof(syscall_surface_chunk[0]):
                pixels-offset);
            if(syscall_read_user(syscall_read_user_context,address+offset*sizeof(u32),
                syscall_surface_chunk,(usize)count*sizeof(u32))||
               bob64_window_server_write_pixels(syscall_window_server,
                syscall_window_owner,handle,offset,syscall_surface_chunk,count)) {
                frame->RAX=(u64)-14;return 0;
            }
            offset+=count;
        }
        frame->RAX=bob64_window_server_present(syscall_window_server,
            syscall_window_owner,handle)?(u64)-14:pixels;
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_READ_FILE||
       frame->RAX==BOB64_SYSCALL_WRITE_FILE) {
        u64 name_address=frame->RCX,name_length=frame->RDX;
        u64 buffer_address=frame->R8,buffer_length=frame->R9;
        char name[BOB64_SYSCALL_MAX_FILENAME+1];
        s64 result;
        if(!name_length||name_length>BOB64_SYSCALL_MAX_FILENAME||
           buffer_length>BOB64_SYSCALL_MAX_BUFFER) {
            frame->RAX=(u64)-22;
            return 0;
        }
        if(bob64_syscall_validate_user_range(syscall_page_table,name_address,name_length)||
           syscall_validate_user_range(syscall_page_table,buffer_address,buffer_length,
               BOB64_PAGE_USER|(frame->RAX==BOB64_SYSCALL_READ_FILE?BOB64_PAGE_WRITE:0),
               BOB64_SYSCALL_MAX_BUFFER)||
           !syscall_read_user||
           syscall_read_user(syscall_read_user_context,name_address,name,(usize)name_length)) {
            frame->RAX=(u64)-14;
            return 0;
        }
        for(u64 i=0;i<name_length;i++)if(!name[i]) {
            frame->RAX=(u64)-22;
            return 0;
        }
        name[name_length]=0;
        if(frame->RAX==BOB64_SYSCALL_WRITE_FILE) {
            if(!syscall_file_write)result=-38;
            else if(syscall_read_user(syscall_read_user_context,buffer_address,
                                      syscall_buffer,(usize)buffer_length))result=-14;
            else result=syscall_file_write(syscall_filesystem_context,name,syscall_buffer,
                                           (usize)buffer_length);
        } else if(!syscall_file_read)result=-38;
        else {
            result=syscall_file_read(syscall_filesystem_context,name,syscall_buffer,
                                     (usize)buffer_length);
            if(result>=0&&((u64)result>buffer_length||(result&&(!syscall_write_user||
               syscall_write_user(syscall_write_user_context,buffer_address,syscall_buffer,
                                  (usize)result)))))result=-14;
        }
        frame->RAX=(u64)result;
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_OPEN_FILE) {
        u64 name_address=frame->RCX,name_length=frame->RDX;
        u32 flags=(u32)frame->R8;
        char name[BOB64_SYSCALL_MAX_FILENAME+1];
        BOB64_SYSCALL_HANDLE *slot=0;
        s64 size;u64 new_id=0;
        if(!name_length||name_length>BOB64_SYSCALL_MAX_FILENAME||
           frame->R8>BOB64_FILE_OPEN_FLAGS||!(flags&(BOB64_FILE_OPEN_READ|BOB64_FILE_OPEN_WRITE))||
           ((flags&BOB64_FILE_OPEN_TRUNCATE)&&!(flags&BOB64_FILE_OPEN_WRITE))||
           ((flags&BOB64_FILE_OPEN_APPEND)&&!(flags&BOB64_FILE_OPEN_WRITE))) {
            frame->RAX=(u64)-22;return 0;
        }
        if(bob64_syscall_validate_user_range(syscall_page_table,name_address,name_length)||
           !syscall_read_user||syscall_read_user(syscall_read_user_context,name_address,
                                                  name,(usize)name_length)) {
            frame->RAX=(u64)-14;return 0;
        }
        for(u64 i=0;i<name_length;i++)if(!name[i]) {frame->RAX=(u64)-22;return 0;}
        name[name_length]=0;
        if(!syscall_file_open) {frame->RAX=(u64)-38;return 0;}
        for(usize i=0;i<BOB64_SYSCALL_OPEN_HANDLES;i++)
            if(!syscall_handles[i].Id){slot=&syscall_handles[i];break;}
        if(!slot) {frame->RAX=(u64)-24;return 0;}
        size=syscall_file_open(syscall_file_stream_context,name,flags);
        if(size<0) {frame->RAX=(u64)size;return 0;}
        for(usize attempt=0;attempt<=BOB64_SYSCALL_OPEN_HANDLES;attempt++) {
            u64 candidate=syscall_next_handle++;
            if(candidate&&!find_handle(candidate)){new_id=candidate;break;}
        }
        if(!new_id){frame->RAX=(u64)-24;return 0;}
        slot->Id=new_id;
        slot->Flags=flags;slot->Position=(flags&BOB64_FILE_OPEN_APPEND)?(u64)size:0;
        for(usize i=0;i<=name_length;i++)slot->Name[i]=name[i];
        frame->RAX=slot->Id;return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_READ_HANDLE||
       frame->RAX==BOB64_SYSCALL_WRITE_HANDLE) {
        u64 id=frame->RCX,address=frame->RDX,length=frame->R8;
        BOB64_SYSCALL_HANDLE *handle=find_handle(id);
        s64 result;
        int writing=frame->RAX==BOB64_SYSCALL_WRITE_HANDLE;
        if(length>BOB64_SYSCALL_MAX_BUFFER) {frame->RAX=(u64)-22;return 0;}
        if(!handle||!(handle->Flags&(writing?BOB64_FILE_OPEN_WRITE:BOB64_FILE_OPEN_READ))) {
            frame->RAX=(u64)-9;return 0;
        }
        if(syscall_validate_user_range(syscall_page_table,address,length,
             BOB64_PAGE_USER|(writing?0:BOB64_PAGE_WRITE),BOB64_SYSCALL_MAX_BUFFER)||
           (length&&!syscall_read_user)) {frame->RAX=(u64)-14;return 0;}
        if(writing) {
            if(!syscall_file_write_at) {frame->RAX=(u64)-38;return 0;}
            if(handle->Flags&BOB64_FILE_OPEN_APPEND) {
                s64 end=syscall_file_open(syscall_file_stream_context,handle->Name,
                    handle->Flags&(BOB64_FILE_OPEN_READ|BOB64_FILE_OPEN_WRITE));
                if(end<0) {frame->RAX=(u64)end;return 0;}
                handle->Position=(u64)end;
            }
            if(handle->Position>0x7fffffffffffffffULL||
               length>0x7fffffffffffffffULL-handle->Position) {
                frame->RAX=(u64)-75;return 0;
            }
            if(length&&syscall_read_user(syscall_read_user_context,address,syscall_buffer,
                                           (usize)length)) {
                frame->RAX=(u64)-14;return 0;
            }
            result=syscall_file_write_at(syscall_file_stream_context,handle->Name,
                       handle->Position,syscall_buffer,(usize)length);
            if(result>=0&&(u64)result<=length&&
               (u64)result<=0x7fffffffffffffffULL-handle->Position)
                handle->Position+=(u64)result;
            else if(result>=0)result=-5;
        } else {
            if(!syscall_file_read_at||!syscall_write_user) {frame->RAX=(u64)-38;return 0;}
            if(handle->Position>0x7fffffffffffffffULL||
               length>0x7fffffffffffffffULL-handle->Position) {
                frame->RAX=(u64)-75;return 0;
            }
            result=syscall_file_read_at(syscall_file_stream_context,handle->Name,
                       handle->Position,syscall_buffer,(usize)length);
            if(result>=0&&(u64)result<=length) {
                if(result&&syscall_write_user(syscall_write_user_context,address,
                                                    syscall_buffer,(usize)result))result=-14;
                else handle->Position+=(u64)result;
            } else if(result>=0)result=-5;
        }
        frame->RAX=(u64)result;return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_SEEK_HANDLE) {
        BOB64_SYSCALL_HANDLE *handle=find_handle(frame->RCX);
        if(!handle)frame->RAX=(u64)-9;
        else if(frame->RDX>0x7fffffffffffffffULL)frame->RAX=(u64)-22;
        else {handle->Position=frame->RDX;frame->RAX=handle->Position;}
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_CLOSE_HANDLE) {
        BOB64_SYSCALL_HANDLE *handle=find_handle(frame->RCX);
        if(!handle)frame->RAX=(u64)-9;
        else {handle->Id=0;frame->RAX=0;}
        return 0;
    }
    if(frame->RAX==BOB64_SYSCALL_EXIT)bob64_user_request_return((s64)frame->RCX);
    else bob64_user_request_return(-38);
    return 1;
}
