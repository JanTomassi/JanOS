#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef void (*irq_handler_t)(uint8_t irq_line, void *context);

/* All software entry paths normalize to this frame before calling C. */
struct i386_trap_frame {
	uint32_t vector;
	uint32_t gs;
	uint32_t fs;
	uint32_t es;
	uint32_t ds;
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t pusha_esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint32_t error_code;
	uint32_t eip;
	uint32_t cs;
	uint32_t eflags;
	uint32_t useresp;
	uint32_t ss;
};

bool irq_register_handler(uint8_t irq_line, irq_handler_t handler, void *context);
bool irq_unregister_handler(uint8_t irq_line, irq_handler_t handler, void *context);
/**
 * DIVISION ERROR                 | 0          | Fault      | #DE        | No
 * DEBUG                          | 1          | Fault/Trap | #DB        | No
 * NON-MASKABLE INTERRUPT         | 2          | Interrupt  | -          | No
 * BREAKPOINT                     | 3          | Trap       | #BP        | No
 * OVERFLOW                       | 4          | Trap       | #OF        | No
 * BOUND RANGE EXCEEDED           | 5          | Fault      | #BR        | No
 * INVALID OPCODE                 | 6          | Fault      | #UD        | No
 * DEVICE NOT AVAILABLE           | 7          | Fault      | #NM        | No
 * DOUBLE FAULT                   | 8          | Abort      | #DF        | Yes (Zero)
 * COPROCESSOR SEGMENT OVERRUN    | 9          | Fault      | -          | No
 * INVALID TSS                    | 10         | Fault      | #TS        | Yes
 * SEGMENT NOT PRESENT            | 11         | Fault      | #NP        | Yes
 * STACK-SEGMENT FAULT            | 12         | Fault      | #SS        | Yes
 * GENERAL PROTECTION FAULT       | 13         | Fault      | #GP        | Yes
 * PAGE FAULT                     | 14         | Fault      | #PF        | Yes
 * RESERVED                       | 15         | -          | -          | No
 * X87 FLOATING-POINT EXCEPTION   | 16         | Fault      | #MF        | No
 * ALIGNMENT CHECK                | 17         | Fault      | #AC        | Yes
 * MACHINE CHECK                  | 18         | Abort      | #MC        | No
 * SIMD FLOATING-POINT EXCEPTION  | 19         | Fault      | #XM/#XF    | No
 * VIRTUALIZATION EXCEPTION       | 20         | Fault      | #VE        | No
 * CONTROL PROTECTION EXCEPTION   | 21         | Fault      | #CP        | Yes
 * RESERVED                       | 22-27      | -          | -          | No
 * HYPERVISOR INJECTION EXCEPTION | 28         | Fault      | #HV        | No
 * VMM COMMUNICATION EXCEPTION    | 29         | Fault      | #VC        | Yes
 * SECURITY EXCEPTION             | 30         | Fault      | #SX        | Yes
 * RESERVED                       | 31         | -          | -          | No
 * TRIPLE FAULT                   | -          | -          | -          | No
 * FPU ERROR INTERRUPT            | IRQ 13     | Interrupt  | #FERR      | No
 */

// From 0 to 1F are intel reserved
#define INTN_DIVISION_ERROR (0x00)
#define INTN_DEBUG (0x01)
#define INTN_NON_MASKABLE_INTERRUPT (0x02)
#define INTN_BREAKPOINT (0x03)
#define INTN_OVERFLOW (0x04)
#define INTN_BOUND_RANGE_EXCEEDED (0x05)
#define INTN_INVALID_OPCODE (0x06)
#define INTN_DEVICE_NOT_AVAILABLE (0x07)
#define INTN_DOUBLE_FAULT (0x08)
#define INTN_COPROCESSOR_SEGMENT_OVERRUN (0x09)
#define INTN_INVALID_TSS (0x0A)
#define INTN_SEGMENT_NOT_PRESENT (0x0B)
#define INTN_STACK_SEGMENT_FAULT (0x0C)
#define INTN_GENERAL_PROTECTION_FAULT (0x0D)
#define INTN_PAGE_FAULT (0x0E)
#define INTN_RESERVED (0x0F)
#define INTN_X87_FLOATING_POINT_EXCEPTION (0x10)
#define INTN_ALIGNMENT_CHECK (0x11)
#define INTN_MACHINE_CHECK (0x12)
#define INTN_SIMD_FLOATING_POINT_EXCEPTION (0x13)
#define INTN_VIRTUALIZATION_EXCEPTION (0x14)
#define INTN_CONTROL_PROTECTION_EXCEPTION (0x15)
#define INTN_HYPERVISOR_INJECTION_EXCEPTION (0x1C)
#define INTN_VMM_COMMUNICATION_EXCEPTION (0x1D)
#define INTN_SECURITY_EXCEPTION (0x1E)

// From 20 to 2F are IRQ must be remaped
#define IRQ_1 32
#define IRQ_2 33
#define IRQ_3 34
#define IRQ_4 35
#define IRQ_5 36
#define IRQ_6 37
#define IRQ_7 38
#define IRQ_8 39
#define IRQ_9 40
#define IRQ_10 41
#define IRQ_11 42
#define IRQ_12 43
#define IRQ_13 44
#define IRQ_14 45
#define IRQ_15 46
#define IRQ_16 47

#define DEFINE_IRQ(num) void isr_##num##_handler(void)

void irq_mask(uint8_t irq);
void irq_unmask(uint8_t irq);
void irq_ack(uint8_t irq);
void irq_prepare(uint8_t irq);
void irq_set_shared(uint8_t irq, bool shared);

void exception_dispatch(struct i386_trap_frame *frame);
