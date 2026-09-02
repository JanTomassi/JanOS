#include <kernel/process/process.h>

#include <kernel/allocator.h>
#include <kernel/display.h>
#include <kernel/process/address_space.h>
#include <kernel/process/stack.h>
#include <kernel/process/wait_queue.h>
#include <kernel/spinlock.h>
#include <kernel/vir_mem.h>
#include <list.h>
#include <arch/i386/smp.h>
#include <kernel/scheduler.h>

struct process {
	process_pid_t pid;
	enum process_state state;
	int status;
	struct process *parent;
	struct address_space *space;
	struct process_stack *kernel_stack;
	struct process_stack *user_stack;
	uintptr_t user_entry;
	void *user_stack_pointer;
	struct i386_context initial_context;
	struct i386_context saved_context;
	struct list_head all;
	struct list_head children;
	struct list_head sibling;
	struct list_head wait_link;
	struct wait_queue *waiting_on;
	struct list_head run_link;
	bool queued;
	uint8_t owner_cpu;
	uint8_t cpu_affinity;
};

static LIST_HEAD(processes);
static spinlock_t process_lock = { 0 };
static process_pid_t next_pid = 1;
static bool initialized;
static allocator_t allocator;

/* This stack is never owned by a process and survives every process exit. */
static uint8_t reaper_stack[PROCESS_DEFAULT_KERNEL_STACK_SIZE]
	__attribute__((aligned(16)));

static void ensure_initialized(void)
{
	if (!initialized) {
		spin_lock_init(&process_lock);
		allocator = get_slab_allocator();
		initialized = true;
	}
}

static struct process *find_pid_locked(process_pid_t pid)
{
	list_for_each(&processes) {
		struct process *process = list_entry(it, struct process, all);
		if (process->pid == pid)
			return process;
	}
	return nullptr;
}

static process_pid_t allocate_pid_locked(void)
{
	for (uint64_t attempts = 0; attempts < UINT32_MAX; ++attempts) {
		process_pid_t candidate = next_pid++;
		if (candidate == 0)
			candidate = next_pid++;
		if (find_pid_locked(candidate) == nullptr)
			return candidate;
	}
	return 0;
}

void process_system_init(void)
{
	ensure_initialized();
}

struct process *process_current(void)
{
	ensure_initialized();
	struct cpu_info *cpu = smp_current_cpu();
	return cpu == nullptr ? nullptr : cpu->current_process;
}

struct process *process_create(struct process *parent)
{
	ensure_initialized();
	struct process *process = allocator.alloc(sizeof(*process)).ptr;
	if (process == nullptr)
		return nullptr;
	*process = (struct process){
		.state = PROCESS_NEW,
		.parent = parent,
		.owner_cpu = PROCESS_CPU_UNASSIGNED,
		.cpu_affinity = PROCESS_CPU_UNASSIGNED,
	};
	RESET_LIST_ITEM(&process->all);
	RESET_LIST_ITEM(&process->children);
	RESET_LIST_ITEM(&process->sibling);
	RESET_LIST_ITEM(&process->wait_link);
	RESET_LIST_ITEM(&process->run_link);
	process->space = address_space_create();
	process->kernel_stack = process_kernel_stack_create(PROCESS_DEFAULT_KERNEL_STACK_SIZE);
	process->user_stack = process_user_stack_create(process->space,
		PROCESS_DEFAULT_USER_STACK_SIZE);
	if (process->space == nullptr || process->kernel_stack == nullptr ||
	    process->user_stack == nullptr)
		goto fail;

	spin_lock(&process_lock);
	process->pid = allocate_pid_locked();
	if (process->pid == 0) {
		spin_unlock(&process_lock);
		goto fail;
	}
	list_add(&process->all, &processes);
	if (parent != nullptr)
		list_add(&process->sibling, &parent->children);
	spin_unlock(&process_lock);
	return process;

fail:
	process_stack_destroy(process->user_stack);
	process_stack_destroy(process->kernel_stack);
	address_space_destroy(process->space);
	allocator.free((fatptr_t){ .ptr = process, .len = sizeof(*process) });
	return nullptr;
}

