#include <kernel/phy_mem.h>
#include <kernel/display.h>
#include <string.h>
#include <stdlib.h>
#include <list.h>

MODULE("Physical Memory");

#define BLOCK_SIZE (KIBI(4ULL))
#define MAX_BLOCKS (GIBI(4ULL) / BLOCK_SIZE) // Limit to x86_32 max addressable memory
static_assert(MAX_BLOCKS == 0x100000);
#define BITMAP_META_SIZE (sizeof(size_t) * 3 + sizeof(struct list_head))
#define BITMAP_WORDS ((BLOCK_SIZE - BITMAP_META_SIZE) / sizeof(uint32_t))
static_assert(BITMAP_WORDS > 0, "Bitmap words must be positive");
#define BITMAP_CHUNK_CAPACITY (BITMAP_WORDS * BIT(sizeof(uint32_t)))

/**
 * Memory book keeping will be keept using a linked list of block all
 * the element of each block will be a fat pointer (address and size)
 * and the last element will rapresent links to next and prev
 **/
#define BOOKING_COUNT BLOCK_SIZE / sizeof(fatptr_t) - 1
struct booking_block {
	fatptr_t allocs[BLOCK_SIZE / sizeof(fatptr_t) - 1];
	struct list_head list;
};
static_assert(sizeof(struct booking_block) == BLOCK_SIZE, "Booking block is not equal to a block, this is not allowed");

struct bitmap_chunk {
	size_t used_blocks;
	size_t capacity;
	size_t base_block;
	struct list_head list;
	uint32_t bits[BITMAP_WORDS];
};
static_assert(sizeof(struct bitmap_chunk) == BLOCK_SIZE, "Bitmap chunk must fit exactly one block");

struct slab_free {
	struct slab_free *next;
};

struct slab_page {
	void *mem;
	struct list_head list;
};

struct slab_cache {
	size_t obj_size;
	struct slab_free *free_list;
	struct list_head pages;
};

#define METADATA_ARENA_SIZE (BLOCK_SIZE * 96)
static uint8_t metadata_arena[METADATA_ARENA_SIZE] = { 0 };
static size_t metadata_offset = 0;

static struct slab_cache bitmap_slab = { 0 };
static struct slab_cache booking_block_slab = { 0 };
static LIST_HEAD(booking_block_list);
static LIST_HEAD(bitmap_chunk_list);

static void *metadata_alloc(size_t size)
{
	const size_t align = sizeof(void *);
	const size_t aligned = (size + (align - 1)) & ~(align - 1);

	if (metadata_offset + aligned > METADATA_ARENA_SIZE)
		return nullptr;

	void *ptr = metadata_arena + metadata_offset;
	metadata_offset += aligned;
	return ptr;
}

static void init_slab_cache(struct slab_cache *cache, size_t obj_size)
{
	cache->obj_size = obj_size;
	cache->free_list = nullptr;
	cache->pages.next = &cache->pages;
	cache->pages.prev = &cache->pages;
}

static void slab_add_page(struct slab_cache *cache)
{
	void *page_mem = metadata_alloc(BLOCK_SIZE);
	if (page_mem == nullptr)
		return;

	struct slab_page *page = metadata_alloc(sizeof(struct slab_page));
	if (page == nullptr)
		return;

	page->mem = page_mem;
	RESET_LIST_ITEM(&page->list);
	list_add(&page->list, &cache->pages);

	const size_t obj_count = BLOCK_SIZE / cache->obj_size;
	for (size_t i = 0; i < obj_count; i++) {
		struct slab_free *obj = (struct slab_free *)((uint8_t *)page_mem + (i * cache->obj_size));
		obj->next = cache->free_list;
		cache->free_list = obj;
	}
}

static void *slab_alloc(struct slab_cache *cache)
{
	if (cache->free_list == nullptr)
		slab_add_page(cache);

	if (cache->free_list == nullptr)
		return nullptr;

	struct slab_free *res = cache->free_list;
	cache->free_list = res->next;
	return res;
}

