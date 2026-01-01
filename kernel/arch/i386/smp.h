#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/multiboot.h>

struct cpu_info {
	uint8_t apic_id;
	bool online;
	void *stack_top;
};

void smp_init(struct multiboot_tag *mbi);
struct cpu_info *smp_get_cpus(size_t *count);
