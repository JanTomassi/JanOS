#include "test.h"

#include <kernel/elf_loader.h>

#include <stdint.h>
#include <string.h>

extern void *malloc(size_t size);
extern void free(void *ptr);

#define TEST_PAGE_SIZE 4096u
#define TEST_PT_LOAD 1u
#define TEST_PT_DYNAMIC 2u
#define TEST_PT_INTERP 3u
#define TEST_PT_TLS 7u
#define TEST_PF_X 1u
#define TEST_PF_W 2u
#define TEST_PF_R 4u
#define TEST_IMAGE_SIZE 0x2200u

uintptr_t round_up_to_page(uintptr_t address)
{
	return (address + TEST_PAGE_SIZE - 1) & ~(uintptr_t)(TEST_PAGE_SIZE - 1);
}

uintptr_t round_down_to_page(uintptr_t address)
{
	return address & ~(uintptr_t)(TEST_PAGE_SIZE - 1);
}

struct test_elf_header {
	uint8_t ident[16];
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint32_t entry;
	uint32_t phoff;
	uint32_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
} __attribute__((packed));

struct test_program_header {
	uint32_t type;
	uint32_t offset;
	uint32_t vaddr;
	uint32_t paddr;
	uint32_t filesz;
	uint32_t memsz;
	uint32_t flags;
	uint32_t align;
} __attribute__((packed));

struct mapped_segment {
	uintptr_t address;
	size_t size;
	uint32_t flags;
	uint8_t *memory;
	bool active;
};

struct loader_fake {
	struct mapped_segment segments[8];
	size_t map_count;
	size_t unmap_count;
	size_t copy_count;
	size_t zero_count;
	size_t protect_count;
	uintptr_t copy_address;
	uintptr_t zero_address;
	size_t copy_size;
	size_t zero_size;
	uint32_t map_flags;
	uint32_t protect_flags;
	bool fail_map;
	bool fail_copy;
	bool fail_zero;
	bool fail_protect;
};

static struct mapped_segment *find_segment(struct loader_fake *fake,
	uintptr_t address, size_t size)
{
	for (size_t i = 0; i < fake->map_count; ++i) {
		struct mapped_segment *segment = &fake->segments[i];
		if (segment->active && address >= segment->address &&
			size <= segment->size && address - segment->address <= segment->size - size)
			return segment;
	}
	return NULL;
}

static void *fake_map(uintptr_t address, size_t size, uint32_t flags, void *context)
{
	struct loader_fake *fake = context;
	if (fake->fail_map || fake->map_count == sizeof(fake->segments) /
		sizeof(fake->segments[0]))
		return NULL;
	uint8_t *memory = malloc(size);
	if (memory == NULL)
		return NULL;
	memset(memory, 0xa5, size);
	fake->segments[fake->map_count++] = (struct mapped_segment){
		.address = address,
		.size = size,
		.memory = memory,
		.active = true,
	};
	fake->map_flags = flags;
	return memory;
}

static void fake_unmap(uintptr_t address, size_t size, void *context)
{
	struct loader_fake *fake = context;
	struct mapped_segment *segment = find_segment(fake, address, size);
	TEST_ASSERT(segment != NULL);
	TEST_ASSERT(segment->address == address && segment->size == size);
	free(segment->memory);
	segment->memory = NULL;
	segment->active = false;
	++fake->unmap_count;
}

static bool fake_copy(uintptr_t address, const void *source, size_t size, void *context)
{
	struct loader_fake *fake = context;
	++fake->copy_count;
	fake->copy_address = address;
	fake->copy_size = size;
	if (fake->fail_copy)
		return false;
	struct mapped_segment *segment = find_segment(fake, address, size);
	if (segment == NULL)
		return false;
	memcpy(segment->memory + (address - segment->address), source, size);
	return true;
}

static bool fake_zero(uintptr_t address, size_t size, void *context)
{
	struct loader_fake *fake = context;
	++fake->zero_count;
	fake->zero_address = address;
	fake->zero_size = size;
	if (fake->fail_zero)
		return false;
	struct mapped_segment *segment = find_segment(fake, address, size);
	if (segment == NULL)
		return false;
	memset(segment->memory + (address - segment->address), 0, size);
	return true;
}

static bool fake_protect(uintptr_t address, size_t size, uint32_t flags, void *context)
{
	struct loader_fake *fake = context;
	++fake->protect_count;
	fake->protect_flags = flags;
	if (fake->fail_protect)
		return false;
	return find_segment(fake, address, size) != NULL;
}

static struct elf_load_ops fake_ops(struct loader_fake *fake)
{
	return (struct elf_load_ops){
		.map = fake_map,
		.unmap = fake_unmap,
		.copy = fake_copy,
		.zero = fake_zero,
		.protect = fake_protect,
		.context = fake,
	};
}

static void cleanup_fake(struct loader_fake *fake)
{
	for (size_t i = 0; i < fake->map_count; ++i) {
		if (fake->segments[i].active)
			free(fake->segments[i].memory);
		fake->segments[i].active = false;
	}
}

