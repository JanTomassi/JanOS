#include "multiboot_exec.h"

#include <kernel/elf_loader.h>
#include <kernel/multiboot.h>
#include <kernel/process/address_space.h>
#include <kernel/process/process.h>
#include <kernel/process/stack.h>
#include <kernel/vir_mem.h>

#include <stdint.h>
#include <string.h>

#define MULTIBOOT_INFO_HEADER_SIZE 8u
#define MULTIBOOT_TAG_HEADER_SIZE 8u
#define USER_TOP 0xc0000000u

struct multiboot_module_image {
	const void *image;
	size_t size;
};

struct load_context {
	struct address_space *space;
};

static bool bounded_calc_name(const char *string, size_t size)
{
	static const char name[] = "calc";
	if (size != sizeof(name) || memcmp(string, name, sizeof(name) - 1) != 0 ||
	    string[sizeof(name) - 1] != '\0')
		return false;
	return true;
}

static bool next_multiboot_tag(const uint8_t *info_end,
	                              const struct multiboot_tag *tag,
	                              const struct multiboot_tag **next)
{
	uintptr_t address = (uintptr_t)tag;
	if (tag->size < MULTIBOOT_TAG_HEADER_SIZE ||
	    (uintptr_t)info_end - address < tag->size)
		return false;

	uint32_t aligned_size = (tag->size + (MULTIBOOT_TAG_ALIGN - 1)) &
		~(MULTIBOOT_TAG_ALIGN - 1);
	if (aligned_size < tag->size || (uintptr_t)info_end - address < aligned_size)
		return false;
	*next = (const struct multiboot_tag *)(address + aligned_size);
	return true;
}

static bool find_calc_module(const void *multiboot_info, size_t info_size,
	                            struct multiboot_module_image *module)
{
	if (multiboot_info == nullptr || module == nullptr ||
	    info_size < MULTIBOOT_INFO_HEADER_SIZE)
		return false;

	const uint8_t *info = multiboot_info;
	uint32_t announced_size;
	memcpy(&announced_size, info, sizeof(announced_size));
	if (announced_size < MULTIBOOT_INFO_HEADER_SIZE || announced_size > info_size)
		return false;
	const uint8_t *info_end = info + announced_size;
	const struct multiboot_tag *tag =
		(const struct multiboot_tag *)(info + MULTIBOOT_INFO_HEADER_SIZE);
	bool found = false;

	while ((uintptr_t)tag < (uintptr_t)info_end) {
		if (tag->type == MULTIBOOT_TAG_TYPE_END) {
			if (tag->size != MULTIBOOT_TAG_HEADER_SIZE)
				return false;
			return found;
		}
		if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
			const struct multiboot_tag_module *candidate =
				(const struct multiboot_tag_module *)tag;
			if (tag->size < sizeof(*candidate))
				return false;
			size_t command_size = tag->size - sizeof(*candidate);
			bool terminated = false;
			for (size_t i = 0; i < command_size; ++i) {
				if (candidate->cmdline[i] == '\0') {
					command_size = i + 1;
					terminated = true;
					break;
				}
			}
			if (!terminated)
				return false;
			if (bounded_calc_name(candidate->cmdline, command_size)) {
				if (candidate->mod_start >= candidate->mod_end)
					return false;
				module->image = (const void *)(uintptr_t)candidate->mod_start;
				module->size = (size_t)(candidate->mod_end - candidate->mod_start);
				found = true;
			}
		}

		const struct multiboot_tag *next;
		if (!next_multiboot_tag(info_end, tag, &next))
			return false;
		tag = next;
	}
	return false;
}

static void *map_load_range(uintptr_t address, size_t size, uint32_t flags,
	                           void *opaque)
{
	struct load_context *context = opaque;
	if (context == nullptr || context->space == nullptr || address < PAGE_SIZE ||
	    address >= USER_TOP || size == 0 || address > USER_TOP - size)
		return nullptr;

	uint16_t mapping_flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_USER_SUPER_BIT;
	if ((flags & ELF_LOAD_WRITE) != 0)
		mapping_flags |= VMM_ENTRY_READ_WRITE_BIT;

	void *mapped = nullptr;
	if (!address_space_map_at(context->space, address, size, mapping_flags, &mapped))
		return nullptr;
	return mapped;
}

static void unmap_load_range(uintptr_t address, size_t size, void *opaque)
{
	struct load_context *context = opaque;
	if (context != nullptr && context->space != nullptr && size != 0)
		(void)address_space_unmap(context->space, (void *)address);
}

bool process_exec_multiboot_calc(const void *multiboot_info,
	                                size_t multiboot_info_size,
	                                struct process_exec_result *result)
{
	if (result == nullptr)
		return false;

	struct multiboot_module_image module;
	if (!find_calc_module(multiboot_info, multiboot_info_size, &module))
		return false;

	struct process *process = process_create(nullptr);
	if (process == nullptr)
		return false;
	struct load_context load_context = {
		.space = process_address_space(process),
	};
	const struct elf_load_ops ops = {
		.map = map_load_range,
		.unmap = unmap_load_range,
		.context = &load_context,
	};
	struct elf_load_result load_result;
	if (!elf32_load(module.image, module.size, &ops, &load_result))
		goto fail;

	const char *argv[] = { "calc", nullptr };
	void *user_stack = nullptr;
	if (!process_user_stack_layout(process_user_stack(process), 1, argv,
	                               &user_stack))
		goto fail;
	const fatptr_t *page_directory = process_page_directory(process);
	if (page_directory == nullptr || page_directory->ptr == nullptr ||
	    !i386_context_init_user(&result->context, load_result.entry,
	                            (uintptr_t)user_stack,
	                            (uintptr_t)page_directory->ptr,
	                            I386_EFLAGS_USER_DEFAULT))
		goto fail;
	if (!process_start(process, load_result.entry, 1, argv))
		goto fail;

	result->process = process;
	return true;

fail:
	process_destroy(process);
	return false;
}
