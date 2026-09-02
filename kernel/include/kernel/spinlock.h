#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	volatile bool locked;
} spinlock_t;

static inline uint32_t local_irq_save(void)
{
	uint32_t flags;
	__asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	return flags;
}

static inline void local_irq_restore(uint32_t flags)
{
	__asm__ volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

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

static inline uint32_t spin_lock_irqsave(spinlock_t *lock)
{
	uint32_t flags = local_irq_save();
	spin_lock(lock);
	return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags)
{
	spin_unlock(lock);
	local_irq_restore(flags);
}
