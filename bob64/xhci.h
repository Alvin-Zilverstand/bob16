#ifndef BOB64_XHCI_H
#define BOB64_XHCI_H

#include "types.h"

#define BOB64_XHCI_USBCMD_RUN       0x00000001u
#define BOB64_XHCI_USBCMD_RESET     0x00000002u
#define BOB64_XHCI_USBSTS_HALTED    0x00000001u
#define BOB64_XHCI_USBSTS_NOT_READY 0x00000800u

typedef int (*BOB64_XHCI_READ32)(void *context,u32 offset,u32 *value);
typedef int (*BOB64_XHCI_WRITE32)(void *context,u32 offset,u32 value);

/* Stops a running xHC if needed, requests reset, and waits for it to settle. */
int bob64_xhci_halt_reset(BOB64_XHCI_READ32 read_register,
        BOB64_XHCI_WRITE32 write_register,void *context,u32 poll_limit,
        u32 *final_status);

#endif
