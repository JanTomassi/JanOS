#include "test.h"

#include <kernel/allocator.h>
#include <kernel/fat16.h>

#include <arch/i386/ahci.h>
#include <arch/i386/ata_pio.h>

#include <stdint.h>
#include <string.h>

extern void *malloc(size_t size);
extern void free(void *ptr);

#define SECTOR_SIZE 512u
#define SECTOR_COUNT 128u

struct test_disk {
	uint8_t bytes[SECTOR_SIZE * SECTOR_COUNT];
	bool fail_reads;
	size_t read_count;
	size_t write_count;
};

static bool allocation_failure;

static fatptr_t fake_alloc(size_t size)
{
	if (allocation_failure)
		return (fatptr_t){ 0 };
	void *ptr = malloc(size);
	return (fatptr_t){ .ptr = ptr, .len = ptr == NULL ? 0 : size };
}

static void fake_free(fatptr_t allocation)
{
	free(allocation.ptr);
}

allocator_t get_gpa_allocator(void)
{
	return (allocator_t){ .alloc = fake_alloc, .free = fake_free };
}

static bool memory_read(void *context, uint64_t lba, uint32_t count, void *destination)
{
	struct test_disk *disk = context;
	if (disk == NULL || destination == NULL || count == 0 || disk->fail_reads ||
		lba >= SECTOR_COUNT || count > SECTOR_COUNT - lba)
		return false;
	++disk->read_count;
	memcpy(destination, disk->bytes + lba * SECTOR_SIZE,
		(size_t)count * SECTOR_SIZE);
	return true;
}

static bool memory_write(void *context, uint64_t lba, uint32_t count, const void *source)
{
	struct test_disk *disk = context;
	if (disk == NULL || source == NULL || count == 0 ||
		lba >= SECTOR_COUNT || count > SECTOR_COUNT - lba)
		return false;
	++disk->write_count;
	memcpy(disk->bytes + lba * SECTOR_SIZE, source,
		(size_t)count * SECTOR_SIZE);
	return true;
}

bool ahci_read28_port(uint8_t port_index, uint32_t lba_addr, uint16_t sector_count,
	void *dest)
{
	(void)port_index;
	(void)lba_addr;
	(void)sector_count;
	(void)dest;
	return false;
}

bool ahci_write28_port(uint8_t port_index, uint32_t lba_addr, uint16_t sector_count,
	const void *src)
{
	(void)port_index;
	(void)lba_addr;
	(void)sector_count;
	(void)src;
	return false;
}

bool ata_pio_28_read(uint8_t channel, uint8_t drive, uint32_t lba_addr,
	uint16_t sector_count, void *dest)
{
	(void)channel;
	(void)drive;
	(void)lba_addr;
	(void)sector_count;
	(void)dest;
	return false;
}

bool ata_pio_28_write(uint8_t channel, uint8_t drive, uint32_t lba_addr,
	uint16_t sector_count, const void *src)
{
	(void)channel;
	(void)drive;
	(void)lba_addr;
	(void)sector_count;
	(void)src;
	return false;
}

static struct block_device make_device(struct test_disk *disk)
{
	return (struct block_device){
		.sector_size = SECTOR_SIZE,
		.sector_count = SECTOR_COUNT,
		.read_fn = memory_read,
		.write_fn = memory_write,
		.context = disk,
	};
}

static void set_name(fat_dir_entry_t *entry, const char *name, const char *ext)
{
	memset(entry, ' ', sizeof(*entry));
	memcpy(entry->name, name, strlen(name));
	memcpy(entry->ext, ext, strlen(ext));
}

static void make_disk(struct test_disk *disk, uint32_t file_size, bool cycle)
{
	memset(disk, 0, sizeof(*disk));
	fat_BS_t *bpb = (fat_BS_t *)disk->bytes;
	bpb->sector_size = SECTOR_SIZE;
	bpb->sectors_per_cluster = 1;
	bpb->reserved_sectors = 1;
	bpb->FAT_count = 1;
	bpb->root_dir_count = 16;
	bpb->sectors_per_FAT = 1;
	bpb->sector_count = SECTOR_COUNT;
	disk->bytes[510] = 0x55;
	disk->bytes[511] = 0xaa;

	uint16_t *fat = (uint16_t *)(disk->bytes + SECTOR_SIZE);
	fat[2] = 3;
	fat[3] = cycle ? 2 : 0xfff8;
	fat_dir_entry_t *entry = (fat_dir_entry_t *)(disk->bytes + 2 * SECTOR_SIZE);
	set_name(entry, "HELLO", "TXT");
	entry->first_cluster_low = 2;
	entry->file_size = file_size;
	entry[1].name[0] = (char)0xe5;
	entry[2].name[0] = 0;

	memset(disk->bytes + 3 * SECTOR_SIZE, 'A', SECTOR_SIZE);
	memset(disk->bytes + 4 * SECTOR_SIZE, 'B', SECTOR_SIZE);
}

