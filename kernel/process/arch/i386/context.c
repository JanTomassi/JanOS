#include <kernel/process/arch/i386/context.h>

#include <kernel/process/process.h>
#include <kernel/process/stack.h>
#include <kernel/vir_mem.h>

#include <string.h>

struct i386_gdtr {
	uint16_t limit;
	uintptr_t base;
} __attribute__((packed));

[[noreturn]] extern void i386_context_enter_asm(const struct i386_context *context);

static struct i386_cpu_state bsp_cpu_state;
static bool bsp_tss_loaded;

static uint64_t tss_descriptor(uintptr_t base, size_t limit)
{
	return (limit & 0xffffu) |
		((base & 0xffffffu) << 16) |
		(0x89ull << 40) |
		(((uint64_t)limit & 0xf0000u) << 32) |
		(((uint64_t)base & 0xff000000ull) << 32);
}

bool i386_tss_install(struct i386_cpu_state *state, uintptr_t kernel_stack_top)
{
	if (state == nullptr || kernel_stack_top == 0 ||
	    (kernel_stack_top & 3u) != 0)
		return false;

	struct i386_gdtr old_gdtr;
	__asm__ volatile("sgdt %0" : "=m"(old_gdtr));
	if (old_gdtr.base == 0 || old_gdtr.limit < 39)
		return false;

	memcpy(state->gdt, (const void *)old_gdtr.base, 5 * sizeof(uint64_t));
	memset(&state->tss, 0, sizeof(state->tss));
	state->tss.esp0 = (uint32_t)kernel_stack_top;
	state->tss.ss0 = I386_KERNEL_DATA_SELECTOR;
	state->tss.iomap_base = sizeof(state->tss);
	state->gdt[5] = tss_descriptor((uintptr_t)&state->tss,
		                               sizeof(state->tss) - 1);

	struct i386_gdtr new_gdtr = {
		.limit = sizeof(state->gdt) - 1,
		.base = (uintptr_t)state->gdt,
	};
	const uint16_t tss_selector = 5u << 3;
	__asm__ volatile("lgdt %0\n\n"
			     "mov %1, %%ax\n\n"
			     "mov %%ax, %%ds\n\n"
			     "mov %%ax, %%es\n\n"
			     "mov %%ax, %%ss\n\n"
			     "ltr %2"
			     :
			     : "m"(new_gdtr), "i"(I386_KERNEL_DATA_SELECTOR),
			       "m"(tss_selector)
			     : "ax", "memory");
	return true;
}

bool i386_tss_init_bsp(void)
{
	struct process *process = process_current();
	struct process_stack *stack = process_kernel_stack(process);
	uintptr_t stack_top = (uintptr_t)process_stack_top(stack);

	if (stack_top == 0)
		return false;
	if (bsp_tss_loaded)
		return i386_tss_set_bsp_kernel_stack(stack_top);
	if (!i386_tss_install(&bsp_cpu_state, stack_top))
		return false;
	bsp_tss_loaded = true;
	return true;
}

bool i386_tss_set_bsp_kernel_stack(uintptr_t kernel_stack_top)
{
	if (!bsp_tss_loaded || kernel_stack_top == 0 ||
	    (kernel_stack_top & 3u) != 0)
		return false;
	bsp_cpu_state.tss.esp0 = (uint32_t)kernel_stack_top;
	return true;
}

static bool user_address(uintptr_t address)
{
	/* The kernel occupies the higher half; user pointers must stay below it. */
	return address >= PAGE_SIZE && address < 0xc0000000u;
}

bool i386_context_init_user(struct i386_context *context, uintptr_t entry,
	uintptr_t user_stack, uintptr_t page_directory, uint32_t eflags)
{
	if (context == nullptr || !user_address(entry) || !user_address(user_stack) ||
	    page_directory == 0 || (page_directory & (PAGE_SIZE - 1)) != 0)
		return false;

	/* iret must see a valid reserved bit and must not grant user I/O privilege. */
	if ((eflags & I386_EFLAGS_RESERVED) == 0 || (eflags & 0x3000u) != 0 ||
	    (eflags & 0x00060000u) != 0)
		return false;

	*context = (struct i386_context){
		.eip = (uint32_t)entry,
		.cs = I386_USER_CODE_SELECTOR | 3u,
		.eflags = eflags,
		.useresp = (uint32_t)user_stack,
		.ss = I386_USER_DATA_SELECTOR | 3u,
		.cr3 = (uint32_t)page_directory,
	};
	return true;
}

[[noreturn]] void i386_context_enter_user(const struct i386_context *context)
{
	i386_context_enter_asm(context);
}

[[noreturn]] void i386_context_return_user(const struct i386_context *context)
{
	i386_context_enter_asm(context);
}