bool process_start(struct process *process, uintptr_t entry, int argc,
                   const char *const argv[])
{
	if (process == nullptr || entry < PAGE_SIZE || entry >= 0xc0000000u)
		return false;
	ensure_initialized();
	spin_lock(&process_lock);
	struct cpu_info *cpu = smp_current_cpu();
	if (cpu == nullptr || process->state != PROCESS_NEW) {
		spin_unlock(&process_lock);
		return false;
	}
	spin_unlock(&process_lock);
	void *user_stack_pointer = nullptr;
	if (!process_user_stack_layout(process->user_stack, argc, argv,
	                               &user_stack_pointer))
		return false;
	const fatptr_t *page_directory = process_page_directory(process);
	struct i386_context context;
	if (page_directory == nullptr || page_directory->ptr == nullptr ||
		!i386_context_init_user(&context, entry, (uintptr_t)user_stack_pointer,
			(uintptr_t)page_directory->ptr, I386_EFLAGS_USER_DEFAULT))
		return false;
	spin_lock(&process_lock);
	if (process->state != PROCESS_NEW) {
		spin_unlock(&process_lock);
		return false;
	}
	process->user_entry = entry;
	process->user_stack_pointer = user_stack_pointer;
	process->initial_context = context;
	process->saved_context = context;
	if (cpu->current_process == nullptr) {
		process->state = PROCESS_RUNNING;
		process->owner_cpu = smp_current_cpu_index();
		process->cpu_affinity = process->owner_cpu;
		cpu->current_process = process;
	} else {
		process->state = PROCESS_READY;
	}
	spin_unlock(&process_lock);
	if (cpu->current_process == process && !i386_tss_init_bsp()) {
		spin_lock(&process_lock);
		cpu->current_process = nullptr;
		process->state = PROCESS_NEW;
		spin_unlock(&process_lock);
		return false;
	}
	if (process->state == PROCESS_READY)
		scheduler_process_ready(process);
	return true;
}

bool process_initial_context(const struct process *process,
							 struct i386_context *context)
{
	if (process == nullptr || context == nullptr || process->state != PROCESS_RUNNING)
		return false;
	*context = process->saved_context;
	return true;
}

void process_destroy(struct process *process)
{
	if (process == nullptr)
		return;
	ensure_initialized();
	spin_lock(&process_lock);
	if (process == process_current() || process->state == PROCESS_DEAD ||
	    process->children.next != &process->children ||
	    process->waiting_on != nullptr) {
		spin_unlock(&process_lock);
		return;
	}
	process->state = PROCESS_DEAD;
	list_rm(&process->all);
	if (process->parent != nullptr)
		list_rm(&process->sibling);
	spin_unlock(&process_lock);
	process_stack_destroy(process->user_stack);
	process_stack_destroy(process->kernel_stack);
	address_space_destroy(process->space);
	allocator.free((fatptr_t){ .ptr = process, .len = sizeof(*process) });
}

process_pid_t process_pid(const struct process *process)
{
	return process == nullptr ? 0 : process->pid;
}

struct process *process_parent(const struct process *process)
{
	return process == nullptr ? nullptr : process->parent;
}

struct address_space *process_address_space(const struct process *process)
{
	return process == nullptr ? nullptr : process->space;
}

const fatptr_t *process_page_directory(const struct process *process)
{
	return process == nullptr ? nullptr :
		address_space_page_directory(process->space);
}

struct process_stack *process_kernel_stack(const struct process *process)
{
	return process == nullptr ? nullptr : process->kernel_stack;
}

struct process_stack *process_user_stack(const struct process *process)
{
	return process == nullptr ? nullptr : process->user_stack;
}

uintptr_t process_user_entry(const struct process *process)
{
	return process == nullptr ? 0 : process->user_entry;
}

void *process_user_stack_pointer(const struct process *process)
{
	return process == nullptr ? nullptr : process->user_stack_pointer;
}

enum process_state process_get_state(const struct process *process)
{
	return process == nullptr ? PROCESS_DEAD : process->state;
}

uint8_t process_owner_cpu(const struct process *process)
{
	return process == nullptr ? PROCESS_CPU_UNASSIGNED : process->owner_cpu;
}

uint8_t process_cpu_affinity(const struct process *process)
{
	return process == nullptr ? PROCESS_CPU_UNASSIGNED : process->cpu_affinity;
}

bool process_set_state(struct process *process, enum process_state state)
{
	if (process == nullptr || state < PROCESS_NEW || state > PROCESS_DEAD)
		return false;
	ensure_initialized();
	spin_lock(&process_lock);
	if (process->state == PROCESS_DEAD ||
	    (state == PROCESS_NEW && process->state != PROCESS_NEW) ||
	    (process->state == PROCESS_ZOMBIE && state != PROCESS_DEAD)) {
		spin_unlock(&process_lock);
		return false;
	}
	process->state = state;
	spin_unlock(&process_lock);
	return true;
}

int process_exit_status(const struct process *process)
{
	return process == nullptr ? 0 : process->status;
}

static void process_reap(struct process *process)
{
	process_stack_destroy(process->user_stack);
	process_stack_destroy(process->kernel_stack);
	address_space_destroy(process->space);
	process->user_stack = nullptr;
	process->kernel_stack = nullptr;
	process->space = nullptr;
	process->user_entry = 0;
	process->user_stack_pointer = nullptr;
}

