#include "mouse.h"

static BOB64_MOUSE_STATE mouse_state;
static u8 mouse_ps2_buttons;
static u8 mouse_usb_buttons[BOB64_MOUSE_USB_DEVICE_LIMIT];
#define MOUSE_EVENT_QUEUE_SIZE 64u
static BOB64_EVENT mouse_event_queue[MOUSE_EVENT_QUEUE_SIZE];
static volatile u32 mouse_event_head,mouse_event_tail;
static volatile u8 mouse_irq_active;

static int mouse_queue_event(const BOB64_EVENT *event) {
#if defined(BOB64_UEFI_ABI)
    u64 flags;
#endif
    u32 head,tail;
    if(!event)return -1;
#if defined(BOB64_UEFI_ABI)
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
#endif
    head=mouse_event_head;tail=mouse_event_tail;
    if((u32)(head-tail)>=MOUSE_EVENT_QUEUE_SIZE) {
#if defined(BOB64_UEFI_ABI)
        if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");
#endif
        return -1;
    }
    mouse_event_queue[head&(MOUSE_EVENT_QUEUE_SIZE-1)]=*event;
    __asm__ volatile("":::"memory");
    mouse_event_head=head+1;
#if defined(BOB64_UEFI_ABI)
    if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");
#endif
    return 0;
}

static void out8(u16 port,u8 value) {
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}

