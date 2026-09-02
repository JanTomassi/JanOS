#include <kernel/process/wait_queue.h>

#include <kernel/process/process.h>

void wait_queue_init(struct wait_queue *queue)
{
	if (queue != nullptr)
		RESET_LIST_ITEM(&queue->waiters);
}

bool wait_queue_enqueue(struct wait_queue *queue, struct process *process)
{
	return process_block(process, queue);
}

bool wait_queue_remove(struct wait_queue *queue, struct process *process)
{
	if (queue == nullptr || process == nullptr)
		return false;
	return process_wake(process, queue);
}

struct process *wait_queue_pop(struct wait_queue *queue)
{
	if (queue == nullptr || queue->waiters.next == &queue->waiters)
		return nullptr;
	struct process *process = process_from_wait_link(queue->waiters.next);
	return process_wake(process, queue) ? process : nullptr;
}

size_t wait_queue_count(const struct wait_queue *queue)
{
	if (queue == nullptr)
		return 0;
	size_t count = 0;
	list_for_each(&queue->waiters)
		++count;
	return count;
}
