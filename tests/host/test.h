#pragma once

#include <stdlib.h>

extern int dprintf(int file_descriptor, const char *format, ...);

static inline void test_fail(const char *file, int line, const char *condition)
{
	dprintf(2, "%s:%d: test failed: %s\n", file, line, condition);
	abort();
}

#define TEST_ASSERT(condition) \
	do { \
		if (!(condition)) \
			test_fail(__FILE__, __LINE__, #condition); \
	} while (0)
