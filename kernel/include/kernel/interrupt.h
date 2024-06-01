#pragma once
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
#define DIVISION_ERROR (0x00)
#define DEBUG (0x01)
#define NON_MASKABLE_INTERRUPT (0x02)
#define BREAKPOINT (0x03)
#define OVERFLOW (0x04)
#define BOUND_RANGE_EXCEEDED (0x05)
#define INVALID_OPCODE (0x06)
#define DEVICE_NOT_AVAILABLE (0x07)
#define DOUBLE_FAULT (0x08)
#define COPROCESSOR_SEGMENT_OVERRUN (0x09)
#define INVALID_TSS (0x0A)
#define SEGMENT_NOT_PRESENT (0x0B)
#define STACK_SEGMENT_FAULT (0x0C)
#define GENERAL_PROTECTION_FAULT (0x0D)
#define PAGE_FAULT (0x0E)
#define RESERVED (0x0F)
#define X87_FLOATING_POINT_EXCEPTION (0x10)
#define ALIGNMENT_CHECK (0x11)
#define MACHINE_CHECK (0x12)
#define SIMD_FLOATING_POINT_EXCEPTION (0x13)
#define VIRTUALIZATION_EXCEPTION (0x14)
#define CONTROL_PROTECTION_EXCEPTION (0x15)
#define HYPERVISOR_INJECTION_EXCEPTION (0x1C)
#define VMM_COMMUNICATION_EXCEPTION (0x1D)
#define SECURITY_EXCEPTION (0x1E)

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
