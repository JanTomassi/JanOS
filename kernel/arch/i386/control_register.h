#pragma once

#include <stdint.h>

struct CR0_reg {
	bool PE : 1;		 // bit 0 Protected Mode Enable
	bool MP : 1;		 // bit 1 Monitor co-processor
	bool EM : 1;		 // bit 2 Emulation
	bool TS : 1;		 // bit 3 Task switched
	bool ET : 1;		 // bit 4 Extension type
	bool NE : 1;		 // bit 5 Numeric error
	uint16_t Reserved1 : 10; // bit 6-15 reserved
	bool WP : 1;		 // bit 16 Write protect
	bool Reserved2 : 1;	 // bit 17 reserved
	bool AM : 1;		 // bit 18 alignment mask
	uint16_t Reserved3 : 10; // bit 19-28 reserved
	bool NW : 1;		 // bit 29 Not-write through
	bool CD : 1;		 // bit 30 Cache disable
	bool PG : 1;		 // bit 31 Paging
};
enum CR0_REG {
	CR0_REG_PE,
	CR0_REG_MP,
	CR0_REG_EM,
	CR0_REG_TS,
	CR0_REG_ET,
	CR0_REG_NE,
	CR0_REG_WP,
	CR0_REG_AM,
	CR0_REG_NW,
	CR0_REG_CD,
	CR0_REG_PG,
};

struct CR4_reg {
	bool VME : 1; // Virtual 8086 Mode Extensions If set, enables support for the virtual interrupt flag (VIF) in virtual-8086 mode.
	bool PVI : 1; // Protected-mode Virtual Interrupts If set, enables support for the virtual interrupt flag (VIF) in protected mode.
	bool TSD : 1; // Time Stamp Disable If set, RDTSC instruction can only be executed when in ring 0, otherwise RDTSC can be used at any privilege level.
	bool DE : 1;  // Debugging Extensions If set, enables debug register based breaks on I/O space access.
	bool PSE
		: 1; // Page Size Extension If set, enables 32-bit paging mode to use 4 MiB huge pages in addition to 4 KiB pages. If PAE is enabled or the processor is in x86-64 long mode this bit is ignored.[12]
	bool PAE
		: 1; // Physical Address Extension If set, changes page table layout to translate 32-bit virtual addresses into extended 36-bit physical addresses.
	bool MCE : 1; // Machine Check Exception If set, enables machine check interrupts to occur.
	bool PGE : 1; // Page Global Enabled If set, address translations (PDE or PTE records) may be shared between address spaces.
	bool PCE : 1; // Performance-Monitoring Counter enable If set, RDPMC can be executed at any privilege level, else RDPMC can only be used in ring 0.
	bool OSFXSR
		: 1; // Operating system support for FXSAVE and FXRSTOR instructions If set, enables Streaming SIMD Extensions (SSE) instructions and fast FPU save & restore.
	bool OSXMMEXCPT : 1; // Operating System Support for Unmasked SIMD Floating-Point Exceptions If set, enables unmasked SSE exceptions.
	bool UMIP : 1;	     // User-Mode Instruction Prevention If set, the SGDT, SIDT, SLDT, SMSW and STR instructions cannot be executed if CPL > 0.[11]
	bool LA57 : 1;	     // 57-Bit Linear Addresses If set, enables 5-Level Paging.[13][14]: 2–18 
	bool VMXE : 1;	     // Virtual Machine Extensions Enable see Intel VT-x x86 virtualization.
	bool SMXE : 1;	     // Safer Mode Extensions Enable see Trusted Execution Technology (TXT)
	bool Reserved : 1;
	bool FSGSBASE : 1; // FSGSBASE Enable If set, enables the instructions RDFSBASE, RDGSBASE, WRFSBASE, and WRGSBASE.
	bool PCIDE : 1;	   // PCID Enable If set, enables process-context identifiers (PCIDs).
	bool OSXSAVE : 1;  // XSAVE and Processor Extended States Enable
	bool KL : 1;	   // Key Locker Enable If set, enables the AES Key Locker instructions.
	bool SMEP : 1;	   // Supervisor Mode Execution Protection Enable If set, execution of code in a higher ring generates a fault.[17]
	bool SMAP : 1;	   // Supervisor Mode Access Prevention Enable If set, access of data in a higher ring generates a fault.[18]
	bool PKE : 1;	   // Protection Key Enable See Intel 64 and IA-32 Architectures Software Developer's Manual.
	bool CET : 1;	   // Control-flow Enforcement Technology If set, enables control-flow enforcement technology.[14]: 2–19 
	bool PKS
		: 1; //Enable Protection Keys for Supervisor-Mode Pages If set, each supervisor-mode linear address is associated with a protection key when 4-level or 5-level paging is in use.[14]: 2–19 
	bool UINTR : 1; //User Interrupts Enable If set, enables user-mode inter-processor interrupts and their associated instructions and data structures.
};

enum CR4_REG {
	CR4_REG_VME,
	CR4_REG_PVI,
	CR4_REG_TSD,
	CR4_REG_DE,
	CR4_REG_PSE,
	CR4_REG_PAE,
	CR4_REG_MCE,
	CR4_REG_PGE,
	CR4_REG_PCE,
	CR4_REG_OSFXSR,
	CR4_REG_OSXMMEXCPT,
	CR4_REG_UMIP,
	CR4_REG_LA57,
	CR4_REG_VMXE,
	CR4_REG_SMXE,
	CR4_REG_FSGSBASE,
	CR4_REG_PCIDE,
	CR4_REG_OSXSAVE,
	CR4_REG_KL,
	CR4_REG_SMEP,
	CR4_REG_SMAP,
	CR4_REG_PKE,
	CR4_REG_CET,
	CR4_REG_PKS,
	CR4_REG_UINTR,
};

void debug_CR_reg(void);
struct CR0_reg get_CR0_reg(void);
bool set_CR0_reg(enum CR0_REG flag, bool val);

size_t get_CR3_reg(void);
bool set_CR3_reg(size_t val);

struct CR4_reg get_CR4_reg(void);
bool set_CR4_reg(enum CR4_REG flag, bool val);
