#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	volatile uint32_t locked;
} spinlock_t;

static inline void spin_lock(spinlock_t *lock)
{
	while (true) {
		uint32_t prev;
		__asm__ volatile("lock xchg %0, %1" : "=r"(prev), "+m"(lock->locked) : "0"(1) : "memory");
		if (prev == 0)
			return;
	}
}

static inline void spin_unlock(spinlock_t *lock)
{
	__asm__ volatile("" : : : "memory");
	lock->locked = 0;
}
