#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "sisa.h"

#define BIT(n)                (1 << (n))
#define X_DOWNTO_Y(val, x, y) (((val) >> (y)) & ((1 << ((x) - (y) + 1)) - 1))
#define SEXT_5(val)           (((-((val) >> 4)) & ~((1 << 5) - 1)) | (val))
#define SEXT_6(val)           (((-((val) >> 5)) & ~((1 << 6) - 1)) | (val))
#define SEXT_8(val)           (((-((val) >> 7)) & ~((1 << 8) - 1)) | (val))

#define INSTR_OPCODE(instr) X_DOWNTO_Y(instr, 15, 12)
#define INSTR_Rd(instr)     X_DOWNTO_Y(instr, 11, 9)
#define INSTR_Ra_9(instr)   X_DOWNTO_Y(instr, 11, 9)
#define INSTR_Ra_6(instr)   X_DOWNTO_Y(instr, 8, 6)
#define INSTR_Rb_9(instr)   X_DOWNTO_Y(instr, 11, 9)
#define INSTR_Rb_0(instr)   X_DOWNTO_Y(instr, 2, 0)
#define INSTR_Sd(instr)     X_DOWNTO_Y(instr, 11, 9)
#define INSTR_Sa(instr)     X_DOWNTO_Y(instr, 8, 6)
#define INSTR_IMM8(instr)   X_DOWNTO_Y(instr, 7, 0)

#define ARIT_LOGIC_F_BITS(instr)    X_DOWNTO_Y(instr, 5, 3)
#define COMPARE_F_BITS(instr)       X_DOWNTO_Y(instr, 5, 3)
#define MOV_F_BITS(instr)           X_DOWNTO_Y(instr, 8, 8)
#define RELATIVE_JUMP_F_BITS(instr) X_DOWNTO_Y(instr, 8, 8)
#define IN_OUT_F_BITS(instr)        X_DOWNTO_Y(instr, 8, 8)
#define MULT_DIV_F_BITS(instr)      X_DOWNTO_Y(instr, 5, 3)
#define ABSOLUTE_JUMP_F_BITS(instr) X_DOWNTO_Y(instr, 2, 0)
#define SPECIAL_F_BITS(instr)       X_DOWNTO_Y(instr, 5, 0)

#define REGS  (sisa->cpu.regfile.general.regs)
#define SREGS (sisa->cpu.regfile.system.regs)

static void sisa_tlb_init(struct sisa_tlb *tlb)
{
	int i;

	/* Setup user pages(0x0000 to 0x2FFF) */
	for (i = 0; i <= 2; i++) {
		tlb->entries[i].vpn = i;
		tlb->entries[i].pfn = i;
		tlb->entries[i].r = 0;
		tlb->entries[i].v = 1;
		tlb->entries[i].p = 0;
	}

	/* Setup kernel pages(0x8000 to 0x8FFF) */
	tlb->entries[i].vpn = 8;
	tlb->entries[i].pfn = 8;
	tlb->entries[i].r = 0;
	tlb->entries[i].v = 1;
	tlb->entries[i].p = 1;

	/* Setup kernel pages(0xC000 to 0xFFFF) */
	for (i = 0; i <= 3; i++) {
		tlb->entries[i + 4].vpn = i + 0xC;
		tlb->entries[i + 4].pfn = i + 0xC;
		tlb->entries[i + 4].r = 1;
		tlb->entries[i + 4].v = 1;
		tlb->entries[i + 4].p = 1;
	}
}

void sisa_init(struct sisa_context *sisa)
{
	int i;

	sisa->cpu.pc = SISA_CODE_LOAD_ADDR;
	sisa->cpu.regfile.system.psw.i = 0;
	sisa->cpu.regfile.system.psw.m = SISA_CPU_MODE_SYSTEM;
	sisa->cpu.status = SISA_CPU_STATUS_FETCH;
	sisa->cpu.exc_happened = 0;
	sisa->cpu.ints_pending = 0;
	sisa->cpu.halted = 0;
	sisa->cpu.cycles = 0;

	for (i = 0; i < SISA_NUM_IO_PORTS; i++)
		sisa->io_ports[i] = 0;

	sisa->io_ports[SISA_IO_PORT_KEYS] = 0xFFFF;

	sisa->cpu.kb_key_buffer = 0;
	sisa_tlb_init(&sisa->itlb);
	sisa_tlb_init(&sisa->dtlb);
	sisa->tlb_enabled = 1;

	sisa->breakpoint_list = NULL;
	sisa->breakpoint_num = 0;
}

