#ifndef BOB_KERNEL_H
#define BOB_KERNEL_H
/* Guest services. bob16 int, char and pointers occupy one 16-bit word. */
void bob_putc(int character);
void bob_puts(const char *text);
void bob_gets(char *buffer, int capacity);
void bob_halt(void);
int bob_getc(void);
int bob_run(int entry);
int bob_run_image(const char *path);
int bob_import_image(int *descriptor);
int bob_run_native(const int *descriptor);
int bob_os_service(int *request);
int bob_fs_register(int *descriptor);
int bob_snapshot(int *descriptor, int operation);
int bob_key(void);
int bob_terminal(void);
int bob_columns(void);
int bob_rows(void);
#endif
