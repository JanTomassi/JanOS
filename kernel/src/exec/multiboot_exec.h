#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <arch/i386/context.h>
#include <kernel/block_device.h>

struct process;

struct process_exec_result {
	struct process *process;
	struct i386_context context;
};

/* Load the Multiboot2 module whose complete command line is "calc". */
bool process_exec_multiboot_calc(const void *multiboot_info,
	                                size_t multiboot_info_size,
	                                struct process_exec_result *result);

/* Load the root-directory ELF named "calc" from a block device. */
bool process_exec_block_device_calc(const struct block_device *device,
	                                   struct process_exec_result *result);
