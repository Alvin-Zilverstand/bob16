/*
	BOB-16
	written by misterbob
	you are free to use and modify this but PLEASE credit me (at least retain the name in some way)

	** read all of the docs before using as they contain some important tidbits **

	**** INSTRUCTION SEMANTICS ****
	(instructions are in order of opcode, nop is 0, add is 1, etc.)
	(instructions with a * change condition codes)
		
		NOP: does nothing other than increment pc

		*ADD (normal normal):		r[dst]  = r[src1] + r[src2]
		ex. add r0 r1 r2

		*ADD (normal immediate): 	r[dst]  = r[src1] + imm[4]
		ex. add r0 r1 4

		*ADD (in place normal): 	r[dst] += r[src1]
		ex. add r0 r1

		*ADD (in place immediate): 	r[dst] += imm[7]
		ex. add r0 4

		*AND (normal normal): 		r[dst]  = r[src1] & r[src2]
		ex. and r0 r1 r2

		*AND (normal immediate): 	r[dst]  = r[src1] & imm[4]
		ex. and r0 r1 4

		*AND (in place normal): 	r[dst] &= r[src1]
		ex. and r0 r1

		*AND (in place immediate): 	r[dst] &= imm[7]
		ex. and r0 4

		*NOT (normal): 		 	r[dst] = !r[src]
		ex. not r0 r1

		*NOT (immediate): 	!r[dst]
		ex. not r1

		*LD:  r[dst] <= memory[pc + imm[9]]
		ex. ld r0 15

		*LDI: r[dst] <= memory[memory[pc + imm[9]]
		ex. ldi r0 15

		*LDR: r[dst] <= memory[r[src] + imm[6]]
		ex. ldr r0 r1 15

		ST:  r[src] => memory[pc + imm[9]]
		ex. st r0 15

		STI: r[src] => memory[memory[pc + imm[9]]
		ex. sti r0 15

		STR: r[src] => memory[r[dst] + imm[6]]
		ex. str r0 r1 15

		BR: if condition codes match any of the nzp flags, pc += imm[9]
		ex. br np 50
		(jumps to incremented pc + 50 if condition codes n or p are 1)

		JMP: pc = r[src]
		ex. jmp r0

		JSR:  r[7] = pc, pc += imm[11]
		ex. jsr 50

		JSRR: r[7] = pc, pc  = r[src]
		ex. jsrr r0

		*LEA: r[src] = pc
		ex. lea r0

		RET: pc = r[7]
		ex. ret

		TRAP (vector 0): halts program
		TRAP (vector 1): prints ascii character in r[0]
		TRAP (vector 2): r[7] = pc, pc = r[0], prints null terminated ascii string
		TRAP (vector 3): r[7] = pc, pc = r[0], gets user inputted string with max length stored in r[1] and stores it sequentially at address in r[0]

		ex. trap 0

	**** REGISTERS ****
	r0...r7: general purpose registers (r0 is usually used as an argument for trap calls, r7 is used for return addresses)
	accumulator: register inside alu that stores results of arithmetic and logical operations
	ir (instruction register): stores the current instruction the cpu is executing
	pc (program counter): stores the memory address of the NEXT instruction to execute (gets incremented during fetch phase of execution, so keep in mind anything using pc-relative addressing will be using the incremented pc)
	cc: 3 bit flags which consist of n (negative), z (zero), and p (positive), are changed whenever an instruction modifies a general purpose register

	**** NOTES ****
	any immediate values in assembly will clamp to be within bounds, e.g. for ADD normal immediate, if you put 60 as the immediate value it will clamp to 15
	any unused bits in a machine code instruction will be set to 0
	strings are NOT packed!!!
	pc is *always* incremented at the start of execution, so instructions that change or use pc such as jmp will use the incremented pc as the base
	sext stands for sign extend and not anything else
	you MUST include trap 0 to halt the program somewhere or else it'll crash
	condition codes are set after instructions with *, so if the result of an instruction with an * is:
		negative 	-> n bit is set
		zero 		-> z bit is set
		positive	-> p bit is set
*/

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <errno.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* Kernel disk exposed to the guest boot ROM through trap 4. */
static uint32_t bootDisk[0x10000];
static bool cpu32;
static bool application32;
static uint32_t applicationArgc;
static uint32_t applicationArgv;
static bool applicationEntryArguments;
static bool filesystemRegistered;
static uint32_t filesystemNames;
static uint32_t filesystemLengths;
static uint32_t filesystemKinds;
static uint32_t filesystemUsed;
enum { GRAPHICS_WIDTH=80, GRAPHICS_HEIGHT=25, GRAPHICS_CELLS=GRAPHICS_WIDTH*GRAPHICS_HEIGHT };
enum { SYSTEM_API_VERSION=1, SYSTEM_DEVICE_CELL_FRAMEBUFFER=1, SYSTEM_INPUT_KEYBOARD=1, SYSTEM_INPUT_MOUSE=2 };
enum { WIDE_FILE_BASE=0x1e000, WIDE_FILE_WORDS=8192 };
static unsigned char graphicsCharacters[GRAPHICS_CELLS];
static unsigned char graphicsColors[GRAPHICS_CELLS];
static bool graphicsActive;
static int terminalAvailable(void);

static void graphicsReset(void) {
    for(unsigned i=0;i<GRAPHICS_CELLS;i++){graphicsCharacters[i]=' ';graphicsColors[i]=7;}
}
static void graphicsPixel(int x,int y,unsigned char character,unsigned char color) {
    if(x<0 || y<0 || x>=GRAPHICS_WIDTH || y>=GRAPHICS_HEIGHT)return;
    unsigned index=(unsigned)y*GRAPHICS_WIDTH+(unsigned)x;
    graphicsCharacters[index]=character?character:' ';graphicsColors[index]=color&15;
}
static void graphicsPresent(void) {
    int ansi=terminalAvailable(),lastColor=-1;
    if(ansi)printf("\033[2J\033[H");
    for(int y=0;y<GRAPHICS_HEIGHT;y++) {
        for(int x=0;x<GRAPHICS_WIDTH;x++) {
            unsigned index=(unsigned)y*GRAPHICS_WIDTH+(unsigned)x;
            int color=graphicsColors[index];
            if(ansi && color!=lastColor) {
                int code=(color&8)?90+(color&7):30+(color&7);
                printf("\033[%dm",code);lastColor=color;
            }
            putchar(graphicsCharacters[index]);
        }
        if(ansi)printf("\033[0m");
        putchar('\n');lastColor=-1;
    }
    fflush(stdout);
}
static void graphicsClose(void) {
    graphicsActive=false;
    if(terminalAvailable())printf("\033[0m\033[2J\033[H");
    fflush(stdout);
}
static uint32_t bootWords;
static uint32_t bootOrigin;
static bool bootWideHeader;
static bool bootMode;
static bool programMode;
static bool programDone;
static bool legacyProgram32;
static bool assembledWide;
static uint32_t assembledWords;
static uint32_t assembledOrigin;
static uint32_t currentInstructionAddress;
static uint16_t currentInstruction;
static jmp_buf programFaultStack[8];
static unsigned programDepth;
static void machineFault(void) {
    if (programMode) {
        if (getenv("BOB16_TRACE_FAULT"))
            fprintf(stderr,"Program fault at 0x%08X (instruction 0x%04X)\n",currentInstructionAddress,currentInstruction);
        longjmp(programFaultStack[programDepth-1], 1);
    }
    exit(1);
}
static unsigned readWord(FILE *file) {
    int lo = fgetc(file), hi = fgetc(file);
    if (lo == EOF || hi == EOF) {
        fprintf(stderr, "%s bob16/bob32 kernel image\n",ferror(file)?"Cannot read":"Truncated");
        exit(1);
    }
    return (unsigned)lo | ((unsigned)hi << 8);
}
static uint32_t readDword(FILE *file) {
    uint32_t lo=readWord(file),hi=readWord(file);
    return lo|(hi<<16);
}

static void bootImage(const char *path);
#ifdef _WIN32
static DWORD originalInputMode;
static bool rawInputStarted;
static void restoreInputMode(void) {
    if(rawInputStarted)SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),originalInputMode);
}
#endif
static int terminalAvailable(void) {
#ifdef _WIN32
    DWORD inputMode,outputMode;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),&inputMode) &&
           GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),&outputMode);
#else
    return 0;
#endif
}
static int terminalKey(void) {
    fflush(stdout);
#ifdef _WIN32
    if(terminalAvailable()) {
        HANDLE input=GetStdHandle(STD_INPUT_HANDLE);DWORD mode,count;INPUT_RECORD record;
        if(!GetConsoleMode(input,&mode))return -1;
        if(!SetConsoleMode(input,mode&~ENABLE_PROCESSED_INPUT))return -1;
        if(!rawInputStarted){originalInputMode=mode;rawInputStarted=true;atexit(restoreInputMode);}
        int key=0;
        while(ReadConsoleInputA(input,&record,1,&count) && count) {
            if(record.EventType!=KEY_EVENT || !record.Event.KeyEvent.bKeyDown)continue;
            key=(unsigned char)record.Event.KeyEvent.uChar.AsciiChar;
            if(!key) switch(record.Event.KeyEvent.wVirtualKeyCode) {
                case VK_UP:key=256;break;case VK_DOWN:key=257;break;
                case VK_LEFT:key=258;break;case VK_RIGHT:key=259;break;
                case VK_HOME:key=260;break;case VK_END:key=261;break;
                case VK_DELETE:key=262;break;default:break;
            }
            if(key)break;
        }
        if(key==13)return '\n';
        if(key==4)return -1;
        return key?key:-1;
    }
#endif
    return getchar();
}
static unsigned terminalMouseButtons;
enum { OS_EVENT_KEY_DOWN=1,OS_EVENT_CHAR=2,OS_EVENT_KEY_UP=3,OS_EVENT_MOUSE_MOVE=4,OS_EVENT_MOUSE_BUTTON_DOWN=5,OS_EVENT_MOUSE_BUTTON_UP=6,OS_EVENT_MOUSE_WHEEL=7 };
static int terminalEventDecode(int kind,int isDown,int character,int virtualKey,int x,int y,
                               unsigned buttons,unsigned *buttonState,
                               int *type,int *value,int *outX,int *outY) {
    if(kind==1) {
        int code=character;
        if(!code)switch(virtualKey) {
            case 38:code=256;break;case 40:code=257;break;
            case 37:code=258;break;case 39:code=259;break;
            case 36:code=260;break;case 35:code=261;break;
            case 46:code=262;break;default:code=512+virtualKey;break;
        }
        *type=isDown?(code<256?OS_EVENT_CHAR:OS_EVENT_KEY_DOWN):OS_EVENT_KEY_UP;
        *value=code;*outX=0;*outY=0;return 1;
    }
    if(kind==2) {
        *outX=x;*outY=y;*value=(int)buttons;
        if(isDown==1)*type=OS_EVENT_MOUSE_MOVE;
        else if(character==4) {
            int delta=(int)(int16_t)(buttons>>16);
            if(!delta)return 0;
            *type=OS_EVENT_MOUSE_WHEEL;*value=delta;
        }
        else if(buttons!=*buttonState) {
            *type=(buttons&~*buttonState)?OS_EVENT_MOUSE_BUTTON_DOWN:OS_EVENT_MOUSE_BUTTON_UP;
            *buttonState=buttons;
        } else return 0;
        return 1;
    }
    return 0;
}
static int terminalEvent(int *type,int *value,int *x,int *y) {
    fflush(stdout);
#ifdef _WIN32
    if(terminalAvailable()) {
        HANDLE input=GetStdHandle(STD_INPUT_HANDLE);DWORD mode,count;INPUT_RECORD record;
        if(!GetConsoleMode(input,&mode))return 0;
        DWORD originalMode=mode;
        mode=(mode&~ENABLE_PROCESSED_INPUT)|ENABLE_MOUSE_INPUT|ENABLE_EXTENDED_FLAGS;
        if(!SetConsoleMode(input,mode))return 0;
        if(!rawInputStarted){originalInputMode=originalMode;rawInputStarted=true;atexit(restoreInputMode);}
        while(ReadConsoleInputA(input,&record,1,&count) && count) {
            if(record.EventType==KEY_EVENT) {
                KEY_EVENT_RECORD key=record.Event.KeyEvent;
                if(terminalEventDecode(1,key.bKeyDown,(unsigned char)key.uChar.AsciiChar,
                    key.wVirtualKeyCode,0,0,0,&terminalMouseButtons,type,value,x,y))return 1;
            }
            if(record.EventType==MOUSE_EVENT) {
                MOUSE_EVENT_RECORD mouse=record.Event.MouseEvent;
                unsigned buttons=mouse.dwButtonState;
                int moving=mouse.dwEventFlags==MOUSE_MOVED;
                if(terminalEventDecode(2,moving,(int)mouse.dwEventFlags,0,
                    mouse.dwMousePosition.X,mouse.dwMousePosition.Y,buttons,
                    &terminalMouseButtons,type,value,x,y))return 1;
            }
        }
        return 0;
    }
#endif
    int key=getchar();if(key==EOF)return 0;if(key=='\r')key='\n';
    return terminalEventDecode(1,1,key,0,0,0,0,&terminalMouseButtons,type,value,x,y);
}
static int terminalEventPoll(int *type,int *value,int *x,int *y) {
#ifdef _WIN32
    if(terminalAvailable()) {
        HANDLE input=GetStdHandle(STD_INPUT_HANDLE);DWORD mode,count;INPUT_RECORD record;
        if(!GetConsoleMode(input,&mode))return 0;
        DWORD originalMode=mode;
        mode=(mode&~ENABLE_PROCESSED_INPUT)|ENABLE_MOUSE_INPUT|ENABLE_EXTENDED_FLAGS;
        if(!SetConsoleMode(input,mode))return 0;
        if(!rawInputStarted){originalInputMode=originalMode;rawInputStarted=true;atexit(restoreInputMode);}
        for(;;) {
            if(!PeekConsoleInputA(input,&record,1,&count)||!count)return 0;
            if(!ReadConsoleInputA(input,&record,1,&count)||!count)return 0;
            if(record.EventType==KEY_EVENT) {
                KEY_EVENT_RECORD key=record.Event.KeyEvent;
                if(terminalEventDecode(1,key.bKeyDown,(unsigned char)key.uChar.AsciiChar,
                    key.wVirtualKeyCode,0,0,0,&terminalMouseButtons,type,value,x,y))return 1;
            } else if(record.EventType==MOUSE_EVENT) {
                MOUSE_EVENT_RECORD mouse=record.Event.MouseEvent;
                unsigned buttons=mouse.dwButtonState;
                int moving=mouse.dwEventFlags==MOUSE_MOVED;
                if(terminalEventDecode(2,moving,(int)mouse.dwEventFlags,0,
                    mouse.dwMousePosition.X,mouse.dwMousePosition.Y,buttons,
                    &terminalMouseButtons,type,value,x,y))return 1;
            }
        }
    }
#else
    (void)type;(void)value;(void)x;(void)y;
#endif
    return 0;
}

