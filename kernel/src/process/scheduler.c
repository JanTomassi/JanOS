#include <kernel/scheduler.h>

#include <kernel/process/process.h>
#include <kernel/process/address_space.h>
#include <kernel/process/stack.h>
#include <kernel/spinlock.h>
#include <arch/i386/context.h>
#include <arch/i386/smp.h>
#include <kernel/vir_mem.h>
#include <list.h>
#include <kernel/ipc.h>

#define SCHEDULER_CPU_LIMIT 16
#define SCHEDULER_QUANTUM 10

static struct list_head runqueues[SCHEDULER_CPU_LIMIT];
static spinlock_t scheduler_lock = { 0 };
static bool initialized;
static uint32_t ticks[SCHEDULER_CPU_LIMIT];
static bool reschedule_pending[SCHEDULER_CPU_LIMIT];

static void runqueue_enqueue(struct list_head *entry, struct list_head *queue)
{
	/* list_add inserts at the front; use the previous node for FIFO order. */
	list_add(entry, queue->prev);
}

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
		cpu = smp_current_cpu_index();
	if (cpu >= SCHEDULER_CPU_LIMIT)
		cpu = 0;
	if (process_cpu_affinity(process) == PROCESS_CPU_UNASSIGNED)
		(void)process_set_affinity(process, cpu);
	uint32_t flags = spin_lock_irqsave(&scheduler_lock);
	if (!process_is_queued(process)) {
		runqueue_enqueue(process_run_link(process), &runqueues[cpu]);
		process_set_queued(process, true);
	}
	spin_unlock_irqrestore(&scheduler_lock, flags);
	smp_send_scheduler_ipi(cpu);
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

static bool save_user_frame(struct process *process,
                            const struct i386_trap_frame *frame,
                            bool ipc_wait)
{
	if (process == nullptr || frame == nullptr || (frame->cs & 3u) != 3u)
		return false;
	struct i386_context saved = {
		.edi = frame->edi, .esi = frame->esi, .ebp = frame->ebp,
		.ebx = frame->ebx, .edx = frame->edx, .ecx = frame->ecx, .eax = frame->eax,
		.eip = frame->eip, .cs = frame->cs, .eflags = frame->eflags,
		.useresp = frame->useresp, .ss = frame->ss,
	};
	if (ipc_wait)
		return process_ipc_wait_save_context(process, &saved);
	process_save_context(process, &saved);
	return true;
}

static void schedule(struct i386_trap_frame *frame, bool rotate)
{
	struct cpu_info *cpu = smp_current_cpu();
	if (cpu == nullptr)
		return;
	uint8_t index = smp_current_cpu_index();
	if (index >= SCHEDULER_CPU_LIMIT)
		index = 0;
	uint32_t flags = spin_lock_irqsave(&scheduler_lock);
	struct process *current = cpu->current_process;
	if (current != nullptr && process_get_state(current) == PROCESS_RUNNING &&
		frame != nullptr && (frame->cs & 3u) == 3u) {
		save_user_frame(current, frame, false);
		if (rotate && process_get_state(current) == PROCESS_RUNNING) {
			process_set_state(current, PROCESS_READY);
			uint8_t target = process_cpu_affinity(current);
			if (target >= SCHEDULER_CPU_LIMIT)
				target = index;
			runqueue_enqueue(process_run_link(current), &runqueues[target]);
			process_set_queued(current, true);
		}
	}
	struct process *next = dequeue(index);
	struct i386_context next_context;
	bool enter_direct = false;
	bool enter_idle = false;
	if (next != nullptr) {
		process_set_state(next, PROCESS_RUNNING);
		cpu->current_process = next;
		(void)address_space_activate(process_address_space(next));
		(void)i386_tss_set_current_kernel_stack((uintptr_t)process_stack_top(process_kernel_stack(next)));
		if (process_load_context(next, &next_context)) {
			if (frame != nullptr && (frame->cs & 3u) == 3u)
				frame_from_context(frame, &next_context);
			else
				enter_direct = true;
		}
	} else {
		/* A blocked or exited process must never remain current while idle. */
		cpu->current_process = nullptr;
		(void)i386_tss_set_current_kernel_stack((uintptr_t)cpu->stack_top);
		enter_idle = frame != nullptr && (frame->cs & 3u) == 3u;
	}
	spin_unlock_irqrestore(&scheduler_lock, flags);
	if (enter_direct)
		i386_context_enter_user(&next_context);
	if (enter_idle)
		scheduler_idle();
}

