#include <kernel/process/process.h>

#include <kernel/allocator.h>
#include <kernel/display.h>
#include <kernel/process/address_space.h>
#include <kernel/process/stack.h>
#include <kernel/process/process_service.h>
#include <kernel/process/wait_queue.h>
#include <kernel/framebuffer.h>
#include <kernel/spinlock.h>
#include <kernel/vir_mem.h>
#include <list.h>
#include <arch/i386/smp.h>
#include <kernel/scheduler.h>
#include <kernel/ipc.h>
#include <kernel/syscall.h>
#include <kernel/framebuffer_boot.h>
#include <string.h>

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
	/* Published only after the reaper has stopped using the process. */
	bool exit_complete;
	uint8_t owner_cpu;
	uint8_t cpu_affinity;
	char name[JANOS_PROCESS_NAME_SIZE];
	struct process_ipc_wait ipc_wait;
};

static LIST_HEAD(processes);
static spinlock_t process_lock = { 0 };
static process_pid_t next_pid = 1;
static bool initialized;
static allocator_t allocator;

/* One stack per scheduler CPU prevents concurrent exits from clobbering it. */
#define PROCESS_REAPER_CPU_LIMIT 16
static uint8_t reaper_stacks[PROCESS_REAPER_CPU_LIMIT]
	[PROCESS_DEFAULT_KERNEL_STACK_SIZE] __attribute__((aligned(16)));

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

struct process *process_create_child(process_pid_t parent_pid)
{
	if (parent_pid == 0)
		return nullptr;
	struct process *process = process_create(nullptr);
	if (process == nullptr)
		return nullptr;
	spin_lock(&process_lock);
	struct process *parent = find_pid_locked(parent_pid);
	if (parent == nullptr || parent->state == PROCESS_ZOMBIE ||
		parent->state == PROCESS_DEAD) {
		spin_unlock(&process_lock);
		process_destroy(process);
		return nullptr;
	}
	process->parent = parent;
	list_add(&process->sibling, &parent->children);
	spin_unlock(&process_lock);
	return process;
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
	memset(process->name, 0, sizeof(process->name));
	if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
		size_t name_length = 0;
		while (name_length + 1 < sizeof(process->name) && argv[0][name_length] != '\0')
			++name_length;
		memcpy(process->name, argv[0], name_length);
	}
	process->initial_context = context;
	process->saved_context = context;
	if (cpu->current_process == nullptr) {
		process->state = PROCESS_RUNNING;
		process->owner_cpu = smp_current_cpu_index();
		process->cpu_affinity = process->owner_cpu;
		cpu->current_process = process;
	} else {
		process->state = PROCESS_READY;
		process->owner_cpu = smp_current_cpu_index();
		process->cpu_affinity = process->owner_cpu;
	}
	spin_unlock(&process_lock);
	if (cpu->current_process == process && !i386_tss_init_current()) {
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
	if (process == nullptr || context == nullptr ||
	    (process->state != PROCESS_RUNNING && process->state != PROCESS_READY))
		return false;
	*context = process->saved_context;
	return true;
}

static void process_release(struct process *process)
{
	if (process == nullptr)
		return;
	process_service_process_exiting(process);
	framebuffer_capability_revoke_process(process);
	ipc_process_cleanup(process);
	process_stack_destroy(process->user_stack);
	process_stack_destroy(process->kernel_stack);
	address_space_destroy(process->space);
	allocator.free((fatptr_t){ .ptr = process, .len = sizeof(*process) });
}