void sisa_destroy(struct sisa_context *sisa)
{
	if (sisa->breakpoint_list) {
		free(sisa->breakpoint_list);
		sisa->breakpoint_list = NULL;
	}
}

static int sisa_tlb_access(struct sisa_context *sisa, const struct sisa_tlb *tlb,
			    uint16_t vaddr, uint16_t *paddr, int word_access, int write)
{
	int i;
	int found;
	uint8_t vpn, pfn, v, r, p;

	if (!sisa->tlb_enabled) {
		*paddr = vaddr;
		return 1;
	}

	if (word_access && vaddr & 1) {
		sisa->cpu.exception = SISA_EXCEPTION_UNALIGNED_ACCESS;
		sisa->cpu.exc_happened = 1;
		sisa->cpu.regfile.system.s3 = vaddr;
		return 0;
	}

	vpn = vaddr >> SISA_PAGE_SHIFT;

	found = 0;
	for (i = 0; i < SISA_NUM_TLB_ENTRIES; i++) {
		if (tlb->entries[i].vpn == vpn) {
			pfn = tlb->entries[i].pfn;
			v = tlb->entries[i].v;
			r = tlb->entries[i].r;
			p = tlb->entries[i].p;
			found = 1;
			break;
		}
	}

	if (!found) {
		if (&sisa->itlb == tlb)
			sisa->cpu.exception = SISA_EXCEPTION_ITLB_MISS;
		else
			sisa->cpu.exception = SISA_EXCEPTION_DTLB_MISS;
		sisa->cpu.exc_happened = 1;
		sisa->cpu.regfile.system.s3 = vaddr;
		return 0;
	} else if (!v) {
		if (&sisa->itlb == tlb)
			sisa->cpu.exception = SISA_EXCEPTION_ITLB_INVALID;
		else
			sisa->cpu.exception = SISA_EXCEPTION_DTLB_INVALID;
		sisa->cpu.exc_happened = 1;
		sisa->cpu.regfile.system.s3 = vaddr;
		return 0;
	} else if (p && sisa->cpu.regfile.system.psw.m == SISA_CPU_MODE_USER) {
		if (&sisa->itlb == tlb)
			sisa->cpu.exception = SISA_EXCEPTION_ITLB_PROTECTED;
		else
			sisa->cpu.exception = SISA_EXCEPTION_DTLB_PROTECTED;
		sisa->cpu.exc_happened = 1;
		sisa->cpu.regfile.system.s3 = vaddr;
		return 0;
	} else if (r && write) {
		sisa->cpu.exception = SISA_EXCEPTION_DTLB_READONLY;
		sisa->cpu.exc_happened = 1;
		sisa->cpu.regfile.system.s3 = vaddr;
		return 0;
	}

	*paddr = (pfn << SISA_PAGE_SHIFT) | (vaddr & (SISA_PAGE_SIZE - 1));

	return 1;
}

