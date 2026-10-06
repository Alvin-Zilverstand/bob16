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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* Kernel disk exposed to the guest boot ROM through trap 4. */
static uint16_t bootDisk[0x10000];
static uint16_t bootWords;
static uint16_t bootOrigin;
static bool bootMode;
static bool programMode;
static bool programDone;
static jmp_buf programFault;

static void machineFault(void) {
    if (programMode) longjmp(programFault, 1);
    exit(1);
}

static unsigned readWord(FILE *file) {
    int lo = fgetc(file), hi = fgetc(file);
    if (lo == EOF || hi == EOF) {
        fprintf(stderr, "Truncated bob16 kernel image\n");
        exit(1);
    }
    return (unsigned)lo | ((unsigned)hi << 8);
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
        GetConsoleMode(input,&mode);
        if(!rawInputStarted){originalInputMode=mode;rawInputStarted=true;atexit(restoreInputMode);}
        SetConsoleMode(input,mode&~ENABLE_PROCESSED_INPUT);
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
        return key;
    }
#endif
    return getchar();
}

typedef uint16_t ureg_t;
typedef int16_t reg_t;

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
	ureg_t ir;
	ureg_t pc;
	bool cc[3];
	alu_t alu;
} cpu;

struct ram_t {
	int16_t memory[0x10000];
	ureg_t mar;
	reg_t mdr;
} ram;
#include "storage.c"

void cpuCycle(void);

static int runProgram(uint16_t entry) {
    if (programMode || entry < 0x9000 || entry >= 0xbe00) return -1;
    struct cpu_t saved = cpu;
    ureg_t savedMar = ram.mar;
    reg_t savedMdr = ram.mdr;
    volatile int status = -2;
    programMode = true;
    programDone = false;
    if (setjmp(programFault) == 0) {
        cpu.pc = entry;
        cpu.regFile[6] = (int16_t)0xc000;
        cpu.regFile[5] = 0;
        cpu.regFile[7] = 0x80;
        unsigned cycles = 0;
        while (!programDone && cpu.pc != 0x80 && cycles < 5000000) {
            cpuCycle();
            ++cycles;
        }
        status = cycles == 5000000 ? -3 : cpu.regFile[0];
    }
    programMode = false;
    cpu = saved;
    ram.mar = savedMar;
    ram.mdr = savedMdr;
    return status;
}

int sext(int16_t val, uint16_t length) {
	int n = 16 - length;
	return (int16_t)((val << n)) >> n;
}

void updateCC(int16_t val) {
	cpu.cc[N] = val < 0;
	cpu.cc[Z] = val == 0;
	cpu.cc[P] = val > 0;
}

void fetch() {
	ram.mar = cpu.pc;
	cpu.pc++;
	ram.mdr = ram.memory[ram.mar];
	cpu.ir = ram.mdr;
}

void evalAddress() {
	int opcode = (cpu.ir >> 12) & 0xF;
	if (opcode == LD || opcode == ST || opcode == LEA || opcode == BR) {
		ram.mar = cpu.pc + sext(cpu.ir & 0x1FF, 9);
	} else if (opcode == LDI || opcode == STI) {
		ram.mar = cpu.pc + sext(cpu.ir & 0x1FF, 9);
		ram.mdr = ram.memory[ram.mar];
		ram.mar = ram.mdr;
	} else if (opcode == LDR || opcode == STR) {
		ram.mar = cpu.regFile[(cpu.ir >> 6) & 0x7] + sext(cpu.ir & 0x3F, 6);
	}
}

void fetchOperands() {
	int opcode = (cpu.ir >> 12) & 0xF;
	if (opcode == LD || opcode == LDI || opcode == LDR) {
		ram.mdr = ram.memory[ram.mar];
	}
}

