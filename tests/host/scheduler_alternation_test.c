#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

/* The scheduler's runnable queue is FIFO: yielding appends the current task. */
struct task {
	char id;
	bool runnable;
};

static struct task *pick(struct task *queue[], size_t *count)
{
	while (*count != 0) {
		struct task *task = queue[0];
		for (size_t i = 1; i < *count; ++i)
			queue[i - 1] = queue[i];
		--*count;
		if (task->runnable)
			return task;
	}
	return 0;
}

int main(void)
{
	struct task a = { 'A', true };
	struct task b = { 'B', true };
	struct task *queue[2] = { &a, &b };
	size_t count = 2;
	for (size_t i = 0; i < 6; ++i) {
		struct task *current = pick(queue, &count);
		assert(current != 0);
		assert(current->id == (i % 2 == 0 ? 'A' : 'B'));
		queue[count++] = current;
	}
	a.runnable = false;
	assert(pick(queue, &count) == &b);
	return 0;
}
