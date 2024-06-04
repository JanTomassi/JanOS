#include <kernel/phy_mem.h>
#include <string.h>
#include <stdlib.h>
#include <list.h>

#define BLOCK_SIZE (KIBI(4ULL))
#define ALLOC_BITMAP \
	(GIBI(4ULL) / BLOCK_SIZE / BIT(sizeof(uint32_t))) // (0x8000)
static_assert(ALLOC_BITMAP == 0x8000); // Limit to x86_32 max addressable memory

/** 
 * Memory book keeping will be keept using a linked list of block all
 * the element of each block will be a fast pointer (address and size)
 * and the last element will rapresent links to next and prev
 **/
#define BOOKING_COUNT BLOCK_SIZE / sizeof(fatptr_t) - 1
struct booking_block {
	fatptr_t allocs[BLOCK_SIZE / sizeof(fatptr_t) - 1];
	struct list_head list;
};
static_assert(sizeof(struct booking_block) == BLOCK_SIZE,
	      "Booking block is not equal to a block, this is not allowed");

static uint32_t allocation_bitmap[ALLOC_BITMAP] = { 0 };
static struct booking_block base_booking_block;
static LIST_HEAD(booking_block_list);
static size_t tot_blocks = sizeof(allocation_bitmap) * 8;
static size_t free_blocks = 0;

void phy_mem_reset()
{
	memset(allocation_bitmap, 0xff, sizeof(allocation_bitmap));

	tot_blocks = sizeof(allocation_bitmap) * 8;
	free_blocks = 0;

	RESET_LIST_ITEM(&booking_block_list);
	list_add(&(base_booking_block.list), &booking_block_list);
};

__attribute__((hot)) static void phy_mem_set_region(size_t addr, size_t len,
						    bool set)
{
	const size_t page_offset = addr / 4096;
	const size_t num_pages = (len / 4096) + 1;

	for (size_t i = 0; i < num_pages; i++) {
		const uint32_t block = (page_offset + i) / 32;
		const uint32_t block_bit = (page_offset + i) % 32;
		const uint32_t bit_idx = 1 << block_bit;

		if ((allocation_bitmap[block] >> block_bit) != set) {
			allocation_bitmap[block] &= (~bit_idx);
			allocation_bitmap[block] |= set << block_bit;
			free_blocks += set ? -1 : 1;
		}
	}
}

size_t phy_mem_get_tot_blocks()
{
	return tot_blocks;
}

size_t phy_mem_get_used_blocks()
{
	return tot_blocks - free_blocks;
}

size_t phy_mem_get_free_blocks()
{
	return free_blocks;
}

void phy_mem_rm_region(size_t addr, size_t len)
{
	phy_mem_set_region(addr, len, true);
}

void phy_mem_add_region(size_t addr, size_t len)
{
	phy_mem_set_region(addr, len, false);
}

__attribute__((hot, malloc(phy_mem_free, 1))) fatptr_t phy_mem_alloc(size_t size)
{
	const size_t req_size = size + (size % BLOCK_SIZE);
	const size_t req_block = req_size / BLOCK_SIZE;
	size_t start_point = 0;
	size_t end_point = start_point;

	if (phy_mem_get_free_blocks() < req_block)
		return (fatptr_t){.ptr=0,.len=0};

	for (size_t i = 0; i < ALLOC_BITMAP * 32; i++) {
		const uint32_t block = i / 32;
		const uint32_t block_bit = i % 32;
		const uint32_t bit_mask = 1 << block_bit;

		if ((allocation_bitmap[block] & bit_mask) == 0) {
			end_point++;
			if (end_point - start_point >= req_block)
				break;
		} else {
			start_point = i + 1;
			end_point = i + 1;
		}
	}

	for (size_t i = start_point; i < end_point; i++) {
		const uint32_t block = i / 32;
		const uint32_t block_bit = i % 32;
		const uint32_t bit_mask = 1 << block_bit;

		allocation_bitmap[block] |= bit_mask;
	}

	list_for_each(&booking_block_list) {
		bool found_spot = false;

		struct booking_block *cur_block =
			list_entry(it, struct booking_block, list);

		for (size_t i = 0; i < BOOKING_COUNT; i++) {
			if (cur_block->allocs[i].ptr == nullptr) {
				cur_block->allocs[i].ptr =
					(void *)(start_point * BLOCK_SIZE);
				cur_block->allocs[i].len =
					end_point * BLOCK_SIZE -
					start_point * BLOCK_SIZE;

				found_spot = true;
				break;
			}
		}
		if (found_spot)
			break;
	}

	return (fatptr_t){.ptr=(void*)(start_point*BLOCK_SIZE),.len=req_size};
}

__attribute__((hot)) void phy_mem_free(fatptr_t addr_ptr)
{
	list_for_each(&booking_block_list) {
		struct booking_block *cur_block =
			list_entry(it, struct booking_block, list);
		for (size_t i = 0; i < BOOKING_COUNT; i++) {
			if (cur_block->allocs[i].ptr == addr_ptr.ptr) {
				phy_mem_add_region(
					(size_t)cur_block->allocs[i].ptr,
					cur_block->allocs[i].len);

				cur_block->allocs[i].ptr = nullptr;
				cur_block->allocs[i].len = 0;

				break;
			}
		}
	}
}