typedef uint32_t ureg_t;
typedef uint16_t instruction_t;
typedef int32_t reg_t;
static reg_t wordValue(uint32_t value) { return cpu32?(int32_t)value:(int16_t)(uint16_t)value; }
static reg_t wordAdd(reg_t a,reg_t b) { return wordValue((uint32_t)a+(uint32_t)b); }

typedef enum INSTRUCTION_E {
	NOP,
	ADD,
	AND,
	NOT,
	LD,
	LDI,
	LDR,
	ST,
	STI,
	STR,
	BR,
	JMP,
	JSR,
	LEA,
	RET,
	TRAP
} INSTRUCTION;

typedef enum TRAP_VECTOR_E {
	HALT,
	PUTC,
	PUTS,
	GETS
} TRAP_VECTOR;

typedef enum CONDITION_CODES_E {
	N,
	Z,
	P
} CONDITION_CODES;

typedef struct alu_t {
	reg_t accumulator;
} alu_t;

struct cpu_t {
	reg_t regFile[8];
	instruction_t ir;
	ureg_t pc;
	bool cc[3];
	alu_t alu;
} cpu;

struct ram_t {
	int32_t memory[0x10000];
	ureg_t mar;
	reg_t mdr;
} ram;
static int32_t memoryRead(ureg_t address);
static void memoryWrite(ureg_t address,int32_t value);
#include "storage.c"

enum { MEMORY_PAGE_SHIFT=12, MEMORY_PAGE_WORDS=1<<MEMORY_PAGE_SHIFT, MEMORY_PAGE_SLOTS=4096 };
typedef struct {uint32_t number;int32_t *words;int used;} SparseMemoryPage;
static SparseMemoryPage sparseMemory[MEMORY_PAGE_SLOTS];
#ifdef BOB_TESTING
static int failNextSparsePageAllocation;
#endif
static int32_t *allocateSparsePage(void) {
#ifdef BOB_TESTING
    if(failNextSparsePageAllocation){failNextSparsePageAllocation=0;return NULL;}
#endif
    return calloc(MEMORY_PAGE_WORDS,sizeof(int32_t));
}
static ureg_t addressValue(uint32_t value) {return cpu32?value:(uint16_t)value;}
static SparseMemoryPage *memoryPage(uint32_t number,int create) {
    uint32_t slot=(number*2654435761u)%MEMORY_PAGE_SLOTS;
    for(unsigned probe=0;probe<MEMORY_PAGE_SLOTS;probe++) {
        SparseMemoryPage *page=&sparseMemory[(slot+probe)%MEMORY_PAGE_SLOTS];
        if(!page->used) {
            if(!create)return NULL;
            page->words=allocateSparsePage();
            if(!page->words)return NULL;
            page->number=number;page->used=1;return page;
        }
        if(page->number==number)return page;
    }
    machineFault();return NULL;
}
static void memoryPageInstall(uint32_t number,int32_t *words) {
    uint32_t slot=(number*2654435761u)%MEMORY_PAGE_SLOTS;
    for(unsigned probe=0;probe<MEMORY_PAGE_SLOTS;probe++) {
        SparseMemoryPage *page=&sparseMemory[(slot+probe)%MEMORY_PAGE_SLOTS];
        if(page->used)continue;
        page->number=number;page->words=words;page->used=1;return;
    }
    machineFault();
}
static int32_t memoryRead(ureg_t address) {
    if(address<0x10000)return ram.memory[address];
    SparseMemoryPage *page=memoryPage(address>>MEMORY_PAGE_SHIFT,0);
    return page?page->words[address&(MEMORY_PAGE_WORDS-1)]:0;
}
static void memoryWrite(ureg_t address,int32_t value) {
    if(address<0x10000){ram.memory[address]=value;return;}
    SparseMemoryPage *page=memoryPage(address>>MEMORY_PAGE_SHIFT,1);
    if(!page)machineFault();
    page->words[address&(MEMORY_PAGE_WORDS-1)]=value;
}
static void memoryReset(void) {
    for(unsigned i=0;i<MEMORY_PAGE_SLOTS;i++)if(sparseMemory[i].used)free(sparseMemory[i].words);
    memset(sparseMemory,0,sizeof(sparseMemory));
}

typedef struct {uint32_t number;int32_t *words;} SavedSparsePage;
static SavedSparsePage *sparseMemorySave(unsigned *count) {
    unsigned used=0,out=0;
    for(unsigned i=0;i<MEMORY_PAGE_SLOTS;i++)if(sparseMemory[i].used&&sparseMemory[i].number>=0x20&&sparseMemory[i].number<0x100)used++;
    *count=used;
    SavedSparsePage *saved=used?calloc(used,sizeof(*saved)):NULL;
    if(used&&!saved)return NULL;
    for(unsigned i=0;i<MEMORY_PAGE_SLOTS;i++)if(sparseMemory[i].used&&sparseMemory[i].number>=0x20&&sparseMemory[i].number<0x100) {
        saved[out].number=sparseMemory[i].number;
        saved[out].words=malloc(MEMORY_PAGE_WORDS*sizeof(int32_t));
        if(!saved[out].words) {
            for(unsigned j=0;j<out;j++)free(saved[j].words);
            free(saved);return NULL;
        }
        memcpy(saved[out].words,sparseMemory[i].words,MEMORY_PAGE_WORDS*sizeof(int32_t));out++;
    }
    *count=used;return saved;
}
static void sparseMemoryRestore(SavedSparsePage *saved,unsigned count) {
    for(unsigned i=0;i<MEMORY_PAGE_SLOTS;i++)if(sparseMemory[i].used&&sparseMemory[i].number>=0x20&&sparseMemory[i].number<0x100) {
        free(sparseMemory[i].words);sparseMemory[i].used=0;sparseMemory[i].words=NULL;
    }
    for(unsigned i=0;i<count;i++)memoryPageInstall(saved[i].number,saved[i].words);
    free(saved);
}

static int runWidePackedFromGuest(uint32_t descriptor,int *started);

