#ifndef SISA_H
#define SISA_H

#include <stdint.h>
#include <stddef.h>

#define SISA_MEMORY_SIZE     (1 << 16)
#define SISA_PAGE_SHIFT      12
#define SISA_PAGE_SIZE       (1 << SISA_PAGE_SHIFT)
#define SISA_CODE_LOAD_ADDR  0xC000
#define SISA_DATA_LOAD_ADDR  0x8000
#define SISA_VGA_START_ADDR  0xA000
#define SISA_NUM_TLB_ENTRIES 8
#define SISA_NUM_IO_PORTS    256
#define SISA_CPU_CLK_FREQ    6250000
#define SISA_TIMER_FREQ      20
#define SISA_NUM_KEYS        4
#define SISA_NUM_SWITCHES    10

enum sisa_opcode {
	SISA_OPCODE_ARIT_LOGIC    = 0b0000,
	SISA_OPCODE_COMPARE       = 0b0001,
	SISA_OPCODE_ADDI          = 0b0010,
	SISA_OPCODE_LOAD          = 0b0011,
	SISA_OPCODE_STORE         = 0b0100,
	SISA_OPCODE_MOV           = 0b0101,
	SISA_OPCODE_RELATIVE_JUMP = 0b0110,
	SISA_OPCODE_IN_OUT        = 0b0111,
	SISA_OPCODE_MULT_DIV      = 0b1000,
	SISA_OPCODE_FLOAT_OP      = 0b1001,
	SISA_OPCODE_ABSOLUTE_JUMP = 0b1010,
	SISA_OPCODE_LOAD_F        = 0b1011,
	SISA_OPCODE_STORE_F       = 0b1100,
	SISA_OPCODE_LOAD_BYTE     = 0b1101,
	SISA_OPCODE_STORE_BYTE    = 0b1110,
	SISA_OPCODE_SPECIAL       = 0b1111,
};

enum sisa_instr_arit_logic_func {
	SISA_INSTR_ARIT_LOGIC_F_AND = 0b000,
	SISA_INSTR_ARIT_LOGIC_F_OR  = 0b001,
	SISA_INSTR_ARIT_LOGIC_F_XOR = 0b010,
	SISA_INSTR_ARIT_LOGIC_F_NOT = 0b011,
	SISA_INSTR_ARIT_LOGIC_F_ADD = 0b100,
	SISA_INSTR_ARIT_LOGIC_F_SUB = 0b101,
	SISA_INSTR_ARIT_LOGIC_F_SHA = 0b110,
	SISA_INSTR_ARIT_LOGIC_F_SHL = 0b111,
};

enum sisa_instr_compare_func {
	SISA_INSTR_COMPARE_F_CMPLT  = 0b000,
	SISA_INSTR_COMPARE_F_CMPLE  = 0b001,
	SISA_INSTR_COMPARE_F_CMPEQ  = 0b011,
	SISA_INSTR_COMPARE_F_CMPLTU = 0b100,
	SISA_INSTR_COMPARE_F_CMPLEU = 0b101,
};

enum sisa_instr_mov_func {
	SISA_INSTR_MOV_F_MOVI  = 0b0,
	SISA_INSTR_MOV_F_MOVHI = 0b1,
};

enum sisa_instr_relative_jump_func {
	SISA_INSTR_RELATIVE_JUMP_F_BZ  = 0b0,
	SISA_INSTR_RELATIVE_JUMP_F_BNZ = 0b1,
};

enum sisa_instr_in_out_func {
	SISA_INSTR_IN_OUT_F_IN  = 0b0,
	SISA_INSTR_IN_OUT_F_OUT = 0b1,
};

enum sisa_instr_mult_div_func {
	SISA_INSTR_MULT_DIV_F_MUL   = 0b000,
	SISA_INSTR_MULT_DIV_F_MULH  = 0b001,
	SISA_INSTR_MULT_DIV_F_MULHU = 0b010,
	SISA_INSTR_MULT_DIV_F_DIV   = 0b100,
	SISA_INSTR_MULT_DIV_F_DIVU  = 0b101,
};

enum sisa_instr_absolute_jump_func {
	SISA_INSTR_ABSOLUTE_JUMP_F_JZ    = 0b000,
	SISA_INSTR_ABSOLUTE_JUMP_F_JNZ   = 0b001,
	SISA_INSTR_ABSOLUTE_JUMP_F_JMP   = 0b011,
	SISA_INSTR_ABSOLUTE_JUMP_F_JAL   = 0b100,
	SISA_INSTR_ABSOLUTE_JUMP_F_CALLS = 0b111,
};

enum sisa_instr_special_func {
	SISA_INSTR_SPECIAL_F_EI     = 0b100000,
	SISA_INSTR_SPECIAL_F_DI     = 0b100001,
	SISA_INSTR_SPECIAL_F_RETI   = 0b100100,
	SISA_INSTR_SPECIAL_F_GETIID = 0b101000,
	SISA_INSTR_SPECIAL_F_RDS    = 0b101100,
	SISA_INSTR_SPECIAL_F_WRS    = 0b110000,
	SISA_INSTR_SPECIAL_F_WRPI   = 0b110100,
	SISA_INSTR_SPECIAL_F_WRVI   = 0b110101,
	SISA_INSTR_SPECIAL_F_WRPD   = 0b110110,
	SISA_INSTR_SPECIAL_F_WRVD   = 0b110111,
	SISA_INSTR_SPECIAL_F_FLUSH  = 0b111000,
	SISA_INSTR_SPECIAL_F_HALT   = 0b111111,
};

