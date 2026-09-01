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

void memblock_init(unsigned long mbi_addr, bool bottom_up);

size_t memblock_bottom_address();
size_t memblock_top_address();
struct memblock_type* memblock_get_memory();
struct memblock_type* memblock_get_reserved();
bool memblock_overlaps(size_t base1, size_t size1, size_t base2, size_t size2);
bool memblock_region_available(size_t base, size_t size);
void memblock_dump(void);

void memblock_add(size_t base, size_t size);
void memblock_reserve(size_t base, size_t size);
void memblock_remove(size_t base, size_t size);
size_t memblock_alloc_range(size_t size, size_t align, size_t start, size_t end);
void memblock_free(size_t base, size_t size);