static int filesystemService(uint32_t request) {
    if(!programMode && (request<0x100 || request>0xf000-4))return -1;
    if(programMode && (request<0x20000 || request>0xffff9))return -1;
    uint32_t op=(uint32_t)memoryRead(request);
    if(!programMode) {
        if(op!=0 || !cpu32)return -1;
        filesystemNames=(uint32_t)memoryRead(request+1);
        filesystemLengths=(uint32_t)memoryRead(request+2);
        filesystemKinds=(uint32_t)memoryRead(request+3);
        filesystemUsed=(uint32_t)memoryRead(request+4);
        uint64_t imageEnd=(uint64_t)bootOrigin+bootWords;
        if(imageEnd>WIDE_FILE_BASE)return -1;
        if(filesystemNames<bootOrigin || (uint64_t)filesystemNames+192>imageEnd ||
           filesystemLengths<bootOrigin || (uint64_t)filesystemLengths+8>imageEnd ||
           filesystemKinds<bootOrigin || (uint64_t)filesystemKinds+8>imageEnd ||
           filesystemUsed<bootOrigin || (uint64_t)filesystemUsed+8>imageEnd)return -1;
        filesystemRegistered=true;return 0;
    }
    if(!application32 || !filesystemRegistered)return -1;
    if((op>>16)!=1)return -4;
    op&=0xffffu;
    uint32_t a=(uint32_t)memoryRead(request+1),b=(uint32_t)memoryRead(request+2),c=(uint32_t)memoryRead(request+3),d=(uint32_t)memoryRead(request+4);
    uint32_t e=(uint32_t)memoryRead(request+5),f=(uint32_t)memoryRead(request+6);
#define APP_RANGE(p,n) ((p)>=0x20000u && (uint64_t)(p)+(uint64_t)(n)<=0x100000u)
    char name[24];unsigned nameLength=0;
    if((op>=2 && op<=4) || op==8 || op==9 || op==10) {
        if(!APP_RANGE(a,1))return -1;
        while(nameLength<sizeof(name)-1 && APP_RANGE(a+nameLength,1)) {
            uint32_t ch=(uint32_t)memoryRead(a+nameLength);
            if(!ch)break;
            if(ch<33 || ch>126)return -1;
            name[nameLength++]=(char)ch;
        }
        if(!nameLength || nameLength==sizeof(name)-1 || nameLength>=24 || (uint32_t)memoryRead(a+nameLength)!=0)return -1;
        name[nameLength]=0;
    }
    int slot=-1;
    if((op>=2 && op<=4) || op==8 || op==9 || op==10)for(int i=0;i<8;i++) {
        if(!memoryRead(filesystemUsed+i))continue;
        unsigned j=0;while(j<23 && (uint32_t)memoryRead(filesystemNames+i*24+j)==(unsigned char)name[j] && name[j])j++;
        if(j==nameLength && !name[j]){slot=i;break;}
    }
    if(op==1) {
        uint32_t index=a,buffer=b,capacity=c,lengthAddress=d;
        if(index>=8)return 0;
        if(!APP_RANGE(buffer,capacity) || !capacity || !APP_RANGE(lengthAddress,1))return -1;
        if(!memoryRead(filesystemUsed+index))return 0;
        unsigned n=0;while(n<23 && memoryRead(filesystemNames+index*24+n))n++;
        if(capacity<=n)return -2;
        for(unsigned i=0;i<n;i++)memoryWrite(buffer+i,memoryRead(filesystemNames+index*24+i));
        memoryWrite(buffer+n,0);memoryWrite(lengthAddress,memoryRead(filesystemLengths+index));
        return (int)memoryRead(filesystemKinds+index)+1;
    }
    if(op==2) {
        uint32_t buffer=b,capacity=c;
        if(slot<0 || memoryRead(filesystemKinds+slot)!=0)return -1;
        uint32_t length=(uint32_t)memoryRead(filesystemLengths+slot);
        if(!APP_RANGE(buffer,capacity) || capacity<=length)return -2;
        uint32_t offset=0;for(int i=0;i<slot;i++)if(memoryRead(filesystemUsed+i))offset+=(uint32_t)memoryRead(filesystemLengths+i)+1;
        for(uint32_t i=0;i<length;i++)memoryWrite(buffer+i,memoryRead(WIDE_FILE_BASE+offset+i));
        memoryWrite(buffer+length,0);return (int)length;
    }
    if(op==9) {
        uint32_t buffer=b,capacity=c;
        if(slot<0 || memoryRead(filesystemKinds+slot)==0)return -1;
        uint32_t length=(uint32_t)memoryRead(filesystemLengths+slot);
        if(!APP_RANGE(buffer,capacity) || capacity<length)return -2;
        uint32_t offset=0;for(int i=0;i<slot;i++)if(memoryRead(filesystemUsed+i))offset+=(uint32_t)memoryRead(filesystemLengths+i)+1;
        for(uint32_t i=0;i<length;i++)memoryWrite(buffer+i,memoryRead(WIDE_FILE_BASE+offset+i));
        return (int)length;
    }
    if(op==10)return slot<0?-1:(int)memoryRead(filesystemKinds+slot);
    if(op==3 || op==8) {
        uint32_t buffer=b,length=c;
        uint32_t kind=op==3?0:d;
        if((op==3 && d!=0) || (op==8 && (kind<1 || kind>2)) ||
           (!kind && length>=512) || !APP_RANGE(buffer,length))return -1;
        if(slot<0)for(int i=0;i<8;i++)if(!memoryRead(filesystemUsed+i)){slot=i;break;}
        if(slot<0)return -1;
        uint32_t total=length+1;
        for(int i=0;i<8;i++)if(i!=slot && memoryRead(filesystemUsed+i))total+=(uint32_t)memoryRead(filesystemLengths+i)+1;
        if(total>WIDE_FILE_WORDS)return -1;
        if(!kind)for(uint32_t i=0;i<length;i++){uint32_t ch=(uint32_t)memoryRead(buffer+i);if(!ch || ch>255)return -1;}
        int32_t packed[WIDE_FILE_WORDS];uint32_t out=0;int newLengths[8],newKinds[8],newUsed[8];char newNames[8][24];
        for(int i=0;i<8;i++) {
            newUsed[i]=i==slot?1:(int)memoryRead(filesystemUsed+i);
            if(!newUsed[i]){newLengths[i]=0;newKinds[i]=0;memset(newNames[i],0,24);continue;}
            uint32_t oldName=filesystemNames+i*24;for(int j=0;j<24;j++)newNames[i][j]=(char)memoryRead(oldName+j);
            if(i==slot){for(int j=0;j<24;j++)newNames[i][j]=j<(int)nameLength?name[j]:0;newLengths[i]=(int)length;newKinds[i]=(int)kind;
                for(uint32_t j=0;j<length;j++)packed[out++]=(int32_t)memoryRead(buffer+j);
                packed[out++]=0;
            } else {
                newLengths[i]=(int)memoryRead(filesystemLengths+i);newKinds[i]=(int)memoryRead(filesystemKinds+i);
                uint32_t offset=0;for(int j=0;j<i;j++)if(memoryRead(filesystemUsed+j))offset+=(uint32_t)memoryRead(filesystemLengths+j)+1;
                for(int j=0;j<=newLengths[i];j++)packed[out++]=(int32_t)memoryRead(WIDE_FILE_BASE+offset+(uint32_t)j);
            }
        }
        for(uint32_t i=0;i<out;i++)memoryWrite(WIDE_FILE_BASE+i,packed[i]);
        for(int i=0;i<8;i++) {for(int j=0;j<24;j++)memoryWrite(filesystemNames+i*24+j,(unsigned char)newNames[i][j]);memoryWrite(filesystemLengths+i,newLengths[i]);memoryWrite(filesystemKinds+i,newKinds[i]);memoryWrite(filesystemUsed+i,newUsed[i]);}
        return 0;
    }
    if(op==4) {
        if(slot<0)return -1;
        int32_t packed[WIDE_FILE_WORDS];uint32_t out=0;
        for(int i=0;i<8;i++)if(memoryRead(filesystemUsed+i) && i!=slot) {
            uint32_t offset=0;for(int j=0;j<i;j++)if(memoryRead(filesystemUsed+j))offset+=(uint32_t)memoryRead(filesystemLengths+j)+1;
            uint32_t length=(uint32_t)memoryRead(filesystemLengths+i);
            for(uint32_t j=0;j<=length;j++)packed[out++]=(int32_t)memoryRead(WIDE_FILE_BASE+offset+j);
        }
        for(uint32_t i=0;i<out;i++)memoryWrite(WIDE_FILE_BASE+i,packed[i]);
        uint32_t removedName=filesystemNames+(uint32_t)slot*24;
        for(int j=0;j<24;j++)memoryWrite(removedName+j,0);
        memoryWrite(filesystemLengths+slot,0);memoryWrite(filesystemKinds+slot,0);memoryWrite(filesystemUsed+slot,0);
        return 0;
    }
    if(op==5) {
        uint32_t values=a,capacity=b;
        if(!APP_RANGE(values,capacity) || capacity<6)return -1;
        uint32_t appPages=0,files=0,fileWords=0;
        for(uint32_t page=0x20;page<0x100;page++)if(memoryPage(page,0))appPages++;
        for(int i=0;i<8;i++)if(memoryRead(filesystemUsed+i)) {files++;fileWords+=(uint32_t)memoryRead(filesystemLengths+i)+1;}
        memoryWrite(values,1);memoryWrite(values+1,0xE0000);memoryWrite(values+2,appPages);
        memoryWrite(values+3,files);memoryWrite(values+4,fileWords);memoryWrite(values+5,WIDE_FILE_WORDS-fileWords);
        return 6;
    }
    if(op==11) {
        uint32_t values=a,capacity=b,inputFlags=SYSTEM_INPUT_KEYBOARD;
        if(!APP_RANGE(values,capacity)||capacity<5)return -1;
#ifdef _WIN32
        if(terminalAvailable())inputFlags|=SYSTEM_INPUT_MOUSE;
#endif
        memoryWrite(values,SYSTEM_API_VERSION);
        memoryWrite(values+1,SYSTEM_DEVICE_CELL_FRAMEBUFFER);
        memoryWrite(values+2,GRAPHICS_WIDTH);memoryWrite(values+3,GRAPHICS_HEIGHT);
        memoryWrite(values+4,inputFlags);return 5;
    }
    if(op==6) {
        uint32_t nameAddress=a,argumentAddress=b,offset=0;char name[24],arguments[128];
        unsigned nameLength=0,argumentLength=0;int target=-1,kind,started=0,childStatus;
        if(!APP_RANGE(c,1))return -1;
        while(nameLength<sizeof(name)-1&&APP_RANGE(nameAddress+nameLength,1)) {
            uint32_t ch=(uint32_t)memoryRead(nameAddress+nameLength);
            if(!ch)break;
            if(ch<33||ch>126)return -1;
            name[nameLength++]=(char)ch;
        }
        if(!nameLength||nameLength>=sizeof(name)-1||!APP_RANGE(nameAddress+nameLength,1)||memoryRead(nameAddress+nameLength))return -1;
        name[nameLength]=0;
        if(argumentAddress) {
            while(argumentLength<sizeof(arguments)-1&&APP_RANGE(argumentAddress+argumentLength,1)) {
                uint32_t ch=(uint32_t)memoryRead(argumentAddress+argumentLength);
                if(ch>255)return -1;
                arguments[argumentLength++]=(char)ch;
                if(!ch)break;
            }
            if(!argumentLength||arguments[argumentLength-1])return -1;
        } else arguments[argumentLength++]=0;
        for(int i=0;i<8;i++)if(memoryRead(filesystemUsed+i)) {
            unsigned j=0;while(j<23&&(uint32_t)memoryRead(filesystemNames+i*24+j)==(unsigned char)name[j]&&name[j])j++;
            if(j==nameLength&&!name[j]){target=i;break;}
        }
        if(target<0)return -1;
        kind=(int)memoryRead(filesystemKinds+target);
        if(kind!=3)return -2;
        for(int i=0;i<target;i++)if(memoryRead(filesystemUsed+i))offset+=(uint32_t)memoryRead(filesystemLengths+i)+1;
        memoryWrite(0xe000,WIDE_FILE_BASE+offset);memoryWrite(0xe001,memoryRead(filesystemLengths+target));
        for(unsigned i=0;i<argumentLength;i++)memoryWrite(0xe100+i,(unsigned char)arguments[i]);
        for(unsigned i=0;i<=nameLength;i++)memoryWrite(0xe200+i,(unsigned char)name[i]);
        memoryWrite(0xe002,0xe100);memoryWrite(0xe003,0xe200);
        childStatus=runWidePackedFromGuest(0xe000,&started);
        if(!started)return childStatus;
        memoryWrite(c,childStatus);return 0;
    }
    if(op==7) {
        uint32_t words=a,capacity=b;
        time_t now=time(NULL);
        if(!APP_RANGE(words,capacity) || capacity<2 || now<0)return -1;
        uint64_t seconds=(uint64_t)now;
        memoryWrite(words,(int32_t)(uint32_t)seconds);
        memoryWrite(words+1,(int32_t)(uint32_t)(seconds>>32));
        return 2;
    }
    if(op==16) { graphicsReset();graphicsActive=true;return 0; }
    if(op==17) {
        if(!graphicsActive || !APP_RANGE(a,b) || b<2)return -1;
        memoryWrite(a,GRAPHICS_WIDTH);memoryWrite(a+1,GRAPHICS_HEIGHT);return 2;
    }
    if(op==18) {
        if(!graphicsActive || (int32_t)a<0 || (int32_t)b<0 || a>=GRAPHICS_WIDTH || b>=GRAPHICS_HEIGHT || c>255 || d>15)return -1;
        graphicsPixel((int)a,(int)b,(unsigned char)c,(unsigned char)d);return 0;
    }
    if(op==19) {
        int x=(int32_t)a,y=(int32_t)b,w=(int32_t)c,h=(int32_t)d;
        if(!graphicsActive || w<=0 || h<=0 || w>GRAPHICS_WIDTH*2 || h>GRAPHICS_HEIGHT*2 || e>255 || f>15)return -1;
        int64_t endX=(int64_t)x+w,endY=(int64_t)y+h;
        int startX=x<0?0:x,startY=y<0?0:y;
        int clippedX=endX>GRAPHICS_WIDTH?GRAPHICS_WIDTH:(int)endX;
        int clippedY=endY>GRAPHICS_HEIGHT?GRAPHICS_HEIGHT:(int)endY;
        for(int py=startY;py<clippedY;py++)for(int px=startX;px<clippedX;px++)graphicsPixel(px,py,(unsigned char)e,(unsigned char)f);
        return 0;
    }
    if(op==20) {
        int x0=(int32_t)a,y0=(int32_t)b,x1=(int32_t)c,y1=(int32_t)d;
        if(!graphicsActive || x0<0 || x0>=GRAPHICS_WIDTH || x1<0 || x1>=GRAPHICS_WIDTH || y0<0 || y0>=GRAPHICS_HEIGHT || y1<0 || y1>=GRAPHICS_HEIGHT || e>255 || f>15)return -1;
        int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,error=dx+dy;
        for(;;){graphicsPixel(x0,y0,(unsigned char)e,(unsigned char)f);if(x0==x1 && y0==y1)break;int twice=2*error;if(twice>=dy){error+=dy;x0+=sx;}if(twice<=dx){error+=dx;y0+=sy;}}
        return 0;
    }
    if(op==21) {
        int x=(int32_t)a,y=(int32_t)b,startX=x;uint32_t text=c,color=d;
        if(!graphicsActive || (int32_t)x<0 || x>=GRAPHICS_WIDTH || y<0 || y>=GRAPHICS_HEIGHT || !APP_RANGE(text,1) || color>15)return -1;
        for(unsigned i=0;i<GRAPHICS_CELLS && APP_RANGE(text+i,1);i++) {
            uint32_t ch=(uint32_t)memoryRead(text+i);if(!ch)break;
            if(ch=='\n'){x=startX;y++;if(y>=GRAPHICS_HEIGHT)break;continue;}
            graphicsPixel(x++,y,(unsigned char)(ch<=255?ch:'?'),(unsigned char)color);
            if(x>=GRAPHICS_WIDTH)break;
        }
        return 0;
    }
    if(op==22) {
        int x=(int32_t)a,y=(int32_t)b,w=(int32_t)c,h=(int32_t)d;uint32_t bitmap=e,color=f;
        if(!graphicsActive || x<0 || y<0 || x>=GRAPHICS_WIDTH || y>=GRAPHICS_HEIGHT || w<=0 || h<=0 || w>GRAPHICS_WIDTH-x || h>GRAPHICS_HEIGHT-y || (uint64_t)w*h>GRAPHICS_CELLS || !APP_RANGE(bitmap,(uint64_t)w*h) || color>15)return -1;
        for(int py=0;py<h;py++)for(int px=0;px<w;px++) {
            uint32_t ch=(uint32_t)memoryRead(bitmap+(uint32_t)py*w+(uint32_t)px);
            if(ch>255)return -1;
            if(ch)graphicsPixel(x+px,y+py,(unsigned char)ch,(unsigned char)color);
        }
        return 0;
    }
    if(op==27) {
        int x=(int32_t)a,y=(int32_t)b,w=(int32_t)c,h=(int32_t)d;uint32_t bitmap=e;
        if(!graphicsActive || x<0 || y<0 || x>=GRAPHICS_WIDTH || y>=GRAPHICS_HEIGHT || w<=0 || h<=0 || w>GRAPHICS_WIDTH-x || h>GRAPHICS_HEIGHT-y || (uint64_t)w*h>GRAPHICS_CELLS || !APP_RANGE(bitmap,(uint64_t)w*h))return -1;
        for(int i=0;i<w*h;i++)if((uint32_t)memoryRead(bitmap+(uint32_t)i)>0xfffu)return -1;
        for(int py=0;py<h;py++)for(int px=0;px<w;px++) {
            uint32_t cell=(uint32_t)memoryRead(bitmap+(uint32_t)py*w+(uint32_t)px);
            uint32_t ch=cell&255u,color=(cell>>8)&15u;
            if(ch)graphicsPixel(x+px,y+py,(unsigned char)ch,(unsigned char)color);
        }
        return 0;
    }
    if(op==23) {
        if(!graphicsActive || a>255 || b>15)return -1;
        for(unsigned i=0;i<GRAPHICS_CELLS;i++){graphicsCharacters[i]=(unsigned char)(a?a:' ');graphicsColors[i]=(unsigned char)b;}
        return 0;
    }
    if(op==24) {if(!graphicsActive)return -1;graphicsPresent();return 0;}
    if(op==25) {if(!graphicsActive)return -1;graphicsClose();return 0;}
    if(op==26) {
        uint32_t event=a,capacity=b;
        if(!APP_RANGE(event,capacity) || capacity<4)return -1;
        int type,value,x,y;
        if(!terminalEvent(&type,&value,&x,&y))return 0;
        memoryWrite(event,type);memoryWrite(event+1,value);
        memoryWrite(event+2,x);memoryWrite(event+3,y);return 1;
    }
    if(op==28) {
        uint32_t event=a,capacity=b;
        if(!APP_RANGE(event,capacity) || capacity<4)return -1;
        int type,value,x,y;
        if(!terminalEventPoll(&type,&value,&x,&y))return 0;
        memoryWrite(event,type);memoryWrite(event+1,value);
        memoryWrite(event+2,x);memoryWrite(event+3,y);return 1;
    }
#undef APP_RANGE
    return -1;
}

