#ifndef BOB_KERNEL_H
#define BOB_KERNEL_H
/* Guest services. bob16 int, char and pointers occupy one 16-bit word. */
void bob_putc(int character);
void bob_puts(char *text);
void bob_gets(char *buffer, int capacity);
void bob_halt(void);
int bob_getc(void);
int bob_run(int entry);
int bob_snapshot(int *descriptor, int operation);
int bob_key(void);
int bob_terminal(void);
int bob_columns(void);
int bob_rows(void);
#endif
