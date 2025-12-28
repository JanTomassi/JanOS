#pragma once

#include <stddef.h>
#include <stdlib.h>

typedef bool phy_mem_is_used;

struct __attribute__((packed)) phy_mem_8pages_state {
	phy_mem_is_used _0 : 1;
	phy_mem_is_used _1 : 1;
	phy_mem_is_used _2 : 1;
	phy_mem_is_used _3 : 1;
	phy_mem_is_used _4 : 1;
	phy_mem_is_used _5 : 1;
	phy_mem_is_used _6 : 1;
	phy_mem_is_used _7 : 1;
};
_Static_assert(sizeof(struct phy_mem_8pages_state) == 1, "Physical memory 8 pages struct is not 1 byte in size");

struct __attribute__((packed)) phy_mem_16pages_state {
	struct phy_mem_8pages_state _0;
	struct phy_mem_8pages_state _1;
};
_Static_assert(sizeof(struct phy_mem_16pages_state) == 2, "Physical memory 16 pages struct is not 2 byte in size");

struct __attribute__((packed)) phy_mem_32pages_state {
	struct phy_mem_8pages_state _0;
	struct phy_mem_8pages_state _1;
	struct phy_mem_8pages_state _2;
	struct phy_mem_8pages_state _3;
};
_Static_assert(sizeof(struct phy_mem_32pages_state) == 4, "Physical memory 32 pages struct is not 4 byte in size");

/** Set all memory as unused
 **/
void phy_mem_reset();

/** Let the memory allocator know that the memory at the addr and of
 * len can be used by the kernel
 **/
void phy_mem_add_region(size_t addr, size_t len);
void phy_mem_rm_region(size_t addr, size_t len);

void *phy_mem_get_region(size_t size);

size_t phy_mem_get_tot_blocks();
size_t phy_mem_get_used_blocks();
size_t phy_mem_get_free_blocks();

void phy_mem_free(fatptr_t addr_ptr);
__attribute__((hot, malloc(phy_mem_free, 1))) fatptr_t phy_mem_alloc(size_t len);
