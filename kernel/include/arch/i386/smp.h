#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/multiboot.h>
#include <arch/i386/context.h>

struct process;

struct cpu_info {
	uint8_t apic_id;
	volatile bool online;
	void *stack_top;
	struct i386_cpu_state tss_state;
	struct process *current_process;
	struct process *idle_process;
	void *scheduler_stack;
	uint32_t interrupt_nesting;
};

struct madt_ioapic_info {
	uintptr_t phys_addr;
	uint32_t gsi_base;
	uint8_t ioapic_id;
};

struct madt_irq_override {
	uint8_t source;
	uint32_t gsi;
	uint16_t flags;
};

void smp_init(struct multiboot_tag *acpi_tag);
struct cpu_info *smp_get_cpus(size_t *count);
struct cpu_info *smp_current_cpu(void);
uint8_t smp_current_cpu_index(void);
bool smp_get_ioapic_info(struct madt_ioapic_info *info);
size_t smp_get_irq_overrides(struct madt_irq_override *out, size_t max);
