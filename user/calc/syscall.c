#include "syscall.h"

[[noreturn]] void user_exit(int status)
{
	(void)user_syscall3(USER_SYS_EXIT, (calc_uint32_t)status, 0, 0);
	__builtin_unreachable();
}