static void make_image(uint8_t *image, size_t size, unsigned int segments)
{
	memset(image, 0, size);
	struct test_elf_header *header = (struct test_elf_header *)image;
	memcpy(header->ident, "\177ELF", 4);
	header->ident[4] = 1;
	header->ident[5] = 1;
	header->ident[6] = 1;
	header->type = 2;
	header->machine = 3;
	header->version = 1;
	header->entry = 0x00401003;
	header->phoff = sizeof(*header);
	header->ehsize = sizeof(*header);
	header->phentsize = sizeof(struct test_program_header);
	header->phnum = (uint16_t)segments;
	for (unsigned int i = 0; i < segments; ++i) {
		struct test_program_header *program = (struct test_program_header *)
			(image + header->phoff + i * header->phentsize);
		program->type = TEST_PT_LOAD;
		program->offset = 0x1000u + i * 0x1000u;
		program->vaddr = 0x00401000u + i * 0x2000u;
		program->filesz = 5u + i;
		program->memsz = 0x1005u + i;
		program->flags = TEST_PF_R | (i == 0 ? TEST_PF_X : TEST_PF_W);
		program->align = TEST_PAGE_SIZE;
		for (uint32_t j = 0; j < program->filesz; ++j)
			image[program->offset + j] = (uint8_t)('A' + i + j);
	}
}

static void test_valid_load(void)
{
	uint8_t image[TEST_IMAGE_SIZE];
	struct loader_fake fake = { 0 };
	struct elf_load_result result;
	struct elf_load_ops ops = fake_ops(&fake);
	make_image(image, sizeof(image), 1);
	TEST_ASSERT(elf32_load(image, sizeof(image), &ops, &result));
	TEST_ASSERT(result.entry == 0x00401003u);
	TEST_ASSERT(result.lowest_address == 0x00401000u);
	TEST_ASSERT(result.highest_address == 0x00403000u);
	TEST_ASSERT(result.segment_count == 1);
	TEST_ASSERT(fake.map_count == 1 && fake.unmap_count == 0);
	TEST_ASSERT(fake.map_flags == (ELF_LOAD_READ | ELF_LOAD_EXEC | ELF_LOAD_WRITE));
	TEST_ASSERT(fake.copy_address == 0x00401000u && fake.copy_size == 5);
	TEST_ASSERT(fake.zero_address == 0x00401005u && fake.zero_size == 0x1000);
	TEST_ASSERT(fake.protect_count == 1);
	TEST_ASSERT(fake.protect_flags == (ELF_LOAD_READ | ELF_LOAD_EXEC));
	struct mapped_segment *segment = &fake.segments[0];
	TEST_ASSERT(segment->memory[0] == 'A' && segment->memory[4] == 'E');
	TEST_ASSERT(segment->memory[5] == 0 && segment->memory[0x1004] == 0);
	cleanup_fake(&fake);
}

static void test_multi_segment_load(void)
{
	uint8_t image[TEST_IMAGE_SIZE];
	struct loader_fake fake = { 0 };
	struct elf_load_result result;
	make_image(image, sizeof(image), 2);
	TEST_ASSERT(elf32_load(image, sizeof(image), &(struct elf_load_ops){
		.map = fake_map,
		.unmap = fake_unmap,
		.copy = fake_copy,
		.zero = fake_zero,
		.protect = fake_protect,
		.context = &fake,
	}, &result));
	TEST_ASSERT(result.segment_count == 2);
	TEST_ASSERT(result.lowest_address == 0x00401000u);
	TEST_ASSERT(result.highest_address == 0x00405000u);
	TEST_ASSERT(fake.map_count == 2 && fake.copy_count == 2 &&
		fake.zero_count == 2 && fake.protect_count == 2);
	cleanup_fake(&fake);
}

static void test_default_copy_path(void)
{
	uint8_t image[TEST_IMAGE_SIZE];
	struct loader_fake fake = { 0 };
	struct elf_load_result result;
	make_image(image, sizeof(image), 1);
	struct elf_load_ops ops = {
		.map = fake_map,
		.unmap = fake_unmap,
		.context = &fake,
	};
	TEST_ASSERT(elf32_load(image, sizeof(image), &ops, &result));
	TEST_ASSERT(fake.copy_count == 0 && fake.zero_count == 0 &&
		fake.protect_count == 0);
	TEST_ASSERT(fake.segments[0].memory[0] == 'A');
	TEST_ASSERT(fake.segments[0].memory[5] == 0);
	cleanup_fake(&fake);
}

