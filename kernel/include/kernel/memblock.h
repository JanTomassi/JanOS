#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEMBLOCK_MAX_REGIONS 128
#define MEMBLOCK_ALLOC_FAIL ((size_t)-1)

enum memblock_flags {
	MEMBLOCK_NONE = 0,
	MEMBLOCK_RESERVED = 1u << 0,
};

struct memblock_region {
	size_t base;
	size_t size;
	unsigned long flags;
};

struct memblock_type {
	unsigned int cnt;
	struct memblock_region regions[MEMBLOCK_MAX_REGIONS];
};

struct memblock {
	struct memblock_type memory;
	struct memblock_type reserved;
	bool bottom_up;
};

static inline size_t memblock_align_up(size_t addr, size_t align)
{
	if (align == 0)
		return addr;

	size_t mask = align - 1;
	return (addr + mask) & ~mask;
}

static inline size_t memblock_align_down(size_t addr, size_t align)
{
	if (align == 0)
		return addr;

	size_t mask = align - 1;
	return addr & ~mask;
}

static inline bool memblock_overlaps(size_t base1, size_t size1, size_t base2, size_t size2)
{
	if (size1 == 0 || size2 == 0)
		return false;

	size_t end1 = base1 + size1;
	size_t end2 = base2 + size2;

	if (end1 < base1)
		end1 = SIZE_MAX;
	if (end2 < base2)
		end2 = SIZE_MAX;

	return (base1 < end2) && (base2 < end1);
}

void memblock_init(bool bottom_up);
void memblock_add(size_t base, size_t size);
void memblock_reserve( size_t base, size_t size);
void memblock_remove(size_t base, size_t size);
size_t memblock_alloc_range(size_t size, size_t align, size_t start, size_t end);
void memblock_free(size_t base, size_t size);
