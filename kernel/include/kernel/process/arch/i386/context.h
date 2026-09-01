#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Selectors installed by arch/i386/boot.s. */
#define I386_KERNEL_CODE_SELECTOR 0x08u
#define I386_KERNEL_DATA_SELECTOR 0x10u
#define I386_USER_CODE_SELECTOR   0x18u
#define I386_USER_DATA_SELECTOR   0x20u

#define I386_EFLAGS_RESERVED      0x00000002u
#define I386_EFLAGS_INTERRUPT_ENABLE 0x00000200u
#define I386_EFLAGS_USER_DEFAULT  (I386_EFLAGS_RESERVED | I386_EFLAGS_INTERRUPT_ENABLE)

struct i386_tss {
	uint32_t previous_task;
	uint32_t esp0;
	uint32_t ss0;
	uint32_t esp1;
	uint32_t ss1;
	uint32_t esp2;
	uint32_t ss2;
	uint32_t cr3;
	uint32_t eip;
	uint32_t eflags;
	uint32_t eax;
	uint32_t ecx;
	uint32_t edx;
	uint32_t ebx;
	uint32_t esp;
	uint32_t ebp;
	uint32_t esi;
	uint32_t edi;
	uint32_t es;
	uint32_t cs;
	uint32_t ss;
	uint32_t ds;
	uint32_t fs;
	uint32_t gs;
	uint32_t ldt;
	uint16_t trap;
	uint16_t iomap_base;
} __attribute__((packed));

/* Keep one state object per CPU; installation affects only the current CPU. */
struct i386_cpu_state {
	uint64_t gdt[6];
	struct i386_tss tss;
};

/* Append a TSS descriptor to the current GDT and load TR (selector 0x28). */
bool i386_tss_install(struct i386_cpu_state *state, uintptr_t kernel_stack_top);

/*
 * A complete software context for entering a 32-bit user address space.
 * The first eight fields are in pusha order; the remaining fields form the
 * privilege-transition frame consumed by iret.
 */
struct i386_context {
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint32_t eip;
	uint32_t cs;
	uint32_t eflags;
	uint32_t useresp;
	uint32_t ss;
	uint32_t cr3;
};

/* Build a context with validated ring-3 selectors and a sanitized EFLAGS. */
bool i386_context_init_user(struct i386_context *context, uintptr_t entry,
	uintptr_t user_stack, uintptr_t page_directory, uint32_t eflags);

/* Both entry points perform the same controlled kernel-to-user transition. */
[[noreturn]] void i386_context_enter_user(const struct i386_context *context);
[[noreturn]] void i386_context_return_user(const struct i386_context *context);
