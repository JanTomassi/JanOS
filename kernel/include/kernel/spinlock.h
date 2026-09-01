#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	volatile bool locked;
} spinlock_t;

static inline void spin_lock_init(spinlock_t *lock)
{
	__atomic_clear(&lock->locked, __ATOMIC_RELAXED);
}

static inline void spin_lock(spinlock_t *lock)
{
	while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE))
		__asm__ volatile("pause" : : : "memory");
}

static inline void spin_unlock(spinlock_t *lock)
{
	__atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}
