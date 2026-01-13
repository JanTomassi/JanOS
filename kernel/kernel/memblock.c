#include <kernel/memblock.h>
#include <string.h>

struct memblock block = {0};

static void memblock_insert_region(struct memblock_type *type, size_t base, size_t size, unsigned long flags)
{
	if (size == 0 || type->cnt >= MEMBLOCK_MAX_REGIONS)
		return;

	unsigned int idx = 0;
	while (idx < type->cnt && type->regions[idx].base <= base)
		idx++;

	for (unsigned int move = type->cnt; move > idx; move--)
		type->regions[move] = type->regions[move - 1];

	type->regions[idx] = (struct memblock_region){
		.base = base,
		.size = size,
		.flags = flags,
	};
	type->cnt++;
}

static void memblock_merge_regions(struct memblock_type *type)
{
	if (type->cnt < 2)
		return;

	unsigned int i = 0;
	while (i + 1 < type->cnt) {
		struct memblock_region *cur = &type->regions[i];
		struct memblock_region *next = &type->regions[i + 1];
		size_t cur_end = cur->base + cur->size;
		size_t next_end = next->base + next->size;

		if (cur_end < cur->base)
			cur_end = SIZE_MAX;
		if (next_end < next->base)
			next_end = SIZE_MAX;

		if ((cur->flags == next->flags) && cur_end >= next->base) {
			if (next_end > cur_end)
				cur->size = next_end - cur->base;
			for (unsigned int move = i + 1; move + 1 < type->cnt; move++)
				type->regions[move] = type->regions[move + 1];
			type->cnt--;
			continue;
		}
		i++;
	}
}

static void memblock_add_region(struct memblock_type *type, size_t base, size_t size, unsigned long flags)
{
	memblock_insert_region(type, base, size, flags);
	memblock_merge_regions(type);
}

static void memblock_remove_range(struct memblock_type *type, size_t base, size_t size)
{
	if (size == 0 || type->cnt == 0)
		return;

	size_t end = base + size;
	if (end < base)
		end = SIZE_MAX;

	for (unsigned int i = 0; i < type->cnt;) {
		struct memblock_region *cur = &type->regions[i];
		size_t cur_end = cur->base + cur->size;

		if (cur_end < cur->base)
			cur_end = SIZE_MAX;

		if (!memblock_overlaps(base, size, cur->base, cur->size)) {
			i++;
			continue;
		}

		if (base <= cur->base && end >= cur_end) {
			for (unsigned int move = i; move + 1 < type->cnt; move++)
				type->regions[move] = type->regions[move + 1];
			type->cnt--;
			continue;
		}

		if (base > cur->base && end < cur_end) {
			if (type->cnt >= MEMBLOCK_MAX_REGIONS)
				return;
			struct memblock_region tail = {
				.base = end,
				.size = cur_end - end,
				.flags = cur->flags,
			};
			cur->size = base - cur->base;
			for (unsigned int move = type->cnt; move > i + 1; move--)
				type->regions[move] = type->regions[move - 1];
			type->regions[i + 1] = tail;
			type->cnt++;
			i += 2;
			continue;
		}

		if (base <= cur->base) {
			cur->base = end;
			cur->size = cur_end - end;
			i++;
			continue;
		}

		cur->size = base - cur->base;
		i++;
	}
}

static size_t memblock_find_bottom_up(size_t size, size_t align, size_t start, size_t end)
{
	for (unsigned int i = 0; i < block.memory.cnt; i++) {
		const struct memblock_region *region = &block.memory.regions[i];
		size_t region_start = region->base;
		size_t region_end = region->base + region->size;

		if (region_end < region->base)
			region_end = SIZE_MAX;

		if (region_start < start)
			region_start = start;
		if (region_end > end)
			region_end = end;
		if (region_start >= region_end)
			continue;

		size_t candidate = memblock_align_up(region_start, align);

		while (candidate + size <= region_end) {
			bool overlap = false;
			for (unsigned int j = 0; j < block.reserved.cnt; j++) {
				const struct memblock_region *res = &block.reserved.regions[j];
				if (res->base + res->size <= candidate)
					continue;
				if (res->base >= candidate + size)
					break;
				overlap = true;
				candidate = memblock_align_up(res->base + res->size, align);
				break;
			}
			if (!overlap)
				return candidate;
			if (candidate + size > region_end)
				break;
		}
	}

	return MEMBLOCK_ALLOC_FAIL;
}

static size_t memblock_find_top_down(size_t size, size_t align, size_t start, size_t end)
{
	for (int i = (int)block.memory.cnt - 1; i >= 0; i--) {
		const struct memblock_region *region = &block.memory.regions[i];
		size_t region_start = region->base;
		size_t region_end = region->base + region->size;

		if (region_end < region->base)
			region_end = SIZE_MAX;

		if (region_start < start)
			region_start = start;
		if (region_end > end)
			region_end = end;
		if (region_start >= region_end || region_end - region_start < size)
			continue;

		if (region_end < size)
			continue;

		size_t candidate = memblock_align_down(region_end - size, align);
		if (candidate < region_start)
			continue;

		while (candidate >= region_start) {
			bool overlap = false;
			for (int j = (int)block.reserved.cnt - 1; j >= 0; j--) {
				const struct memblock_region *res = &block.reserved.regions[j];
				if (res->base >= candidate + size)
					continue;
				if (res->base + res->size <= candidate)
					break;
				overlap = true;
				if (res->base < size)
					return MEMBLOCK_ALLOC_FAIL;
				candidate = memblock_align_down(res->base - size, align);
				break;
			}
			if (!overlap)
				return candidate;
			if (candidate < region_start)
				break;
		}
	}

	return MEMBLOCK_ALLOC_FAIL;
}

void memblock_init(bool bottom_up)
{
	memset(&block, 0, sizeof(block));
	block.bottom_up = bottom_up;
}

void memblock_add(size_t base, size_t size)
{
	memblock_add_region(&block.memory, base, size, MEMBLOCK_NONE);
}

void memblock_reserve(size_t base, size_t size)
{
	memblock_add_region(&block.reserved, base, size, MEMBLOCK_RESERVED);
}

void memblock_remove(size_t base, size_t size)
{
	memblock_remove_range(&block.memory, base, size);
}

size_t memblock_alloc_range(size_t size, size_t align, size_t start, size_t end)
{
	if (size == 0)
		return MEMBLOCK_ALLOC_FAIL;

	if (align == 0)
		align = 1;
	if (end == 0)
		end = SIZE_MAX;

	size_t addr = block.bottom_up
		? memblock_find_bottom_up(size, align, start, end)
		: memblock_find_top_down(size, align, start, end);

	if (addr != MEMBLOCK_ALLOC_FAIL)
		memblock_reserve(addr, size);

	return addr;
}

void memblock_free(size_t base, size_t size)
{
	memblock_remove_range(&block.reserved, base, size);
}