static void sisa_demw_execute(struct sisa_context *sisa)
{
	const uint16_t instr = sisa->cpu.ir;

	switch (INSTR_OPCODE(instr)) {
	case SISA_OPCODE_ARIT_LOGIC:
		switch (ARIT_LOGIC_F_BITS(instr)) {
		case SISA_INSTR_ARIT_LOGIC_F_AND:
			REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] & REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_ARIT_LOGIC_F_OR:
			REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] | REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_ARIT_LOGIC_F_XOR:
			REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] ^ REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_ARIT_LOGIC_F_NOT:
			REGS[INSTR_Rd(instr)] = ~REGS[INSTR_Ra_6(instr)];
			break;
		case SISA_INSTR_ARIT_LOGIC_F_ADD:
			REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] + REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_ARIT_LOGIC_F_SUB:
			REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] - REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_ARIT_LOGIC_F_SHA: {
			int shift = SEXT_5(X_DOWNTO_Y(REGS[INSTR_Rb_0(instr)], 4, 0));
			if (shift > 0) {
				REGS[INSTR_Rd(instr)] = (int16_t)REGS[INSTR_Ra_6(instr)] << shift;
			} else {
				REGS[INSTR_Rd(instr)] = (int16_t)REGS[INSTR_Ra_6(instr)] >> -shift;
			}
			break;
		}
		case SISA_INSTR_ARIT_LOGIC_F_SHL: {
			int shift = SEXT_5(X_DOWNTO_Y(REGS[INSTR_Rb_0(instr)], 4, 0));
			if (shift > 0) {
				REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] << shift;
			} else {
				REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] >> -shift;
			}
			break;
		}
		}
		break;
	case SISA_OPCODE_COMPARE:
		switch (COMPARE_F_BITS(instr)) {
		case SISA_INSTR_COMPARE_F_CMPLT:
			REGS[INSTR_Rd(instr)] = (int16_t)REGS[INSTR_Ra_6(instr)] < (int16_t)REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_COMPARE_F_CMPLE:
			REGS[INSTR_Rd(instr)] = (int16_t)REGS[INSTR_Ra_6(instr)] <= (int16_t)REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_COMPARE_F_CMPEQ:
			REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] == REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_COMPARE_F_CMPLTU:
			REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] < REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_COMPARE_F_CMPLEU:
			REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] <= REGS[INSTR_Rb_0(instr)];
			break;
		default:
			sisa->cpu.exception = SISA_EXCEPTION_ILLEGAL_INSTR;
			sisa->cpu.exc_happened = 1;
			break;
		}
		break;
	case SISA_OPCODE_ADDI:
		REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] + SEXT_6(X_DOWNTO_Y(instr, 5, 0));
		break;
	case SISA_OPCODE_LOAD: {
		uint16_t paddr;
		uint16_t vaddr = REGS[INSTR_Ra_6(instr)] + (SEXT_6(X_DOWNTO_Y(instr, 5, 0)) << 1);

		if (!sisa_tlb_access(sisa, &sisa->dtlb, vaddr, &paddr, 1, 0)) {
			break;
		}

		REGS[INSTR_Rd(instr)] = sisa->memory[paddr + 1] << 8 |  sisa->memory[paddr];
		break;
	}
	case SISA_OPCODE_STORE: {
		uint16_t paddr;
		uint16_t vaddr = REGS[INSTR_Ra_6(instr)] + (SEXT_6(X_DOWNTO_Y(instr, 5, 0)) << 1);

		if (!sisa_tlb_access(sisa, &sisa->dtlb, vaddr, &paddr, 1, 1)) {
			break;
		}

		sisa->memory[paddr] = REGS[INSTR_Rb_9(instr)] & 0xFF;
		sisa->memory[paddr + 1] = REGS[INSTR_Rb_9(instr)] >> 8;
		break;
	}
	case SISA_OPCODE_MOV:
		switch (MOV_F_BITS(instr)) {
		case SISA_INSTR_MOV_F_MOVI:
			REGS[INSTR_Rd(instr)] = SEXT_8(INSTR_IMM8(instr));
			break;
		case SISA_INSTR_MOV_F_MOVHI:
			REGS[INSTR_Rd(instr)] = (INSTR_IMM8(instr) << 8) | (REGS[INSTR_Ra_9(instr)] & 0xFF);
			break;
		}
		break;
	case SISA_OPCODE_RELATIVE_JUMP:
		switch (RELATIVE_JUMP_F_BITS(instr)) {
		case SISA_INSTR_RELATIVE_JUMP_F_BZ:
			if (REGS[INSTR_Rb_9(instr)] == 0) {
				sisa->cpu.pc += (int8_t)INSTR_IMM8(instr) << 1;
			}
			break;
		case SISA_INSTR_RELATIVE_JUMP_F_BNZ:
			if (REGS[INSTR_Rb_9(instr)] != 0) {
				sisa->cpu.pc += (int8_t)INSTR_IMM8(instr) << 1;
			}
			break;
		}

		break;
	case SISA_OPCODE_IN_OUT:
		switch (IN_OUT_F_BITS(instr)) {
		case SISA_INSTR_IN_OUT_F_IN:
			REGS[INSTR_Rd(instr)] = sisa->io_ports[INSTR_IMM8(instr)];
			break;
		case SISA_INSTR_IN_OUT_F_OUT: {
			uint8_t port = INSTR_IMM8(instr);
			sisa->io_ports[port] = REGS[INSTR_Rb_9(instr)];

			/* If there's a pending key in the kb buffer, copy it to the I/O port */
			if (port == SISA_IO_PORT_KB_CLEAR_CHAR && sisa->cpu.kb_key_buffer) {
				sisa->io_ports[SISA_IO_PORT_KB_READ_CHAR] = sisa->cpu.kb_key_buffer;
				sisa->io_ports[SISA_IO_PORT_KB_DATA_READY] = 1;
				sisa->cpu.ints_pending |= BIT(SISA_INTERRUPT_KEYBOARD);
				sisa->cpu.kb_key_buffer = 0;
			} else {
				sisa->io_ports[SISA_IO_PORT_KB_READ_CHAR] = 0;
				sisa->io_ports[SISA_IO_PORT_KB_DATA_READY] = 0;
			}

			break;
		}
		}
		break;
	case SISA_OPCODE_MULT_DIV:
		switch (MULT_DIV_F_BITS(instr)) {
		case SISA_INSTR_MULT_DIV_F_MUL:
			REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] * REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_MULT_DIV_F_MULH:
			REGS[INSTR_Rd(instr)] = ((int32_t)REGS[INSTR_Ra_6(instr)] * (int32_t)REGS[INSTR_Rb_0(instr)]) >> 16;
			break;
		case SISA_INSTR_MULT_DIV_F_MULHU:
			REGS[INSTR_Rd(instr)] = ((uint32_t)REGS[INSTR_Ra_6(instr)] * (uint32_t)REGS[INSTR_Rb_0(instr)]) >> 16;
			break;
		case SISA_INSTR_MULT_DIV_F_DIV:
			if (REGS[INSTR_Rb_0(instr)] == 0) {
				sisa->cpu.exception = SISA_EXCEPTION_DIVISION_BY_ZERO;
				sisa->cpu.exc_happened = 1;
				break;
			}
			REGS[INSTR_Rd(instr)] = (int16_t)REGS[INSTR_Ra_6(instr)] / (int16_t)REGS[INSTR_Rb_0(instr)];
			break;
		case SISA_INSTR_MULT_DIV_F_DIVU:
			if (REGS[INSTR_Rb_0(instr)] == 0) {
				sisa->cpu.exception = SISA_EXCEPTION_DIVISION_BY_ZERO;
				sisa->cpu.exc_happened = 1;
				break;
			}
			REGS[INSTR_Rd(instr)] = REGS[INSTR_Ra_6(instr)] / REGS[INSTR_Rb_0(instr)];
			break;
		default:
			sisa->cpu.exception = SISA_EXCEPTION_ILLEGAL_INSTR;
			sisa->cpu.exc_happened = 1;
			break;
		}
		break;
	case SISA_OPCODE_ABSOLUTE_JUMP:
		switch (ABSOLUTE_JUMP_F_BITS(instr)) {
		case SISA_INSTR_ABSOLUTE_JUMP_F_JZ:
			if (REGS[INSTR_Rb_9(instr)] == 0) {
				sisa->cpu.pc = REGS[INSTR_Ra_6(instr)] - 2;
			}
			break;
		case SISA_INSTR_ABSOLUTE_JUMP_F_JNZ:
			if (REGS[INSTR_Rb_9(instr)] != 0) {
				sisa->cpu.pc = REGS[INSTR_Ra_6(instr)] - 2;
			}
			break;
		case SISA_INSTR_ABSOLUTE_JUMP_F_JMP:
			sisa->cpu.pc = REGS[INSTR_Ra_6(instr)] - 2;
			break;
		case SISA_INSTR_ABSOLUTE_JUMP_F_JAL: {
			uint16_t pc = sisa->cpu.pc;
			sisa->cpu.pc = REGS[INSTR_Ra_6(instr)] - 2;
			REGS[INSTR_Rd(instr)] = pc + 2;
			break;
		}
		case SISA_INSTR_ABSOLUTE_JUMP_F_CALLS:
			sisa->cpu.regfile.system.s3 = REGS[INSTR_Ra_6(instr)];
			sisa->cpu.exception = SISA_EXCEPTION_CALLS;
			sisa->cpu.exc_happened = 1;
			break;
		default:
			sisa->cpu.exception = SISA_EXCEPTION_ILLEGAL_INSTR;
			sisa->cpu.exc_happened = 1;
			break;
		}
		break;
	case SISA_OPCODE_LOAD_BYTE: {
		uint16_t paddr;
		uint16_t vaddr = REGS[INSTR_Ra_6(instr)] + SEXT_6(X_DOWNTO_Y(instr, 5, 0));

		if (!sisa_tlb_access(sisa, &sisa->dtlb, vaddr, &paddr, 0, 0)) {
			break;
		}

		REGS[INSTR_Rd(instr)] = SEXT_8(sisa->memory[paddr]);
		break;
	}
	case SISA_OPCODE_STORE_BYTE: {
		uint16_t paddr;
		uint16_t vaddr = REGS[INSTR_Ra_6(instr)] + SEXT_6(X_DOWNTO_Y(instr, 5, 0));

		if (!sisa_tlb_access(sisa, &sisa->dtlb, vaddr, &paddr, 0, 1)) {
			break;
		}

		sisa->memory[paddr] = REGS[INSTR_Rb_9(instr)] & 0xFF;
		break;
	}
	case SISA_OPCODE_SPECIAL:
		switch (SPECIAL_F_BITS(instr)) {
		case SISA_INSTR_SPECIAL_F_EI:
			sisa->cpu.regfile.system.psw.i = 1;
			break;
		case SISA_INSTR_SPECIAL_F_DI:
			sisa->cpu.regfile.system.psw.i = 0;
			break;
		case SISA_INSTR_SPECIAL_F_RETI:
			sisa->cpu.regfile.system.s7 = sisa->cpu.regfile.system.s0;
			sisa->cpu.pc = sisa->cpu.regfile.system.s1 - 2;
			break;
		case SISA_INSTR_SPECIAL_F_GETIID: {
			int lsb = ffs(sisa->cpu.ints_pending);

			/* Clear the highest priority interrupt and return its id */
			if (lsb) {
				sisa->cpu.ints_pending &= ~BIT(lsb - 1);
				REGS[INSTR_Rd(instr)] = lsb - 1;
			} else {
				REGS[INSTR_Rd(instr)] = 0;
			}
			break;
		}
		case SISA_INSTR_SPECIAL_F_RDS:
			REGS[INSTR_Rd(instr)] = SREGS[INSTR_Sa(instr)];
			break;
		case SISA_INSTR_SPECIAL_F_WRS:
			SREGS[INSTR_Sd(instr)] = REGS[INSTR_Ra_6(instr)];
			break;
		case SISA_INSTR_SPECIAL_F_WRPI: {
			uint8_t entry = REGS[INSTR_Ra_6(instr)];
			uint16_t value = REGS[INSTR_Rb_9(instr)];
			sisa->itlb.entries[entry].pfn = X_DOWNTO_Y(value, 3, 0);
			sisa->itlb.entries[entry].r = X_DOWNTO_Y(value, 4, 4);
			sisa->itlb.entries[entry].v = X_DOWNTO_Y(value, 5, 5);
			sisa->itlb.entries[entry].p = X_DOWNTO_Y(value, 6, 6);
			break;
		}
		case SISA_INSTR_SPECIAL_F_WRVI: {
			uint8_t entry = REGS[INSTR_Ra_6(instr)];
			uint16_t value = REGS[INSTR_Rb_9(instr)];
			sisa->itlb.entries[entry].vpn = X_DOWNTO_Y(value, 3, 0);
			break;
		}
		case SISA_INSTR_SPECIAL_F_WRPD: {
			uint8_t entry = REGS[INSTR_Ra_6(instr)];
			uint16_t value = REGS[INSTR_Rb_9(instr)];
			sisa->dtlb.entries[entry].pfn = X_DOWNTO_Y(value, 3, 0);
			sisa->dtlb.entries[entry].r = X_DOWNTO_Y(value, 4, 4);
			sisa->dtlb.entries[entry].v = X_DOWNTO_Y(value, 5, 5);
			sisa->dtlb.entries[entry].p = X_DOWNTO_Y(value, 6, 6);
			break;
		}
		case SISA_INSTR_SPECIAL_F_WRVD: {
			uint8_t entry = REGS[INSTR_Ra_6(instr)];
			uint16_t value = REGS[INSTR_Rb_9(instr)];
			sisa->dtlb.entries[entry].vpn = X_DOWNTO_Y(value, 3, 0);
			break;
		}
		case SISA_INSTR_SPECIAL_F_FLUSH:
			break;
		case SISA_INSTR_SPECIAL_F_HALT:
			sisa->cpu.halted = 1;
			break;
		}
		break;
	default:
		sisa->cpu.exception = SISA_EXCEPTION_ILLEGAL_INSTR;
		sisa->cpu.exc_happened = 1;
		break;
	}
}