static void slab_free(struct slab_cache *cache, void *ptr)
{
	struct slab_free *node = ptr;
	node->next = cache->free_list;
	cache->free_list = node;
}

static struct booking_block *alloc_booking_block(void)
{
	struct booking_block *block = slab_alloc(&booking_block_slab);
	if (block == nullptr)
		return nullptr;

	memset(block, 0, sizeof(*block));
	RESET_LIST_ITEM(&block->list);
	list_add(&block->list, booking_block_list.prev);

	return block;
}

static bool booking_block_is_empty(const struct booking_block *block)
{
	for (size_t i = 0; i < BOOKING_COUNT; i++) {
		if (block->allocs[i].ptr != nullptr)
			return false;
	}
	return true;
}

static struct bitmap_chunk *find_chunk(size_t block_idx)
{
	list_for_each(&bitmap_chunk_list) {
		struct bitmap_chunk *chunk = list_entry(it, struct bitmap_chunk, list);
		if (block_idx >= chunk->base_block && block_idx < chunk->base_block + chunk->capacity)
			return chunk;
	}
	return nullptr;
}

static bool bitmap_block_used(const struct bitmap_chunk *chunk, size_t block_idx)
{
	const size_t local = block_idx - chunk->base_block;
	const size_t word = local / 32;
	const size_t bit = local % 32;
	return (chunk->bits[word] & (1u << bit)) != 0;
}

static void bitmap_set_block(struct bitmap_chunk *chunk, size_t block_idx, bool set)
{
	const size_t local = block_idx - chunk->base_block;
	const size_t word = local / 32;
	const size_t bit = local % 32;
	const uint32_t mask = 1u << bit;
	const bool cur = (chunk->bits[word] & mask) != 0;

	if (cur == set)
		return;

	if (set) {
		chunk->bits[word] |= mask;
		chunk->used_blocks += 1;
	} else {
		chunk->bits[word] &= ~mask;
		chunk->used_blocks -= 1;
	}
}

static size_t bitmap_total_blocks(void)
{
	size_t total = 0;
	list_for_each(&bitmap_chunk_list) {
		struct bitmap_chunk *chunk = list_entry(it, struct bitmap_chunk, list);
		total += chunk->capacity;
	}
	return total;
}

static size_t bitmap_free_blocks(void)
{
	size_t free = 0;
	list_for_each(&bitmap_chunk_list) {
		struct bitmap_chunk *chunk = list_entry(it, struct bitmap_chunk, list);
		free += chunk->capacity - chunk->used_blocks;
	}
	return free;
}

static bool init_bitmap_chunks(void)
{
	size_t base = 0;
	while (base < MAX_BLOCKS) {
		struct bitmap_chunk *chunk = slab_alloc(&bitmap_slab);
		if (chunk == nullptr)
			return false;

		memset(chunk, 0, sizeof(*chunk));
		memset(chunk->bits, 0xff, sizeof(chunk->bits));

		const size_t remaining = MAX_BLOCKS - base;
		chunk->capacity = remaining < BITMAP_CHUNK_CAPACITY ? remaining : BITMAP_CHUNK_CAPACITY;
		chunk->used_blocks = chunk->capacity;
		chunk->base_block = base;

		RESET_LIST_ITEM(&chunk->list);
		list_add(&chunk->list, bitmap_chunk_list.prev);

		base += chunk->capacity;
	}

	return true;
}

void phy_mem_reset()
{
	metadata_offset = 0;

	init_slab_cache(&bitmap_slab, sizeof(struct bitmap_chunk));
	init_slab_cache(&booking_block_slab, sizeof(struct booking_block));

	RESET_LIST_ITEM(&booking_block_list);
	RESET_LIST_ITEM(&bitmap_chunk_list);

	if (!init_bitmap_chunks())
		return;

	alloc_booking_block();
};

