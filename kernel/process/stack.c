#include <kernel/process/stack.h>

#include <kernel/allocator.h>
#include <kernel/process/address_space.h>
#include <kernel/vir_mem.h>
#include <string.h>

struct process_stack {
	void *base;
	size_t size;
	bool user;
	struct address_space *space;
};

static allocator_t allocator;
static bool allocator_ready;

static void ensure_allocator(void)
{
	if (!allocator_ready) {
		allocator = get_slab_allocator();
		allocator_ready = true;
	}
}

static void *alloc_object(size_t size)
{
	ensure_allocator();
	return allocator.alloc(size).ptr;
}

static void free_object(void *ptr, size_t size)
{
	if (ptr != nullptr)
		allocator.free((fatptr_t){ .ptr = ptr, .len = size });
}

static struct process_stack *stack_alloc(size_t size, bool user,
	                                         struct address_space *space)
{
	if (size == 0 || (size & (PAGE_SIZE - 1)) != 0 || (user && space == nullptr))
		return nullptr;
	struct process_stack *stack = alloc_object(sizeof(*stack));
	if (stack == nullptr)
		return nullptr;
	*stack = (struct process_stack){ .size = size, .user = user, .space = space };
	if (!user) {
		fatptr_t memory = allocator.alloc(size);
		if (memory.ptr == nullptr)
			goto fail_stack;
		stack->base = memory.ptr;
		return stack;
	}

	if (!address_space_map_at(space, PROCESS_DEFAULT_USER_STACK_TOP - size, size,
	                          VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_USER_SUPER_BIT,
	                          &stack->base))
		goto fail_stack;
	return stack;

fail_stack:
	free_object(stack, sizeof(*stack));
	return nullptr;
}

struct process_stack *process_kernel_stack_create(size_t size)
{
	return stack_alloc(size, false, nullptr);
}

struct process_stack *process_user_stack_create(struct address_space *space,
	                                                size_t size)
{
	return stack_alloc(size, true, space);
}

void process_stack_destroy(struct process_stack *stack)
{
	if (stack == nullptr)
		return;
	if (stack->user) {
		address_space_unmap(stack->space, stack->base);
	} else {
		allocator.free((fatptr_t){ .ptr = stack->base, .len = stack->size });
	}
	free_object(stack, sizeof(*stack));
}

void *process_stack_base(const struct process_stack *stack)
{
	return stack == nullptr ? nullptr : stack->base;
}

void *process_stack_top(const struct process_stack *stack)
{
	return stack == nullptr ? nullptr : stack->base + stack->size;
}

size_t process_stack_size(const struct process_stack *stack)
{
	return stack == nullptr ? 0 : stack->size;
}

bool process_stack_is_user(const struct process_stack *stack)
{
	return stack != nullptr && stack->user;
}

bool process_user_stack_layout(struct process_stack *stack, int argc,
                               const char *const argv[], void **user_sp)
{
	if (stack == nullptr || !stack->user || argc < 0 || user_sp == nullptr ||
	    (argc != 0 && argv == nullptr))
		return false;
	uint32_t old_cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3) : : "memory");
	const fatptr_t *page_directory = address_space_page_directory(stack->space);
	if (page_directory == nullptr || page_directory->ptr == nullptr)
		return false;
	bool switched = old_cr3 != (uint32_t)(uintptr_t)page_directory->ptr;
	if (switched && !address_space_activate(stack->space))
		return false;
	uintptr_t cursor = (uintptr_t)process_stack_top(stack);
	uintptr_t addresses[argc > 0 ? argc : 1];
	for (int i = argc - 1; i >= 0; --i) {
		size_t length = strlen(argv[i]) + 1;
		if (cursor < (uintptr_t)stack->base + length)
			goto fail;
		cursor = (cursor - length) & ~(uintptr_t)(sizeof(uintptr_t) - 1);
		memcpy((void *)cursor, argv[i], length);
		addresses[i] = cursor;
	}
	cursor &= ~(uintptr_t)(sizeof(uintptr_t) - 1);
	if (cursor < (uintptr_t)stack->base + (size_t)(argc + 2) * sizeof(uintptr_t))
		goto fail;
	cursor -= (size_t)(argc + 2) * sizeof(uintptr_t);
	uintptr_t *vector = (uintptr_t *)cursor;
	vector[0] = (uintptr_t)argc;
	for (int i = 0; i < argc; ++i)
		vector[i + 1] = addresses[i];
	vector[argc + 1] = 0;
	*user_sp = (void *)cursor;
	if (switched)
		__asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
	return true;

fail:
	if (switched)
		__asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
	return false;
}
