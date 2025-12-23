#pragma once
#include <stdint.h>

struct ext_feature_info {
	bool SSE3 : 1; //Streaming SIMD Extensions 3 (SSE3). A value of 1 indicates the processor supports this technology.
	uint8_t reserved_1 : 2;
	bool MONITOR : 1; // MONITOR/MWAIT. A value of 1 indicates the processor supports this feature.
	bool DS_CPL : 1; // CPL Qualified Debug Store. A value of 1 indicates the processor supports the extensions to the Debug Store feature to allow for branch message storage qualified by CPL.
	uint8_t reserved_2 : 2;
	bool EST : 1; // Enhanced Intel SpeedStep technology. A value of 1 indicates that the processor supports this technology.
	bool TM2 : 1; // Thermal Monitor 2. A value of 1 indicates whether the processor supports this technology.
	bool reserved_3 : 1;
	bool CNXT_ID : 1; // L1 Context ID. // A value of 1 indicates the L1 data cache mode can be set to either adaptive mode or shared mode. A value of 0 indicates this feature is not supported. See definition of the IA32_MISC_ENABLE MSR Bit 24 (L1 Data Cache Context Mode) for details.
	uint32_t reserved_4 : 21;
};
struct feature_info {
	bool FPU : 1; // Floating Point Unit On-Chip. The processor
		      // contains an x87 FPU.
	bool VME : 1; // Virtual 8086 Mode Enhancements. Virtual 8086
		      // mode enhancements, including CR4.VME for
		      // controlling the feature, CR4.PVI for
		      // protected mode virtual interrupts, software
		      // interrupt indirection, expansion of the TSS
		      // with the software indirection bitmap, and
		      // EFLAGS.VIF and EFLAGS.VIP flags.
	bool DE : 1; // Debugging Extensions. Support for I/O
		     // breakpoints, including CR4.DE for controlling
		     // the feature, and optional trapping of accesses
		     // to DR4 and DR5.
	bool PSE : 1; // Page Size Extension. Large pages of size 4
		      // MByte are supported, including CR4.PSE for
		      // controlling the feature, the defined dirty
		      // bit in PDE (Page Directory Entries), optional
		      // reserved bit trapping in CR3, PDEs, and PTEs.
	bool TSC : 1; // Time Stamp Counter. The RDTSC instruction is
		      // supported, including CR4.TSD for controlling
		      // privilege.
	bool MSR : 1; // Model Specific Registers RDMSR and WRMSR
		      // Instructions. The RDMSR and WRMSR
		      // instructions are supported. Some of the MSRs
		      // are implementation dependent.
	bool PAE : 1; // Physical Address Extension. Physical
		      // addresses greater than 32 bits are supported:
		      // extended page table entry formats, an extra
		      // level in the page translation tables is
		      // defined, 2-MByte pages are supported instead
		      // of 4 Mbyte pages if PAE bit is 1. The actual
		      // number of address bits beyond 32 is not
		      // defined, and is implementation specific.
	bool MCE : 1; // Machine Check Exception. Exception 18 is
		      // defined for Machine Checks, including CR4.MCE
		      // for controlling the feature. This feature
		      // does not define the model-specific
		      // implementations of machine-check error
		      // logging, reporting, and processor
		      // shutdowns. Machine Check exception handlers
		      // may have to depend on processor version to do
		      // model specific processing of the exception,
		      // or test for the presence of the Machine Check
		      // feature.
	bool CX8 : 1; // CMPXCHG8B Instruction. The
		      // compare-and-exchange 8 bytes (64 bits)
		      // instruction is supported (implicitly locked
		      // and atomic).
	bool APIC : 1; // APIC On-Chip. The processor contains an
		       // Advanced Programmable Interrupt Controller
		       // (APIC), responding to memory mapped commands
		       // in the physical address range FFFE0000H to
		       // FFFE0FFFH (by default - some processors
		       // permit the APIC to be relocated).
	bool reserved_1 : 1; // Reserved
	bool SEP : 1; // SYSENTER and SYSEXIT Instructions. The
		      // SYSENTER and SYSEXIT and associated MSRs are
		      // supported. 12 MTRR Memory Type Range
		      // Registers. MTRRs are supported. The MTRRcap
		      // MSR contains feature bits that describe what
		      // memory types are supported, how many variable
		      // MTRRs are supported, and whether fixed MTRRs
		      // are supported.
	bool PGE : 1; // PTE Global Bit. The global bit in page
		      // directory entries (PDEs) and page table
		      // entries (PTEs) is supported, indicating TLB
		      // entries that are common to different
		      // processes and need not be flushed. The
		      // CR4.PGE bit controls this feature.
	bool MCA : 1; // Machine Check Architecture. The Machine Check
		      // Architecture, which provides a compatible
		      // mechanism for error reporting in P6 family,
		      // Pentium 4, Intel Xeon processors, and future
		      // processors, is supported. The MCG_CAP MSR
		      // contains feature bits describing how many
		      // banks of error reporting MSRs are supported.
	bool CMOV : 1; // Conditional Move Instructions. The
		       // conditional move instruction CMOV is
		       // supported. In addition, if x87 FPU is
		       // present as indicated by the CPUID.FPU
		       // feature bit, then the FCOMI and FCMOV
		       // instructions are supported
	bool PAT : 1; // Page Attribute Table. Page Attribute Table is
		      // supported. This feature augments the Memory
		      // Type Range Registers (MTRRs), allowing an
		      // operating system to specify attributes of
		      // memory on a 4K granularity through a linear
		      // address.
	bool PSE_36 : 1; // 36-Bit Page Size Extension. Extended
			 // 4-MByte pages that are capable of
			 // addressing physical memory beyond 4 GBytes
			 // are supported. This feature indicates that
			 // the upper four bits of the physical
			 // address of the 4-MByte page is encoded by
			 // bits 13-16 of the page directory entry.
	bool PSN : 1; // Processor Serial Number. The processor
		      // supports the 96-bit processor identification
		      // number feature and the feature is enabled.
	bool CLFSH : 1; // CLFLUSH Instruction. CLFLUSH Instruction is
			// supported. 20 Reserved Reserved
	bool DS : 1; // Debug Store. The processor supports the
		     // ability to write debug information into a
		     // memory resident buffer. This feature is used
		     // by the branch trace store (BTS) and precise
		     // event-based sampling (PEBS) facilities (see
		     // Chapter 15, Debugging and Performance
		     // Monitoring, in the IA-32 Intel Architecture
		     // Software Developer's Manual, Volume 3).
	bool ACPI : 1; // Thermal Monitor and Software Controlled
		       // Clock Facilities. The processor implements
		       // internal MSRs that allow processor
		       // temperature to be monitored and processor
		       // performance to be modulated in predefined
		       // duty cycles under software control.
	bool MMX : 1; // Intel MMX Technology. The processor supports
		      // the Intel MMX technology.
	bool FXSR : 1; // FXSAVE and FXRSTOR Instructions. The FXSAVE
		       // and FXRSTOR instructions are supported for
		       // fast save and restore of the floating point
		       // context. Presence of this bit also indicates
		       // that CR4.OSFXSR is available for an
		       // operating system to indicate that it
		       // supports the FXSAVE and FXRSTOR
		       // instructions.
	bool SSE : 1; // SSE. The processor supports the SSE
		      // extensions.
	bool SSE2 : 1; // SSE2. The processor supports the SSE2
		       // extensions.
	bool SS : 1; // Self Snoop. The processor supports the
		     // management of conflicting memory types by
		     // performing a snoop of its own cache structure
		     // for transactions issued to the bus.
	bool HTT : 1; // Hyper-Threading Technology. The processor
		      // supports Hyper-Threading Technology.
	bool TM : 1; // Thermal Monitor. The processor implements the
		     // thermal monitor automatic thermal control
		     // circuitry (TCC).
	bool reserved_2 : 1; // Reserved
	bool PBE : 1; // Pending Break Enable. The processor supports
		      // the use of the FERR#/PBE# pin when the
		      // processor is in the stop-clock state (STPCLK#
		      // is asserted) to signal the processor that an
		      // interrupt is pending and that the processor
		      // should return to normal operation to handle
		      // the interrupt. Bit 10 (PBE enable) in the
		      // IA32_MISC_ENABLE MSR enables this capability.
};

struct ext_feature_info cpuid_get_ext_feature_info();
struct feature_info cpuid_get_feature_info();
