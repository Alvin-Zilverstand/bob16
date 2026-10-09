#ifndef BOB64_SYSCALL_H
#define BOB64_SYSCALL_H

#include "abi.h"
#include "interrupts.h"
#include "paging.h"
#include "event.h"
#include "window_server.h"

#define BOB64_SYSCALL_CONTEXT_MAX_DEPTH 8u
#define BOB64_USER_ENTRY_MAX_DEPTH 8u

typedef struct {
    u64 KernelRsp;
    s64 ReturnValue;
    s64 PreviousReturnValue;
    u64 PreviousInterruptState;
    u64 PreviousActive;
} BOB64_USER_RETURN_FRAME;

_Static_assert(sizeof(BOB64_USER_RETURN_FRAME)==40,
               "assembly user-return frame layout");
_Static_assert(__builtin_offsetof(BOB64_USER_RETURN_FRAME,ReturnValue)==8&&
               __builtin_offsetof(BOB64_USER_RETURN_FRAME,PreviousReturnValue)==16&&
               __builtin_offsetof(BOB64_USER_RETURN_FRAME,PreviousInterruptState)==24&&
               __builtin_offsetof(BOB64_USER_RETURN_FRAME,PreviousActive)==32,
               "assembly user-return frame offsets");

typedef void (*BOB64_SYSCALL_WRITE)(void *context,u8 character);
typedef int (*BOB64_SYSCALL_READ_USER)(void *context,u64 address,
                                       void *destination,usize length);
typedef int (*BOB64_SYSCALL_WRITE_USER)(void *context,u64 address,
                                        const void *source,usize length);
typedef s64 (*BOB64_SYSCALL_FILE_READ)(void *context,const char *name,
                                       u8 *buffer,usize capacity);
typedef s64 (*BOB64_SYSCALL_FILE_WRITE)(void *context,const char *name,
                                        const u8 *buffer,usize length);
typedef s64 (*BOB64_SYSCALL_FILE_LIST)(void *context,BOB64_FILE_INFO *entries,
                                       usize capacity);
typedef s64 (*BOB64_SYSCALL_FILE_DELETE)(void *context,const char *name);
typedef s64 (*BOB64_SYSCALL_FILE_OPEN)(void *context,const char *name,u32 flags);
typedef s64 (*BOB64_SYSCALL_FILE_READ_AT)(void *context,const char *name,u64 offset,
                                          u8 *buffer,usize capacity);
typedef s64 (*BOB64_SYSCALL_FILE_WRITE_AT)(void *context,const char *name,u64 offset,
                                           const u8 *buffer,usize length);
typedef int (*BOB64_SYSCALL_EVENT_WAIT)(void *context,BOB64_EVENT *event);
typedef int (*BOB64_SYSCALL_APP_RUN)(void *context,const char *name,
                                     usize argument_count,
                                     const char *const *arguments,
                                     s64 *exit_status);

extern volatile u64 bob64_user_active;
extern volatile u64 bob64_user_depth;
extern BOB64_USER_RETURN_FRAME bob64_user_return_frames[BOB64_USER_ENTRY_MAX_DEPTH];
extern volatile s64 bob64_user_return_value;

void bob64_syscall_set_write(BOB64_SYSCALL_WRITE write_character,void *context);
void bob64_syscall_set_read_user(BOB64_SYSCALL_READ_USER read_user,void *context);
void bob64_syscall_set_write_user(BOB64_SYSCALL_WRITE_USER write_user,void *context);
void bob64_syscall_set_filesystem(BOB64_SYSCALL_FILE_READ read_file,
                                  BOB64_SYSCALL_FILE_WRITE write_file,void *context);
void bob64_syscall_set_file_manager(BOB64_SYSCALL_FILE_LIST list_files,
                                    BOB64_SYSCALL_FILE_DELETE delete_file,
                                    void *context);
void bob64_syscall_set_file_stream(BOB64_SYSCALL_FILE_OPEN open_file,
                                   BOB64_SYSCALL_FILE_READ_AT read_at,
                                   BOB64_SYSCALL_FILE_WRITE_AT write_at,
                                   void *context);
void bob64_syscall_set_wait_event(BOB64_SYSCALL_EVENT_WAIT wait_event,void *context);
void bob64_syscall_set_app_runner(BOB64_SYSCALL_APP_RUN run_application,
                                  void *context);
void bob64_syscall_set_window_server(BOB64_WINDOW_SERVER *server,u64 owner);
void bob64_syscall_set_address_space(const BOB64_PAGE_TABLE *page_table);
/* Nested kernel app runs save and restore the complete syscall service state. */
int bob64_syscall_context_push(void);
int bob64_syscall_context_pop(void);
void bob64_user_request_return(s64 value);
int bob64_syscall_validate_user_range(const BOB64_PAGE_TABLE *page_table,
                                      u64 address,u64 length);
/* Returns zero to resume the app, one to return from bob64_enter_user. */
u64 BOB64_MS_ABI bob64_syscall_dispatch(BOB64_INTERRUPT_FRAME *frame);
s64 BOB64_MS_ABI bob64_enter_user(u64 entry,u64 startup,u64 stack);

#endif
