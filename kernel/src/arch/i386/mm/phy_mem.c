#include <kernel/memblock.h>
#include <kernel/phy_mem.h>
#include <kernel/display.h>
#include <kernel/sections.h>
#include <string.h>
#include <stdlib.h>
#include <list.h>
#include <kernel/spinlock.h>

MODULE("Physical Memory");

#define ceil_div(x, y) ((x) / (y) + ((x) % (y) != 0))

static struct buddy_bitmap{
	size_t  pool_start;     // buddy managed memory start
	size_t  pool_size;      // buddy managed memory size
	size_t  min_block_size; // level 0 size
	uint8_t levels;
	size_t  top_childs_count; // Number of childs from level 0
	size_t  level_0_count;

	// one bit per block
	uint8_t *bitmap;
	size_t bitmap_size;
	size_t bitmap_managed_size;
} buddy_bitmap;

static spinlock_t buddy_lock = { 0 };

__pure static inline fatptr_t phy_mem_invalid(void)
{
	return (fatptr_t){ .ptr = nullptr, .len = 0 };
}

__pure static inline size_t bitmap_get_max_idx(size_t level){
	const size_t idx_s = (1 << level)-1;
	const size_t idx_e = (1 << (level+1))-1; // Not inclusive range
	const size_t idx_count = idx_e - idx_s;
	return idx_count;
}

__hot static inline bool bitmap_get_bit(size_t idx)
{
	if(idx/8 >= buddy_bitmap.bitmap_size){
		panic("bitmap access is outside range: max is %x, requested %x", buddy_bitmap.bitmap_size*8, idx);
	}
        return buddy_bitmap.bitmap[idx/8] >> (idx % 8) & 0x1;
}

__hot static inline bool bitmap_set_bit(size_t idx, bool val)
{
	if(idx/8 >= buddy_bitmap.bitmap_size){
		panic("bitmap access is outside range: max is %x, requested %x", buddy_bitmap.bitmap_size*8, idx);
	}
	const bool prev_val = bitmap_get_bit(idx);
	buddy_bitmap.bitmap[idx/8] = (buddy_bitmap.bitmap[idx/8] & ~(1<< (idx % 8))) | (val << (idx % 8));
	return prev_val;
}

__hot __const static inline size_t bitmap_get_index(size_t tree, size_t level, size_t idx)
{
	if(tree >= buddy_bitmap.level_0_count){
		panic("Requested tree is to big: max is %x, requested %x", buddy_bitmap.level_0_count, tree);
	}else if (level >= buddy_bitmap.levels){
		panic("Requested level is to depth: max is %x, requested %x", buddy_bitmap.levels - 1, level);
	}

	const size_t idx_count = bitmap_get_max_idx(level);

	if (idx >= idx_count){
		panic("Requested idx is to big: max is %x, requested %x", idx_count + 1, idx);
	}

	const size_t level_start = (1u << level) - 1;
	return tree * buddy_bitmap.top_childs_count + level_start + idx;
}

__hot __const static inline size_t bitmap_get_tree_from_idx(size_t idx)
{
	return idx / buddy_bitmap.top_childs_count;
}

__hot __const static inline size_t bitmap_get_level_from_idx(size_t idx)
{
	const unsigned long tree_idx = idx % buddy_bitmap.top_childs_count;
	if (tree_idx == 0) return 0;
	return (sizeof(tree_idx) * 8) - 1 - __builtin_clzl(tree_idx+1);
}

__hot __const static inline size_t bitmap_get_idx_at_level(size_t idx, size_t level)
{
	const size_t idx_s = (1u << level)-1;
	const size_t idx_e = (1u << (level+1))-1; // Not inclusive range
	const size_t idx_count = idx_e - idx_s;
	const size_t local = idx % buddy_bitmap.top_childs_count;
	return (local - idx_s) % idx_count;
}

static inline void bitmap_print_state_of_idx(size_t idx)
{
	size_t level = bitmap_get_level_from_idx(idx);

	kprintf("global_idx: %x, tree %x, level %x, idx %x, state %x\n",
		idx,
		bitmap_get_tree_from_idx(idx),
		level,
		bitmap_get_idx_at_level(idx, level),
		bitmap_get_bit(idx));
}

