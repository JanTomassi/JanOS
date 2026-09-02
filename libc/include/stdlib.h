#pragma once
#include <stddef.h>
#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((__noreturn__)) void abort(void);

#if defined(__is_kernel)
#define BIT(x) (8 * x)
#define KIBI(x) (1024 * (x))
#define MIBI(x) (1024 * (KIBI(x)))
#define GIBI(x) (1024 * (MIBI(x)))

struct fatptr {
	size_t len;
	void *ptr;
};
typedef struct fatptr fatptr_t;
#endif

#ifdef __cplusplus
}
#endif
