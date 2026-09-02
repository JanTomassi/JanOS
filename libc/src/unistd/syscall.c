#include <janos/syscall.h>
#include <stdint.h>
#include <unistd.h>

static int32_t syscall3(uint32_t number, uint32_t arg1, uint32_t arg2,
		uint32_t arg3)
{
	int32_t result;
	__asm__ volatile("int $0x80" : "=a"(result)
		: "a"(number), "b"(arg1), "c"(arg2), "d"(arg3) : "memory");
	return result;
}

ssize_t read(int fd, void *buffer, size_t length)
{
	return syscall3(JANOS_SYS_READ, (uint32_t)fd,
		(uint32_t)(uintptr_t)buffer, (uint32_t)length);
}

ssize_t write(int fd, const void *buffer, size_t length)
{
	return syscall3(JANOS_SYS_WRITE, (uint32_t)fd,
		(uint32_t)(uintptr_t)buffer, (uint32_t)length);
}

int sched_yield(void)
{
	return syscall3(JANOS_SYS_YIELD, 0, 0, 0);
}

_Noreturn void _Exit(int status)
{
	(void)syscall3(JANOS_SYS_EXIT, (uint32_t)status, 0, 0);
	__builtin_unreachable();
}
