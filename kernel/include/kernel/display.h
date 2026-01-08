#pragma once

#include <stdint.h>
#include <stdlib.h>

typedef struct {
	uint32_t width;
	uint32_t height;

	void (*puts)(const char *str);
	void (*putc)(char ch);
} display_t;

#define DISPLAY_MAX_DISPS 4

#define MODULE(name) \
  _Pragma("GCC diagnostic push");                                  \
  _Pragma("GCC diagnostic ignored \"-Wunused-variable\"");  \
  static char *__MODULE_NAME = name;                               \
  _Pragma("GCC diagnostic pop");
#define mprint(...) __mprintf(__MODULE_NAME, __VA_ARGS__);

#define kerror(...)                                                                                      \
	{                                                                                                \
		kprintf("***KERNEL ERROR*** in %s:%d in function: %s:\n", __FILE__, __LINE__, __func__); \
		kprintf(__VA_ARGS__);                                                                    \
	}
#define BUG(...)                                                                                           \
	{                                                                                                  \
		kprintf("\n\n*** KERNEL BUG*** in %s:%d in function %s:\n", __FILE__, __LINE__, __func__); \
		kprintf(__VA_ARGS__);                                                                      \
		_Pragma("GCC diagnostic push");                                                            \
		_Pragma("GCC diagnostic ignored \"-Wanalyzer-infinite-loop\"");                            \
		while (1)                                                                                  \
			;                                                                                  \
		_Pragma("GCC diagnostic pop");                                                             \
	}
#define panic(...)                                                                                      \
	{                                                                                               \
		kprintf("***KERNEL PANIC*** in %s:%d in function: %s\n", __FILE__, __LINE__, __func__); \
		kprintf(__VA_ARGS__);                                                                   \
		abort();                                                                                \
	}

uint8_t display_register(display_t);
bool display_setcurrent(uint8_t);
display_t *display_getcurrent();
void __mprintf(char *m, ...);
int kprintf(const char *str, ...);
