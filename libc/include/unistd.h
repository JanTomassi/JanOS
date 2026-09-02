#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef __PTRDIFF_TYPE__ ssize_t;

ssize_t read(int fd, void *buffer, size_t length);
ssize_t write(int fd, const void *buffer, size_t length);
int sched_yield(void);
_Noreturn void _Exit(int status);

#ifdef __cplusplus
}
#endif