void cpuCycle(void);

static int runProgram(uint32_t entry) {
    bool savedCpu32=cpu32;
    bool savedLegacyProgram32=legacyProgram32;
    bool savedProgramMode=programMode,savedProgramDone=programDone;
    volatile uint32_t legacyBase=savedCpu32?0xc200u:0xa000u;
    volatile uint32_t legacyEnd=savedCpu32?0xd000u:0xbe00u;
    if (programDepth>=8 || (application32 ? (entry < 0x20000 || entry >= 0xf0000) : (entry < legacyBase || entry >= legacyEnd))) return -1;
    struct cpu_t saved = cpu;
    ureg_t savedMar = ram.mar;
    reg_t savedMdr = ram.mdr;
    volatile int status = -2;
    legacyProgram32=savedCpu32&&!application32;
    programMode = true;
    programDone = false;
    bool savedApplicationEntryArguments=applicationEntryArguments;
    unsigned frame=programDepth++;
    if (setjmp(programFaultStack[frame]) == 0) {
        if(!application32)cpu32=false;
        cpu.pc = addressValue(entry);
        cpu.regFile[6] = wordValue(application32?0x100000u:(savedCpu32?0xd000u:0xc000u));
        cpu.regFile[5] = 0;
        cpu.regFile[7] = 0x80;
        if(application32 && applicationEntryArguments) { cpu.regFile[0]=wordValue(applicationArgc);cpu.regFile[1]=wordValue(applicationArgv); }
        applicationEntryArguments=false;
        unsigned cycles = 0;
        while (!programDone && cpu.pc != 0x80 && cycles < 5000000) {
            cpuCycle();
            ++cycles;
        }
        status = cycles == 5000000 ? -3 : (int32_t)(uint32_t)cpu.regFile[0];
    }
    programDepth--;
    programMode = savedProgramMode;
    if(application32 && graphicsActive)graphicsClose();
    if(savedCpu32 && !application32 && bootWideHeader) {
        uint64_t imageEnd=(uint64_t)bootOrigin+bootWords;
        uint32_t restoreStart=bootOrigin>legacyBase?bootOrigin:legacyBase;
        uint32_t restoreEnd=imageEnd<legacyEnd?(uint32_t)imageEnd:legacyEnd;
        for(uint32_t address=restoreStart;address<restoreEnd;address++)memoryWrite(address,wordValue(bootDisk[address-bootOrigin]));
    }
    cpu32=savedCpu32;
    legacyProgram32=savedLegacyProgram32;
    applicationEntryArguments=savedApplicationEntryArguments;
    programDone=savedProgramDone;
    cpu = saved;
    ram.mar = savedMar;
    ram.mdr = savedMdr;
    return status;
}
static int runWideImageFromGuest(uint32_t pathAddress);
static int runWidePackedFromGuest(uint32_t descriptor,int *started);
static int importWideImageFromGuest(uint32_t descriptor);

int sext(int16_t val, uint16_t length) {
    if(!length || length>16)machineFault();
    unsigned bits=(uint16_t)val & ((1u<<length)-1);
    return (int)bits-(int)((bits & (1u<<(length-1)))?(1u<<length):0);
}

void updateCC(reg_t val) {
    val=wordValue((uint32_t)val);
	cpu.cc[N] = val < 0;
	cpu.cc[Z] = val == 0;
	cpu.cc[P] = val > 0;
}

void fetch() {
	ram.mar = cpu.pc;
	currentInstructionAddress=(uint32_t)cpu.pc;
	cpu.pc = addressValue(cpu.pc+1);
	ram.mdr = memoryRead(ram.mar);
    if(cpu32 && ((uint32_t)ram.mdr & 0xffff0000u)) {
        fprintf(stderr,"Reserved upper bits in bob32 instruction at %04X\n",ram.mar);machineFault();
    }
	cpu.ir = (uint16_t)ram.mdr;
    currentInstruction=cpu.ir;
}

void evalAddress() {
	int opcode = (cpu.ir >> 12) & 0xF;
	if (opcode == LD || opcode == ST || opcode == LEA || opcode == BR) {
		ram.mar = addressValue(cpu.pc + sext(cpu.ir & 0x1FF, 9));
	} else if (opcode == LDI || opcode == STI) {
		ram.mar = addressValue(cpu.pc + sext(cpu.ir & 0x1FF, 9));
		ram.mdr = memoryRead(ram.mar);
		ram.mar = addressValue((uint32_t)ram.mdr);
	} else if (opcode == LDR || opcode == STR) {
		ram.mar = addressValue((uint32_t)cpu.regFile[(cpu.ir >> 6) & 0x7] + (uint32_t)sext(cpu.ir & 0x3F, 6));
	}
}

void fetchOperands() {
	int opcode = (cpu.ir >> 12) & 0xF;
	if (opcode == LD || opcode == LDI || opcode == LDR) {
		ram.mdr = memoryRead(ram.mar);
	}
}

