#include <stdlib.h>
#include <string.h>

#include <kernel/allocator.h>
#include <kernel/fat16.h>

static uint32_t fat16_root_dir_sectors(const fat_BS_t *bpb)
{
	uint32_t entries_bytes = (uint32_t)bpb->root_dir_count * 32;
	uint32_t sector_size = bpb->sector_size;

	return (entries_bytes + (sector_size - 1)) / sector_size;
}

static uint32_t fat16_fat_start_lba(const fat_BS_t *bpb)
{
	return bpb->reserved_sectors;
}

static uint32_t fat16_root_dir_lba(const fat_BS_t *bpb, uint32_t fat_start_lba)
{
	return fat_start_lba + ((uint32_t)bpb->FAT_count * bpb->sectors_per_FAT);
}

static uint32_t fat16_data_start_lba(uint32_t root_dir_lba, uint32_t root_dir_sectors)
{
	return root_dir_lba + root_dir_sectors;
}

void fat16_compute_layout(const fat_BS_t *bpb, fat16_layout_t *out)
{
	out->fat_start_lba = fat16_fat_start_lba(bpb);
	out->root_dir_sectors = fat16_root_dir_sectors(bpb);
	out->root_dir_lba = fat16_root_dir_lba(bpb, out->fat_start_lba);

	out->data_start_lba = fat16_data_start_lba(out->root_dir_lba, out->root_dir_sectors);
	out->sectors_per_cluster = bpb->sectors_per_cluster;
}

fat_BS_t *read_fat_boot_section(struct storage_device dev){
	allocator_t gpa_alloc = get_gpa_allocator();
	fatptr_t boot_sector = gpa_alloc.alloc(512);

	fat_BS_t *boot_sector_p = boot_sector.ptr;
	memset(boot_sector_p, 0, 512);

	storage_read_device(&dev, 0, 1, boot_sector_p);

	return boot_sector_p;
}
