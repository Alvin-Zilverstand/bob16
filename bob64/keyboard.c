#include "keyboard.h"

static BOB64_KEYBOARD_STATE keyboard_state;
#define KEYBOARD_EVENT_QUEUE_SIZE 64u
static BOB64_EVENT keyboard_event_queue[KEYBOARD_EVENT_QUEUE_SIZE];
static volatile u32 keyboard_event_head,keyboard_event_tail;
static volatile u8 keyboard_irq_active;
static BOB64_KEYBOARD_HID_STATE keyboard_hid_state;

static int keyboard_queue_event(const BOB64_EVENT *event) {
#if defined(BOB64_UEFI_ABI)
    u64 flags;
#endif
    u32 head,tail;
    if(!event)return -1;
#if defined(BOB64_UEFI_ABI)
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
#endif
    head=keyboard_event_head;tail=keyboard_event_tail;
    if((u32)(head-tail)>=KEYBOARD_EVENT_QUEUE_SIZE) {
#if defined(BOB64_UEFI_ABI)
        if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");
#endif
        return -1;
    }
    keyboard_event_queue[head&(KEYBOARD_EVENT_QUEUE_SIZE-1)]=*event;
    __asm__ volatile("":::"memory");
    keyboard_event_head=head+1;
#if defined(BOB64_UEFI_ABI)
    if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");
#endif
    return 0;
}

static int keyboard_hid_contains(const u8 keys[6],u8 key) {
    for(u32 i=0;i<6;i++)if(keys[i]==key)return 1;
    return 0;
}

static int keyboard_hid_scancode(u8 usage,u8 *scancode,u8 *extended) {
    static const u8 letter_scan[26]={
        0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,
        0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,
        0x15,0x2c
    };
    if(!scancode||!extended)return -1;
    *extended=0;
    if(usage>=4&&usage<=29)*scancode=letter_scan[usage-4];
    else if(usage>=30&&usage<=38)*scancode=(u8)(0x02+usage-30);
    else switch(usage) {
        case 39:*scancode=0x0b;break;
        case 40:*scancode=0x1c;break;
        case 41:*scancode=0x01;break;
        case 42:*scancode=0x0e;break;
        case 43:*scancode=0x0f;break;
        case 44:*scancode=0x39;break;
        case 45:*scancode=0x0c;break;
        case 46:*scancode=0x0d;break;
        case 47:*scancode=0x1a;break;
        case 48:*scancode=0x1b;break;
        case 49:*scancode=0x2b;break;
        case 51:*scancode=0x27;break;
        case 52:*scancode=0x28;break;
        case 53:*scancode=0x29;break;
        case 54:*scancode=0x33;break;
        case 55:*scancode=0x34;break;
        case 56:*scancode=0x35;break;
        case 57:*scancode=0x3a;break;
        case 58:case 59:case 60:case 61:case 62:case 63:
        case 64:case 65:case 66:case 67:
            *scancode=(u8)(0x3b+usage-58);break;
        case 68:*scancode=0x57;break;
        case 69:*scancode=0x58;break;
        case 79:*scancode=0x4d;*extended=1;break;
        case 80:*scancode=0x4b;*extended=1;break;
        case 81:*scancode=0x50;*extended=1;break;
        case 82:*scancode=0x48;*extended=1;break;
        case 73:*scancode=0x52;*extended=1;break;
        case 74:*scancode=0x47;*extended=1;break;
        case 75:*scancode=0x49;*extended=1;break;
        case 76:*scancode=0x53;*extended=1;break;
        case 77:*scancode=0x4f;*extended=1;break;
        case 78:*scancode=0x51;*extended=1;break;
        default:return -1;
    }
    return 0;
}

static int keyboard_hid_emit_scancode(BOB64_KEYBOARD_HID_STATE *state,
                                      u8 scancode,u8 extended,int released) {
    BOB64_EVENT event;
    if(!state)return -1;
    if(extended)(void)bob64_keyboard_decode_event(&state->KeyState,0xe0,&event);
    if(bob64_keyboard_decode_event(&state->KeyState,
       (u8)(scancode|(released?0x80:0)),&event))return -1;
    return keyboard_queue_event(&event);
}