void execute() {
	switch ((cpu.ir >> 12) & 0xF) {
		case NOP:
			if(cpu.ir & 0x0fff)machineFault();
			break;
		case ADD:
			switch ((cpu.ir >> 7) & 0x3) {
				case 0:
					if (cpu.ir & 0b1) {
						printf("bad instruction: %X\n at memory address %X\n", cpu.ir, (uint16_t)(cpu.pc - 1));
						machineFault();
					}

					cpu.alu.accumulator = wordAdd(cpu.regFile[(cpu.ir >> 4) & 0x7],cpu.regFile[(cpu.ir >> 1) & 0x7]);
					break;
				case 1:
					cpu.alu.accumulator = wordAdd(cpu.regFile[(cpu.ir >> 4) & 0x7],sext(cpu.ir & 0xF, 4));
					break;
				case 2:
					if (cpu.ir & 0xF) {
						printf("bad instruction: %X\n at memory address %X\n", cpu.ir, (uint16_t)(cpu.pc - 1));
						machineFault();
					}

					cpu.alu.accumulator = wordAdd(cpu.regFile[(cpu.ir >> 9) & 0x7],cpu.regFile[(cpu.ir >> 4) & 0x7]);
					break;
				case 3:
					cpu.alu.accumulator = wordAdd(cpu.regFile[(cpu.ir >> 9) & 0x7],sext(cpu.ir & 0x7F, 7));
					break;
			}

			break;
		case AND:
			switch ((cpu.ir >> 7) & 0x3) {
				case 0:
					if (cpu.ir & 0x1) {
						printf("bad instruction: %X\n at memory address %X\n", cpu.ir, (uint16_t)(cpu.pc - 1));
						machineFault();
					}

					cpu.alu.accumulator = cpu.regFile[(cpu.ir >> 4) & 0x7] & cpu.regFile[(cpu.ir >> 1) & 0x7];
					break;
				case 1:
					cpu.alu.accumulator = cpu.regFile[(cpu.ir >> 4) & 0x7] & sext(cpu.ir & 0xF, 4);
					break;
				case 2:
					if (cpu.ir & 0xF) {
						printf("bad instruction: %X\n at memory address %X\n", cpu.ir, (uint16_t)(cpu.pc - 1));
						machineFault();
					}

					cpu.alu.accumulator = cpu.regFile[(cpu.ir >> 9) & 0x7] & cpu.regFile[(cpu.ir >> 4) & 0x7];
					break;
				case 3:
					cpu.alu.accumulator = cpu.regFile[(cpu.ir >> 9) & 0x7] & sext(cpu.ir & 0x7F, 7);
					break;
			}

			break;
		case NOT:
			if (cpu.ir & ((cpu.ir & 0x100)?0xff:0x1f)) {
				printf("bad instruction: %X\n at memory address %X\n", cpu.ir, (uint16_t)(cpu.pc - 1));
				machineFault();
			}

			if (cpu.ir & 0x100) {
				cpu.alu.accumulator = ~cpu.regFile[(cpu.ir >> 9) & 0x7];
			} else {
				cpu.alu.accumulator = ~cpu.regFile[(cpu.ir >> 5) & 0x7];
			}

			break;
		case LD:
			break;
		case LDI:
			break;
		case LDR:
			break;
		case ST:
			break;
		case STI:
			break;
		case STR:
			break;
		case BR:
			if (
				(cpu.cc[N] && ((cpu.ir >> 11) & 1)) ||
				(cpu.cc[Z] && ((cpu.ir >> 10) & 1)) ||
				(cpu.cc[P] && ((cpu.ir >> 9) & 1))
			) {
				cpu.pc = addressValue(cpu.pc + sext(cpu.ir & 0x1FF, 9));
			}

			break;
		case JMP:
			if (cpu.ir & 0x1FF) {
				printf("bad instruction: %X\n at memory address %X\n", cpu.ir, (uint16_t)(cpu.pc - 1));
				machineFault();
			}

			cpu.pc = addressValue((uint32_t)cpu.regFile[(cpu.ir >> 9) & 0x7]);
			break;
		case JSR:
		{
			if(!(cpu.ir & 0x800) && (cpu.ir & 0xff))machineFault();
			ureg_t target = addressValue((uint32_t)cpu.regFile[(cpu.ir >> 8) & 0x7]);
			cpu.regFile[7] = wordValue(cpu.pc);
			if (cpu.ir & 0x800) {
				cpu.pc = addressValue(cpu.pc + sext(cpu.ir & 0x7FF, 11));
			} else {
				cpu.pc = target;
			}

			break;
		}
		case LEA:
			break;
		case RET:
			if(cpu.ir & 0x0fff)machineFault();
			cpu.pc = addressValue((uint32_t)cpu.regFile[7]);
			break;
		case TRAP:
			if (cpu.ir & 0xFF) {
				printf("bad instruction: %X\n at memory address %X\n", cpu.ir, (uint16_t)(cpu.pc - 1));
				machineFault();
			}

			switch ((cpu.ir >> 8) & 0xF) {
				case HALT:
					if (programMode) { programDone = true; break; }
					exit(0);
					break;
				case PUTC:
					putchar(cpu.regFile[0] & 0xFF);
					break;
				case PUTS:
				{
					uint64_t remaining = (uint64_t)UINT32_MAX+1;
					cpu.regFile[7] = wordValue(cpu.pc);
					cpu.pc = addressValue((uint32_t)cpu.regFile[0]);
					while (true) {
						if (!remaining--) machineFault();
						reg_t character=wordValue((uint32_t)memoryRead(cpu.pc));
						if (character == 0) break;
						cpu.regFile[0] = character;
						putchar(cpu.regFile[0] & 0xFF);
						if(cpu32 && cpu.pc==UINT32_MAX)machineFault();
						cpu.pc=addressValue(cpu.pc+1);
					}

					putchar('\n');
					
					cpu.pc = addressValue((uint32_t)cpu.regFile[7]);
					break;
				}
				case GETS:
				{
					if(cpu32 && ((uint32_t)cpu.regFile[1] & 0xffff0000u))machineFault();
					int n = (uint16_t)cpu.regFile[1];
					cpu.regFile[7] = wordValue(cpu.pc);
					uint32_t address = addressValue((uint32_t)cpu.regFile[0]);
					uint64_t end = (uint64_t)address + (unsigned)n;
                    uint32_t legacyBase=legacyProgram32?0xc200u:0xa000u;
                    uint32_t legacyEnd=0xe000u;
                    int guestRange=application32?(address>=0x20000 && end<=0x100000):((address>=legacyBase && end<=legacyEnd) || address>=0x10000);
					if (n < 2 || end > (uint64_t)UINT32_MAX+1 || (programMode && !guestRange)) {
						fprintf(stderr, "Invalid input buffer\n"); machineFault();
					}
					char *buf = calloc((size_t)n, 1);
					if (!buf) machineFault();
					if (!fgets(buf, n, stdin)) buf[0] = '\0';
					size_t length=strlen(buf);
					for (size_t i = 0; i <= length; i++) {
						memoryWrite(address + (uint32_t)i,buf[i] & 0xFF);
					}
					free(buf);

					cpu.pc = addressValue((uint32_t)cpu.regFile[7]);
					break;
				}
				case 4: /* disk_read(dst=r0, word_offset=r1, count=r2) */
				{
					if(cpu32 && (((uint32_t)cpu.regFile[1] | (uint32_t)cpu.regFile[2]) & 0xffff0000u)) {
						fprintf(stderr,"Invalid boot disk argument\n");machineFault();
					}
					uint32_t dst = addressValue((uint32_t)cpu.regFile[0]);
					unsigned offset = (uint16_t)cpu.regFile[1];
					unsigned count = (uint16_t)cpu.regFile[2];
					uint64_t end=(uint64_t)dst+count;
					if (programMode || !bootMode || offset + count > bootWords ||
					    dst < 0x100 || end > (uint64_t)UINT32_MAX+1 || (dst < 0xf000 && end > 0xf000)) {
						fprintf(stderr, "Invalid boot disk read\n"); machineFault();
					}
					for (unsigned i = 0; i < count; ++i)
						memoryWrite(dst + i,wordValue(bootDisk[offset + i]));
					cpu.regFile[0] = wordValue(count);
					break;
				}
				case 5:
					cpu.regFile[0] = (int16_t)getchar();
					break;
				case 6:
					if(programMode){cpu.regFile[0]=-1;break;}
					application32=cpu32 && ((uint32_t)cpu.regFile[0]>=0x20000u && (uint32_t)cpu.regFile[0]<0xf0000u);
					if(cpu32 && ((uint32_t)cpu.regFile[0]&0xffff0000u) &&
					   ((uint32_t)cpu.regFile[0]<0x20000u || (uint32_t)cpu.regFile[0]>=0xf0000u))cpu.regFile[0]=-2;
					else cpu.regFile[0] = wordValue((uint32_t)runProgram(cpu32?(uint32_t)cpu.regFile[0]:(uint16_t)cpu.regFile[0]));
					application32=false;
					break;
				case 7:
					if(programMode)machineFault();
					cpu.regFile[0]=wordValue((uint32_t)snapshotService(cpu32?(uint32_t)cpu.regFile[0]:(uint16_t)cpu.regFile[0],cpu.regFile[1]));
					break;
				case 8:
					cpu.regFile[0]=(int16_t)terminalKey();
					break;
				case 9:
					cpu.regFile[0]=(int16_t)terminalAvailable();
					break;
				case 10:
				case 11:
					cpu.regFile[0]=((cpu.ir>>8)&15)==10?80:25;
#ifdef _WIN32
					{
						CONSOLE_SCREEN_BUFFER_INFO info;
						if(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE),&info))cpu.regFile[0]=((cpu.ir>>8)&15)==10?info.dwSize.X:info.srWindow.Bottom-info.srWindow.Top+1;
					}
#endif
					break;
				case 12:
					if(programMode || !cpu32)cpu.regFile[0]=-4;
					else cpu.regFile[0]=wordValue((uint32_t)runWideImageFromGuest((uint32_t)cpu.regFile[0]));
					break;
				case 13:
					if(!cpu32)cpu.regFile[0]=-4;
                    else cpu.regFile[0]=wordValue((uint32_t)runWidePackedFromGuest((uint32_t)cpu.regFile[0],NULL));
					break;
				case 14:
					if(programMode || !cpu32)cpu.regFile[0]=-4;
					else cpu.regFile[0]=wordValue((uint32_t)importWideImageFromGuest((uint32_t)cpu.regFile[0]));
					break;
				case 15:
					if(!cpu32)cpu.regFile[0]=-4;
					else cpu.regFile[0]=wordValue((uint32_t)filesystemService((uint32_t)cpu.regFile[0]));
					break;
				default:
					machineFault();
			}

			break;
	}
}

void storeResult() {
	int opcode = (cpu.ir >> 12) & 0xF;
	int dest = (cpu.ir >> 9) & 0x7;
	if (opcode == LD || opcode == LDI || opcode == LDR) {
		cpu.regFile[dest] = wordValue((uint32_t)ram.mdr);
		updateCC(cpu.regFile[dest]);
	} else if (opcode == ST || opcode == STI || opcode == STR) {
					uint32_t storeBase=application32?0x20000u:(legacyProgram32?0xc200u:0xa000u);
					uint32_t storeEnd=application32?0x100000u:0xe000u;
					bool allowedStore=ram.mar>=storeBase && ram.mar<storeEnd;
					if (!application32 && !legacyProgram32 && ram.mar>=0x9800u && ram.mar<0x9810u) allowedStore=true;
					if (programMode && !allowedStore) {
                        if (getenv("BOB16_TRACE_FAULT"))
                            fprintf(stderr,"Rejected store to 0x%08X (valid 0x%08X..0x%08X%s)\n",ram.mar,storeBase,storeEnd-1,(!application32&&!legacyProgram32?" or 0x00009800..0x0000980F":""));
                        machineFault();
                    }
		memoryWrite(ram.mar,wordValue((uint32_t)cpu.regFile[dest]));
	} else if (opcode == ADD || opcode == AND || opcode == NOT) {
		cpu.regFile[dest] = wordValue((uint32_t)cpu.alu.accumulator);
		updateCC(cpu.regFile[dest]);
	} else if (opcode == LEA) {
		cpu.regFile[dest] = wordValue(ram.mar);
		updateCC(cpu.regFile[dest]);
	}
}

void cpuCycle() {
	fetch();
	evalAddress();
	fetchOperands();
	execute();
	storeResult();
}

int parseReg(const char* s) {
	if (!s) {
		//string doesn't exist
		return -1;
	}

	if (strlen(s) != 2 || (s[0] != 'R' && s[0] != 'r')) {
		//incorrect format
		return -2;
	}

	if (s[1] < '0' || s[1] > '7') {
		//out of bounds
		return -3;
	}

	return s[1] - '0';
}

