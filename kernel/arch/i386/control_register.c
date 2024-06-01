#include <kernel/display.h>

#include <stddef.h>
#include "./control_register.h"

struct CR0_reg get_CR0_reg(void)
{
	struct CR0_reg CR0;
	__asm__ volatile("mov %%cr0, %%eax;"
			 "mov %%eax, %0"
			 : "=g"(CR0)
			 :
			 : "%eax");
	return CR0;
}

static void debug_CR0_reg()
{
	struct CR0_reg CR0 = get_CR0_reg();

	kprintf("CR0:\n[PE: %u, MP: %u, EM: %u, TS: %u, ET: %u, NE: %u, WP: %u, AM: %u, NW: %u, CD: %u, PG: %u]\n",
		CR0.PE, CR0.MP, CR0.EM, CR0.TS, CR0.ET, CR0.NE, CR0.WP, CR0.AM,
		CR0.NW, CR0.CD, CR0.PG);
}

bool set_CR0_reg(enum CR0_REG flag, bool val)
{
	struct CR0_reg CR0 = get_CR0_reg();

	switch (flag) {
	case CR0_REG_PE:
		CR0.PE = val;
		break;
	case CR0_REG_MP:
		CR0.MP = val;
		break;
	case CR0_REG_EM:
		CR0.EM = val;
		break;
	case CR0_REG_TS:
		CR0.TS = val;
		break;
	case CR0_REG_ET:
		CR0.ET = val;
		break;
	case CR0_REG_NE:
		CR0.NE = val;
		break;
	case CR0_REG_WP:
		CR0.WP = val;
		break;
	case CR0_REG_AM:
		CR0.AM = val;
		break;
	case CR0_REG_NW:
		CR0.NW = val;
		break;
	case CR0_REG_CD:
		CR0.CD = val;
		break;
	case CR0_REG_PG:
		CR0.PG = val;
		break;
	default:
		return false;
	}

	__asm__ volatile("mov %0, %%eax;"
			 "mov %%eax, %%cr0;"
			 :
			 : "g"(CR0)
			 : "%eax");
	return true;
}

size_t get_CR3_reg(void)
{
	size_t CR3;
	__asm__ volatile("mov %%cr3, %%eax;"
			 "mov %%eax, %0;"
			 : "=g"(CR3)
			 :
			 : "%eax");
	return CR3;
}
bool set_CR3_reg(size_t val)
{
	__asm__ volatile("mov %0   , %%eax; "
			 "mov %%eax, %%cr3 "
			 :
			 : "g"(val)
			 : "%eax");

	return get_CR3_reg() == val;
}

struct CR4_reg get_CR4_reg(void)
{
	struct CR4_reg CR4;
	__asm__ volatile("mov %%cr4, %%eax;"
			 "mov %%eax, %0"
			 : "=g"(CR4)
			 :
			 : "%eax");
	return CR4;
}

static void debug_CR4_reg()
{
	struct CR4_reg CR4 = get_CR4_reg();

	kprintf("CR4:\n[VME: %u, PVI: %u, TSD: %u, DE: %u, PSE: %u, PAE: %u"
		", MCE: %u, PGE: %u, PCE: %u, OSFXSR: %u, OSXMMEXCPT: %u"
		", UMIP: %u, LA57: %u, VMXE: %u, SMXE: %u, FSGSBASE: %u"
		", PCIDE: %u, OSXSAVE: %u, KL: %u, SMEP: %u, SMAP: %u"
		", PKE: %u, CET: %u, PKS: %u, UINTR: %u]\n",
		CR4.VME, CR4.PVI, CR4.TSD, CR4.DE, CR4.PSE, CR4.PAE, CR4.MCE,
		CR4.PGE, CR4.PCE, CR4.OSFXSR, CR4.OSXMMEXCPT, CR4.UMIP,
		CR4.LA57, CR4.VMXE, CR4.SMXE, CR4.FSGSBASE, CR4.PCIDE,
		CR4.OSXSAVE, CR4.KL, CR4.SMEP, CR4.SMAP, CR4.PKE, CR4.CET,
		CR4.PKS, CR4.UINTR);
}

bool set_CR4_reg(enum CR4_REG flag, bool val)
{
	struct CR4_reg CR4 = get_CR4_reg();

	switch (flag) {
	case CR4_REG_VME:
		CR4.VME = val;
		break;
	case CR4_REG_PVI:
		CR4.PVI = val;
		break;
	case CR4_REG_TSD:
		CR4.TSD = val;
		break;
	case CR4_REG_DE:
		CR4.DE = val;
		break;
	case CR4_REG_PSE:
		CR4.PSE = val;
		break;
	case CR4_REG_PAE:
		CR4.PAE = val;
		break;
	case CR4_REG_MCE:
		CR4.MCE = val;
		break;
	case CR4_REG_PGE:
		CR4.PGE = val;
		break;
	case CR4_REG_PCE:
		CR4.PCE = val;
		break;
	case CR4_REG_OSFXSR:
		CR4.OSFXSR = val;
		break;
	case CR4_REG_OSXMMEXCPT:
		CR4.OSXMMEXCPT = val;
		break;
	case CR4_REG_UMIP:
		CR4.UMIP = val;
		break;
	case CR4_REG_LA57:
		CR4.LA57 = val;
		break;
	case CR4_REG_VMXE:
		CR4.VMXE = val;
		break;
	case CR4_REG_SMXE:
		CR4.SMXE = val;
		break;
	case CR4_REG_FSGSBASE:
		CR4.FSGSBASE = val;
		break;
	case CR4_REG_PCIDE:
		CR4.PCIDE = val;
		break;
	case CR4_REG_OSXSAVE:
		CR4.OSXSAVE = val;
		break;
	case CR4_REG_KL:
		CR4.KL = val;
		break;
	case CR4_REG_SMEP:
		CR4.SMEP = val;
		break;
	case CR4_REG_SMAP:
		CR4.SMAP = val;
		break;
	case CR4_REG_PKE:
		CR4.PKE = val;
		break;
	case CR4_REG_CET:
		CR4.CET = val;
		break;
	case CR4_REG_PKS:
		CR4.PKS = val;
		break;
	case CR4_REG_UINTR:
		CR4.UINTR = val;
		break;
	default:
		return false;
	}

	__asm__ volatile("mov %0, %%eax;"
			 "mov %%eax, %%cr4;"
			 :
			 : "g"(CR4)
			 : "%eax");
	return true;
}

void debug_CR_reg(void)
{
	debug_CR0_reg();
	debug_CR4_reg();
}