void execute() {
	switch ((cpu.ir >> 12) & 0xF) {
		case NOP:
			break;
		case ADD:
			switch ((cpu.ir >> 7) & 0x3) {
				case 0:
					if (cpu.ir & 0b1) {
						printf("bad instruction: %X\n at memory address %X\n", cpu.ir, (uint16_t)(cpu.pc - 1));
						machineFault();
					}

					cpu.alu.accumulator = cpu.regFile[(cpu.ir >> 4) & 0x7] + cpu.regFile[(cpu.ir >> 1) & 0x7];
					break;
				case 1:
					cpu.alu.accumulator = cpu.regFile[(cpu.ir >> 4) & 0x7] + sext(cpu.ir & 0xF, 4);
					break;
				case 2:
					if (cpu.ir & 0xF) {
						printf("bad instruction: %X\n at memory address %X\n", cpu.ir, (uint16_t)(cpu.pc - 1));
						machineFault();
					}

					cpu.alu.accumulator = cpu.regFile[(cpu.ir >> 9) & 0x7] + cpu.regFile[(cpu.ir >> 4) & 0x7];
					break;
				case 3:
					cpu.alu.accumulator = cpu.regFile[(cpu.ir >> 9) & 0x7] + sext(cpu.ir & 0x7F, 7);
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
			if (cpu.ir & 0x1F) {
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
				cpu.pc = cpu.pc + sext(cpu.ir & 0x1FF, 9);
			}

			break;
		case JMP:
			if (cpu.ir & 0x1FF) {
				printf("bad instruction: %X\n at memory address %X\n", cpu.ir, (uint16_t)(cpu.pc - 1));
				machineFault();
			}

			cpu.pc = cpu.regFile[(cpu.ir >> 9) & 0x7];
			break;
		case JSR:
		{
			ureg_t target = (ureg_t)cpu.regFile[(cpu.ir >> 8) & 0x7];
			cpu.regFile[7] = cpu.pc;
			if (cpu.ir & 0x800) {
				cpu.pc += sext(cpu.ir & 0x7FF, 11);
			} else {
				cpu.pc = target;
			}

			break;
		}
		case LEA:
			break;
		case RET:
			cpu.pc = cpu.regFile[7];
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
					unsigned remaining = 0x10000;
					cpu.regFile[7] = cpu.pc;
					cpu.pc = cpu.regFile[0];
					while (cpu.ir != 0) {
						if (!remaining--) machineFault();
						cpu.ir = ram.memory[cpu.pc];
						if (cpu.ir == 0) break;
						cpu.regFile[0] = cpu.ir;
						putchar(cpu.regFile[0] & 0xFF);
						cpu.pc++;					
					}

					putchar('\n');
					
					cpu.pc = cpu.regFile[7];
					break;
				}
				case GETS:
				{
					int n = (uint16_t)cpu.regFile[1];
					cpu.regFile[7] = cpu.pc;
					unsigned address = (uint16_t)cpu.regFile[0];
					if (n < 2 || address + (unsigned)n > 0x10000 ||
					    (programMode && (address < 0x9000 || address + (unsigned)n > 0xe000))) {
						fprintf(stderr, "Invalid input buffer\n"); machineFault();
					}
					char *buf = calloc((size_t)n, 1);
					if (!buf) machineFault();
					if (!fgets(buf, n, stdin)) buf[0] = '\0';
					for (size_t i = 0; i <= strlen(buf); i++) {
						ram.memory[address + i] = buf[i] & 0xFF;
					}
					free(buf);

					cpu.pc = cpu.regFile[7];
					break;
				}
				case 4: /* disk_read(dst=r0, word_offset=r1, count=r2) */
				{
					unsigned dst = (uint16_t)cpu.regFile[0];
					unsigned offset = (uint16_t)cpu.regFile[1];
					unsigned count = (uint16_t)cpu.regFile[2];
					if (programMode || !bootMode || offset + count > bootWords ||
					    dst < 0x100 || dst + count > 0xf000) {
						fprintf(stderr, "Invalid boot disk read\n"); machineFault();
					}
					for (unsigned i = 0; i < count; ++i)
						ram.memory[dst + i] = (int16_t)bootDisk[offset + i];
					cpu.regFile[0] = (int16_t)count;
					break;
				}
				case 5:
					cpu.regFile[0] = (int16_t)getchar();
					break;
				case 6:
					cpu.regFile[0] = (int16_t)runProgram((uint16_t)cpu.regFile[0]);
					break;
				case 7:
					if(programMode)machineFault();
					cpu.regFile[0]=(int16_t)snapshotService((uint16_t)cpu.regFile[0],cpu.regFile[1]);
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
		cpu.regFile[dest] = ram.mdr;
		updateCC(cpu.regFile[dest]);
	} else if (opcode == ST || opcode == STI || opcode == STR) {
		if (programMode && (ram.mar < 0x9000 || ram.mar >= 0xe000)) machineFault();
		ram.memory[ram.mar] = cpu.regFile[dest];
	} else if (opcode == ADD || opcode == AND || opcode == NOT) {
		cpu.regFile[dest] = cpu.alu.accumulator;
		updateCC(cpu.regFile[dest]);
	} else if (opcode == LEA) {
		cpu.regFile[dest] = ram.mar;
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

int assemble() {
	memset(ram.memory, 0, sizeof(ram.memory));

	FILE* file;
	char fileName[256];

	setbuf(stdout, NULL);
	printf("Enter source assembly file name: \n");
	if (!fgets(fileName, 256, stdin)) return -8;
	fileName[strcspn(fileName, "\n")] = '\0';
	file = fopen(fileName, "r");
	if (!file) {
		printf("Source file doesn't exist!\n");
		return -1;
	}

	int instructionCount = 0;
	int lineCount = 0;
	char line[256];
	while (fgets(line, 256, file)) {
		uint16_t instruction = 0;
		size_t n = strspn(line, " \t\r");
		char* lineTrimmed = line + n;

		if (lineTrimmed[0] == '\0' || lineTrimmed[0] == ';') {
			lineCount++;
			continue;
		}

		char* tokens[4] = { NULL, NULL, NULL, NULL };
		int tokenCount = 0;
		char* token = strtok(lineTrimmed, " \t\n");
		while (token != NULL) {
			if (tokenCount == 4) {
				printf("Too many tokens on line %d!\n", lineCount);
				return -2;
			}

			tokens[tokenCount] = token;
			tokenCount++;
			token = strtok(NULL, " \t\n");
		}

		if (tokenCount == 0) {
			lineCount++;
			continue;
		}

		if (strcmp(tokens[0], ".fill") == 0) {
			if (tokenCount != 2) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			if (sscanf(tokens[1], "%hx", &instruction) != 1) {
				printf("Wrong token on line %d!\n", lineCount);
				return -3;
			}

		} else if (strcmp(tokens[0], ".stringz") == 0) {
			if (tokenCount != 2) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			for (size_t i = 0; i < strlen(tokens[1]); i++) {
				ram.memory[instructionCount] = tokens[1][i] & 0x00FF;
				instructionCount++;
			}
			
			ram.memory[instructionCount] = 0;
			instructionCount++;
			if (instructionCount > 0xFFFF) {
				printf("Too many instructions!\n");
				return -7;
			}

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
					int imm = atoi(tokens[3]);
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
					int imm = atoi(tokens[2]);
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
					int imm = atoi(tokens[3]);
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
					int imm = atoi(tokens[2]);
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

			int r0 = parseReg(tokens[1]);
			int r1 = parseReg(tokens[2]);
			if (r0 < 0) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			if (r1 < 0) { //imm
				instruction += (1 << 8);
			} else { //normal
				instruction += (r1 << 5);
			}

		} else if (strcmp(tokens[0], "ld") == 0) {
			instruction += (4 << 12);
			if (tokenCount != 3) {
				printf("Wrong amount of tokens on line %d!\n", lineCount);
				return -4;
			}

			int r0 = parseReg(tokens[1]);
			if (r0 == -1) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			int imm = atoi(tokens[2]);
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
			if (r0 == -1) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			int imm = atoi(tokens[2]);
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
			if (r0 == -1 || r1 == -1) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			instruction += (r1 << 6);
			int imm = atoi(tokens[3]);
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
			if (r0 == -1) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			int imm = atoi(tokens[2]);
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
			if (r0 == -1) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			int imm = atoi(tokens[2]);
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
			if (r0 == -1 || r1 == -1) {
				printf("Wrong tokens on line %d!\n", lineCount);
				return -3;
			}

			instruction += (r0 << 9);
			instruction += (r1 << 6);
			int imm = atoi(tokens[3]);
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
				}
			}

			instruction += ((conditionCodes & 0b111) << 9);

			int imm = atoi(tokens[2]);
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

			int imm = atoi(tokens[1]);
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
			int imm = atoi(tokens[2]);
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

			int vector = atoi(tokens[1]);
			if (vector < 0 || vector > 15) {
				printf("Bad trap vector on line %d!\n", lineCount);
				return -5;
			}

			instruction += ((vector & 0xF) << 8);

		} else {
			printf("Unkown opcode on line %d!\n", lineCount);
			return -6;
		}

		ram.memory[instructionCount] = instruction;
		instructionCount++;
		if (instructionCount > 0xFFFF) {
			printf("Too many instructions!\n");
			return -7;
		}

		lineCount++;
	}

	printf("Starting execution!\n");
	fclose(file);
	return 0;
}

