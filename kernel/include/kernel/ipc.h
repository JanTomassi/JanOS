#pragma once

#include <stdbool.h>

struct process;

void ipc_init(void);
bool ipc_register_syscalls(void);
void ipc_process_cleanup(struct process *process);
