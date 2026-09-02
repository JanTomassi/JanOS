#pragma once

enum janos_syscall_number {
	JANOS_SYS_EXIT = 1,
	JANOS_SYS_YIELD = 2,
	JANOS_SYS_READ = 3,
	JANOS_SYS_WRITE = 4,
	JANOS_SYS_MAX = 32,
};