enum sisa_exception {
	SISA_EXCEPTION_ILLEGAL_INSTR    = 0x0,
	SISA_EXCEPTION_UNALIGNED_ACCESS = 0x1,
	SISA_EXCEPTION_DIVISION_BY_ZERO = 0x4,
	SISA_EXCEPTION_ITLB_MISS        = 0x6,
	SISA_EXCEPTION_DTLB_MISS        = 0x7,
	SISA_EXCEPTION_ITLB_INVALID     = 0x8,
	SISA_EXCEPTION_DTLB_INVALID     = 0x9,
	SISA_EXCEPTION_ITLB_PROTECTED   = 0xA,
	SISA_EXCEPTION_DTLB_PROTECTED   = 0xB,
	SISA_EXCEPTION_DTLB_READONLY    = 0xC,
	SISA_EXCEPTION_PROTECTED_INSTR  = 0xD,
	SISA_EXCEPTION_CALLS            = 0xE,
	SISA_EXCEPTION_INTERRUPT        = 0xF,
};

enum sisa_interrupt {
	SISA_INTERRUPT_TIMER    = 0x0,
	SISA_INTERRUPT_KEY      = 0x1,
	SISA_INTERRUPT_SWITCH   = 0x2,
	SISA_INTERRUPT_KEYBOARD = 0x3,
};

enum sisa_io_port {
	SISA_IO_PORT_LEDS_GREEN     = 5,
	SISA_IO_PORT_LEDS_RED       = 6,
	SISA_IO_PORT_KEYS           = 7,
	SISA_IO_PORT_SWITCHES       = 8,
	SISA_IO_PORT_VGA_CURSOR     = 11,
	SISA_IO_PORT_VGA_CURSOR_EN  = 12,
	SISA_IO_PORT_KB_READ_CHAR   = 15,
	SISA_IO_PORT_KB_DATA_READY  = 16,
	SISA_IO_PORT_KB_CLEAR_CHAR  = 16,
	SISA_IO_PORT_CYCLES         = 20,
	SISA_IO_PORT_MILLIS_COUNTER = 21,
};

enum sisa_cpu_mode {
	SISA_CPU_MODE_USER   = 0,
	SISA_CPU_MODE_SYSTEM = 1,
};

enum sisa_cpu_status {
	SISA_CPU_STATUS_FETCH,
	SISA_CPU_STATUS_DEMW,
	SISA_CPU_STATUS_SYSTEM,
	SISA_CPU_STATUS_NOP,
};

struct sisa_tlb {
	struct {
		uint16_t vpn : 4;
		uint16_t pfn : 4;
		uint16_t r   : 1;
		uint16_t v   : 1;
		uint16_t p   : 1;
	} entries[SISA_NUM_TLB_ENTRIES];
};

struct sisa_cpu {
	struct {
		union {
			struct {
				uint16_t r0;
				uint16_t r1;
				uint16_t r2;
				uint16_t r3;
				uint16_t r4;
				uint16_t r5;
				uint16_t r6;
				uint16_t r7;
			};
			uint16_t regs[8];
		} general;
		union {
			struct {
				uint16_t s0;
				uint16_t s1;
				uint16_t s2;
				uint16_t s3;
				uint16_t s4;
				uint16_t s5;
				uint16_t s6;
				union {
					uint16_t s7;
					struct {
						uint16_t m: 1;
						uint16_t i: 1;
					} psw;
				};
			};
			uint16_t regs[8];
		} system;
	} regfile;
	uint16_t pc;
	uint16_t ir;
	enum sisa_cpu_status status;
	enum sisa_exception exception;
	int exc_happened;
	uint16_t ints_pending;
	uint8_t kb_key_buffer;
	int halted;
	uint64_t cycles;
};

struct sisa_context {
	struct sisa_cpu cpu;
	uint8_t memory[SISA_MEMORY_SIZE];
	uint16_t io_ports[SISA_NUM_IO_PORTS];
	struct sisa_tlb itlb;
	struct sisa_tlb dtlb;
	int tlb_enabled;
	uint16_t *breakpoint_list;
	unsigned int breakpoint_num;
};

void sisa_init(struct sisa_context *sisa);
void sisa_destroy(struct sisa_context *sisa);
void sisa_step_cycle(struct sisa_context *sisa);
void sisa_load_binary(struct sisa_context *sisa, uint16_t address, void *data, size_t size);

int sisa_cpu_is_halted(const struct sisa_context *sisa);
int sisa_breakpoint_reached(const struct sisa_context *sisa);
void sisa_add_breakpoint(struct sisa_context *sisa, uint16_t addr);
void sisa_set_pc(struct sisa_context *sisa, uint16_t pc);
void sisa_tlb_set_enabled(struct sisa_context *sisa, int enabled);
int sisa_tlb_is_enabled(const struct sisa_context *sisa);
void sisa_keys_set(struct sisa_context *sisa, uint8_t keys);
void sisa_switches_set(struct sisa_context *sisa, uint16_t switches);
void sisa_keyboard_press(struct sisa_context *sisa, uint8_t key);

void sisa_print_dump(const struct sisa_context *sisa);
void sisa_print_tlb_dump(const struct sisa_context *sisa);
void sisa_print_vga_dump(const struct sisa_context *sisa);

#endif
