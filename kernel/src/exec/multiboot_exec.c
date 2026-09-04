#include "multiboot_exec.h"

#include <kernel/elf_loader.h>
#include <kernel/allocator.h>
#include <kernel/fat16.h>
#include <kernel/multiboot.h>
#include <kernel/process/address_space.h>
#include <kernel/process/process.h>
#include <kernel/process/stack.h>
#include <kernel/vir_mem.h>

#include <stdint.h>
#include <string.h>

#define USER_TOP 0xc0000000u

struct multiboot_module_image {
	uintptr_t physical_start;
	size_t size;
};

struct load_context {
	struct address_space *space;
};

static bool bounded_module_name(const char *string, size_t size, const char *name)
{
	size_t name_size = strlen(name) + 1;
	if (size != name_size || memcmp(string, name, name_size - 1) != 0 ||
	    string[name_size - 1] != '\0')
		return false;
	return true;
}

static bool next_multiboot_tag(const uint8_t *info_end,
	                              const struct multiboot_tag *tag,
	                              const struct multiboot_tag **next)
{
	uintptr_t address = (uintptr_t)tag;
	if (address > (uintptr_t)info_end ||
	    (uintptr_t)info_end - address < MULTIBOOT_TAG_HEADER_SIZE ||
	    tag->size < MULTIBOOT_TAG_HEADER_SIZE ||
	    (uintptr_t)info_end - address < tag->size)
		return false;

	uint32_t aligned_size = (tag->size + (MULTIBOOT_TAG_ALIGN - 1)) &
		~(MULTIBOOT_TAG_ALIGN - 1);
	if (aligned_size < tag->size || (uintptr_t)info_end - address < aligned_size)
		return false;
	*next = (const struct multiboot_tag *)(address + aligned_size);
	return true;
}