static int parseImmediate(const char *text,int *value) {
    const char *p=text;if(*p=='+' || *p=='-')p++;
    if(!*p)return 0;
    for(;*p;p++)if(*p<'0' || *p>'9')return 0;
    errno=0;char *end;long parsed=strtol(text,&end,10);
    if(errno==ERANGE || *end || parsed<INT_MIN || parsed>INT_MAX)return 0;
    *value=(int)parsed;return 1;
}
static int readAssemblyLine(FILE *file,char line[257]) {
    size_t length=0;int ch,readAny=0,tooLong=0;
    while((ch=fgetc(file))!=EOF) {
        readAny=1;
        if(ch=='\n')break;
        if(ch==0) {
            while((ch=fgetc(file))!=EOF && ch!='\n'){}
            line[0]=0;return -2;
        }
        if(length<256)line[length++]=(char)ch;
        else tooLong=1;
    }
    if(ferror(file)){line[0]=0;return -3;}
    if(!readAny && ch==EOF)return 0;
    if(length && line[length-1]=='\r')length--;
    line[length]=0;
    if(tooLong || length>255)return -1;
    return 1;
}
static int assembleSource(FILE *file);
int assemble() {
	memset(ram.memory, 0, sizeof(ram.memory));

	FILE* file;
	char fileName[256];

	setbuf(stdout, NULL);
	printf("Enter source assembly file name: \n");
	if (!fgets(fileName, 256, stdin)) return -8;
    if(!strchr(fileName,'\n')) {
        int next=fgetc(stdin);
        if(next=='\r')next=fgetc(stdin);
        if(next!=EOF && next!='\n') {
            while(next!=EOF && next!='\n')next=fgetc(stdin);
            printf("Source file name exceeds 255 characters!\n");return -1;
        }
    }
	fileName[strcspn(fileName, "\r\n")] = '\0';
	file = fopen(fileName, "r");
	if (!file) {
		printf("Source file doesn't exist!\n");
		return -1;
	}

    int result=assembleSource(file);
    if(fclose(file) && !result){fprintf(stderr,"Cannot close assembly source\n");result=-1;}
    if(!result)printf("Starting execution!\n");
    return result;
}
static int assembleSource(FILE *file) {

	uint32_t instructionCount = 0;
	assembledOrigin=0;
	int lineCount = 0;
	char line[257];
	int lineStatus;
	while ((lineStatus=readAssemblyLine(file,line))>0) {
		uint32_t instruction = 0;
		size_t n = strspn(line, " \t\r");
		char* lineTrimmed = line + n;

		if (lineTrimmed[0] == '\0' || lineTrimmed[0] == ';') {
			lineCount++;
			continue;
		}

		char* tokens[4] = { NULL, NULL, NULL, NULL };
		int tokenCount = 0;
		char* token = strtok(lineTrimmed, " \t\r\n");
		while (token != NULL) {
			if (tokenCount == 4) {
				printf("Too many tokens on line %d!\n", lineCount);
				return -2;
			}

			tokens[tokenCount] = token;
			tokenCount++;
			token = strtok(NULL, " \t\r\n");
		}

		if (tokenCount == 0) {
			lineCount++;
			continue;
		}
		if(strcmp(tokens[0],".org")==0) {
			char *end;errno=0;unsigned long long value=tokenCount==2?strtoull(tokens[1],&end,0):0;
			if(!cpu32 || tokenCount!=2 || tokens[1][0]=='-' || end==tokens[1] || *end || errno==ERANGE || value>UINT32_MAX || value<instructionCount) {
				printf("Invalid .org directive on line %d!\n",lineCount);return -3;
			}
			if(!instructionCount)assembledOrigin=(uint32_t)value;
			instructionCount=(uint32_t)value;lineCount++;continue;
		}

        if(strcmp(tokens[0],"nop")==0) {
            if(tokenCount!=1){printf("Wrong amount of tokens on line %d!\n",lineCount);return -4;}
		} else if (strcmp(tokens[0], ".fill") == 0) {
			if (tokenCount != 2) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

            char *end;errno=0;unsigned long value=strtoul(tokens[1],&end,16);
			if (tokens[1][0]=='+' || tokens[1][0]=='-' || end==tokens[1] || *end || errno==ERANGE || value>(cpu32?UINT32_MAX:0xffffu)) {
				printf("Wrong token on line %d!\n", lineCount);
				return -3;
			}
            instruction=(uint32_t)value;

		} else if (strcmp(tokens[0], ".stringz") == 0) {
			if (tokenCount != 2) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

            size_t length=strlen(tokens[1]);
			uint64_t assemblyLimit=instructionCount>=0x10000?(uint64_t)UINT32_MAX+1:0x10000;
			if((uint64_t)instructionCount+length+1>assemblyLimit){printf("Too many instructions!\n");return -7;}
			for (size_t i = 0; i < length; i++) {
				memoryWrite(instructionCount, tokens[1][i] & 0x00FF);
				instructionCount++;
			}
			
			memoryWrite(instructionCount, 0);
			instructionCount++;
            lineCount++;continue;

		} else if (strcmp(tokens[0], "add") == 0) {
			instruction += (1 << 12);

			if (tokenCount == 4) { //normal
				int dst = parseReg(tokens[1]);
				int src0 = parseReg(tokens[2]);
				int src1 = parseReg(tokens[3]);
				if (dst < 0 || src0 < 0) {
					printf("Wrong tokens on line %d!\n", lineCount);
					return -3;
				}

				instruction += (dst << 9);
				instruction += (src0 << 4);
				if (src1 < 0) { //imm
					instruction += (1 << 7);
					int imm; if(!parseImmediate(tokens[3],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
					if (imm > 7) {
						imm = 7;
					}

					if (imm < -8) {
						imm = -8;
					}

					instruction += imm & 0xF;
				} else { //normal
					instruction += (src1 << 1);
				}

			} else if (tokenCount == 3) { //in place
				int dst = parseReg(tokens[1]);
				int src = parseReg(tokens[2]);
				if (dst < 0) {
					printf("Wrong tokens on line %d!\n", lineCount);
					return -3;
				}

				instruction += (dst << 9);
				if (src < 0) { //imm
					instruction += (3 << 7);
					int imm; if(!parseImmediate(tokens[2],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
					if (imm > 63) {
						imm = 63;
					}

					if (imm < -64) {
						imm = -64;
					}

					instruction += imm & 0x7F;
				} else { //normal
					instruction += (2 << 7);
					instruction += (src << 4);
				}

			} else {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

		} else if (strcmp(tokens[0], "and") == 0) {
			instruction += (2 << 12);

			if (tokenCount == 4) { //normal
				int r0 = parseReg(tokens[1]);
				int r1 = parseReg(tokens[2]);
				int r2 = parseReg(tokens[3]);
				if (r0 < 0 || r1 < 0) {
					printf("Wrong tokens on line %d!\n", lineCount);
					return -3;
				}

				instruction += (r0 << 9);
				instruction += (r1 << 4);
				if (r2 < 0) { //imm
					instruction += (1 << 7);
					int imm; if(!parseImmediate(tokens[3],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
					if (imm > 7) {
						imm = 7;
					}

					if (imm < -8) {
						imm = -8;
					}

					instruction += imm & 0xF;
				} else { //normal
					instruction += (r2 << 1);
				}

			} else if (tokenCount == 3) { //in place
				int r0 = parseReg(tokens[1]);
				int r1 = parseReg(tokens[2]);
				if (r0 < 0) {
					printf("Wrong tokens on line %d!\n", lineCount);
					return -3;
				}

				instruction += (r0 << 9);
				if (r1 < 0) { //imm
					instruction += (3 << 7);
					int imm; if(!parseImmediate(tokens[2],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
					if (imm > 63) {
						imm = 63;
					}

					if (imm < -64) {
						imm = -64;
					}

					instruction += imm & 0x7F;
				} else { //normal
					instruction += (2 << 7);
					instruction += (r1 << 4);
				}

			} else {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

		} else if (strcmp(tokens[0], "not") == 0) {
			instruction += (3 << 12);
			if(tokenCount!=2 && tokenCount!=3) {
				printf("Wrong amount of tokens on line %d!\n",lineCount);return -4;
			}

			int r0 = parseReg(tokens[1]);
			if (r0 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			if(tokenCount==2) {
				instruction += (1 << 8);
			} else {
				int r1=parseReg(tokens[2]);
				if(r1<0) {
					printf("Wrong tokens on line %d!\n",lineCount);return -3;
				}
				instruction += (r1 << 5);
			}

		} else if (strcmp(tokens[0], "ld") == 0) {
			instruction += (4 << 12);
			if (tokenCount != 3) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int r0 = parseReg(tokens[1]);
			if (r0 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			int imm; if(!parseImmediate(tokens[2],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
			if (imm > 255) {
				imm = 255;
			}

			if (imm < -256) {
				imm = -256;
			}

			instruction += (imm & 0x1FF);

		} else if (strcmp(tokens[0], "ldi") == 0) {
			instruction += (5 << 12);

			if (tokenCount != 3) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int r0 = parseReg(tokens[1]);
			if (r0 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			int imm; if(!parseImmediate(tokens[2],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
			if (imm > 255) {
				imm = 255;
			}

			if (imm < -256) {
				imm = -256;
			}

			instruction += (imm & 0x1FF);
		} else if (strcmp(tokens[0], "ldr") == 0) {
			instruction += (6 << 12);

			if (tokenCount != 4) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int r0 = parseReg(tokens[1]);
			int r1 = parseReg(tokens[2]);
			if (r0 < 0 || r1 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			instruction += (r1 << 6);
			int imm; if(!parseImmediate(tokens[3],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
			if (imm > 31) {
				imm = 31;
			}

			if (imm < -32) {
				imm = -32;
			}

			instruction += (imm & 0x3F);
		} else if (strcmp(tokens[0], "st") == 0) {
			instruction += (7 << 12);

			if (tokenCount != 3) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int r0 = parseReg(tokens[1]);
			if (r0 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			int imm; if(!parseImmediate(tokens[2],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
			if (imm > 255) {
				imm = 255;
			}

			if (imm < -256) {
				imm = -256;
			}

			instruction += (imm & 0x1FF);
		} else if (strcmp(tokens[0], "sti") == 0) {
			instruction += (8 << 12);

			if (tokenCount != 3) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int r0 = parseReg(tokens[1]);
			if (r0 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			int imm; if(!parseImmediate(tokens[2],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
			if (imm > 255) {
				imm = 255;
			}

			if (imm < -256) {
				imm = -256;
			}

			instruction += (imm & 0x1FF);
		} else if (strcmp(tokens[0], "str") == 0) {
			instruction += (9 << 12);

			if (tokenCount != 4) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int r0 = parseReg(tokens[1]);
			int r1 = parseReg(tokens[2]);
			if (r0 < 0 || r1 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			instruction += (r1 << 6);
			int imm; if(!parseImmediate(tokens[3],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
			if (imm > 31) {
				imm = 31;
			}

			if (imm < -32) {
				imm = -32;
			}

			instruction += (imm & 0x3F);
		} else if (strcmp(tokens[0], "br") == 0) {
			instruction += (10 << 12);

			if (tokenCount != 3) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			size_t l = strlen(tokens[1]);
			if (l > 3 || l == 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			int conditionCodes = 0;
			for (size_t i = 0; i < l; i++) {
				if (tokens[1][i] == 'n') {
					conditionCodes |= 0b100;
				} else if (tokens[1][i] == 'z') {
					conditionCodes |= 0b010;
				} else if (tokens[1][i] == 'p') {
					conditionCodes |= 0b001;
				} else {
                    printf("Invalid branch condition on line %d!\n",lineCount);return -3;
				}
			}

			instruction += ((conditionCodes & 0b111) << 9);

			int imm; if(!parseImmediate(tokens[2],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
			if (imm > 255) {
				imm = 255;
			}

			if (imm < -256) {
				imm = -256;
			}

			instruction += (imm & 0x1FF);

		} else if (strcmp(tokens[0], "jmp") == 0) {
			instruction += (11 << 12);

			if (tokenCount != 2) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int r0 = parseReg(tokens[1]);
			if (r0 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += ((r0 & 0x7) << 9);

		} else if (strcmp(tokens[0], "jsr") == 0) {
			instruction += (12 << 12);
			instruction += (1 << 11);
			
			if (tokenCount != 2) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int imm; if(!parseImmediate(tokens[1],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
			if (imm > 1023) {
				imm = 1023;
			}

			if (imm < -1024) {
				imm = -1024;
			}

			instruction += (imm & 0x7FF);

		} else if (strcmp(tokens[0], "jsrr") == 0) {
			instruction += (12 << 12);

			if (tokenCount != 2) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int r0 = parseReg(tokens[1]);
			if (r0 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 8);

		} else if (strcmp(tokens[0], "lea") == 0) {
			instruction += (13 << 12);

			if (tokenCount != 3) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int r0 = parseReg(tokens[1]);
			if (r0 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			int imm; if(!parseImmediate(tokens[2],&imm)){printf("Invalid decimal operand on line %d!\n",lineCount);return -3;}
			if (imm > 255) {
				imm = 255;
			}

			if (imm < -256) {
				imm = -256;
			}

			instruction += (imm & 0x1FF);

		} else if (strcmp(tokens[0], "ret") == 0) {
			instruction += (14 << 12);
			
			if (tokenCount != 1) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

		} else if (strcmp(tokens[0], "trap") == 0) {
			instruction += (15 << 12);

			if (tokenCount != 2) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int vector; if(!parseImmediate(tokens[1],&vector)){printf("Invalid trap vector on line %d!\n",lineCount);return -5;}
			if (vector < 0 || vector > 11) {
				printf("Bad trap vector on line %d!\n", lineCount);
				return -5;
			}

			instruction += ((vector & 0xF) << 8);

		} else {
			printf("Unkown opcode on line %d!\n", lineCount);
			return -6;
		}

		if (instructionCount == UINT32_MAX) {
			printf("Too many instructions!\n");
			return -7;
		}
		memoryWrite(instructionCount++, instruction);

		lineCount++;
	}
	if(lineStatus<0) {
        if(lineStatus==-1)printf("Source line %d exceeds 255 characters!\n",lineCount);
        else if(lineStatus==-2)printf("Source line %d contains a zero byte!\n",lineCount);
        else fprintf(stderr,"Cannot read assembly source\n");
        return -2;
    }

	if(ferror(file)){fprintf(stderr,"Cannot read assembly source\n");return -1;}
	assembledWords=instructionCount-assembledOrigin;
	return 0;
}

static void bootImage(const char *path) {
    filesystemRegistered=false;
    graphicsActive=false;graphicsReset();
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); exit(1); }
    char magic[4];
    if (fread(magic, 1, 4, file) != 4 || (memcmp(magic, "B16K", 4) && memcmp(magic,"B32K",4))) {
        fprintf(stderr, "Not a bob16/bob32 kernel image\n"); exit(1);
    }
    cpu32=!memcmp(magic,"B32K",4);
    unsigned version=readWord(file),headerSize=0;
    uint32_t origin,entry,words,checksum;
    if(cpu32 && version==2) {
        headerSize=readWord(file);origin=readDword(file);entry=readDword(file);
        words=readDword(file);checksum=readDword(file);
    } else {
        origin=readWord(file);entry=readWord(file);words=readWord(file);checksum=readWord(file);
        if(cpu32)checksum|=(uint32_t)readWord(file)<<16;
    }
    uint64_t end=(uint64_t)origin+words;
    int validV1=version==1 && words && origin>=0x100 && end<=0xa000 && entry>=origin && entry<end;
    int validV2=cpu32 && version==2 && headerSize==24 && words && words<=0x10000 &&
        end<=(uint64_t)UINT32_MAX+1 && entry>=origin && entry<end;
    if (!validV1 && !validV2) {
        fprintf(stderr, "Invalid bob16/bob32 kernel header\n"); exit(1);
    }
    uint32_t sum = 0;
    for (unsigned i = 0; i < words; ++i) {
        bootDisk[i] = readWord(file);
        if(cpu32)bootDisk[i]|=(uint32_t)readWord(file)<<16;
        sum += bootDisk[i];
    }
    if ((cpu32?sum:(sum & 0xffff)) != checksum || fgetc(file) != EOF) {
        fprintf(stderr, "Invalid kernel checksum or length\n"); exit(1);
    }
    if(ferror(file)){fprintf(stderr,"Cannot read kernel image\n");exit(1);}
    if(fclose(file)){fprintf(stderr,"Cannot close kernel image\n");exit(1);}
    memoryReset();
    bootWords=words;bootOrigin=origin;bootWideHeader=validV2;
    bootMode = true;
    memset(&cpu, 0, sizeof(cpu));
    memset(&ram, 0, sizeof(ram));
    if(validV2) {
        for(uint32_t i=0;i<words;i++)memoryWrite(origin+i,wordValue(bootDisk[i]));
        cpu.pc=entry;updateCC(0);return;
    }
    /* Real guest instructions: load disk parameters, read kernel, jump to entry. */
    uint16_t rom[] = {0x4007, 0x4207, 0x4407, 0xf400, 0x4606, 0xb600,
                      0xf000, 0, (uint16_t)origin, 0, (uint16_t)words, (uint16_t)entry};
    for (unsigned i = 0; i < sizeof(rom) / sizeof(rom[0]); ++i) ram.memory[i] = wordValue(rom[i]);
    updateCC(0);
}

static int prepareApplicationArguments(const char *programName,const char *text) {
    char storage[128];
    uint32_t pointers[64];
    unsigned length=0,argc=1,nameLength=0;
    if(!programName)programName="app";
    while(programName[nameLength] && nameLength<sizeof(storage)-1)nameLength++;
    if(programName[nameLength])return -4;
    if(!text)text="";
    while(text[length] && length<sizeof(storage)-1) {storage[length]=text[length];length++;}
    if(text[length])return -4;
    storage[length]=0;
    unsigned scan=0;
    while(storage[scan]) {
        while(storage[scan]==' ' || storage[scan]=='\t')scan++;
        if(!storage[scan])break;
        if(argc==63)return -4;
        pointers[argc++]=0xF0000u+nameLength+1+scan;
        unsigned write=scan;int quote=0;
        while(storage[scan]) {
            unsigned char c=(unsigned char)storage[scan];
            if(quote) {
                if(c==(unsigned)quote) {quote=0;scan++;continue;}
                if(c=='\\' && storage[scan+1])scan++;
            } else {
                if(c==' ' || c=='\t')break;
                if(c=='"' || c=='\'') {quote=c;scan++;continue;}
                if(c=='\\' && storage[scan+1])scan++;
            }
            storage[write++]=storage[scan++];
        }
        if(quote)return -4;
        int separated=storage[scan]!=0;
        storage[write]=0;if(separated)scan++;
    }
    for(unsigned i=0;i<nameLength;i++)memoryWrite(0xF0000u+i,(unsigned char)programName[i]);
    memoryWrite(0xF0000u+nameLength,0);
    for(unsigned i=0;i<length;i++)memoryWrite(0xF0000u+nameLength+1+i,(unsigned char)storage[i]);
    memoryWrite(0xF0000u+nameLength+1+length,0);
    pointers[0]=0xF0000u;
    uint32_t argv=0xF0000u+nameLength+length+2;
    for(unsigned i=0;i<argc;i++)memoryWrite(argv+i,pointers[i]);
    memoryWrite(argv+argc,0);
    applicationArgc=argc;applicationArgv=argv;return 0;
}

static int runWideImageFromGuest(uint32_t pathAddress) {
    char path[512];unsigned length=0;
    if(pathAddress<0x100 || pathAddress>=0xf000)return -4;
    while(length<sizeof(path)-1 && pathAddress+length<0xf000) {
        uint32_t character=(uint32_t)memoryRead(pathAddress+length);
        if(!character){if(!length)return -4;break;}
        if(character>255)return -4;
        path[length++]=(char)character;
    }
    if(length==sizeof(path)-1 || pathAddress+length>=0xf000)return -4;
    path[length]=0;
    FILE *file=fopen(path,"rb");if(!file)return -5;
    unsigned char header[24];int valid=fread(header,1,sizeof(header),file)==sizeof(header);
    if(valid && (memcmp(header,"B32K",4) || header[4]!=2 || header[5]!=0 || header[6]!=24 || header[7]!=0))valid=0;
    uint32_t origin=0,entry=0,count=0,checksum=0;
    if(valid) {
        for(unsigned i=0;i<4;i++) {
            origin|=(uint32_t)header[8+i]<<(8*i);entry|=(uint32_t)header[12+i]<<(8*i);
            count|=(uint32_t)header[16+i]<<(8*i);checksum|=(uint32_t)header[20+i]<<(8*i);
        }
        uint64_t end=(uint64_t)origin+count;
        if(!count || count>0x10000 || origin<0x20000 || end>0x100000 || entry<origin || entry>=end)valid=0;
    }
    uint32_t *words=NULL;uint32_t sum=0;
    if(valid) {words=malloc((size_t)count*sizeof(*words));if(!words)valid=0;}
    if(valid)for(uint32_t i=0;i<count;i++) {
        unsigned char bytes[4];if(fread(bytes,1,4,file)!=4){valid=0;break;}
        words[i]=(uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
        sum+=words[i];
    }
    if(valid && (sum!=checksum || fgetc(file)!=EOF || ferror(file)))valid=0;
    if(fclose(file))valid=0;
    if(!valid){free(words);return -4;}
    uint32_t firstPage=origin>>MEMORY_PAGE_SHIFT,lastPage=(origin+count-1)>>MEMORY_PAGE_SHIFT;unsigned neededPages=0,freePages=0;
    for(uint32_t page=firstPage;page<=lastPage;page++)if(!memoryPage(page,0))neededPages++;
    for(unsigned i=0;i<MEMORY_PAGE_SLOTS;i++)if(!sparseMemory[i].used)freePages++;
    if(neededPages>freePages){free(words);return -5;}
    typedef struct {uint32_t number;int32_t *words;} PendingPage;
    PendingPage *pending=neededPages?calloc(neededPages,sizeof(*pending)):NULL;
    if(neededPages && !pending){free(words);return -5;}
    unsigned pendingCount=0;
    for(uint32_t page=firstPage;page<=lastPage;page++)if(!memoryPage(page,0)) {
        pending[pendingCount].number=page;
        pending[pendingCount].words=allocateSparsePage();
        if(!pending[pendingCount].words) {
            for(unsigned i=0;i<pendingCount;i++)free(pending[i].words);
            free(pending);free(words);return -5;
        }
        pendingCount++;
    }
    for(unsigned i=0;i<pendingCount;i++)memoryPageInstall(pending[i].number,pending[i].words);
    free(pending);
    for(uint32_t i=0;i<count;i++)memoryWrite(origin+i,wordValue(words[i]));
    free(words);
    application32=true;applicationEntryArguments=true;
    if(prepareApplicationArguments(path,"")) {application32=false;applicationEntryArguments=false;return -4;}
    int status=runProgram(entry);
    application32=false;applicationEntryArguments=false;applicationArgc=0;applicationArgv=0;
    return status;
}

static int runWidePackedFromGuest(uint32_t descriptor,int *started) {
    if(started)*started=0;
    if(descriptor<0x300 || descriptor>0xf000-4)return -4;
    uint32_t address=(uint32_t)memoryRead(descriptor),length=(uint32_t)memoryRead(descriptor+1);
    uint32_t argumentAddress=(uint32_t)memoryRead(descriptor+2);
    uint32_t programNameAddress=(uint32_t)memoryRead(descriptor+3);
    const char *arguments=NULL;
    char argumentBuffer[128];
    if(argumentAddress) {
        if(argumentAddress<0x100 || argumentAddress>=0xf000)return -4;
        unsigned i=0;
        while(i<sizeof(argumentBuffer)-1 && argumentAddress+i<0xf000) {
            uint32_t c=(uint32_t)memoryRead(argumentAddress+i);
            if(c>255)return -4;
            argumentBuffer[i++]=(char)c;
            if(!c)break;
        }
        if(!i || argumentBuffer[i-1])return -4;
        arguments=argumentBuffer;
    }
    char programNameBuffer[128];
    if(programNameAddress) {
        if(programNameAddress<0x100 || programNameAddress>=0xf000)return -4;
        unsigned i=0;
        while(i<sizeof(programNameBuffer)-1 && programNameAddress+i<0xf000) {
            uint32_t c=(uint32_t)memoryRead(programNameAddress+i);
            if(c>255)return -4;
            programNameBuffer[i++]=(char)c;
            if(!c)break;
        }
        if(!i || programNameBuffer[i-1])return -4;
    } else programNameBuffer[0]=0;
    int oldStorage=address>=0xf000&&address<0x10000&&length<=4096&&address+length<=0x10000;
    int wideStorage=address>=WIDE_FILE_BASE&&address<WIDE_FILE_BASE+WIDE_FILE_WORDS&&length<=WIDE_FILE_WORDS&&address+length<=WIDE_FILE_BASE+WIDE_FILE_WORDS;
    if((!oldStorage&&!wideStorage)||!length)return -4;
    if((uint32_t)memoryRead(address)!=0x4233324bu || (uint32_t)memoryRead(address+1)!=0x00180002u)return -4;
    uint32_t origin=(uint32_t)memoryRead(address+2),entry=(uint32_t)memoryRead(address+3);
    uint32_t count=(uint32_t)memoryRead(address+4),checksum=(uint32_t)memoryRead(address+5);
    if(!count || count+6!=length || count>0x10000 || origin<0x20000 || (uint64_t)origin+count>0x100000 || entry<origin || entry>=origin+count)return -4;
    uint32_t sum=0;for(uint32_t i=0;i<count;i++)sum+=(uint32_t)memoryRead(address+6+i);
    if(sum!=checksum)return -4;
    uint32_t firstPage=origin>>MEMORY_PAGE_SHIFT,lastPage=(origin+count-1)>>MEMORY_PAGE_SHIFT;unsigned neededPages=0,freePages=0;
    for(uint32_t page=firstPage;page<=lastPage;page++)if(!memoryPage(page,0))neededPages++;
    for(unsigned i=0;i<MEMORY_PAGE_SLOTS;i++)if(!sparseMemory[i].used)freePages++;
    uint32_t argumentPage=0xF0000u>>MEMORY_PAGE_SHIFT;
    int needsArgumentPage=!memoryPage(argumentPage,0) && (argumentPage<firstPage || argumentPage>lastPage);
    if(neededPages+(unsigned)needsArgumentPage>freePages)return -5;
    typedef struct {uint32_t number;int32_t *words;} PendingPage;
    PendingPage *pending=neededPages?calloc(neededPages,sizeof(*pending)):NULL;
    if(neededPages && !pending)return -5;
    unsigned pendingCount=0;
    for(uint32_t page=firstPage;page<=lastPage;page++)if(!memoryPage(page,0)) {
        pending[pendingCount].number=page;pending[pendingCount].words=allocateSparsePage();
        if(!pending[pendingCount].words) {for(unsigned i=0;i<pendingCount;i++)free(pending[i].words);free(pending);return -5;}
        pendingCount++;
    }
    bool nested=programMode;
    unsigned savedPageCount=0;
    SavedSparsePage *savedPages=nested?sparseMemorySave(&savedPageCount):NULL;
    if(nested&&savedPageCount&&!savedPages) {for(unsigned i=0;i<pendingCount;i++)free(pending[i].words);free(pending);return -5;}
    bool savedApplication32=application32,savedEntryArguments=applicationEntryArguments,savedGraphicsActive=graphicsActive;
    uint32_t savedArgc=applicationArgc,savedArgv=applicationArgv;
    unsigned char savedGraphicsCharacters[GRAPHICS_CELLS],savedGraphicsColors[GRAPHICS_CELLS];
    if(nested){memcpy(savedGraphicsCharacters,graphicsCharacters,sizeof(graphicsCharacters));memcpy(savedGraphicsColors,graphicsColors,sizeof(graphicsColors));}
    for(unsigned i=0;i<pendingCount;i++)memoryPageInstall(pending[i].number,pending[i].words);
    free(pending);
    if(needsArgumentPage && !memoryPage(argumentPage,1)) {if(nested)sparseMemoryRestore(savedPages,savedPageCount);return -5;}
    for(uint32_t i=0;i<count;i++)memoryWrite(origin+i,wordValue((uint32_t)memoryRead(address+6+i)));
    application32=true;applicationEntryArguments=programNameAddress!=0;
    int prepared=programNameAddress?prepareApplicationArguments(programNameBuffer,arguments):0;
    if(!prepared&&started)*started=1;
    int status=prepared?prepared:runProgram(entry);
    if(nested) {
        sparseMemoryRestore(savedPages,savedPageCount);
        memcpy(graphicsCharacters,savedGraphicsCharacters,sizeof(graphicsCharacters));
        memcpy(graphicsColors,savedGraphicsColors,sizeof(graphicsColors));graphicsActive=savedGraphicsActive;
        application32=savedApplication32;applicationEntryArguments=savedEntryArguments;applicationArgc=savedArgc;applicationArgv=savedArgv;
    } else {application32=false;applicationEntryArguments=false;applicationArgc=0;applicationArgv=0;}
    return status;
}

static int importWideImageFromGuest(uint32_t descriptor) {
    if(descriptor<0x300 || descriptor>0xf000-3)return -4;
    uint32_t pathAddress=(uint32_t)memoryRead(descriptor),destination=(uint32_t)memoryRead(descriptor+1),capacity=(uint32_t)memoryRead(descriptor+2);
    if(pathAddress<0x100 || pathAddress>=0xf000)return -4;
    int oldStorage=destination>=0xf000&&destination<0x10000&&capacity<=0x10000-destination;
    int wideStorage=destination>=WIDE_FILE_BASE&&destination<WIDE_FILE_BASE+WIDE_FILE_WORDS&&capacity<=WIDE_FILE_BASE+WIDE_FILE_WORDS-destination;
    if((!oldStorage&&!wideStorage)||capacity<7)return -4;
    char path[512];unsigned pathLength=0;
    while(pathLength<sizeof(path)-1 && pathAddress+pathLength<0xf000) {uint32_t c=(uint32_t)memoryRead(pathAddress+pathLength);if(!c)break;if(c>255)return -4;path[pathLength++]=(char)c;}
    if(!pathLength || pathLength==sizeof(path)-1)return -4;
    path[pathLength]=0;
    FILE *file=fopen(path,"rb");if(!file)return -1;
    unsigned char header[24];int valid=fread(header,1,sizeof(header),file)==sizeof(header);
    if(valid && (memcmp(header,"B32K",4)||header[4]!=2||header[5]!=0||header[6]!=24||header[7]!=0))valid=0;
    uint32_t origin=0,entry=0,count=0,checksum=0;
    if(valid)for(unsigned i=0;i<4;i++){origin|=(uint32_t)header[8+i]<<(8*i);entry|=(uint32_t)header[12+i]<<(8*i);count|=(uint32_t)header[16+i]<<(8*i);checksum|=(uint32_t)header[20+i]<<(8*i);}
    if(valid && (!count || count>0x10000 || origin<0x20000 || (uint64_t)origin+count>0x100000 || entry<origin || entry>=origin+count))valid=0;
    if(valid && count+7>capacity){fclose(file);return -5;}
    uint32_t sum=0;memoryWrite(destination,0x4233324b);memoryWrite(destination+1,0x00180002);memoryWrite(destination+2,origin);memoryWrite(destination+3,entry);memoryWrite(destination+4,count);memoryWrite(destination+5,checksum);
    for(uint32_t i=0;valid && i<count;i++) {unsigned char b[4];if(fread(b,1,4,file)!=4){valid=0;break;}uint32_t value=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);sum+=value;memoryWrite(destination+6+i,value);}
    if(valid && (sum!=checksum || fgetc(file)!=EOF || ferror(file)))valid=0;
    if(fclose(file))valid=0;
    if(!valid)return -2;
    return (int)(count+6);
}

static int parseCycleLimit(const char *text,unsigned long *limit) {
    if(!*text)return 0;
    for(const char *p=text;*p;p++)if(*p<'0' || *p>'9')return 0;
    errno=0;char *end;unsigned long value=strtoul(text,&end,10);
    if(errno==ERANGE || *end || !value)return 0;
    *limit=value;return 1;
}
int main(int argc, char** argv) {
    memoryReset();
#ifdef _WIN32
	HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode;
	if (GetConsoleMode(console, &mode)) SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
	unsigned long maxCycles = 0;
	if (argc == 2 && strcmp(argv[1], "--os") == 0) {
		bootImage("build/kernel.b16");
	} else if(argc==3 && strcmp(argv[1],"--asm32")==0) {
        cpu32=true;
        FILE *assembly=fopen(argv[2],"rb");
        if(!assembly){perror(argv[2]);return 1;}
        int result=assembleSource(assembly);
        if(fclose(assembly) && !result){fprintf(stderr,"Cannot close assembly source\n");return 1;}
        if(result){fprintf(stderr,"B32 assembly failed (%d)\n",result);return 1;}
        assembledWide=true;
        cpu.pc=assembledOrigin;
        if(assembledWords<=0x10000) {
            bootOrigin=assembledOrigin;bootWords=assembledWords;bootWideHeader=true;
            for(uint32_t i=0;i<bootWords;i++)bootDisk[i]=(uint32_t)memoryRead(bootOrigin+i);
        }
	} else if (argc >= 3 && strcmp(argv[1], "--boot") == 0) {
		if (argc == 5 && strcmp(argv[3], "--max-cycles") == 0) {
            if(!parseCycleLimit(argv[4],&maxCycles)){fprintf(stderr,"--max-cycles requires a positive decimal integer within unsigned long range\n");return 1;}
        }
		else if (argc != 3) { fprintf(stderr, "Usage: %s --boot kernel.b16 [--max-cycles N]\n", argv[0]); return 1; }
		bootImage(argv[2]);
	} else {
	if (argc != 1) { fprintf(stderr, "Usage: %s [--os | --boot image.b16/b32 [--max-cycles N] | --asm32 source.basm]\n", argv[0]); return 1; }
	int assembleReturn = -1;
	while (assembleReturn < 0) {
		assembleReturn = assemble();
		if (assembleReturn == -8) return 1;
	}
	}

	uint32_t identityWords[0x8d00];
	uint64_t bootEnd=(uint64_t)bootOrigin+bootWords;
	for(unsigned i=0;i<0x8d00;i++) {
		unsigned address=0x300+i;
		identityWords[i]=bootMode?((uint64_t)address>=bootOrigin && (uint64_t)address<bootEnd?bootDisk[address-bootOrigin]:0):(uint32_t)ram.memory[address];
        if(!cpu32)identityWords[i]&=65535u;
	}
	kernelIdentity=snapshotHash(identityWords,0x8d00);
	if(bootWideHeader) {
		uint32_t *wideIdentity=malloc((size_t)(bootWords+3)*sizeof(*wideIdentity));
		if(!wideIdentity){fprintf(stderr,"Cannot allocate image identity\n");return 1;}
		wideIdentity[0]=bootOrigin;wideIdentity[1]=bootWords;wideIdentity[2]=cpu.pc;
		memcpy(wideIdentity+3,bootDisk,(size_t)bootWords*sizeof(*bootDisk));
		kernelIdentity=snapshotHash(wideIdentity,bootWords+3);free(wideIdentity);
	} else if(assembledWide) {
        uint32_t *wideIdentity=malloc((size_t)(assembledWords+3)*sizeof(*wideIdentity));
        if(!wideIdentity){fprintf(stderr,"Cannot allocate assembly identity\n");return 1;}
		wideIdentity[0]=assembledOrigin;wideIdentity[1]=assembledWords;wideIdentity[2]=assembledOrigin;
		for(uint32_t i=0;i<assembledWords;i++)wideIdentity[i+3]=(uint32_t)memoryRead(assembledOrigin+i);
        kernelIdentity=snapshotHash(wideIdentity,assembledWords+3);free(wideIdentity);
	}
	unsigned long cycles = 0;
	while (true) {
		if (maxCycles && cycles++ >= maxCycles) { fprintf(stderr, "CPU cycle limit reached\n"); return 2; }
		cpuCycle();
	}

	return 0;
}