void sisa_step_cycle(struct sisa_context *sisa)
{
	if (sisa->cpu.halted)
		return;

	switch (sisa->cpu.status) {
	case SISA_CPU_STATUS_FETCH: {
		uint16_t paddr;

		if (!sisa_tlb_access(sisa, &sisa->itlb, sisa->cpu.pc, &paddr, 1, 0)) {
			sisa->cpu.status = SISA_CPU_STATUS_NOP;
			break;
		}

		sisa->cpu.ir = sisa->memory[paddr + 1] << 8 | sisa->memory[paddr];
		sisa->cpu.status = SISA_CPU_STATUS_DEMW;
		break;
	}
	case SISA_CPU_STATUS_DEMW:
		sisa_demw_execute(sisa);
		sisa->cpu.pc += 2;
		if (sisa->cpu.exc_happened) {
			sisa->cpu.status = SISA_CPU_STATUS_SYSTEM;
			break;
		} else if (sisa->cpu.regfile.system.psw.i && sisa->cpu.ints_pending) {
			sisa->cpu.exception = SISA_EXCEPTION_INTERRUPT;
			sisa->cpu.exc_happened = 1;
			sisa->cpu.status = SISA_CPU_STATUS_SYSTEM;
			break;
		}
		sisa->cpu.status = SISA_CPU_STATUS_FETCH;
		break;
	case SISA_CPU_STATUS_NOP:
		sisa->cpu.status = SISA_CPU_STATUS_SYSTEM;
		break;
	case SISA_CPU_STATUS_SYSTEM:
		sisa->cpu.regfile.system.s0 = sisa->cpu.regfile.system.s7;
		sisa->cpu.regfile.system.s1 = sisa->cpu.pc;
		sisa->cpu.regfile.system.s2 = sisa->cpu.exception;
		sisa->cpu.pc = sisa->cpu.regfile.system.s5;
		sisa->cpu.regfile.system.psw.i = 0;
		sisa->cpu.regfile.system.psw.m = SISA_CPU_MODE_SYSTEM;
		sisa->cpu.status = SISA_CPU_STATUS_FETCH;
		/* Is this the best place to clear the exception flag? */
		sisa->cpu.exc_happened = 0;
		break;
	}

	sisa->cpu.cycles++;

	/* Timer interrupt generator */
	if (sisa->cpu.cycles % (SISA_CPU_CLK_FREQ / SISA_TIMER_FREQ) == 0) {
		sisa->cpu.ints_pending |= BIT(SISA_INTERRUPT_TIMER);
	}

	/* Update milliseconds counter */
	if (sisa->cpu.cycles % (SISA_CPU_CLK_FREQ / 1000) == 0 &&
	    sisa->io_ports[SISA_IO_PORT_MILLIS_COUNTER] > 0) {
		sisa->io_ports[SISA_IO_PORT_MILLIS_COUNTER]--;
	}

	/* Update pseudorandom number (cycles) */
	sisa->io_ports[SISA_IO_PORT_CYCLES] = (uint16_t)sisa->cpu.cycles;
}