__hot static inline void bitmap_update_top(size_t idx)
{
    const size_t tree = bitmap_get_tree_from_idx(idx);

    size_t level  = bitmap_get_level_from_idx(idx);
    size_t offset = bitmap_get_idx_at_level(idx, level);

	while (level > 0) {
		const size_t parent_level = level - 1;
		const size_t parent_offset = offset >> 1;
		const size_t parent = bitmap_get_index(tree, parent_level, parent_offset);
		const size_t left = bitmap_get_index(tree, level, parent_offset << 1);
		const size_t right = bitmap_get_index(tree, level, (parent_offset << 1) + 1);
		bitmap_set_bit(parent, bitmap_get_bit(left) && bitmap_get_bit(right));
		level = parent_level;
		offset = parent_offset;
	}
}


__hot static inline void bitmap_update_down(size_t idx, bool new_val)
{
	size_t tree = bitmap_get_tree_from_idx(idx);
	size_t level = bitmap_get_level_from_idx(idx);
	size_t offset = bitmap_get_idx_at_level(idx, level);

	for (size_t l = level + 1; l < buddy_bitmap.levels; l++) {
		size_t shift = l - level;
		size_t start = offset << shift;
		size_t count = 1u << shift;

		for (size_t i = start; i < start + count; i++) {
			size_t child_idx = bitmap_get_index(tree, l, i);
			bitmap_set_bit(child_idx, new_val);
		}
	}
}

__hot static inline bool bitmap_block_is_free(size_t idx)
{
	const size_t level = bitmap_get_level_from_idx(idx);
	const size_t tree = bitmap_get_tree_from_idx(idx);
	const size_t offset = bitmap_get_idx_at_level(idx, level);

	if (bitmap_get_bit(idx))
		return false;

	for (size_t l = level + 1; l < buddy_bitmap.levels; l++) {
		const size_t shift = l - level;
		const size_t start = offset << shift;
		const size_t count = 1u << shift;
		for (size_t i = start; i < start + count; i++)
			if (bitmap_get_bit(bitmap_get_index(tree, l, i)))
				return false;
	}

	return true;
}

__hot static inline bool bitmap_use_block(size_t idx)
{
	if (!bitmap_block_is_free(idx))
		return false;

	bitmap_set_bit(idx, true);

	bitmap_update_top(idx);

	bitmap_update_down(idx, true);

	return true;
}

__hot static inline size_t buddy_req_level(size_t len, size_t leaf, size_t minb)
{
	const size_t blocks = ceil_div(len, minb);
	const size_t floor_k =
		(sizeof((unsigned long)blocks) * 8) - 1 - __builtin_clzl((unsigned long)blocks);
	const int k = (int)leaf - (int)(floor_k + ((blocks & (blocks - 1u)) != 0u));
	return (k <= 0) ? 0 : (size_t)k;
}

__hot static inline void buddy_tree_range(phy_mem_alloc_mode_t mode, size_t tree_cnt,
                                   size_t *t_lo, size_t *t_hi)
{
	*t_lo = 0;
	*t_hi = tree_cnt ? tree_cnt - 1 : 0;

	if (mode == PHY_MEM_ALLOC_LOW_16BIT)
		*t_hi = *t_lo = 0;
	// HIGH_ONLY scans all trees (including tree 0) but enforces addr >= +64KiB via offset range
}

__hot static inline bool buddy_offset_range(phy_mem_alloc_mode_t mode,
                                      uintptr_t tree_base,
                                      size_t len, size_t blk_size,
                                      size_t idx_n, size_t *o0, size_t *o1)
{
	const size_t PHY_MEM_64K = (size_t)1u << 16;
	*o0 = 0;
	*o1 = idx_n;

	switch(mode){
	case PHY_MEM_ALLOC_LOW_16BIT:
	case PHY_MEM_ALLOC_LOW_1M:
	{
		const size_t limit = mode == PHY_MEM_ALLOC_LOW_16BIT ? (size_t)1u << 16 : (size_t)1u << 20;
		if (len > limit)  BUG("low-memory request too big: %x bytes", len);
		if (blk_size > limit) BUG("low-memory block too big: %x bytes", blk_size);

		if (tree_base >= limit)
			return false;
		/* Physical address zero is reserved; SIPI vector zero is not usable. */
		if (tree_base == 0)
			*o0 = 1;
		if (tree_base + idx_n * blk_size > limit)
			*o1 = (limit - tree_base) / blk_size;
		return *o0 < *o1;
	}
	break;

	case PHY_MEM_ALLOC_HIGH:
	{
		const uintptr_t boundary = PHY_MEM_64K;

		// skip blocks that would start before boundary
		if (tree_base < boundary) {
			const size_t delta = (size_t)(boundary - tree_base);
			const size_t skip  = ceil_div(delta, blk_size);
			if (skip >= idx_n) return false;
			*o0 = skip;
		}
		// (tree_base >= boundary) => start at 0
		return *o0 < *o1;
	}
	break;

	default:
		return true; // ANY
	}
}