static int keyboard_hid_emit(BOB64_KEYBOARD_HID_STATE *state,u8 usage,
                             int released) {
    u8 scancode,extended;
    if(keyboard_hid_scancode(usage,&scancode,&extended))return 0;
    return keyboard_hid_emit_scancode(state,scancode,extended,released);
}

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
    keyboard_hid_state=(BOB64_KEYBOARD_HID_STATE){0};
    keyboard_event_head=keyboard_event_tail=0;
}

void bob64_keyboard_irq_set_active(int active) {
    keyboard_event_head=keyboard_event_tail=0;
    keyboard_irq_active=(u8)(active!=0);
}

int bob64_keyboard_irq_capture(u8 scancode) {
    BOB64_EVENT event;
    if(bob64_keyboard_decode_event(&keyboard_state,scancode,&event))return -1;
    return keyboard_queue_event(&event);
}

int bob64_keyboard_hid_report_device(BOB64_KEYBOARD_HID_STATE *state,
                                     const u8 report[8]) {
    static const u8 modifier_scancodes[8]={0x1d,0x2a,0x38,0,0x1d,0x36,0x38,0};
    static const u8 modifier_extended[8]={0,0,0,0,1,0,1,0};
    u8 modifiers;
    int queued=0;
    if(!state||!report)return -1;
    modifiers=report[0];
    for(u32 i=0;i<6;i++)if(report[i+2]>=1&&report[i+2]<=3)return -1;
    for(u32 bit=0;bit<8;bit++) {
        u8 mask=(u8)(1u<<bit);
        if(!modifier_scancodes[bit]||
           ((modifiers^state->Modifiers)&mask)==0)continue;
        if(keyboard_hid_emit_scancode(state,modifier_scancodes[bit],
              modifier_extended[bit],!(modifiers&mask)))queued=-1;
    }
    for(u32 i=0;i<6;i++) {
        u8 old_key=state->Keys[i];
        if(old_key&&!keyboard_hid_contains(report+2,old_key)&&
           keyboard_hid_emit(state,old_key,1))queued=-1;
    }
    for(u32 i=0;i<6;i++) {
        u8 new_key=report[i+2];
        if(new_key&&!keyboard_hid_contains(state->Keys,new_key)&&
           keyboard_hid_emit(state,new_key,0))queued=-1;
    }
    state->Modifiers=modifiers;
    for(u32 i=0;i<6;i++)state->Keys[i]=report[i+2];
    return queued;
}

int bob64_keyboard_hid_report(const u8 report[8]) {
    return bob64_keyboard_hid_report_device(&keyboard_hid_state,report);
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

int bob64_keyboard_event_input(const BOB64_EVENT *event) {
    if(!event||event->Type!=BOB64_EVENT_KEY_DOWN)return -1;
    if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x48))
        return BOB64_KEYBOARD_INPUT_UP;
    if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x50))
        return BOB64_KEYBOARD_INPUT_DOWN;
    return event->Character?(int)event->Character:-1;
}

int bob64_keyboard_poll(void) {
    BOB64_EVENT event;
    while(!bob64_keyboard_irq_pop_event(&event)) {
        int input=bob64_keyboard_event_input(&event);
        if(input>=0)return input;
    }
    if(keyboard_irq_active)return -1;
    if(bob64_keyboard_poll_event(&event))return -1;
    return bob64_keyboard_event_input(&event);
}

int bob64_keyboard_poll_event(BOB64_EVENT *event) {
    u8 status,scancode;
    if(!event)return -1;
    if(!bob64_keyboard_irq_pop_event(event))return 0;
    if(keyboard_irq_active)return -1;
    __asm__ volatile("inb $0x64,%0":"=a"(status));
    if(status==0xff||!(status&1)||(status&0x20))return -1;
    __asm__ volatile("inb $0x60,%0":"=a"(scancode));
    return bob64_keyboard_decode_event(&keyboard_state,scancode,event);
}