static void bootImage(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); exit(1); }
    char magic[4];
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, "B16K", 4)) {
        fprintf(stderr, "Not a bob16 kernel image\n"); exit(1);
    }
    unsigned version = readWord(file), origin = readWord(file);
    unsigned entry = readWord(file), words = readWord(file), checksum = readWord(file);
    if (version != 1 || !words || origin < 0x100 || origin + words > 0x9000 ||
        entry < origin || entry >= origin + words) {
        fprintf(stderr, "Invalid bob16 kernel header\n"); exit(1);
    }
    unsigned sum = 0;
    for (unsigned i = 0; i < words; ++i) { bootDisk[i] = readWord(file); sum += bootDisk[i]; }
    if ((sum & 0xffff) != checksum || fgetc(file) != EOF) {
        fprintf(stderr, "Invalid kernel checksum or length\n"); exit(1);
    }
    fclose(file);
    bootWords = (uint16_t)words;
    bootOrigin = (uint16_t)origin;
    bootMode = true;
    memset(&cpu, 0, sizeof(cpu));
    memset(&ram, 0, sizeof(ram));
    /* Real guest instructions: load disk parameters, read kernel, jump to entry. */
    uint16_t rom[] = {0x4007, 0x4207, 0x4407, 0xf400, 0x4606, 0xb600,
                      0xf000, 0, (uint16_t)origin, 0, (uint16_t)words, (uint16_t)entry};
    for (unsigned i = 0; i < sizeof(rom) / sizeof(rom[0]); ++i) ram.memory[i] = (int16_t)rom[i];
    updateCC(0);
}

