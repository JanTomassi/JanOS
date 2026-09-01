#include <kernel/process/stack.h>

#include <kernel/allocator.h>
#include <kernel/process/address_space.h>
#include <kernel/phy_mem.h>
#include <kernel/vir_mem.h>
#include <list.h>
#include <string.h>

struct stack_page {
	fatptr_t physical;
	struct list_head list;
};

struct process_stack {
	void *base;
	size_t size;
	bool user;
	struct address_space *space;
	struct vmm_entry *virtual_range;
	struct list_head pages;
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
	RESET_LIST_ITEM(&stack->pages);

	if (!user) {
		fatptr_t memory = allocator.alloc(size);
		if (memory.ptr == nullptr)
			goto fail_stack;
		stack->base = memory.ptr;
		stack->virtual_range = nullptr;
		return stack;
	}

	stack->virtual_range = vmm_alloc(size, VMM_ENTRY_PRESENT_BIT |
		VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_USER_SUPER_BIT);
	if (stack->virtual_range == nullptr)
		goto fail_stack;
	stack->base = stack->virtual_range->ptr;
	for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
		struct stack_page *page = alloc_object(sizeof(*page));
		if (page == nullptr)
			goto fail_user;
		page->physical = phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH);
		if (page->physical.ptr == nullptr) {
			free_object(page, sizeof(*page));
			goto fail_user;
		}
		struct vmm_entry one_page = { .ptr = stack->base + offset,
			.size = PAGE_SIZE,
			.flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT |
			VMM_ENTRY_USER_SUPER_BIT };
		RESET_LIST_ITEM(&one_page.list);
		map_pages(&page->physical, &one_page);
		list_add(&page->list, &stack->pages);
	}
	return stack;

fail_user:
	while (stack->pages.next != &stack->pages) {
		struct stack_page *page = list_pop_entry(&stack->pages, struct stack_page, list);
		phy_mem_free(page->physical);
		free_object(page, sizeof(*page));
	}
	vmm_free(stack->virtual_range->ptr);
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
		void *address = stack->base;
		while (stack->pages.next != &stack->pages) {
			struct stack_page *page = list_pop_entry(&stack->pages, struct stack_page, list);
			unmap_page(nullptr, address);
			phy_mem_free(page->physical);
			free_object(page, sizeof(*page));
			address += PAGE_SIZE;
		}
		vmm_free(stack->virtual_range->ptr);
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
	uintptr_t cursor = (uintptr_t)process_stack_top(stack);
	uintptr_t addresses[argc > 0 ? argc : 1];
	for (int i = argc - 1; i >= 0; --i) {
		size_t length = strlen(argv[i]) + 1;
		if (cursor < (uintptr_t)stack->base + length)
			return false;
		cursor = (cursor - length) & ~(uintptr_t)(sizeof(uintptr_t) - 1);
		memcpy((void *)cursor, argv[i], length);
		addresses[i] = cursor;
	}
	cursor &= ~(uintptr_t)(sizeof(uintptr_t) - 1);
	if (cursor < (uintptr_t)stack->base + (size_t)(argc + 2) * sizeof(uintptr_t))
		return false;
	cursor -= (size_t)(argc + 2) * sizeof(uintptr_t);
	uintptr_t *vector = (uintptr_t *)cursor;
	vector[0] = (uintptr_t)argc;
	for (int i = 0; i < argc; ++i)
		vector[i + 1] = addresses[i];
	vector[argc + 1] = 0;
	*user_sp = (void *)cursor;
	return true;
}
