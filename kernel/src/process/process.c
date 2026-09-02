#include <kernel/process/process.h>

#include <kernel/allocator.h>
#include <kernel/display.h>
#include <kernel/process/address_space.h>
#include <kernel/process/stack.h>
#include <kernel/process/wait_queue.h>
#include <kernel/spinlock.h>
#include <kernel/vir_mem.h>
#include <list.h>

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
	struct list_head all;
	struct list_head children;
	struct list_head sibling;
	struct list_head wait_link;
	struct wait_queue *waiting_on;
};

static LIST_HEAD(processes);
static spinlock_t process_lock = { 0 };
static process_pid_t next_pid = 1;
static bool initialized;
static allocator_t allocator;
static struct process *current_process;

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
	return current_process;
}

struct process *process_create(struct process *parent)
{
	ensure_initialized();
	struct process *process = allocator.alloc(sizeof(*process)).ptr;
	if (process == nullptr)
		return nullptr;
	*process = (struct process){ .state = PROCESS_NEW, .parent = parent };
	RESET_LIST_ITEM(&process->all);
	RESET_LIST_ITEM(&process->children);
	RESET_LIST_ITEM(&process->sibling);
	RESET_LIST_ITEM(&process->wait_link);
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
	if (current_process != nullptr || process->state != PROCESS_NEW) {
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
	uint32_t old_cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3) : : "memory");
	if (!address_space_activate(process->space))
		return false;

	spin_lock(&process_lock);
	if (current_process != nullptr || process->state != PROCESS_NEW) {
		spin_unlock(&process_lock);
		__asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
		return false;
	}
	process->user_entry = entry;
	process->user_stack_pointer = user_stack_pointer;
	process->initial_context = context;
	process->state = PROCESS_RUNNING;
	current_process = process;
	spin_unlock(&process_lock);
	if (!i386_tss_init_bsp()) {
		spin_lock(&process_lock);
		current_process = nullptr;
		process->state = PROCESS_NEW;
		spin_unlock(&process_lock);
		__asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
		return false;
	}
	return true;
}

bool process_initial_context(const struct process *process,
							 struct i386_context *context)
{
	if (process == nullptr || context == nullptr || process->state != PROCESS_RUNNING)
		return false;
	*context = process->initial_context;
	return true;
}

void process_destroy(struct process *process)
{
	if (process == nullptr)
		return;
	ensure_initialized();
	spin_lock(&process_lock);
	if (process == current_process || process->state == PROCESS_DEAD ||
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
	struct process *process = current_process;
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
	current_process = nullptr;
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
	return process_set_state(process, PROCESS_READY);
}

struct process *process_from_wait_link(struct list_head *link)
{
	return link == nullptr ? nullptr : list_entry(link, struct process, wait_link);
}