void scheduler_tick(struct i386_trap_frame *frame)
{
	if (!initialized)
		return;
	ipc_tick();
	/* A timer interrupt is also the local wakeup path when the CPU was idle. */
	struct cpu_info *current_cpu = smp_current_cpu();
	if (current_cpu != nullptr && current_cpu->current_process == nullptr) {
		schedule(frame, false);
		return;
	}
	uint8_t cpu = smp_current_cpu_index();
	if (cpu >= SCHEDULER_CPU_LIMIT)
		cpu = 0;
	bool user_frame = frame != nullptr && (frame->cs & 3u) == 3u;
	if (reschedule_pending[cpu] && user_frame) {
		reschedule_pending[cpu] = false;
		schedule(frame, true);
		return;
	}
	if (++ticks[cpu] >= SCHEDULER_QUANTUM) {
		ticks[cpu] = 0;
		/* Never change address space or current_process under a kernel frame. */
		if (user_frame)
			schedule(frame, true);
		else
			reschedule_pending[cpu] = true;
	}
}

void scheduler_lapic_timer(struct i386_trap_frame *frame)
{
	scheduler_tick(frame);
}

void scheduler_ipi(struct i386_trap_frame *frame)
{
	if (!initialized)
		return;
	uint8_t cpu = smp_current_cpu_index();
	if (cpu >= SCHEDULER_CPU_LIMIT)
		cpu = 0;
	struct cpu_info *current_cpu = smp_current_cpu();
	if (current_cpu == nullptr)
		return;
	if (current_cpu->current_process == nullptr) {
		reschedule_pending[cpu] = false;
		schedule(frame, false);
		return;
	}
	reschedule_pending[cpu] = true;
}

void scheduler_yield(struct i386_trap_frame *frame) { schedule(frame, true); }

bool scheduler_block_current(struct wait_queue *queue, struct i386_trap_frame *frame)
{
	struct process *current = process_current();
	if (current == nullptr)
		return false;
	if (process_get_state(current) != PROCESS_BLOCKED) {
		if (process_get_state(current) == PROCESS_READY) {
			uint32_t flags = spin_lock_irqsave(&scheduler_lock);
			for (size_t i = 0; i < SCHEDULER_CPU_LIMIT; ++i) {
				list_for_each(&runqueues[i]) {
					if (it != process_run_link(current))
						continue;
					list_rm(it);
					RESET_LIST_ITEM(it);
					process_set_queued(current, false);
					break;
				}
			}
			process_set_state(current, PROCESS_RUNNING);
			spin_unlock_irqrestore(&scheduler_lock, flags);
			return false;
		}
		if (!process_block(current, queue))
			return false;
	} else if (process_waiting_queue(current) != queue) {
		return false;
	}
	/* Save before the wakeup path can publish a completed IPC result. */
	bool saved = save_user_frame(current, frame, true);
	if (!saved && !process_ipc_wait_active(current) &&
		process_get_state(current) == PROCESS_READY) {
		uint32_t flags = spin_lock_irqsave(&scheduler_lock);
		for (size_t i = 0; i < SCHEDULER_CPU_LIMIT; ++i) {
			list_for_each(&runqueues[i]) {
				if (it != process_run_link(current))
					continue;
				list_rm(it);
				RESET_LIST_ITEM(it);
				process_set_queued(current, false);
				break;
			}
		}
		(void)process_set_state(current, PROCESS_RUNNING);
		spin_unlock_irqrestore(&scheduler_lock, flags);
		return false;
	}
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
	if (cpu >= count || cpu >= SCHEDULER_CPU_LIMIT || process == nullptr)
		return false;
	uint32_t flags = spin_lock_irqsave(&scheduler_lock);
	uint8_t old_cpu = process_cpu_affinity(process);
	if (old_cpu == cpu) {
		spin_unlock_irqrestore(&scheduler_lock, flags);
		return true;
	}
	if (process_is_queued(process)) {
		if (old_cpu < SCHEDULER_CPU_LIMIT) {
			list_rm(process_run_link(process));
		} else {
			for (size_t i = 0; i < SCHEDULER_CPU_LIMIT; ++i) {
				list_for_each(&runqueues[i]) {
					if (it != process_run_link(process))
						continue;
					list_rm(it);
					break;
				}
			}
		}
		RESET_LIST_ITEM(process_run_link(process));
		process_set_queued(process, false);
	}
	bool result = process_set_affinity(process, cpu);
	if (result && process_get_state(process) == PROCESS_READY)
		runqueue_enqueue(process_run_link(process), &runqueues[cpu]),
		process_set_queued(process, true);
	spin_unlock_irqrestore(&scheduler_lock, flags);
	if (result && cpu != smp_current_cpu_index())
		smp_send_scheduler_ipi(cpu);
	return result;
}

[[noreturn]] void scheduler_idle(void)
{
	for (;;)
		__asm__ volatile("sti; hlt" ::: "memory");
}
