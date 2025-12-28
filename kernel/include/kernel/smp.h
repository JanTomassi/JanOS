#pragma once

#include <kernel/acpi.h>
#include <kernel/multiboot.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SMP_MAX_CPUS 16
#define SMP_TRAMPOLINE_BASE 0x7000
#define SMP_TRAMPOLINE_ALIGN 0x1000

struct smp_cpu {
	uint8_t lapic_id;
	bool bsp;
	bool present;
	bool started;
};

struct smp_enumeration_result {
	struct smp_cpu cpus[SMP_MAX_CPUS];
	size_t cpu_count;
	uint32_t lapic_address;
	bool lapic_overridden;
};

void smp_init(const struct multiboot_tag_old_acpi *old_rsdp, const struct multiboot_tag_new_acpi *new_rsdp);