__attribute__((hot)) static void phy_mem_set_region(size_t addr, size_t len, bool set)
{
	const size_t page_offset = addr / BLOCK_SIZE;
	const size_t num_pages = (len / BLOCK_SIZE) + 1;

	for (size_t i = 0; i < num_pages; i++) {
		const size_t block_idx = page_offset + i;
		struct bitmap_chunk *chunk = find_chunk(block_idx);

		if (chunk == nullptr)
			continue;

		bitmap_set_block(chunk, block_idx, set);
	}
}

size_t phy_mem_get_tot_blocks()
{
	return bitmap_total_blocks();
}

size_t phy_mem_get_used_blocks()
{
	const size_t total = bitmap_total_blocks();
	return total - bitmap_free_blocks();
}

size_t phy_mem_get_free_blocks()
{
	return bitmap_free_blocks();
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
	const size_t req_block = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	const size_t req_size = req_block * BLOCK_SIZE;
	size_t start_block = 0;
	bool found = false;
	size_t run = 0;

	if (req_block == 0)
		return (fatptr_t){ .ptr = 0, .len = 0 };

	if (bitmap_free_blocks() < req_block)
		return (fatptr_t){ .ptr = 0, .len = 0 };

	list_rev_for_each(&bitmap_chunk_list) {
		struct bitmap_chunk *chunk = list_entry(it, struct bitmap_chunk, list);
		for (size_t i = chunk->capacity; i > 0 ; i--) {
			const size_t block_idx = chunk->base_block + (i - 1);

			if (!bitmap_block_used(chunk, block_idx)) {
				if (run == 0)
					start_block = block_idx;

				run += 1;

				if (run >= req_block) {
					found = true;
					goto found_blocks;
				}
			} else {
				run = 0;
			}
		}
	}

found_blocks:
	if (!found)
		return (fatptr_t){ .ptr = 0, .len = 0 };

	struct booking_block *target_block = nullptr;
	size_t slot_idx = 0;

	list_for_each(&booking_block_list) {
		struct booking_block *cur_block = list_entry(it, struct booking_block, list);
		for (size_t i = 0; i < BOOKING_COUNT; i++) {
			if (cur_block->allocs[i].ptr == nullptr) {
				target_block = cur_block;
				slot_idx = i;
				goto slot_found;
			}
		}
	}

	target_block = alloc_booking_block();
	slot_idx = 0;

slot_found:
	if (target_block == nullptr)
		return (fatptr_t){ .ptr = 0, .len = 0 };

	target_block->allocs[slot_idx].ptr = (void *)(start_block * BLOCK_SIZE);
	target_block->allocs[slot_idx].len = req_size;

	for (size_t i = 0; i < req_block; i++) {
		const size_t block_idx = start_block + i;
		struct bitmap_chunk *chunk = find_chunk(block_idx);
		if (chunk != nullptr)
			bitmap_set_block(chunk, block_idx, true);
	}

	return (fatptr_t){ .ptr = (void *)(start_block * BLOCK_SIZE), .len = req_size };
}

