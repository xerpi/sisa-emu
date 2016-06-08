#ifndef SISA_H
#define SISA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SISA_MEMORY_SIZE     (1 << 16)
#define SISA_PAGE_SHIFT      12
#define SISA_PAGE_SIZE       (1 << SISA_PAGE_SHIFT)
#define SISA_CODE_LOAD_ADDR  0xC000
#define SISA_DATA_LOAD_ADDR  0x8000
#define SISA_VGA_START_ADDR  0xA000
#define SISA_NUM_TLB_ENTRIES 8

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

enum sisa_instr_mov_func {
	SISA_INSTR_ARIT_LOGIC_F_MOVI  = 0b0,
	SISA_INSTR_ARIT_LOGIC_F_MOVHI = 0b1,
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
				uint16_t s7;
			};
			uint16_t regs[8];
		} system;
	} regfile;
	uint16_t pc;
	uint16_t ir;
	enum sisa_cpu_status status;
	enum sisa_exception exception;
	bool exc_happened;
};

struct sisa_context {
	struct sisa_cpu cpu;
	uint8_t memory[SISA_MEMORY_SIZE];
	struct sisa_tlb itlb;
	struct sisa_tlb dtlb;
};

void sisa_init(struct sisa_context *sisa);
void sisa_step_cycle(struct sisa_context *sisa);
void sisa_load_binary(struct sisa_context *sisa, uint16_t address, void *data, size_t size);
void sisa_print_dump(struct sisa_context *sisa);

#endif