[[noreturn]] static void process_reaper_cleanup(void *argument)
{
	struct process *process = argument;
	process_reap(process);
	for (;;)
		__asm__ volatile("hlt");
}

void process_exit(struct process *process, int status)
{
	if (process == nullptr)
		return;
	if (process == process_current()) {
		process_exit_current(status);
		return;
	}
	spin_lock(&process_lock);
	if (process->state == PROCESS_DEAD || process->state == PROCESS_ZOMBIE) {
		spin_unlock(&process_lock);
		return;
	}
	if (process->waiting_on != nullptr) {
		list_rm(&process->wait_link);
		process->waiting_on = nullptr;
	}
	process->status = status;
	process->state = PROCESS_ZOMBIE;
	spin_unlock(&process_lock);
	process_reap(process);
}

[[noreturn]] void process_exit_current(int status)
{
	ensure_initialized();
	spin_lock(&process_lock);
	struct cpu_info *cpu = smp_current_cpu();
	struct process *process = cpu == nullptr ? nullptr : cpu->current_process;
	if (process == nullptr) {
		spin_unlock(&process_lock);
		__asm__ volatile("cli");
		for (;;)
			__asm__ volatile("hlt");
	}
	if (process->state == PROCESS_ZOMBIE || process->state == PROCESS_DEAD) {
		spin_unlock(&process_lock);
		__asm__ volatile("cli");
		for (;;)
			__asm__ volatile("hlt");
	}
	process->status = status;
	process->state = PROCESS_ZOMBIE;
	if (process->waiting_on != nullptr) {
		list_rm(&process->wait_link);
		process->waiting_on = nullptr;
	}
	cpu->current_process = nullptr;
	spin_unlock(&process_lock);

	const fatptr_t *kernel_page_directory = vmm_kernel_page_directory();
	if (kernel_page_directory == nullptr ||
		!vmm_page_directory_activate(kernel_page_directory) ||
		!i386_tss_set_bsp_kernel_stack((uintptr_t)reaper_stack + sizeof(reaper_stack)))
		panic("Unable to enter process reaper\n");
	i386_reaper_enter((uintptr_t)reaper_stack + sizeof(reaper_stack),
	                  process_reaper_cleanup, process);
}

struct process *process_find_child(const struct process *parent, process_pid_t pid)
{
	if (parent == nullptr)
		return nullptr;
	ensure_initialized();
	spin_lock(&process_lock);
	struct process *result = find_pid_locked(pid);
	if (result != nullptr && result->parent != parent)
		result = nullptr;
	spin_unlock(&process_lock);
	return result;
}

size_t process_child_count(const struct process *parent)
{
	if (parent == nullptr)
		return 0;
	size_t count = 0;
	spin_lock(&process_lock);
	list_for_each(&parent->children)
		++count;
	spin_unlock(&process_lock);
	return count;
}

bool process_block(struct process *process, struct wait_queue *queue)
{
	if (process == nullptr || queue == nullptr || process->waiting_on != nullptr)
		return false;
	if (!process_set_state(process, PROCESS_BLOCKED))
		return false;
	process->waiting_on = queue;
	list_add(&process->wait_link, &queue->waiters);
	return true;
}

bool process_wake(struct process *process, struct wait_queue *queue)
{
	if (process == nullptr || process->waiting_on != queue)
		return false;
	list_rm(&process->wait_link);
	process->waiting_on = nullptr;
	bool result = process_set_state(process, PROCESS_READY);
	if (result)
		scheduler_process_ready(process);
	return result;
}

struct process *process_from_wait_link(struct list_head *link)
{
	return link == nullptr ? nullptr : list_entry(link, struct process, wait_link);
}

void process_save_context(struct process *process, const struct i386_context *context)
{
	if (process != nullptr && context != nullptr)
		process->saved_context = *context;
}

bool process_load_context(const struct process *process, struct i386_context *context)
{
	if (process == nullptr || context == nullptr)
		return false;
	*context = process->saved_context;
	return true;
}

struct list_head *process_run_link(struct process *process)
{
	return process == nullptr ? nullptr : &process->run_link;
}

struct process *process_from_run_link(struct list_head *link)
{
	return link == nullptr ? nullptr : list_entry(link, struct process, run_link);
}

bool process_is_queued(const struct process *process)
{
	return process != nullptr && process->queued;
}

void process_set_queued(struct process *process, bool queued)
{
	if (process != nullptr)
		process->queued = queued;
}

bool process_set_affinity(struct process *process, uint8_t cpu)
{
	if (process == nullptr || cpu >= 16)
		return false;
	process->cpu_affinity = cpu;
	return true;
}

struct wait_queue *process_waiting_queue(const struct process *process)
{
	return process == nullptr ? nullptr : process->waiting_on;
}