static bool find_module(const void *multiboot_info, size_t info_size,
	                       const char *name, struct multiboot_module_image *module)
{
	if (multiboot_info == nullptr || module == nullptr || name == nullptr ||
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
			if (bounded_module_name(candidate->cmdline, command_size, name)) {
				if (candidate->mod_start >= candidate->mod_end)
					return false;
				module->physical_start = candidate->mod_start;
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

bool process_find_multiboot_module(const void *multiboot_info,
	                                  size_t multiboot_info_size, const char *name,
	                                  struct multiboot_module_info *module)
{
	if (module == nullptr)
		return false;
	struct multiboot_module_image image;
	if (!find_module(multiboot_info, multiboot_info_size, name, &image))
		return false;
	*module = (struct multiboot_module_info){
		.physical_start = image.physical_start,
		.size = image.size,
	};
	return true;
}

static void *map_load_range(uintptr_t address, size_t size, uint32_t flags,
	                           void *opaque)
{
	(void)flags;
	struct load_context *context = opaque;
	if (context == nullptr || context->space == nullptr || address < PAGE_SIZE ||
	    address >= USER_TOP || size == 0 || address > USER_TOP - size)
		return nullptr;

	uint16_t mapping_flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_USER_SUPER_BIT;
	/* ELF loading is staged writable; the loader applies final permissions. */
	mapping_flags |= VMM_ENTRY_READ_WRITE_BIT;

	void *mapped = nullptr;
	if (!address_space_map_at(context->space, address, size, mapping_flags, &mapped))
		return nullptr;
	return mapped;
}

static bool copy_load_bytes(uintptr_t address, const void *source, size_t size,
							void *opaque)
{
	struct load_context *context = opaque;
	return context != nullptr && address_space_copy_to(context->space, address, source, size);
}

static bool zero_load_bytes(uintptr_t address, size_t size, void *opaque)
{
	struct load_context *context = opaque;
	return context != nullptr && address_space_zero(context->space, address, size);
}

static bool protect_load_range(uintptr_t address, size_t size, uint32_t flags,
							void *opaque)
{
	struct load_context *context = opaque;
	if (context == nullptr)
		return false;
	uint16_t mapping_flags = VMM_ENTRY_USER_SUPER_BIT;
	if ((flags & ELF_LOAD_WRITE) != 0)
		mapping_flags |= VMM_ENTRY_READ_WRITE_BIT;
	return address_space_protect(context->space, address, size, mapping_flags);
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

	const char *argv[] = { "calc", nullptr };
	return process_exec_multiboot_app(multiboot_info, multiboot_info_size,
		"calc", 1, argv, result);
}

bool process_map_block_device_file(const struct block_device *device,
	                              const char *name, struct process *process,
	                              uint16_t flags, void **address, size_t *file_size)
{
	if (device == nullptr || name == nullptr || process == nullptr ||
	    address == nullptr || file_size == nullptr ||
	    (flags & VMM_ENTRY_USER_SUPER_BIT) == 0)
		return false;
	struct fat16_file file;
	if (!fat16_file_open(device, name, &file) || file.entry.file_size == 0)
		return false;
	size_t size = file.entry.file_size;
	if (size > USER_TOP - PAGE_SIZE)
		return false;
	size_t mapped_size = (size_t)round_up_to_page((uintptr_t)size);
	if (mapped_size == 0 || mapped_size > USER_TOP - PAGE_SIZE)
		return false;
	void *mapped = nullptr;
	if (!address_space_map(process_address_space(process), mapped_size,
	                       flags | VMM_ENTRY_READ_WRITE_BIT, &mapped))
		return false;
	allocator_t allocator = get_gpa_allocator();
	fatptr_t image = allocator.alloc(size);
	if (image.ptr == nullptr)
		goto fail_map;
	size_t bytes = 0;
	bool loaded = fat16_file_read_at(&file, 0, image.ptr, size, &bytes) &&
		bytes == size && address_space_copy_to(process_address_space(process),
			(uintptr_t)mapped, image.ptr, size);
	allocator.free(image);
	if (!loaded || !address_space_protect(process_address_space(process),
			(uintptr_t)mapped, mapped_size, flags))
		goto fail_map;
	*address = mapped;
	*file_size = size;
	return true;

fail_map:
	(void)address_space_unmap(process_address_space(process), mapped);
	return false;
}

bool process_exec_multiboot_app(const void *multiboot_info,
	                               size_t multiboot_info_size, const char *name,
	                               int argc, const char *const argv[],
	                               struct process_exec_result *result)
{
	if (result == nullptr || name == nullptr || argc < 0 ||
	    (argc != 0 && argv == nullptr))
		return false;
	if (!process_load_multiboot_app(multiboot_info, multiboot_info_size, name, result))
		return false;
	if (!process_start(result->process, result->entry, argc, argv) ||
	    !process_initial_context(result->process, &result->context)) {
		process_destroy(result->process);
		return false;
	}
	return true;
}

bool process_load_multiboot_app(const void *multiboot_info,
	                               size_t multiboot_info_size, const char *name,
	                               struct process_exec_result *result)
{
	if (result == nullptr || name == nullptr)
		return false;
	struct multiboot_module_image module;
	if (!find_module(multiboot_info, multiboot_info_size, name, &module))
		return false;

	allocator_t allocator = get_gpa_allocator();
	fatptr_t module_copy = allocator.alloc(module.size);
	if (module_copy.ptr == nullptr)
		return false;
	memcpy(module_copy.ptr, (const void *)module.physical_start, module.size);

	struct process *process = process_create(nullptr);
	if (process == nullptr)
		goto fail_copy;
	struct load_context load_context = {
		.space = process_address_space(process),
	};
	const struct elf_load_ops ops = {
		.map = map_load_range,
		.unmap = unmap_load_range,
		.copy = copy_load_bytes,
		.zero = zero_load_bytes,
		.protect = protect_load_range,
		.context = &load_context,
	};
	struct elf_load_result load_result;
	if (!elf32_load(module_copy.ptr, module.size, &ops, &load_result))
		goto fail;

	result->process = process;
	result->entry = load_result.entry;
	return true;

fail:
	process_destroy(process);
fail_copy:
	allocator.free(module_copy);
	return false;
}

bool process_exec_block_device_calc(const struct block_device *device,
	                                   struct process_exec_result *result)
{
	const char *argv[] = { "calc", nullptr };
	return process_exec_block_device_app(device, "calc", 1, argv, result);
}

bool process_load_block_device_app_for_parent(const struct block_device *device,
	                                           const char *name,
	                                           struct process *parent,
	                                           struct process_exec_result *result)
{
	if (device == nullptr || name == nullptr || result == nullptr)
		return false;
	struct process *process = process_create(parent);
	if (process == nullptr)
		return false;
	struct load_context load_context = {
		.space = process_address_space(process),
	};
	const struct elf_load_ops ops = {
		.map = map_load_range,
		.unmap = unmap_load_range,
		.copy = copy_load_bytes,
		.zero = zero_load_bytes,
		.protect = protect_load_range,
		.context = &load_context,
	};
	struct elf_load_result load_result;
	if (!elf32_load_fat16(device, name, &ops, &load_result)) {
		process_destroy(process);
		return false;
	}
	result->process = process;
	result->entry = load_result.entry;
	return true;
}

bool process_load_block_device_app(const struct block_device *device,
	                               const char *name,
	                               struct process_exec_result *result)
{
	return process_load_block_device_app_for_parent(device, name, nullptr, result);
}

bool process_exec_block_device_app(const struct block_device *device,
	                              const char *name, int argc,
	                              const char *const argv[],
	                              struct process_exec_result *result)
{
	if (device == nullptr || name == nullptr || result == nullptr || argc < 0 ||
	    (argc != 0 && argv == nullptr))
		return false;
	if (!process_load_block_device_app(device, name, result))
		return false;
	if (!process_start(result->process, result->entry, argc, argv) ||
	    !process_initial_context(result->process, &result->context)) {
		process_destroy(result->process);
		return false;
	}
	return true;
}