void sisa_load_binary(struct sisa_context *sisa, uint16_t address, void *data, size_t size)
{
	memcpy(sisa->memory + address, data, size);
}

int sisa_cpu_is_halted(const struct sisa_context *sisa)
{
	return sisa->cpu.halted;
}

int sisa_breakpoint_reached(const struct sisa_context *sisa)
{
	int i;

	/* Only check for breakpoints when status is fetch */
	if (sisa->cpu.status != SISA_CPU_STATUS_FETCH)
		return 0;

	for (i = 0; i < sisa->breakpoint_num; i++) {
		if (sisa->cpu.pc == sisa->breakpoint_list[i])
			return 1;
	}

	return 0;
}

void sisa_add_breakpoint(struct sisa_context *sisa, uint16_t addr)
{
	sisa->breakpoint_list = realloc(sisa->breakpoint_list,
		sisa->breakpoint_num + 1);

	sisa->breakpoint_list[sisa->breakpoint_num] = addr;
	sisa->breakpoint_num++;
}

void sisa_set_pc(struct sisa_context *sisa, uint16_t pc)
{
	sisa->cpu.pc = pc;
}

void sisa_tlb_set_enabled(struct sisa_context *sisa, int enabled)
{
	sisa->tlb_enabled = enabled;
}

int sisa_tlb_is_enabled(const struct sisa_context *sisa)
{
	return sisa->tlb_enabled;
}

