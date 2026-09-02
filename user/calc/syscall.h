#pragma once

typedef __SIZE_TYPE__ calc_size_t;
typedef __UINT32_TYPE__ calc_uint32_t;
typedef __INT32_TYPE__ calc_int32_t;

enum {
	USER_SYS_EXIT = 1,
	USER_SYS_READ = 3,
	USER_SYS_WRITE = 4,
};

static inline calc_int32_t user_syscall3(calc_uint32_t number, calc_uint32_t arg1,
		calc_uint32_t arg2, calc_uint32_t arg3)
{
	calc_int32_t result;
	__asm__ volatile("int $0x80" : "=a"(result)
		: "a"(number), "b"(arg1), "c"(arg2), "d"(arg3) : "memory");
	return result;
}

static inline calc_int32_t user_read(int fd, void *buffer, calc_size_t length)
{
	return user_syscall3(USER_SYS_READ, (calc_uint32_t)fd, (calc_uint32_t)buffer, length);
}

static inline calc_int32_t user_write(int fd, const void *buffer, calc_size_t length)
{
	return user_syscall3(USER_SYS_WRITE, (calc_uint32_t)fd, (calc_uint32_t)buffer, length);
}

[[noreturn]] void user_exit(int status);
