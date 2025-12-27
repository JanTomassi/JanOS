#include <kernel/display.h>
#include <kernel/scheduler.h>

#include <string.h>

struct sched_item {
	struct sched_task task;
	size_t order;
};

struct cpu_queue {
	struct sched_item items[SCHED_MAX_TASKS_PER_CPU];
	size_t size;
};

static struct cpu_queue queues[SCHED_MAX_CPUS];
static size_t queue_count = 0;
static size_t order_counter = 0;

static bool task_is_higher_priority(const struct sched_item *a,
				    const struct sched_item *b)
{
	if (a->task.priority == b->task.priority)
		return a->order < b->order;

	return a->task.priority > b->task.priority;
}

static void heapify_up(struct cpu_queue *queue, size_t idx)
{
	while (idx > 0) {
		size_t parent = (idx - 1) / 2;
		if (task_is_higher_priority(&queue->items[idx],
					    &queue->items[parent])) {
			struct sched_item tmp = queue->items[parent];
			queue->items[parent] = queue->items[idx];
			queue->items[idx] = tmp;
			idx = parent;
			continue;
		}
		break;
	}
}

static void heapify_down(struct cpu_queue *queue, size_t idx)
{
	while (true) {
		size_t left = idx * 2 + 1;
		size_t right = idx * 2 + 2;
		size_t best = idx;

		if (left < queue->size &&
		    task_is_higher_priority(&queue->items[left],
					    &queue->items[best]))
			best = left;

		if (right < queue->size &&
		    task_is_higher_priority(&queue->items[right],
					    &queue->items[best]))
			best = right;

		if (best != idx) {
			struct sched_item tmp = queue->items[idx];
			queue->items[idx] = queue->items[best];
			queue->items[best] = tmp;
			idx = best;
			continue;
		}
		break;
	}
}

void scheduler_init(size_t cpu_count)
{
	if (cpu_count > SCHED_MAX_CPUS) {
		kprintf("Scheduler: capping CPUs to %u (requested %u)\n",
			SCHED_MAX_CPUS, (uint32_t)cpu_count);
		queue_count = SCHED_MAX_CPUS;
	} else {
		queue_count = cpu_count;
	}

	memset(queues, 0, sizeof(queues));
	order_counter = 0;

	kprintf("Scheduler ready for %u CPU queue(s) with priority heap.\n",
		(uint32_t)queue_count);
}

size_t scheduler_cpu_count(void)
{
	return queue_count;
}

bool scheduler_add_task(const char *name, uint8_t priority, size_t preferred_cpu)
{
	if (preferred_cpu >= queue_count)
		return false;

	struct cpu_queue *queue = &queues[preferred_cpu];
	if (queue->size >= SCHED_MAX_TASKS_PER_CPU)
		return false;

	struct sched_item item = {
		.task = {
			.name = name,
			.priority = priority,
			.cpu = preferred_cpu,
		},
		.order = order_counter++,
	};

	queue->items[queue->size] = item;
	queue->size++;
	heapify_up(queue, queue->size - 1);

	return true;
}

bool scheduler_pop_next(size_t cpu, struct sched_task *task)
{
	if (cpu >= queue_count || task == NULL)
		return false;

	struct cpu_queue *queue = &queues[cpu];
	if (queue->size == 0)
		return false;

	struct sched_item top = queue->items[0];
	queue->size--;
	if (queue->size > 0) {
		queue->items[0] = queue->items[queue->size];
		heapify_down(queue, 0);
	}

	*task = top.task;
	return true;
}

bool scheduler_peek_next(size_t cpu, struct sched_task *task)
{
	if (cpu >= queue_count || task == NULL)
		return false;

	struct cpu_queue *queue = &queues[cpu];
	if (queue->size == 0)
		return false;

	*task = queue->items[0].task;
	return true;
}

bool scheduler_has_tasks(size_t cpu)
{
	if (cpu >= queue_count)
		return false;

	return queues[cpu].size > 0;
}
