#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <kernel/process/arch/i386/context.h>

struct process;

struct process_exec_result {
	struct process *process;
	struct i386_context context;
};

/* Load the Multiboot2 module whose complete command line is "calc". */
bool process_exec_multiboot_calc(const void *multiboot_info,
	                                size_t multiboot_info_size,
	                                struct process_exec_result *result);
