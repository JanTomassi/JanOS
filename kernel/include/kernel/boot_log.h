#pragma once

#include <stddef.h>

#define KERNEL_BOOT_LOG_CAPACITY (64u * 1024u)

struct boot_log {
	char data[KERNEL_BOOT_LOG_CAPACITY];
	size_t head;
	size_t count;
};

void boot_log_init(struct boot_log *log);
size_t boot_log_write(struct boot_log *log, const char *buffer, size_t length);
size_t boot_log_read(struct boot_log *log, char *buffer, size_t length);
size_t boot_log_available(const struct boot_log *log);
