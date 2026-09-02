#include <stdlib.h>

#if !defined(__is_libk)
#include <unistd.h>
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-infinite-loop"
__attribute__((__noreturn__)) void abort(void)
{
#if defined(__is_libk)
	// TODO: Add proper kernel panic.
	asm volatile("hlt");
#else
	_Exit(134);
#endif
	while (1) {
	}
	__builtin_unreachable();
}
#pragma GCC diagnostic pop
