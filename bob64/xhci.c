#include "xhci.h"

static int xhci_wait_status(BOB64_XHCI_READ32 read_register,void *context,
        u32 mask,u32 expected,u32 poll_limit,u32 *status_out) {
    for(u32 i=0;i<poll_limit;i++) {
        u32 status;
        if(read_register(context,4,&status))return -1;
        if(status_out)*status_out=status;
        if((status&mask)==expected)return 0;
    }
    return -2;
}

int bob64_xhci_halt_reset(BOB64_XHCI_READ32 read_register,
        BOB64_XHCI_WRITE32 write_register,void *context,u32 poll_limit,
        u32 *final_status) {
    u32 status,command;
    int result;
    if(!read_register||!write_register||!poll_limit)return -1;
    result=xhci_wait_status(read_register,context,
        BOB64_XHCI_USBSTS_NOT_READY,0,poll_limit,&status);
    if(result)return result;
    if(!(status&BOB64_XHCI_USBSTS_HALTED)) {
        if(read_register(context,0,&command)||
           write_register(context,0,command&~BOB64_XHCI_USBCMD_RUN))return -1;
        result=xhci_wait_status(read_register,context,
            BOB64_XHCI_USBSTS_HALTED,BOB64_XHCI_USBSTS_HALTED,
            poll_limit,&status);
        if(result)return result;
    }
    if(read_register(context,0,&command)||
       write_register(context,0,command|BOB64_XHCI_USBCMD_RESET))return -1;
    for(u32 i=0;i<poll_limit;i++) {
        if(read_register(context,0,&command)||
           read_register(context,4,&status))return -1;
        if(!(command&BOB64_XHCI_USBCMD_RESET)&&
           !(status&BOB64_XHCI_USBSTS_NOT_READY)) {
            if(final_status)*final_status=status;
            return (status&BOB64_XHCI_USBSTS_HALTED)?0:-3;
        }
    }
    return -2;
}
