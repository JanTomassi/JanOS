#include <kernel/boot_log.h>

#include <stddef.h>

void boot_log_init(struct boot_log *log)
{
	if (log != NULL)
		*log = (struct boot_log){ 0 };
}

size_t boot_log_write(struct boot_log *log, const char *buffer, size_t length)
{
	if (log == NULL || (buffer == NULL && length != 0))
		return 0;
	for (size_t i = 0; i < length; ++i) {
		if (log->count == KERNEL_BOOT_LOG_CAPACITY) {
			log->head = (log->head + 1) % KERNEL_BOOT_LOG_CAPACITY;
			--log->count;
		}
		log->data[(log->head + log->count) % KERNEL_BOOT_LOG_CAPACITY] = buffer[i];
		++log->count;
	}
	return length;
}

size_t boot_log_read(struct boot_log *log, char *buffer, size_t length)
{
	if (log == NULL || buffer == NULL || length == 0)
		return 0;
	size_t count = length < log->count ? length : log->count;
	for (size_t i = 0; i < count; ++i)
		buffer[i] = log->data[(log->head + i) % KERNEL_BOOT_LOG_CAPACITY];
	log->head = (log->head + count) % KERNEL_BOOT_LOG_CAPACITY;
	log->count -= count;
	return count;
}

size_t boot_log_available(const struct boot_log *log)
{
	return log == NULL ? 0 : log->count;
}
