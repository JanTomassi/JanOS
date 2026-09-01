#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <list.h>

struct process;

struct wait_queue {
	struct list_head waiters;
};

void wait_queue_init(struct wait_queue *queue);
bool wait_queue_enqueue(struct wait_queue *queue, struct process *process);
bool wait_queue_remove(struct wait_queue *queue, struct process *process);
struct process *wait_queue_pop(struct wait_queue *queue);
size_t wait_queue_count(const struct wait_queue *queue);