__hot fatptr_t phy_mem_alloc(size_t len, phy_mem_alloc_mode_t mode)
{
	spin_lock(&buddy_lock);
	if (!len) {
		spin_unlock(&buddy_lock);
		return phy_mem_invalid();
	}

	const size_t leaf = buddy_bitmap.levels - 1;
	const size_t minb = buddy_bitmap.min_block_size;

	const size_t level = buddy_req_level(len, leaf, minb);
	const size_t size  = minb << (leaf - level);
	if (len > (minb << leaf)) {
		spin_unlock(&buddy_lock);
		return phy_mem_invalid();
	}
	const size_t idx_n = bitmap_get_max_idx(level);

	size_t t_lo, t_hi;
	buddy_tree_range(mode, buddy_bitmap.level_0_count, &t_lo, &t_hi);

	const uintptr_t pool = (uintptr_t)buddy_bitmap.pool_start;
	const size_t tree_span = (minb << leaf);

	for (size_t t = t_hi;; --t) {
		const uintptr_t base = pool + t * tree_span;

		size_t o_s, o_e;
		if (!buddy_offset_range(mode, base, len, size, idx_n, &o_s, &o_e)) {
			if (t == t_lo) break;
			continue;
		}

		for (size_t o = o_s; o < o_e; ++o) {
			const size_t p = bitmap_get_index(t, level, o);
			const uintptr_t addr = base + o * size;
			if (addr < pool || addr + size > pool + buddy_bitmap.pool_size)
				continue;
			if (bitmap_use_block(p)) {
				fatptr_t result = (fatptr_t){
					.ptr = (void*)addr,
					.len = size,
				};
				spin_unlock(&buddy_lock);
				return result;
			}
		}

		if (t == t_lo) break;
	}

	spin_unlock(&buddy_lock);
	return phy_mem_invalid();
}


__hot void phy_mem_free(fatptr_t alloc)
{
	if (alloc.ptr == nullptr || alloc.len == 0)
		return;
	spin_lock(&buddy_lock);

	const uintptr_t addr = (uintptr_t)alloc.ptr;
	const uintptr_t base = (uintptr_t)buddy_bitmap.pool_start;
	const uintptr_t end  = base + (uintptr_t)buddy_bitmap.pool_size;

	if (addr < base || addr >= end)
		BUG("phy_mem_free: ptr out of pool");
	if (alloc.len > buddy_bitmap.pool_size || addr + alloc.len > end)
		BUG("phy_mem_free: range out of pool");
	if (alloc.len % buddy_bitmap.min_block_size != 0)
		BUG("phy_mem_free: len not multiple of min block");

	const size_t leaf_level = buddy_bitmap.levels - 1;
	const size_t tree_size  = buddy_bitmap.min_block_size << leaf_level; /* max block per tree */

	/* length must be a power-of-two multiple of min_block_size */
	const size_t alloc_count = alloc.len / buddy_bitmap.min_block_size;
	if (alloc_count == 0 || (alloc_count & (alloc_count - 1)) != 0)
		BUG("phy_mem_free: len not power-of-two buddy size");

	/* alloc_count = 2^(leaf_level - alloc_level)  =>  alloc_level = leaf_level - log2(alloc_count) */
	const unsigned k = (unsigned)(sizeof(unsigned long long) * 8 - 1 - __builtin_clzll((unsigned long long)alloc_count));
	if (k > leaf_level)
		BUG("phy_mem_free: len too small for tree");
	const size_t alloc_level = leaf_level - (size_t)k;

	const size_t off_bytes   = (size_t)(addr - base);
	const size_t tree_id     = off_bytes / tree_size;
	const size_t off_in_tree = off_bytes % tree_size;

	/* must not cross a tree boundary */
	if (off_in_tree + alloc.len > tree_size)
		BUG("phy_mem_free: allocation crosses tree boundary");

	const size_t block_size = tree_size >> alloc_level;

	/* must be aligned to the block size at alloc_level */
	if ((off_in_tree % block_size) != 0)
		BUG("phy_mem_free: ptr misaligned for level");

	const size_t i = off_in_tree / block_size; /* position within that level */
	const size_t idx = bitmap_get_index(tree_id, alloc_level, i);

	/* Expect it to be currently allocated; if already free => double-free/corruption */
	if (!bitmap_set_bit(idx, false))
		BUG("phy_mem_free: double free or corrupt bitmap");

	bitmap_update_top(idx);

	bitmap_update_down(idx, false);
	spin_unlock(&buddy_lock);
}