static void test_layout_and_names(void)
{
	fat_BS_t bpb = { 0 };
	fat16_layout_t layout = { 0 };
	bpb.sector_size = SECTOR_SIZE;
	bpb.sectors_per_cluster = 1;
	bpb.reserved_sectors = 1;
	bpb.FAT_count = 1;
	bpb.root_dir_count = 16;
	bpb.sectors_per_FAT = 1;
	fat16_compute_layout(&bpb, &layout);
	TEST_ASSERT(layout.fat_start_lba == 1);
	TEST_ASSERT(layout.root_dir_lba == 2);
	TEST_ASSERT(layout.root_dir_sectors == 1);
	TEST_ASSERT(layout.data_start_lba == 3);
	TEST_ASSERT(layout.sector_size == SECTOR_SIZE);

	bpb.sector_size = 0;
	fat16_compute_layout(&bpb, &layout);
	TEST_ASSERT(layout.fat_start_lba == 0 && layout.root_dir_lba == 0 &&
		layout.data_start_lba == 0 && layout.sector_size == 0);

	fat_dir_entry_t entry = { 0 };
	set_name(&entry, "HELLO", "TXT");
	char name[13];
	TEST_ASSERT(fat16_decode_83_name(&entry, name, sizeof(name)));
	TEST_ASSERT(memcmp(name, "HELLO.TXT", sizeof("HELLO.TXT")) == 0);
	TEST_ASSERT(!fat16_decode_83_name(&entry, name, 5));
	entry.name[0] = (char)0xe5;
	TEST_ASSERT(fat16_dir_entry_is_deleted(&entry));
	entry.name[0] = 0;
	TEST_ASSERT(fat16_dir_entry_is_unused(&entry));
	TEST_ASSERT(fat16_is_end_of_chain(0xfff8));
	TEST_ASSERT(!fat16_is_end_of_chain(0xfff7));
}

static void test_files_and_partial_reads(void)
{
	struct test_disk disk;
	make_disk(&disk, 600, false);
	struct block_device device = make_device(&disk);
	fat_dir_entry_t entries[4] = { 0 };
	TEST_ASSERT(fat16_read_root_dir(&device, &(fat16_layout_t){
		.fat_start_lba = 1,
		.root_dir_lba = 2,
		.root_dir_sectors = 1,
		.data_start_lba = 3,
		.sectors_per_cluster = 1,
		.sector_size = SECTOR_SIZE,
	}, entries, 4));
	TEST_ASSERT(entries[0].file_size == 600 && entries[1].name[0] == 0);

	fat_dir_entry_t found;
	TEST_ASSERT(fat16_find_entry_by_name(&device, "hello.txt", &found));
	TEST_ASSERT(found.first_cluster_low == 2);
	TEST_ASSERT(!fat16_find_entry_by_name(&device, "missing.bin", &found));

	struct fat16_file file;
	TEST_ASSERT(fat16_file_open(&device, "HELLO.TXT", &file));
	uint8_t content[600];
	size_t bytes = 0;
	TEST_ASSERT(fat16_file_read_at(&file, 0, content, sizeof(content), &bytes));
	TEST_ASSERT(bytes == sizeof(content));
	for (size_t i = 0; i < 512; ++i)
		TEST_ASSERT(content[i] == 'A');
	for (size_t i = 512; i < sizeof(content); ++i)
		TEST_ASSERT(content[i] == 'B');

	uint8_t partial[100];
	TEST_ASSERT(fat16_file_read_at(&file, 500, partial, sizeof(partial), &bytes));
	TEST_ASSERT(bytes == sizeof(partial));
	for (size_t i = 0; i < 12; ++i)
		TEST_ASSERT(partial[i] == 'A');
	for (size_t i = 12; i < sizeof(partial); ++i)
		TEST_ASSERT(partial[i] == 'B');

	TEST_ASSERT(fat16_file_read_at(&file, 600, partial, sizeof(partial), &bytes));
	TEST_ASSERT(bytes == 0);
	TEST_ASSERT(!fat16_file_read_at(&file, 601, partial, sizeof(partial), &bytes));
	TEST_ASSERT(!fat16_file_read_at(&file, 0, NULL, 0, &bytes));
	disk.fail_reads = true;
	TEST_ASSERT(!fat16_file_read_at(&file, 0, partial, sizeof(partial), &bytes));
}

static void test_block_device_dispatch(void)
{
	struct test_disk disk;
	make_disk(&disk, 0, false);
	struct block_device device = make_device(&disk);
	uint8_t sector[SECTOR_SIZE] = { 0 };
	TEST_ASSERT(block_device_read(&device, 1, 1, sector));
	TEST_ASSERT(disk.read_count == 1);
	TEST_ASSERT(block_device_write(&device, 1, 1, sector));
	TEST_ASSERT(disk.write_count == 1);
	TEST_ASSERT(!block_device_read(&device, 0, 0, sector));
	TEST_ASSERT(!block_device_read(&device, SECTOR_COUNT, 1, sector));
	TEST_ASSERT(!block_device_read(&device, UINT32_MAX + 1ull, 1, sector));
	TEST_ASSERT(!block_device_read(&device, 0, UINT16_MAX + 1u, sector));
	TEST_ASSERT(!block_device_write(&device, 0, 0, sector));
	device.sector_size = 1024;
	TEST_ASSERT(!block_device_read(&device, 0, 1, sector));
	TEST_ASSERT(!block_device_write(&device, 0, 1, sector));
}

static void test_allocation_failures_and_cycles(void)
{
	struct test_disk disk;
	make_disk(&disk, 600, false);
	struct block_device device = make_device(&disk);
	allocation_failure = true;
	TEST_ASSERT(read_fat_boot_section(device) == NULL);
	allocation_failure = false;

	make_disk(&disk, 8u * SECTOR_SIZE + 1, true);
	device = make_device(&disk);
	device.sector_count = 8;
	struct fat16_file file;
	TEST_ASSERT(fat16_file_open(&device, "HELLO.TXT", &file));
	uint8_t *content = malloc(8u * SECTOR_SIZE + 1);
	TEST_ASSERT(content != NULL);
	size_t bytes = 0;
	TEST_ASSERT(!fat16_file_read_at(&file, 0, content, 8u * SECTOR_SIZE + 1, &bytes));
	TEST_ASSERT(bytes == 8u * SECTOR_SIZE);
	free(content);
}

int main(void)
{
	test_layout_and_names();
	test_files_and_partial_reads();
	test_block_device_dispatch();
	test_allocation_failures_and_cycles();
	return 0;
}