__attribute__((hot, malloc(phy_mem_free, 1))) fatptr_t phy_mem_alloc_below(size_t size, size_t max_addr)
{
	const size_t req_block = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	const size_t req_size = req_block * BLOCK_SIZE;
	const size_t max_block = max_addr / BLOCK_SIZE;
	size_t start_block = 0;
	bool found = false;
	size_t run = 0;

	if (req_block == 0 || max_block == 0 || req_block > max_block)
		return (fatptr_t){ .ptr = 0, .len = 0 };
	if (bitmap_free_blocks() < req_block)
		return (fatptr_t){ .ptr = 0, .len = 0 };

	list_for_each(&bitmap_chunk_list) {
		struct bitmap_chunk *chunk = list_entry(it, struct bitmap_chunk, list);
		for (size_t i = 0; i < chunk->capacity; i++) {
			const size_t block_idx = chunk->base_block + i;

			if (block_idx + req_block > max_block)
				goto end_search;

			if (!bitmap_block_used(chunk, block_idx)) {
				if (run == 0)
					start_block = block_idx;

				run += 1;

				if (run >= req_block) {
					found = true;
					goto found_blocks;
				}
			} else {
				run = 0;
			}
		}
	}

end_search:
	if (!found)
		return (fatptr_t){ .ptr = 0, .len = 0 };

found_blocks:
	struct booking_block *target_block = nullptr;
	size_t slot_idx = 0;

	list_for_each(&booking_block_list) {
		struct booking_block *cur_block = list_entry(it, struct booking_block, list);
		for (size_t i = 0; i < BOOKING_COUNT; i++) {
			if (cur_block->allocs[i].ptr == nullptr) {
				target_block = cur_block;
				slot_idx = i;
				goto slot_found;
			}
		}
	}

	target_block = alloc_booking_block();
	slot_idx = 0;

slot_found:
	if (target_block == nullptr)
		return (fatptr_t){ .ptr = 0, .len = 0 };

	target_block->allocs[slot_idx].ptr = (void *)(start_block * BLOCK_SIZE);
	target_block->allocs[slot_idx].len = req_size;

	for (size_t i = 0; i < req_block; i++) {
		const size_t block_idx = start_block + i;
		struct bitmap_chunk *chunk = find_chunk(block_idx);
		if (chunk != nullptr)
			bitmap_set_block(chunk, block_idx, true);
	}

	return (fatptr_t){ .ptr = (void *)(start_block * BLOCK_SIZE), .len = req_size };
}

__attribute__((hot)) void phy_mem_free(fatptr_t addr_ptr)
{
	list_for_each(&booking_block_list) {
		struct booking_block *cur_block = list_entry(it, struct booking_block, list);
		for (size_t i = 0; i < BOOKING_COUNT; i++) {
			if (cur_block->allocs[i].ptr == addr_ptr.ptr) {
				const size_t start_block = (size_t)cur_block->allocs[i].ptr / BLOCK_SIZE;
				const size_t block_count = cur_block->allocs[i].len / BLOCK_SIZE;

				for (size_t blk = 0; blk < block_count; blk++) {
					const size_t block_idx = start_block + blk;
					struct bitmap_chunk *chunk = find_chunk(block_idx);
					if (chunk != nullptr)
						bitmap_set_block(chunk, block_idx, false);
				}

				cur_block->allocs[i].ptr = nullptr;
				cur_block->allocs[i].len = 0;

				if (booking_block_is_empty(cur_block) &&
				    (cur_block->list.next != &booking_block_list || cur_block->list.prev != &booking_block_list)) {
					list_rm(&cur_block->list);
					slab_free(&booking_block_slab, cur_block);
				}

				break;
			}
		}
	}
}

void phy_mem_init(const struct multiboot_tag_mmap *mmap_tag, const struct multiboot_tag_elf_sections *elf_tag)
{
	mprint("Initializing physical memory allocator\n");

	phy_mem_reset();

	for (const struct multiboot_mmap_entry *mmap = mmap_tag->entries; (multiboot_uint8_t *)mmap < (multiboot_uint8_t *)mmap_tag + mmap_tag->size;
	     mmap = (multiboot_memory_map_t *)((unsigned long)mmap + mmap_tag->entry_size)) {
		if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
			phy_mem_add_region(mmap->addr, mmap->len);
		}
	}

	const Elf32_Shdr *elf_sec = (const Elf32_Shdr *)elf_tag->sections;
	for (size_t i = 0; i < elf_tag->num; i++)
		phy_mem_rm_region(elf_sec[i].sh_addr, elf_sec[i].sh_size);

	mprint("Physical memory allocator ready | total blocks: %x | used: %x | free: %x\n", phy_mem_get_tot_blocks(), phy_mem_get_used_blocks(),
	       phy_mem_get_free_blocks());
}