__init static void phy_mem_remove_unreachable(void)
{
	size_t reg_ptr = buddy_bitmap.pool_start;
	const size_t leaf_level = buddy_bitmap.levels - 1;
	const size_t small_size = buddy_bitmap.min_block_size;
	const size_t tree_size  = small_size << leaf_level; // max block size per tree

	do {
		const bool inside_pool =
			reg_ptr < buddy_bitmap.pool_start + buddy_bitmap.pool_size &&
			reg_ptr + small_size <= buddy_bitmap.pool_start + buddy_bitmap.pool_size;
		if (!inside_pool || !memblock_region_available(reg_ptr, small_size)) {
			uintptr_t addr = (uintptr_t)reg_ptr;
			uintptr_t base = (uintptr_t)buddy_bitmap.pool_start;

			size_t off_bytes = (size_t)(addr - base);
			size_t tree_id   = off_bytes / tree_size;
			size_t i         = (off_bytes % tree_size) / small_size; // leaf index

			const size_t to_remove = bitmap_get_index(tree_id, leaf_level, i);
			bitmap_set_bit(to_remove, true);
			bitmap_update_top(to_remove);
		}

		reg_ptr += small_size;
	} while (reg_ptr < buddy_bitmap.pool_start + buddy_bitmap.bitmap_managed_size);
}

__init void phy_mem_init()
{
	mprint("Initializing physical memory allocator\n");

	const size_t levels = 11;
	const size_t min_block_size = 4096;

	const size_t top_addr = memblock_top_address() & ~0xFFF;
	const size_t bottom_addr = (memblock_bottom_address() + 0xFFF) & ~0xFFF;
	const size_t pool_size = top_addr - bottom_addr;
	if (top_addr <= bottom_addr || pool_size < min_block_size)
		panic("No usable physical memory range\n");
	mprint("Physical address ranges: [%x-%x]\n", bottom_addr, top_addr);
	mprint("Number of pages in pool: %x\n", pool_size / min_block_size);

	// Get the size of the buddy allocator, one bit per page, and align start and end to page
	const size_t top_childs_count = ((1<<(levels))-1);
	const size_t level_0_count =
		ceil_div(pool_size, min_block_size * (1<<(levels-1))); // Divide by the biggest level size

	const size_t bitmap_size = (ceil_div((top_childs_count * level_0_count) // count of node needed per big block
					    , 8) + 0xFFF) & ~0xFFF;
	mprint("Required memory for buddy allocator: %x\n", bitmap_size);

	const size_t phy_mem_addr = memblock_alloc_range(bitmap_size, 4096, 0, -1);
	if (phy_mem_addr == MEMBLOCK_ALLOC_FAIL)
		panic("Unable to reserve buddy bitmap\n");
	memset((void*)phy_mem_addr, 0, bitmap_size);
	mprint("Buddy allocator addr: %x\n", phy_mem_addr);

        uint8_t *const bitmap = (uint8_t*)phy_mem_addr;

	buddy_bitmap = (struct buddy_bitmap){
		.pool_start = bottom_addr,
		.pool_size = pool_size,
		.min_block_size = min_block_size,
		.levels = levels,
		.top_childs_count = top_childs_count,
		.level_0_count = level_0_count,
		.bitmap = bitmap,
		.bitmap_size = bitmap_size,
		.bitmap_managed_size = level_0_count * (min_block_size * (1<<(levels-1))),
	};

	phy_mem_remove_unreachable();

	size_t free_pages = 0;
	const size_t leaf_nodes = bitmap_get_max_idx(levels - 1);
	for (size_t tree = 0; tree < level_0_count; tree++) {
		for (size_t page = 0; page < leaf_nodes; page++) {
			if (!bitmap_get_bit(bitmap_get_index(tree, levels - 1, page)))
				free_pages++;
		}
	}
	kprintf("buddy: pool %x-%x, bitmap %x-%x, free pages %x\n",
	         bottom_addr, bottom_addr + pool_size,
	         phy_mem_addr, phy_mem_addr + bitmap_size, free_pages);

}
