#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <kernel/allocator.h>
#include <kernel/fat16.h>

static bool fat16_read_root_dir_sectors(const struct storage_device *device, const fat16_layout_t *layout,
					uint8_t *out)
{
	if (layout->root_dir_sectors == 0 || layout->root_dir_sectors > UINT16_MAX)
		return false;

	return storage_read_device(device, layout->root_dir_lba,
				   (uint16_t)layout->root_dir_sectors, out);
}

static size_t fat16_copy_root_dir_entries(const uint8_t *raw, size_t raw_entries,
					  fat_dir_entry_t *entries, size_t max_entries)
{
	size_t stored = 0;
	const fat_dir_entry_t *raw_entries_ptr = (const fat_dir_entry_t *)raw;

	for (size_t i = 0; i < raw_entries && stored < max_entries; ++i) {
		const fat_dir_entry_t *entry = &raw_entries_ptr[i];
		if (fat16_dir_entry_is_unused(entry))
			break;
		if (fat16_dir_entry_is_deleted(entry))
			continue;

		memcpy(&entries[stored], entry, sizeof(*entry));
		++stored;
	}

	return stored;
}

static size_t fat16_trim_spaces(const char *input, size_t max_len)
{
	size_t len = max_len;

	while (len > 0 && input[len - 1] == ' ')
		--len;

	return len;
}

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

bool fat16_read_root_dir(const struct storage_device *device, const fat16_layout_t *layout,
			 fat_dir_entry_t *entries, size_t max_entries)
{
	if (device == nullptr || layout == nullptr || entries == nullptr || max_entries == 0)
		return false;
	if (layout->sector_size == 0 || layout->root_dir_sectors == 0)
		return false;

	size_t raw_size = (size_t)layout->sector_size * layout->root_dir_sectors;
	size_t raw_entries = raw_size / sizeof(fat_dir_entry_t);
	allocator_t gpa_alloc = get_gpa_allocator();
	fatptr_t raw_buf = gpa_alloc.alloc(raw_size);
	if (raw_buf.ptr == nullptr)
		return false;

	bool ok = fat16_read_root_dir_sectors(device, layout, raw_buf.ptr);
	if (ok)
		fat16_copy_root_dir_entries(raw_buf.ptr, raw_entries, entries, max_entries);

	gpa_alloc.free(raw_buf);
	return ok;
}

bool fat16_dir_entry_is_unused(const fat_dir_entry_t *entry)
{
	if (entry == nullptr)
		return true;

	return (uint8_t)entry->name[0] == 0x00;
}

bool fat16_dir_entry_is_deleted(const fat_dir_entry_t *entry)
{
	if (entry == nullptr)
		return true;

	return (uint8_t)entry->name[0] == 0xE5;
}

bool fat16_decode_83_name(const fat_dir_entry_t *entry, char *out, size_t out_size)
{
	if (entry == nullptr || out == nullptr || out_size == 0)
		return false;
	if (fat16_dir_entry_is_unused(entry) || fat16_dir_entry_is_deleted(entry))
		return false;

	size_t name_len = fat16_trim_spaces(entry->name, sizeof(entry->name));
	size_t ext_len = fat16_trim_spaces(entry->ext, sizeof(entry->ext));
	size_t needed = name_len + (ext_len ? 1 : 0) + ext_len + 1;
	if (needed > out_size)
		return false;

	memcpy(out, entry->name, name_len);
	size_t offset = name_len;
	if (ext_len > 0) {
		out[offset++] = '.';
		memcpy(out + offset, entry->ext, ext_len);
		offset += ext_len;
	}
	out[offset] = '\0';
	return true;
}