int main(int argc, char** argv) {
#ifdef _WIN32
	HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode;
	if (GetConsoleMode(console, &mode)) SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
	unsigned long maxCycles = 0;
	if (argc == 2 && strcmp(argv[1], "--os") == 0) {
		bootImage("build/kernel.b16");
	} else if (argc >= 3 && strcmp(argv[1], "--boot") == 0) {
		if (argc == 5 && strcmp(argv[3], "--max-cycles") == 0) maxCycles = strtoul(argv[4], NULL, 10);
		else if (argc != 3) { fprintf(stderr, "Usage: %s --boot kernel.b16 [--max-cycles N]\n", argv[0]); return 1; }
		bootImage(argv[2]);
	} else {
	if (argc != 1) { fprintf(stderr, "Usage: %s [--os | --boot kernel.b16 [--max-cycles N]]\n", argv[0]); return 1; }
	int assembleReturn = -1;
	while (assembleReturn < 0) {
		assembleReturn = assemble();
		if (assembleReturn == -8) return 1;
	}
	}

	uint16_t identityWords[0x8d00];
	for(unsigned i=0;i<0x8d00;i++) {
		unsigned address=0x300+i;
		identityWords[i]=bootMode?(address>=bootOrigin && address<bootOrigin+bootWords?bootDisk[address-bootOrigin]:0):(uint16_t)ram.memory[address];
	}
	kernelIdentity=snapshotHash(identityWords,0x8d00);
	unsigned long cycles = 0;
	while (true) {
		if (maxCycles && cycles++ >= maxCycles) { fprintf(stderr, "CPU cycle limit reached\n"); return 2; }
		cpuCycle();
	}

	return 0;
}
