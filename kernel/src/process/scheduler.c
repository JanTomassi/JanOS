#include <kernel/scheduler.h>

#include <kernel/process/process.h>
#include <kernel/process/address_space.h>
#include <kernel/process/stack.h>
#include <kernel/spinlock.h>
#include <arch/i386/context.h>
#include <arch/i386/smp.h>
#include <kernel/vir_mem.h>
#include <list.h>

#define SCHEDULER_CPU_LIMIT 16
#define SCHEDULER_QUANTUM 10

static struct list_head runqueues[SCHEDULER_CPU_LIMIT];
static spinlock_t scheduler_lock = { 0 };
static bool initialized;
static uint32_t ticks[SCHEDULER_CPU_LIMIT];

void scheduler_init(void)
{
	if (initialized)
		return;
	spin_lock_init(&scheduler_lock);
	for (size_t i = 0; i < SCHEDULER_CPU_LIMIT; ++i)
		RESET_LIST_ITEM(&runqueues[i]);
	initialized = true;
}

void scheduler_process_ready(struct process *process)
{
	if (process == nullptr || process_get_state(process) != PROCESS_READY)
		return;
	scheduler_init();
	uint8_t cpu = process_cpu_affinity(process);
	if (cpu >= SCHEDULER_CPU_LIMIT)
		cpu = 0;
	uint32_t flags = spin_lock_irqsave(&scheduler_lock);
	if (!process_is_queued(process)) {
		list_add(process_run_link(process), &runqueues[cpu]);
		process_set_queued(process, true);
	}
	spin_unlock_irqrestore(&scheduler_lock, flags);
}

static struct process *dequeue(uint8_t cpu)
{
	if (cpu >= SCHEDULER_CPU_LIMIT)
		return nullptr;
	while (runqueues[cpu].next != &runqueues[cpu]) {
		struct list_head *link = runqueues[cpu].next;
		list_rm(link);
		RESET_LIST_ITEM(link);
		struct process *process = process_from_run_link(link);
		process_set_queued(process, false);
		if (process_get_state(process) == PROCESS_READY)
			return process;
	}
	return nullptr;
}

static void frame_from_context(struct i386_trap_frame *frame,
                               const struct i386_context *context)
{
	frame->gs = frame->fs = frame->es = frame->ds = I386_KERNEL_DATA_SELECTOR;
	frame->edi = context->edi; frame->esi = context->esi; frame->ebp = context->ebp;
	frame->ebx = context->ebx; frame->edx = context->edx; frame->ecx = context->ecx;
	frame->eax = context->eax;
	frame->eip = context->eip; frame->cs = context->cs; frame->eflags = context->eflags;
	frame->useresp = context->useresp; frame->ss = context->ss;
}

static void schedule(struct i386_trap_frame *frame, bool rotate)
{
	struct cpu_info *cpu = smp_current_cpu();
	if (frame == nullptr || cpu == nullptr)
		return;
	uint8_t index = smp_current_cpu_index();
	if (index >= SCHEDULER_CPU_LIMIT)
		index = 0;
	uint32_t flags = spin_lock_irqsave(&scheduler_lock);
	struct process *current = cpu->current_process;
	if (current != nullptr && (frame->cs & 3u) == 3u) {
		struct i386_context saved = {
			.edi = frame->edi, .esi = frame->esi, .ebp = frame->ebp,
			.ebx = frame->ebx, .edx = frame->edx, .ecx = frame->ecx, .eax = frame->eax,
			.eip = frame->eip, .cs = frame->cs, .eflags = frame->eflags,
			.useresp = frame->useresp, .ss = frame->ss,
		};
		process_save_context(current, &saved);
		if (rotate && process_get_state(current) == PROCESS_RUNNING) {
			process_set_state(current, PROCESS_READY);
			list_add(process_run_link(current), &runqueues[index]);
			process_set_queued(current, true);
		}
	}
	struct process *next = dequeue(index);
	if (next != nullptr) {
		process_set_state(next, PROCESS_RUNNING);
		cpu->current_process = next;
		(void)address_space_activate(process_address_space(next));
		(void)i386_tss_set_bsp_kernel_stack((uintptr_t)process_stack_top(process_kernel_stack(next)));
		struct i386_context context;
		if (process_load_context(next, &context))
			frame_from_context(frame, &context);
	}
	spin_unlock_irqrestore(&scheduler_lock, flags);
}

void scheduler_tick(struct i386_trap_frame *frame)
{
	uint8_t cpu = smp_current_cpu_index();
	if (cpu >= SCHEDULER_CPU_LIMIT)
		cpu = 0;
	if (++ticks[cpu] >= SCHEDULER_QUANTUM) {
		ticks[cpu] = 0;
		schedule(frame, true);
	}
}

void scheduler_yield(struct i386_trap_frame *frame) { schedule(frame, true); }

bool scheduler_block_current(struct wait_queue *queue, struct i386_trap_frame *frame)
{
	struct process *current = process_current();
	if (current == nullptr || !process_block(current, queue))
		return false;
	schedule(frame, false);
	return true;
}

bool scheduler_wake(struct process *process)
{
	struct wait_queue *queue = process_waiting_queue(process);
	return queue != nullptr && process_wake(process, queue);
}

bool scheduler_set_affinity(struct process *process, uint8_t cpu)
{
	size_t count = 0;
	smp_get_cpus(&count);
	return cpu < count && cpu < SCHEDULER_CPU_LIMIT &&
		process_set_affinity(process, cpu);
}

[[noreturn]] void scheduler_idle(void)
{
	for (;;)
		__asm__ volatile("sti; hlt" ::: "memory");
}