void process_destroy(struct process *process)
{
	if (process == nullptr)
		return;
	ensure_initialized();
	spin_lock(&process_lock);
	if (process == process_current() ||
	    (process->state != PROCESS_NEW &&
	     (process->state != PROCESS_ZOMBIE || !process->exit_complete)) ||
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
	process_release(process);
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
	struct process_stack *user_stack;
	struct process_stack *kernel_stack;
	struct address_space *space;
	uint32_t flags = spin_lock_irqsave(&process_lock);
	user_stack = process->user_stack;
	kernel_stack = process->kernel_stack;
	space = process->space;
	/* Hide teardown targets before destroying them outside the lock. */
	process->user_stack = nullptr;
	process->kernel_stack = nullptr;
	process->space = nullptr;
	process->user_entry = 0;
	process->user_stack_pointer = nullptr;
	spin_unlock_irqrestore(&process_lock, flags);
	process_stack_destroy(user_stack);
	process_stack_destroy(kernel_stack);
	address_space_destroy(space);
}

static bool process_autoreap(struct process *process)
{
	if (process == nullptr)
		return false;
	bool autoreaped = false;
	uint32_t flags = spin_lock_irqsave(&process_lock);
	if (process->parent == nullptr &&
		process->state == PROCESS_ZOMBIE &&
		process->children.next == &process->children) {
		process->state = PROCESS_DEAD;
		list_rm(&process->all);
		autoreaped = true;
	}
	spin_unlock_irqrestore(&process_lock, flags);
	return autoreaped;
}

static void process_publish_exit_complete(struct process *process)
{
	uint32_t flags = spin_lock_irqsave(&process_lock);
	process->exit_complete = true;
	spin_unlock_irqrestore(&process_lock, flags);
}

[[noreturn]] static void process_reaper_cleanup(void *argument)
{
	struct process *process = argument;
	process_reap(process);
	if (process_autoreap(process))
		process_release(process);
	else
		process_publish_exit_complete(process);
	/* An exiting process may be the only runnable process on this CPU. */
	scheduler_yield(nullptr);
	for (;;)
		__asm__ volatile("sti; hlt" ::: "memory", "cc");
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
	if (process->state != PROCESS_NEW) {
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
	process_service_process_exiting(process);
	(void)framebuffer_console_release(process);
	framebuffer_capability_revoke_process(process);
	ipc_process_cleanup(process);
	process_reap(process);
	if (process_autoreap(process))
		process_release(process);
	else
		process_publish_exit_complete(process);
}

[[noreturn]] void process_exit_current(int status)
{
	ensure_initialized();
	spin_lock(&process_lock);
	struct cpu_info *cpu = smp_current_cpu();
	struct process *process = cpu == nullptr ? nullptr : cpu->current_process;
	if (process == nullptr) {
		spin_unlock(&process_lock);
		__asm__ volatile("cli" ::: "memory", "cc");
		for (;;)
			__asm__ volatile("hlt");
	}
	if (process->state == PROCESS_ZOMBIE || process->state == PROCESS_DEAD) {
		spin_unlock(&process_lock);
		__asm__ volatile("cli" ::: "memory", "cc");
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
	process_service_process_exiting(process);
	(void)framebuffer_console_release(process);
	framebuffer_capability_revoke_process(process);
	ipc_process_cleanup(process);

	const fatptr_t *kernel_page_directory = vmm_kernel_page_directory();
	uint8_t cpu_index = smp_current_cpu_index();
	if (cpu_index >= PROCESS_REAPER_CPU_LIMIT)
		cpu_index = 0;
	uintptr_t reaper_stack_top =
		(uintptr_t)reaper_stacks[cpu_index] + PROCESS_DEFAULT_KERNEL_STACK_SIZE;
	if (kernel_page_directory == nullptr ||
		!vmm_page_directory_activate(kernel_page_directory) ||
		!i386_tss_set_current_kernel_stack(reaper_stack_top))
		panic("Unable to enter process reaper\n");
	i386_reaper_enter(reaper_stack_top,
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

bool process_exists(process_pid_t pid)
{
	if (pid == 0)
		return false;
	ensure_initialized();
	spin_lock(&process_lock);
	bool result = find_pid_locked(pid) != nullptr;
	spin_unlock(&process_lock);
	return result;
}

struct process *process_find_pid(process_pid_t pid)
{
	if (pid == 0)
		return nullptr;
	ensure_initialized();
	spin_lock(&process_lock);
	struct process *result = find_pid_locked(pid);
	spin_unlock(&process_lock);
	return result;
}

static void process_fill_snapshot(const struct process *process,
	                              struct janos_process_info *info)
{
	*info = (struct janos_process_info){
		.pid = process->pid,
		.state = (uint32_t)process->state,
		.status = process->status,
		.cpu = process->owner_cpu,
		.affinity = process->cpu_affinity,
		.entry = (uint32_t)process->user_entry,
		.address_space = address_space_id(process->space),
	};
	memcpy(info->name, process->name, sizeof(info->name));
}

int32_t process_snapshot(size_t index, struct janos_process_info *info)
{
	if (info == nullptr)
		return -JANOS_EINVAL;
	ensure_initialized();
	spin_lock(&process_lock);
	size_t current = 0;
	struct process *found = nullptr;
	list_for_each(&processes) {
		if (current++ == index) {
			found = list_entry(it, struct process, all);
			break;
		}
	}
	if (found != nullptr)
		process_fill_snapshot(found, info);
	spin_unlock(&process_lock);
	return found == nullptr ? -JANOS_ENOENT : 0;
}

int32_t process_snapshot_pid(process_pid_t pid, struct janos_process_info *info)
{
	if (pid == 0 || info == nullptr)
		return -JANOS_EINVAL;
	ensure_initialized();
	spin_lock(&process_lock);
	struct process *found = find_pid_locked(pid);
	if (found != nullptr)
		process_fill_snapshot(found, info);
	spin_unlock(&process_lock);
	return found == nullptr ? -JANOS_ENOENT : 0;
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

int32_t process_wait_child(struct process *parent, process_pid_t pid,
                           uintptr_t status_address, uint32_t options)
{
	if (parent == nullptr || pid == 0 ||
	    (options & ~JANOS_PROCESS_WAIT_NOHANG) != 0)
		return -JANOS_EINVAL;
	ensure_initialized();
	struct process *child;
	int32_t status;
	struct process *reaped = nullptr;
	uint32_t flags = spin_lock_irqsave(&process_lock);
	child = find_pid_locked(pid);
	if (child == nullptr || child->parent != parent) {
		spin_unlock_irqrestore(&process_lock, flags);
		return -JANOS_ESRCH;
	}
	if (child->state == PROCESS_ZOMBIE && child->exit_complete &&
	    child->children.next == &child->children) {
		status = child->status;
		if (status_address != 0 &&
		    !address_space_copy_to(parent->space, status_address, &status,
		                           sizeof(status))) {
			spin_unlock_irqrestore(&process_lock, flags);
			return -SYSCALL_EFAULT;
		}
		child->state = PROCESS_DEAD;
		list_rm(&child->all);
		list_rm(&child->sibling);
		reaped = child;
		spin_unlock_irqrestore(&process_lock, flags);
		process_release(reaped);
		return (int32_t)pid;
	}
	if ((options & JANOS_PROCESS_WAIT_NOHANG) != 0) {
		spin_unlock_irqrestore(&process_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&process_lock, flags);
	/* The shell polls so it can continue forwarding input events. */
	return -JANOS_EAGAIN;
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

bool process_wait_detach(struct process *process, struct wait_queue *queue)
{
	if (process == nullptr || process->waiting_on != queue)
		return false;
	list_rm(&process->wait_link);
	process->waiting_on = nullptr;
	return true;
}

bool process_wait_requeue(struct process *process, struct wait_queue *from,
                          struct wait_queue *to)
{
	if (process == nullptr || from == nullptr || to == nullptr ||
	    process->waiting_on != from || process_get_state(process) != PROCESS_BLOCKED)
		return false;
	spin_lock(&process_lock);
	if (process->waiting_on != from || process->state != PROCESS_BLOCKED) {
		spin_unlock(&process_lock);
		return false;
	}
	list_rm(&process->wait_link);
	list_add(&process->wait_link, &to->waiters);
	process->waiting_on = to;
	spin_unlock(&process_lock);
	return true;
}

bool process_ipc_wait_begin(struct process *process, uint32_t syscall,
	                         uintptr_t user_buffer, uint32_t deadline)
{
	if (process == nullptr)
		return false;
	spin_lock(&process_lock);
	if (process->ipc_wait.active) {
		spin_unlock(&process_lock);
		return false;
	}
	process->ipc_wait = (struct process_ipc_wait){
		.active = true, .syscall = syscall, .user_buffer = (uint32_t)user_buffer,
		.deadline = deadline,
	};
	spin_unlock(&process_lock);
	return true;
}

bool process_ipc_wait_active(const struct process *process)
{
	if (process == nullptr)
		return false;
	spin_lock(&process_lock);
	bool active = process->ipc_wait.active;
	spin_unlock(&process_lock);
	return active;
}

uint32_t process_ipc_wait_deadline(const struct process *process)
{
	if (process == nullptr)
		return 0;
	spin_lock(&process_lock);
	uint32_t deadline = process->ipc_wait.deadline;
	spin_unlock(&process_lock);
	return deadline;
}

uintptr_t process_ipc_wait_reply_buffer(const struct process *process)
{
	if (process == nullptr)
		return 0;
	spin_lock(&process_lock);
	uintptr_t buffer = process->ipc_wait.active ? process->ipc_wait.user_buffer : 0;
	spin_unlock(&process_lock);
	return buffer;
}

bool process_ipc_wait_set_message(struct process *process, uint32_t endpoint,
                                  const struct janos_ipc_message *message)
{
	if (process == nullptr || message == nullptr)
		return false;
	spin_lock(&process_lock);
	if (!process->ipc_wait.active) {
		spin_unlock(&process_lock);
		return false;
	}
	process->ipc_wait.endpoint = endpoint;
	process->ipc_wait.message = *message;
	spin_unlock(&process_lock);
	return true;
}

bool process_ipc_wait_get_message(const struct process *process, uint32_t *endpoint,
                                  struct janos_ipc_message *message)
{
	if (process == nullptr || endpoint == nullptr || message == nullptr)
		return false;
	spin_lock(&process_lock);
	if (!process->ipc_wait.active) {
		spin_unlock(&process_lock);
		return false;
	}
	*endpoint = process->ipc_wait.endpoint;
	*message = process->ipc_wait.message;
	spin_unlock(&process_lock);
	return true;
}

bool process_ipc_wait_complete(struct process *process, int32_t result,
	                               const struct janos_ipc_message *message)
{
	if (process == nullptr)
		return false;
	spin_lock(&process_lock);
	if (!process->ipc_wait.active) {
		spin_unlock(&process_lock);
		return false;
	}
	bool copied = true;
	if (message != nullptr) {
		if (!address_space_copy_to(process->space, process->ipc_wait.user_buffer,
		                           message, sizeof(*message)))
			copied = false;
	}
	if (!copied)
		result = -SYSCALL_EFAULT;
	if (message != nullptr && copied)
		process->ipc_wait.message = *message;
	process->saved_context.eax = (uint32_t)result;
	process->ipc_wait.active = false;
	spin_unlock(&process_lock);
	return true;
}

bool process_ipc_wait_cancel(struct process *process)
{
	return process_ipc_wait_complete(process, -SYSCALL_ESRCH, nullptr);
}

bool process_ipc_wait_save_context(struct process *process,
                                   const struct i386_context *context)
{
	if (process == nullptr || context == nullptr)
		return false;
	spin_lock(&process_lock);
	if (!process->ipc_wait.active) {
		spin_unlock(&process_lock);
		return false;
	}
	process->saved_context = *context;
	spin_unlock(&process_lock);
	return true;
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
