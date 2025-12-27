#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SMP_MAX_CPUS 8

struct smp_cpu {
	uint8_t id;
	bool bootstrap;
	bool online;
};

void smp_init(void);
size_t smp_get_cpu_count(void);
const struct smp_cpu *smp_get_cpus(void);
void smp_bootstrap_aps(void);
void smp_ap_entry(void);