static u8 in8(u16 port) {
    u8 value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static int wait_input_empty(void) {
    for(u32 i=0;i<100000;i++) {
        u8 status=in8(0x64);
        if(status==0xff)return -1;
        if(!(status&2))return 0;
    }
    return -1;
}

static int send_mouse_byte(u8 command) {
    if(wait_input_empty())return -1;
    out8(0x64,0xd4);
    if(wait_input_empty())return -1;
    out8(0x60,command);
    for(u32 i=0;i<100000;i++) {
        u8 status=in8(0x64);
        if(status==0xff)return -1;
        if((status&0x21)==0x21)return in8(0x60)==0xfa?0:-1;
    }
    return -1;
}

static int read_mouse_response(u8 *value) {
    if(!value)return -1;
    for(u32 i=0;i<100000;i++) {
        u8 status=in8(0x64);
        if(status==0xff)return -1;
        if((status&0x21)==0x21) {*value=in8(0x60);return 0;}
    }
    return -1;
}

static int set_sample_rate(u8 rate) {
    return send_mouse_byte(0xf3)||send_mouse_byte(rate)?-1:0;
}

int bob64_mouse_reset(BOB64_MOUSE_STATE *state,u32 width,u32 height) {
    if(!state||!width||!height||width>0x7fffffffU||height>0x7fffffffU)return -1;
    state->Width=width;state->Height=height;
    state->X=(s32)(width/2);state->Y=(s32)(height/2);
    state->Packet[0]=state->Packet[1]=state->Packet[2]=state->Packet[3]=0;
    state->PacketLength=0;state->PacketSize=3;state->Buttons=0;
    state->DeviceId=0;state->Enabled=0;
    return 0;
}

int bob64_mouse_irq_queue_init(u32 width,u32 height) {
    if(bob64_mouse_reset(&mouse_state,width,height))return -1;
    mouse_ps2_buttons=0;
    for(u32 i=0;i<BOB64_MOUSE_USB_DEVICE_LIMIT;i++)mouse_usb_buttons[i]=0;
    mouse_event_head=mouse_event_tail=0;mouse_irq_active=0;
    return 0;
}

void bob64_mouse_irq_set_active(int active) {
    mouse_event_head=mouse_event_tail=0;
    mouse_irq_active=(u8)(active!=0);
}

int bob64_mouse_irq_capture(u8 value) {
    BOB64_EVENT event;
    if(bob64_mouse_decode(&mouse_state,value,&event))return -1;
    return mouse_queue_event(&event);
}

int bob64_mouse_irq_pop_event(BOB64_EVENT *event) {
    u32 tail=mouse_event_tail,head=mouse_event_head;
    if(!event||tail==head)return -1;
    __asm__ volatile("":::"memory");
    *event=mouse_event_queue[tail&(MOUSE_EVENT_QUEUE_SIZE-1)];
    __asm__ volatile("":::"memory");
    mouse_event_tail=tail+1;
    return 0;
}

int bob64_mouse_irq_service(void) {
    u8 status,value;
    __asm__ volatile("inb $0x64,%0":"=a"(status));
    if(status==0xff||!(status&1)||!(status&0x20))return -1;
    __asm__ volatile("inb $0x60,%0":"=a"(value));
    return bob64_mouse_irq_capture(value);
}

static s32 signed_byte(u8 value) {
    return value<128?(s32)value:(s32)value-256;
}

static u8 mouse_combined_buttons(void) {
    u8 buttons=mouse_ps2_buttons;
    for(u32 i=0;i<BOB64_MOUSE_USB_DEVICE_LIMIT;i++)
        buttons|=mouse_usb_buttons[i];
    return buttons;
}

static int mouse_apply_report(BOB64_MOUSE_STATE *state,u8 buttons,s32 dx,
        s32 dy,s32 wheel,BOB64_EVENT *event) {
    s32 old_x,old_y,new_x,new_y;
    s64 proposed_x,proposed_y;
    if(!state||!event||!state->Width||!state->Height)return -1;
    old_x=state->X;old_y=state->Y;
    proposed_x=(s64)old_x+dx;proposed_y=(s64)old_y+dy;
    if(proposed_x<0)proposed_x=0;
    if(proposed_y<0)proposed_y=0;
    if((u64)proposed_x>=state->Width)proposed_x=(s64)state->Width-1;
    if((u64)proposed_y>=state->Height)proposed_y=(s64)state->Height-1;
    new_x=(s32)proposed_x;new_y=(s32)proposed_y;
    state->X=new_x;state->Y=new_y;
    if(new_x==old_x&&new_y==old_y&&buttons==state->Buttons&&!wheel)return -1;
    event->Type=buttons!=state->Buttons?BOB64_EVENT_MOUSE_BUTTON:
                wheel?BOB64_EVENT_MOUSE_WHEEL:BOB64_EVENT_MOUSE_MOVE;
    event->Key=0;event->Character=0;event->Modifiers=0;
    event->X=new_x;event->Y=new_y;
    event->DeltaX=new_x-old_x;event->DeltaY=new_y-old_y;
    event->Buttons=buttons;event->Wheel=wheel;event->WindowHandle=0;
    event->ScreenX=new_x;event->ScreenY=new_y;
    state->Buttons=buttons;
    return 0;
}

int bob64_mouse_decode(BOB64_MOUSE_STATE *state,u8 value,BOB64_EVENT *event) {
    u8 flags,buttons;
    s32 dx,dy,wheel=0;
    if(!state||!event||!state->Width||!state->Height)return -1;
    if(!state->PacketLength&&!(value&8))return -1;
    if(state->PacketSize!=3&&state->PacketSize!=4)return -1;
    state->Packet[state->PacketLength++]=value;
    if(state->PacketLength<state->PacketSize)return -1;
    state->PacketLength=0;
    flags=state->Packet[0];buttons=(u8)(flags&7);
    if(state->PacketSize==4) {
        u8 wheel_nibble=(u8)(state->Packet[3]&15);
        wheel=(wheel_nibble&8)?(s32)wheel_nibble-16:(s32)wheel_nibble;
        if(state->DeviceId==4)buttons|=(u8)((state->Packet[3]&0x30)>>1);
    }
    dx=(flags&0x40)?0:signed_byte(state->Packet[1]);
    dy=(flags&0x80)?0:-signed_byte(state->Packet[2]);
    if(state==&mouse_state) {
        mouse_ps2_buttons=buttons;
        buttons=mouse_combined_buttons();
    }
    return mouse_apply_report(state,buttons,dx,dy,wheel,event);
}

int bob64_mouse_usb_report_device(u32 device_index,u8 buttons,s8 delta_x,
                                  s8 delta_y,s8 wheel) {
    BOB64_EVENT event;
    int result;
    if(device_index>=BOB64_MOUSE_USB_DEVICE_LIMIT)return -1;
#if defined(BOB64_UEFI_ABI)
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
#endif
    mouse_usb_buttons[device_index]=(u8)(buttons&7u);
    result=mouse_apply_report(&mouse_state,mouse_combined_buttons(),
                              (s32)delta_x,(s32)delta_y,(s32)wheel,&event);
    if(!result)result=mouse_queue_event(&event);
#if defined(BOB64_UEFI_ABI)
    if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");
#endif
    return result;
}

int bob64_mouse_usb_report(u8 buttons,s8 delta_x,s8 delta_y,s8 wheel) {
    return bob64_mouse_usb_report_device(0,buttons,delta_x,delta_y,wheel);
}

int bob64_mouse_init(u32 width,u32 height) {
    u8 device_id;
    if(bob64_mouse_irq_queue_init(width,height))return -1;
    if(wait_input_empty())return -1;
    out8(0x64,0xa8);
    if(send_mouse_byte(0xf4))return -1;
    mouse_state.Enabled=1;
    if(!set_sample_rate(200)&&!set_sample_rate(100)&&!set_sample_rate(80)&&
       !send_mouse_byte(0xf2)&&!read_mouse_response(&device_id)&&
       (device_id==3||device_id==4)) {
        mouse_state.DeviceId=device_id;
        mouse_state.PacketSize=4;
    }
    return 0;
}

int bob64_mouse_poll_event(BOB64_EVENT *event) {
    u8 status,value;
    if(!event)return -1;
    if(!bob64_mouse_irq_pop_event(event))return 0;
    if(mouse_irq_active||!mouse_state.Enabled)return -1;
    status=in8(0x64);
    if(status==0xff||!(status&1)||!(status&0x20))return -1;
    value=in8(0x60);
    return bob64_mouse_decode(&mouse_state,value,event);
}