void sisa_keys_set(struct sisa_context *sisa, uint8_t keys)
{
	uint8_t current_keys = sisa->io_ports[SISA_IO_PORT_KEYS];

	if (current_keys ^ keys) {
		sisa->io_ports[SISA_IO_PORT_KEYS] = keys;
		sisa->cpu.ints_pending |= BIT(SISA_INTERRUPT_KEY);
	}
}

void sisa_switches_set(struct sisa_context *sisa, uint16_t switches)
{
	uint16_t current_switches = sisa->io_ports[SISA_IO_PORT_SWITCHES];

	if (current_switches ^ switches) {
		sisa->io_ports[SISA_IO_PORT_SWITCHES] = switches;
		sisa->cpu.ints_pending |= BIT(SISA_INTERRUPT_SWITCH);
	}
}

void sisa_key_toggle(struct sisa_context *sisa, uint8_t key_num)
{
	uint8_t current_keys;

	if (key_num >= SISA_NUM_KEYS)
		return;

	current_keys = sisa->io_ports[SISA_IO_PORT_KEYS];

	sisa_keys_set(sisa, current_keys ^ (1 << key_num));
}

void sisa_switch_toggle(struct sisa_context *sisa, uint8_t switch_num)
{
	uint16_t current_switches;

	if (switch_num >= SISA_NUM_SWITCHES)
		return;

	current_switches = sisa->io_ports[SISA_IO_PORT_SWITCHES];

	sisa_switches_set(sisa, current_switches ^ (1 << switch_num));
}

