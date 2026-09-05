#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/i386/context.h>
#include <kernel/block_device.h>

struct process;

struct process_exec_result {
	struct process *process;
	uintptr_t entry;
	struct i386_context context;
};

struct multiboot_module_info {
	uintptr_t physical_start;
	size_t size;
};

bool process_find_multiboot_module(const void *multiboot_info,
	                                  size_t multiboot_info_size, const char *name,
	                                  struct multiboot_module_info *module);

/* Load a Multiboot2 module by its complete command line. */
bool process_exec_multiboot_app(const void *multiboot_info,
	                               size_t multiboot_info_size,
	                               const char *name, int argc,
	                               const char *const argv[],
	                               struct process_exec_result *result);
bool process_load_multiboot_app(const void *multiboot_info,
	                               size_t multiboot_info_size,
	                               const char *name,
	                               struct process_exec_result *result);

/* Compatibility wrapper for the original calc boot path. */
bool process_exec_multiboot_calc(const void *multiboot_info,
	                                size_t multiboot_info_size,
	                                struct process_exec_result *result);

/* Load an ELF from a FAT16 root directory without starting it. */
bool process_load_block_device_app(const struct block_device *device,
	                                   const char *name,
	                                   struct process_exec_result *result);
bool process_load_block_device_app_for_parent(const struct block_device *device,
	                                           const char *name,
	                                           struct process *parent,
	                                           struct process_exec_result *result);
bool process_load_block_device_app_for_parent_pid(const struct block_device *device,
	                                               const char *name,
	                                               uint32_t parent_pid,
	                                               struct process_exec_result *result);

/* Load and start an ELF from a FAT16 root directory. */
bool process_exec_block_device_app(const struct block_device *device,
	                                  const char *name, int argc,
	                                  const char *const argv[],
	                                  struct process_exec_result *result);

/* Map a FAT16 file into a process as read-only user memory. */
bool process_map_block_device_file(const struct block_device *device,
	                                  const char *name, struct process *process,
	                                  uint16_t flags, void **address,
	                                  size_t *file_size);

/* Compatibility wrapper for the original calc disk path. */
bool process_exec_block_device_calc(const struct block_device *device,
	                                   struct process_exec_result *result);
