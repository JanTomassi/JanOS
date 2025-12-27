#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCHED_MAX_CPUS 8
#define SCHED_MAX_TASKS_PER_CPU 32

struct sched_task {
	const char *name;
	uint8_t priority;
	size_t cpu;
};

void scheduler_init(size_t cpu_count);
size_t scheduler_cpu_count(void);
bool scheduler_add_task(const char *name, uint8_t priority, size_t preferred_cpu);
bool scheduler_peek_next(size_t cpu, struct sched_task *task);
bool scheduler_pop_next(size_t cpu, struct sched_task *task);
bool scheduler_has_tasks(size_t cpu);