void sisa_keyboard_press(struct sisa_context *sisa, uint8_t key)
{
	if (sisa->io_ports[SISA_IO_PORT_KB_READ_CHAR]) {
		sisa->cpu.kb_key_buffer = key;
	} else {
		sisa->io_ports[SISA_IO_PORT_KB_READ_CHAR] = key;
		sisa->io_ports[SISA_IO_PORT_KB_DATA_READY] = 1;
		sisa->cpu.ints_pending |= BIT(SISA_INTERRUPT_KEYBOARD);
		sisa->cpu.kb_key_buffer = 0;
	}
}

void sisa_print_dump(const struct sisa_context *sisa)
{
	int i;

	static const char *status_str[] = {
		"fetch", "demw", "system", "nop"
	};

	printf("%s\n", status_str[sisa->cpu.status]);
	printf("  pc: 0x%04X\n", sisa->cpu.pc);
	printf("  ir: 0x%04X\n", sisa->cpu.ir);

	for (i = 0; i < 8; i++) {
		printf("  r%i: 0x%04X\n", i, sisa->cpu.regfile.general.regs[i]);
	}

	putchar('\n');
}

void sisa_print_tlb_dump(const struct sisa_context *sisa)
{
	int i;
	const struct sisa_tlb *itlb = &sisa->itlb;
	const struct sisa_tlb *dtlb = &sisa->dtlb;

	printf("ITLB:\n");

	for (i = 0; i < SISA_NUM_TLB_ENTRIES; i++) {
		printf("  [%i]: vpn: 0x%01X -> pfn: 0x%01X, {r: %d, v: %d, p: %d}\n",
			i, itlb->entries[i].vpn, itlb->entries[i].pfn,
			itlb->entries[i].r, itlb->entries[i].v,
			itlb->entries[i].p);
	}

	printf("DTLB:\n");

	for (i = 0; i < SISA_NUM_TLB_ENTRIES; i++) {
		printf("  [%i]: vpn: 0x%01X -> pfn: 0x%01X, {r: %d, v: %d, p: %d}\n",
			i, dtlb->entries[i].vpn, dtlb->entries[i].pfn,
			dtlb->entries[i].r, dtlb->entries[i].v,
			dtlb->entries[i].p);
	}

	putchar('\n');
}

