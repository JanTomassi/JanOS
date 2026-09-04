#pragma once

#include <stdbool.h>

struct block_device;
struct process;

bool process_service_register_syscalls(void);
void process_service_configure(struct process *service,
	                             const struct block_device *device);
