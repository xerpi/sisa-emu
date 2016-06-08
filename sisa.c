#include <stdio.h>
#include <string.h>
#include "sisa.h"

#define X_DOWNTO_Y(val, x, y) (((val) >> (y)) & ((1 << ((x) - (y) + 1)) - 1))
#define SEXT_5(val)           (((-((val) >> 4)) & ~((1 << 5) - 1)) | (val))
#define SEXT_8(val)           (((-((val) >> 7)) & ~((1 << 8) - 1)) | (val))

#define INSTR_OPCODE(instr) X_DOWNTO_Y(instr, 15, 12)
#define INSTR_Rd(instr)     X_DOWNTO_Y(instr, 11, 9)
#define INSTR_Ra_9(instr)   X_DOWNTO_Y(instr, 11, 9)
#define INSTR_Ra_6(instr)   X_DOWNTO_Y(instr, 8, 6)
#define INSTR_Rb_9(instr)   X_DOWNTO_Y(instr, 11, 9)
#define INSTR_Rb_0(instr)   X_DOWNTO_Y(instr, 2, 0)
#define INSTR_IMM8(instr)   X_DOWNTO_Y(instr, 7, 0)

#define MOV_F_BITS(instr)        X_DOWNTO_Y(instr, 8, 8)
#define ARIT_LOGIC_F_BITS(instr) X_DOWNTO_Y(instr, 5, 3)

#define REGS (sisa->cpu.regfile.general.regs)

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
	sisa->cpu.pc = SISA_CODE_LOAD_ADDR;
	sisa->cpu.regfile.system.s7 = 1;
	sisa->cpu.status = SISA_CPU_STATUS_FETCH;
	sisa->cpu.exc_happened = false;

	sisa_tlb_init(&sisa->itlb);
	sisa_tlb_init(&sisa->dtlb);
}

static void sisa_tlb_access(const struct sisa_tlb *tlb, uint8_t vpn, uint8_t *pfn,
			    uint8_t *v, uint8_t *r, uint8_t *p, uint8_t *miss)
{
	int i;

	for (i = 0; i < SISA_NUM_TLB_ENTRIES; i++) {
		if (tlb->entries[i].vpn == vpn) {
			*pfn = tlb->entries[i].pfn;
			*v = tlb->entries[i].v;
			*r = tlb->entries[i].r;
			*p = tlb->entries[i].p;
			*miss = 0;
			return;
		}
	}

	*miss = 1;
}

static void sisa_demw_execute(struct sisa_context *sisa)
{
	const uint16_t instr = sisa->cpu.ir;

	switch (INSTR_OPCODE(instr)) {
	case SISA_OPCODE_MOV:
		switch (MOV_F_BITS(instr)) {
		case SISA_INSTR_ARIT_LOGIC_F_MOVI:
			REGS[INSTR_Rd(instr)] = SEXT_8(INSTR_IMM8(instr));
			break;
		case SISA_INSTR_ARIT_LOGIC_F_MOVHI:
			REGS[INSTR_Rd(instr)] = INSTR_IMM8(instr) << 8 | REGS[INSTR_Rd(instr)];
			break;
		}
		break;
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
	default:
		printf("Invalid instruction!\n");
		break;
	}
}

void sisa_step_cycle(struct sisa_context *sisa)
{
	switch (sisa->cpu.status) {
	case SISA_CPU_STATUS_FETCH: {
		uint8_t vpn, pfn, v, r, p, miss;
		uint16_t paddr;

		if (sisa->cpu.pc & 1) {
			sisa->cpu.exception = SISA_EXCEPTION_UNALIGNED_ACCESS;
			sisa->cpu.exc_happened = true;
			sisa->cpu.status = SISA_CPU_STATUS_NOP;
			break;
		}

		vpn = sisa->cpu.pc >> SISA_PAGE_SHIFT;

		sisa_tlb_access(&sisa->itlb, vpn, &pfn, &v, &r, &p, &miss);

		if (miss) {
			sisa->cpu.exception = SISA_EXCEPTION_ITLB_MISS;
			sisa->cpu.exc_happened = true;
			sisa->cpu.status = SISA_CPU_STATUS_NOP;
			break;
		} else if (!v) {
			sisa->cpu.exception = SISA_EXCEPTION_ITLB_INVALID;
			sisa->cpu.exc_happened = true;
			sisa->cpu.status = SISA_CPU_STATUS_NOP;
			break;
		} else if (p && !(sisa->cpu.regfile.system.s7 & 1)) {
			sisa->cpu.exception = SISA_EXCEPTION_ITLB_PROTECTED;
			sisa->cpu.exc_happened = true;
			sisa->cpu.status = SISA_CPU_STATUS_NOP;
			break;
		}

		paddr = pfn << SISA_PAGE_SHIFT | sisa->cpu.pc & (SISA_PAGE_SIZE - 1);

		sisa->cpu.ir = sisa->memory[paddr + 1] << 8 | sisa->memory[paddr];
		sisa->cpu.status = SISA_CPU_STATUS_DEMW;
		break;
	}
	case SISA_CPU_STATUS_DEMW:
		sisa_demw_execute(sisa);
		sisa->cpu.pc += 2;
		sisa->cpu.status = SISA_CPU_STATUS_FETCH;
		break;
	case SISA_CPU_STATUS_NOP:
		sisa->cpu.status = SISA_CPU_STATUS_SYSTEM;
		break;
	}
}

void sisa_load_binary(struct sisa_context *sisa, uint16_t address, void *data, size_t size)
{
	memcpy(sisa->memory + address, data, size);
}

void sisa_print_dump(struct sisa_context *sisa)
{
	int i;

	static const char *status_str[] = {
		"fetch", "demw", "system", "nop"
	};

	printf("%s\n", status_str[sisa->cpu.status]);
	printf("pc: 0x%04X\n", sisa->cpu.pc);
	printf("ir: 0x%04X\n", sisa->cpu.ir);

	for (i = 0; i < 8; i++) {
		printf("r%i: 0x%04X\n", i, sisa->cpu.regfile.general.regs[i]);
	}
}