void sisa_print_vga_dump(const struct sisa_context *sisa)
{
	int i, j;
	const int num_cols = 80;
	const int num_rows = 30;
	char c;

	for (i = 0; i < num_cols + 2; i++)
		putchar('-');

	putchar('\n');

	for (i = 0; i < num_rows; i++) {
		putchar('|');
		for (j = 0; j < num_cols; j++) {
			c = sisa->memory[SISA_VGA_START_ADDR + (i * num_cols + j) * 2];
			if (isgraph(c))
				putchar(c);
			else
				putchar(' ');
		}
		puts("|");
	}

	for (i = 0; i < num_cols + 2; i++)
		putchar('-');

	putchar('\n');
}

static void print_binary(uint32_t n, int num_bits)
{
	int i;

	for (i = 0; i < num_bits; i++) {
		printf("%d ", (n >> (num_bits - i - 1)) & 1);
	}
}

void sisa_print_leds_dump(const struct sisa_context *sisa)
{
	uint16_t red_leds = sisa->io_ports[SISA_IO_PORT_LEDS_RED];
	uint16_t green_leds = sisa->io_ports[SISA_IO_PORT_LEDS_GREEN];

	printf("LEDs Red: 0x%02X --> ", red_leds);
	print_binary(red_leds, SISA_NUM_RED_LEDS);
	printf("   LEDs Green: 0x%01X --> ", green_leds);
	print_binary(green_leds, SISA_NUM_GREEN_LEDS);

	putchar('\n');
}

void sisa_print_keys_dump(const struct sisa_context *sisa)
{
	uint16_t keys = sisa->io_ports[SISA_IO_PORT_KEYS];

	printf("Keys: 0x%01X --> ", keys);
	print_binary(keys, SISA_NUM_KEYS);

	putchar('\n');
}

void sisa_print_switches_dump(const struct sisa_context *sisa)
{
	uint16_t switches = sisa->io_ports[SISA_IO_PORT_SWITCHES];

	printf("Switches: 0x%03X --> ", switches);
	print_binary(switches, SISA_NUM_SWITCHES);

	putchar('\n');
}

void sisa_print_7segments_dump(const struct sisa_context *sisa)
{
	int i, j;
	uint16_t control = sisa->io_ports[SISA_IO_PORT_7SEG_CONTROL];
	uint16_t value = sisa->io_ports[SISA_IO_PORT_7SEG_VALUE];

	printf("7 segments: ");

	for (i = 0; i < SISA_NUM_7SEGS; i++) {
		j = SISA_NUM_7SEGS - i - 1;

		if (control & BIT(j))
			printf("%X ", (value >> (j * 4)) & 0xF);
		else
			printf("X ");
	}

	putchar('\n');
}