static void test_reject_header_errors(void)
{
	uint8_t image[TEST_IMAGE_SIZE];
	struct loader_fake fake = { 0 };
	struct elf_load_result result;
	struct elf_load_ops ops = fake_ops(&fake);
	struct test_elf_header *header;
	make_image(image, sizeof(image), 1);
	header = (struct test_elf_header *)image;
	TEST_ASSERT(!elf32_load(NULL, sizeof(image), &ops, &result));
	TEST_ASSERT(!elf32_load(image, sizeof(image), NULL, &result));
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, NULL));
	TEST_ASSERT(!elf32_load(image, sizeof(*header) - 1, &ops, &result));
	header->ident[0] = 0;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 1);
	header = (struct test_elf_header *)image;
	header->ident[4] = 2;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 1);
	header = (struct test_elf_header *)image;
	header->ident[5] = 2;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 1);
	header = (struct test_elf_header *)image;
	header->ident[6] = 2;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 1);
	header = (struct test_elf_header *)image;
	header->type = 3;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 1);
	header = (struct test_elf_header *)image;
	header->machine = 0;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 1);
	header = (struct test_elf_header *)image;
	header->version = 0;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 1);
	header = (struct test_elf_header *)image;
	header->ehsize--;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 1);
	header = (struct test_elf_header *)image;
	header->phentsize--;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 1);
	header = (struct test_elf_header *)image;
	header->phoff = sizeof(image) - 1;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 1);
	struct test_program_header *program =
		(struct test_program_header *)(image + sizeof(*header));
	program->offset = sizeof(image) - 2;
	program->filesz = 5;
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	make_image(image, sizeof(image), 0);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
}

static void test_reject_segments(void)
{
	uint8_t image[TEST_IMAGE_SIZE];
	struct loader_fake fake;
	struct elf_load_result result;
	struct elf_load_ops ops;
	struct test_program_header *program;

	make_image(image, sizeof(image), 1);
	program = (struct test_program_header *)(image + sizeof(struct test_elf_header));
	program->memsz = program->filesz - 1;
	fake = (struct loader_fake){ 0 };
	ops = fake_ops(&fake);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	TEST_ASSERT(fake.map_count == 0);

	make_image(image, sizeof(image), 1);
	program = (struct test_program_header *)(image + sizeof(struct test_elf_header));
	program->vaddr = 0x1000;
	fake = (struct loader_fake){ 0 };
	ops = fake_ops(&fake);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));

	make_image(image, sizeof(image), 1);
	program = (struct test_program_header *)(image + sizeof(struct test_elf_header));
	program->align = 3;
	fake = (struct loader_fake){ 0 };
	ops = fake_ops(&fake);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));

	make_image(image, sizeof(image), 1);
	program = (struct test_program_header *)(image + sizeof(struct test_elf_header));
	const uint32_t forbidden_types[] = { TEST_PT_DYNAMIC, TEST_PT_INTERP, TEST_PT_TLS };
	for (size_t type_index = 0; type_index < sizeof(forbidden_types) /
		sizeof(forbidden_types[0]); ++type_index) {
		make_image(image, sizeof(image), 1);
		program = (struct test_program_header *)(image + sizeof(struct test_elf_header));
		program->type = forbidden_types[type_index];
		fake = (struct loader_fake){ 0 };
		ops = fake_ops(&fake);
		TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	}

	make_image(image, sizeof(image), 1);
	program = (struct test_program_header *)(image + sizeof(struct test_elf_header));
	program->type = 0;
	fake = (struct loader_fake){ 0 };
	ops = fake_ops(&fake);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));

	make_image(image, sizeof(image), 2);
	program = (struct test_program_header *)(image + sizeof(struct test_elf_header));
	program[1].vaddr = program[0].vaddr + 0x100;
	fake = (struct loader_fake){ 0 };
	ops = fake_ops(&fake);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	TEST_ASSERT(fake.unmap_count == 1);

	make_image(image, sizeof(image), 1);
	program = (struct test_program_header *)(image + sizeof(struct test_elf_header));
	program->flags = TEST_PF_R;
	fake = (struct loader_fake){ 0 };
	ops = fake_ops(&fake);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
}

static void test_callback_failures(void)
{
	uint8_t image[TEST_IMAGE_SIZE];
	struct elf_load_result result;
	struct loader_fake fake;
	struct elf_load_ops ops;

	make_image(image, sizeof(image), 1);
	fake = (struct loader_fake){ .fail_map = true };
	ops = fake_ops(&fake);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	TEST_ASSERT(fake.map_count == 0 && fake.unmap_count == 0);

	fake = (struct loader_fake){ .fail_copy = true };
	ops = fake_ops(&fake);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	TEST_ASSERT(fake.unmap_count == 1);

	fake = (struct loader_fake){ .fail_zero = true };
	ops = fake_ops(&fake);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	TEST_ASSERT(fake.unmap_count == 1);

	fake = (struct loader_fake){ .fail_protect = true };
	ops = fake_ops(&fake);
	TEST_ASSERT(!elf32_load(image, sizeof(image), &ops, &result));
	TEST_ASSERT(fake.unmap_count == 1);
}

int main(void)
{
	_Static_assert(sizeof(struct test_elf_header) == 52, "ELF header layout changed");
	_Static_assert(sizeof(struct test_program_header) == 32, "ELF program header layout changed");
	test_valid_load();
	test_multi_segment_load();
	test_default_copy_path();
	test_reject_header_errors();
	test_reject_segments();
	test_callback_failures();
	return 0;
}
