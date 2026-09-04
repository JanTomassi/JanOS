#include "test.h"

#include <kernel/memblock.h>
#include <kernel/multiboot.h>

#include <stdint.h>
#include <string.h>

uint8_t HIGHER_HALF;

int kprintf(const char *format, ...)
{
	(void)format;
	return 0;
}

struct test_mbi {
	uint8_t bytes[128];
};

static uintptr_t make_mbi(struct test_mbi *storage)
{
	memset(storage, 0, sizeof(*storage));
	uint32_t mmap_offset = MULTIBOOT_INFO_HEADER_SIZE;
	struct multiboot_tag_mmap *mmap =
		(struct multiboot_tag_mmap *)(storage->bytes + mmap_offset);
	mmap->type = MULTIBOOT_TAG_TYPE_MMAP;
	mmap->size = sizeof(*mmap) + sizeof(multiboot_memory_map_t);
	mmap->entry_size = sizeof(multiboot_memory_map_t);
	mmap->entry_version = 0;
	mmap->entries[0] = (multiboot_memory_map_t){
		.addr = 0x1000,
		.len = 0x9000,
		.type = MULTIBOOT_MEMORY_AVAILABLE,
	};
	uint32_t end_offset = mmap_offset +
		((mmap->size + MULTIBOOT_TAG_ALIGN - 1) & ~(MULTIBOOT_TAG_ALIGN - 1));
	struct multiboot_tag *end = (struct multiboot_tag *)(storage->bytes + end_offset);
	end->type = MULTIBOOT_TAG_TYPE_END;
	end->size = MULTIBOOT_TAG_HEADER_SIZE;
	*(uint32_t *)storage->bytes = end_offset + MULTIBOOT_TAG_HEADER_SIZE;
	*(uint32_t *)(storage->bytes + sizeof(uint32_t)) = 0;
	return (uintptr_t)storage->bytes;
}

static void test_ranges(void)
{
	TEST_ASSERT(!memblock_overlaps(0x1000, 0, 0x1000, 1));
	TEST_ASSERT(memblock_overlaps(0x1000, 0x1000, 0x1800, 1));
	TEST_ASSERT(!memblock_overlaps(0x1000, 0x1000, 0x2000, 1));
	TEST_ASSERT(memblock_overlaps(SIZE_MAX - 3, 8, SIZE_MAX - 1, 4));

	struct test_mbi storage;
	uintptr_t mbi = make_mbi(&storage);
	memblock_init((unsigned long)mbi, false);
	TEST_ASSERT(memblock_region_available(0x1000, 0x1000));
	TEST_ASSERT(!memblock_region_available(0x0000, 0x1000));
	memblock_reserve(0x3000, 0x1000);
	TEST_ASSERT(!memblock_region_available(0x3000, 0x1000));
	TEST_ASSERT(memblock_region_available(0x4000, 0x1000));

	size_t allocation = memblock_alloc_range(0x1000, 0x1000, 0, SIZE_MAX);
	TEST_ASSERT(allocation == 0x9000);
	TEST_ASSERT(!memblock_region_available(allocation, 0x1000));
	memblock_free(allocation, 0x1000);
	TEST_ASSERT(memblock_region_available(allocation, 0x1000));

	memblock_init((unsigned long)mbi, true);
	memblock_reserve(0x1000, 0x1000);
	allocation = memblock_alloc_range(0x1000, 0x1000, 0, SIZE_MAX);
	TEST_ASSERT(allocation == 0x2000);
}

static void test_splitting_and_failure(void)
{
	memblock_add(0x1000, 0x9000);
	memblock_reserve(0x3000, 0x2000);
	struct memblock_type *memory = memblock_get_memory();
	struct memblock_type *reserved = memblock_get_reserved();
	TEST_ASSERT(memory->cnt == 1 && memory->regions[0].base == 0x1000);
	bool found_reservation = false;
	for (unsigned int i = 0; i < reserved->cnt; ++i)
		if (reserved->regions[i].base == 0x3000 && reserved->regions[i].size == 0x2000)
			found_reservation = true;
	TEST_ASSERT(found_reservation);
	memblock_remove(0x5000, 0x1000);
	TEST_ASSERT(memory->cnt == 2);
	TEST_ASSERT(memory->regions[0].base == 0x1000 &&
		memory->regions[0].size == 0x4000);
	TEST_ASSERT(memory->regions[1].base == 0x6000 &&
		memory->regions[1].size == 0x4000);
	TEST_ASSERT(memblock_alloc_range(0, 1, 0, SIZE_MAX) == MEMBLOCK_ALLOC_FAIL);
	TEST_ASSERT(memblock_alloc_range(0x1000, 0x1000, 0x9000, 0x9000) ==
		MEMBLOCK_ALLOC_FAIL);
}

int main(void)
{
	struct test_mbi storage;
	(void)make_mbi(&storage);
	test_ranges();
	/* The previous test leaves only ordinary regions after its final init. */
	memblock_init((unsigned long)make_mbi(&storage), true);
	test_splitting_and_failure();
	return 0;
}
