#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <kernel/interrupt.h>

struct process;
struct wait_queue;

void scheduler_init(void);
void scheduler_process_ready(struct process *process);
void scheduler_tick(struct i386_trap_frame *frame);
void scheduler_yield(struct i386_trap_frame *frame);
bool scheduler_block_current(struct wait_queue *queue, struct i386_trap_frame *frame);
bool scheduler_wake(struct process *process);
bool scheduler_set_affinity(struct process *process, uint8_t cpu);
[[noreturn]] void scheduler_idle(void);
