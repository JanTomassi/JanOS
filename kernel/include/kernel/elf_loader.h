#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct storage_device;

struct elf_load_ops {
	/* map must reserve exactly the requested user virtual range and return a
	 * kernel-accessible pointer to it. It may reject addresses above user space. */
	void *(*map)(uintptr_t address, size_t size, uint32_t flags, void *context);
	void (*unmap)(uintptr_t address, size_t size, void *context);
	void *context;
};

enum elf_load_flags {
	ELF_LOAD_READ = 1u << 0,
	ELF_LOAD_WRITE = 1u << 1,
	ELF_LOAD_EXEC = 1u << 2,
};

struct elf_load_result {
	uintptr_t entry;
	uintptr_t lowest_address;
	uintptr_t highest_address;
	unsigned int segment_count;
};

bool elf32_load(const void *image, size_t image_size,
		const struct elf_load_ops *ops, struct elf_load_result *result);

bool elf32_load_fat16(const struct storage_device *device, const char *name,
			const struct elf_load_ops *ops, struct elf_load_result *result);
