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
	out->sector_size = bpb->sector_size;
}

fat_BS_t *read_fat_boot_section(struct storage_device dev){
	allocator_t gpa_alloc = get_gpa_allocator();
	fatptr_t boot_sector = gpa_alloc.alloc(512);

	fat_BS_t *boot_sector_p = boot_sector.ptr;
	memset(boot_sector_p, 0, 512);

	storage_read_device(&dev, 0, 1, boot_sector_p);

	return boot_sector_p;
}

static bool fat16_read_fat_sector(const struct storage_device *device, const fat16_layout_t *layout,
				  uint32_t sector_index, uint8_t *out_sector)
{
	uint32_t lba = layout->fat_start_lba + sector_index;

	return storage_read_device(device, lba, 1, out_sector);
}

static uint16_t fat16_extract_entry(const uint8_t *sector, uint32_t offset)
{
	return (uint16_t)(sector[offset] | ((uint16_t)sector[offset + 1] << 8));
}

uint16_t fat16_read_fat_entry(const struct storage_device *device, const fat16_layout_t *layout, uint16_t cluster)
{
	if (device == nullptr || layout == nullptr || layout->sector_size == 0)
		return 0;

	uint32_t fat_offset = (uint32_t)cluster * 2;
	uint32_t sector_index = fat_offset / layout->sector_size;
	uint32_t entry_offset = fat_offset % layout->sector_size;

	allocator_t gpa_alloc = get_gpa_allocator();
	fatptr_t sector_buf = gpa_alloc.alloc(layout->sector_size);
	if (sector_buf.ptr == nullptr)
		return 0;

	uint16_t entry = 0;
	if (fat16_read_fat_sector(device, layout, sector_index, sector_buf.ptr)
	    && entry_offset + 1 < layout->sector_size) {
		entry = fat16_extract_entry(sector_buf.ptr, entry_offset);
	}

	gpa_alloc.free(sector_buf);

	return entry;
}

bool fat16_is_end_of_chain(uint16_t entry)
{
	return entry >= 0xFFF8;
}
