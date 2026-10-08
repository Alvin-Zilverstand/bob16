#include "keyboard.h"

static BOB64_KEYBOARD_STATE keyboard_state;
#define KEYBOARD_EVENT_QUEUE_SIZE 64u
static BOB64_EVENT keyboard_event_queue[KEYBOARD_EVENT_QUEUE_SIZE];
static volatile u32 keyboard_event_head,keyboard_event_tail;
static volatile u8 keyboard_irq_active;

static const u8 unshifted[128]={
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0a]='9',[0x0b]='0',
    [0x0c]='-',[0x0d]='=',[0x0e]='\b',[0x0f]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',
    [0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
    [0x1a]='[',[0x1b]=']',[0x1c]='\n',[0x1e]='a',[0x1f]='s',
    [0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',[0x24]='j',
    [0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',[0x29]='`',
    [0x2b]='\\',[0x2c]='z',[0x2d]='x',[0x2e]='c',[0x2f]='v',
    [0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',[0x34]='.',
    [0x35]='/',[0x39]=' '
};

static const u8 shifted[128]={
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',
    [0x07]='^',[0x08]='&',[0x09]='*',[0x0a]='(',[0x0b]=')',
    [0x0c]='_',[0x0d]='+',[0x10]='Q',[0x11]='W',[0x12]='E',
    [0x13]='R',[0x14]='T',[0x15]='Y',[0x16]='U',[0x17]='I',
    [0x18]='O',[0x19]='P',[0x1a]='{',[0x1b]='}',[0x1e]='A',
    [0x1f]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',
    [0x24]='J',[0x25]='K',[0x26]='L',[0x27]=':',[0x28]='"',
    [0x29]='~',[0x2b]='|',[0x2c]='Z',[0x2d]='X',[0x2e]='C',
    [0x2f]='V',[0x30]='B',[0x31]='N',[0x32]='M',[0x33]='<',
    [0x34]='>',[0x35]='?'
};

void bob64_keyboard_reset(void) {
    keyboard_state.LeftShift=0;keyboard_state.RightShift=0;
    keyboard_state.LeftControl=0;keyboard_state.RightControl=0;
    keyboard_state.LeftAlt=0;keyboard_state.RightAlt=0;
    keyboard_state.CapsLock=0;keyboard_state.Extended=0;
    keyboard_state.PauseRemaining=0;
    keyboard_event_head=keyboard_event_tail=0;
}

void bob64_keyboard_irq_set_active(int active) {
    keyboard_event_head=keyboard_event_tail=0;
    keyboard_irq_active=(u8)(active!=0);
}

int bob64_keyboard_irq_capture(u8 scancode) {
    BOB64_EVENT event;
    u32 head=keyboard_event_head,tail=keyboard_event_tail;
    if(bob64_keyboard_decode_event(&keyboard_state,scancode,&event))return -1;
    if((u32)(head-tail)>=KEYBOARD_EVENT_QUEUE_SIZE)return -1;
    keyboard_event_queue[head&(KEYBOARD_EVENT_QUEUE_SIZE-1)]=event;
    __asm__ volatile("":::"memory");
    keyboard_event_head=head+1;
    return 0;
}

int bob64_keyboard_irq_pop_event(BOB64_EVENT *event) {
    u32 tail=keyboard_event_tail,head=keyboard_event_head;
    if(!event||tail==head)return -1;
    __asm__ volatile("":::"memory");
    *event=keyboard_event_queue[tail&(KEYBOARD_EVENT_QUEUE_SIZE-1)];
    __asm__ volatile("":::"memory");
    keyboard_event_tail=tail+1;
    return 0;
}

int bob64_keyboard_irq_service(void) {
    u8 status,scancode;
    __asm__ volatile("inb $0x64,%0":"=a"(status));
    if(status==0xff||!(status&1)||(status&0x20))return -1;
    __asm__ volatile("inb $0x60,%0":"=a"(scancode));
    return bob64_keyboard_irq_capture(scancode);
}

int bob64_keyboard_decode_event(BOB64_KEYBOARD_STATE *state,u8 scancode,
                                BOB64_EVENT *event) {
    u8 released,code,letter,extended;
    int character=0;
    if(!state||!event)return -1;
    if(state->PauseRemaining) { state->PauseRemaining--;return -1; }
    if(scancode==0xe1) { state->PauseRemaining=5;state->Extended=0;return -1; }
    if(scancode==0xe0) { state->Extended=1;return -1; }
    extended=state->Extended;state->Extended=0;
    released=(u8)(scancode&0x80);code=(u8)(scancode&0x7f);
    if(!extended&&(code==0x2a||code==0x36)) {
        u8 *shift=code==0x2a?&state->LeftShift:&state->RightShift;
        *shift=(u8)!released;
    } else if(code==0x1d) {
        u8 *control=extended?&state->RightControl:&state->LeftControl;
        *control=(u8)!released;
    } else if(code==0x38) {
        u8 *alt=extended?&state->RightAlt:&state->LeftAlt;
        *alt=(u8)!released;
    } else if(code==0x3a&&!released)state->CapsLock^=1;
    else if(!released&&!extended) {
        letter=(u8)(code>=0x10&&code<=0x32&&unshifted[code]>='a'&&unshifted[code]<='z');
        u8 shift=(u8)(state->LeftShift||state->RightShift);
        character=letter?((shift^state->CapsLock)?shifted[code]:unshifted[code]):
                  (shift&&shifted[code]?shifted[code]:unshifted[code]);
        if(code==0x01)character=0x1b;
    }
    event->Type=released?BOB64_EVENT_KEY_UP:BOB64_EVENT_KEY_DOWN;
    event->Key=(extended?BOB64_EVENT_KEY_EXTENDED:0u)|code;
    event->Character=(u32)character;
    event->Modifiers=(state->LeftShift||state->RightShift?BOB64_EVENT_MOD_SHIFT:0u)|
                     (state->CapsLock?BOB64_EVENT_MOD_CAPS_LOCK:0u)|
                     (state->LeftControl||state->RightControl?BOB64_EVENT_MOD_CONTROL:0u)|
                     (state->LeftAlt||state->RightAlt?BOB64_EVENT_MOD_ALT:0u);
    event->X=0;event->Y=0;event->DeltaX=0;event->DeltaY=0;
    event->Buttons=0;event->Wheel=0;event->WindowHandle=0;
    event->ScreenX=0;event->ScreenY=0;
    return 0;
}

int bob64_keyboard_decode(BOB64_KEYBOARD_STATE *state,u8 scancode) {
    BOB64_EVENT event;
    if(bob64_keyboard_decode_event(state,scancode,&event)||
       event.Type!=BOB64_EVENT_KEY_DOWN||!event.Character)return -1;
    return (int)event.Character;
}

int bob64_keyboard_poll(void) {
    u8 status,scancode;
    if(keyboard_irq_active) {
        BOB64_EVENT event;
        while(!bob64_keyboard_irq_pop_event(&event))
            if(event.Type==BOB64_EVENT_KEY_DOWN&&event.Character)
                return (int)event.Character;
        return -1;
    }
    __asm__ volatile("inb $0x64,%0":"=a"(status));
    if(status==0xff||!(status&1))return -1;
    if(status&0x20) { __asm__ volatile("inb $0x60,%0":"=a"(scancode));return -1; }
    __asm__ volatile("inb $0x60,%0":"=a"(scancode));
    return bob64_keyboard_decode(&keyboard_state,scancode);
}

int bob64_keyboard_poll_event(BOB64_EVENT *event) {
    u8 status,scancode;
    if(!event)return -1;
    if(keyboard_irq_active)return bob64_keyboard_irq_pop_event(event);
    __asm__ volatile("inb $0x64,%0":"=a"(status));
    if(status==0xff||!(status&1)||(status&0x20))return -1;
    __asm__ volatile("inb $0x60,%0":"=a"(scancode));
    return bob64_keyboard_decode_event(&keyboard_state,scancode,event);
}
