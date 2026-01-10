#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/storage.h>

typedef struct fat_BS{
	uint8_t jmp_code[3]; // jump to boot code
	char oem_id[8]; // OEM identifier
	uint16_t sector_size; // num of bytes per sector
	uint8_t sectors_per_cluster; // num of sectors per cluster.
	uint16_t reserved_sectors; // num of reserved sectors;
	uint8_t FAT_count; // num of file allocation tables
	uint16_t root_dir_count; // num of root directory entries
	uint16_t sector_count; // total sectors in the logical volume
	uint8_t media_type;
	uint16_t sectors_per_FAT; // num of sector per file allocation table only fat12/fat16
	uint16_t track_count; // num of sectors per track (do not trust)
	uint16_t heads_count; // num of heads on the storage media (do not trust)
	uint32_t hidden_sectors; // num of hidden sectors (beginning lba)
	uint32_t large_sector_count; // total sectors in the logical volume if more the 0xffff

	// this will be cast to it's specific type once the driver
	// actually knows what type of FAT this is.
	uint8_t	extended_section[0x1FF-0x024];
}__attribute__((packed)) fat_BS_t;

typedef struct fat1_EBR{
	uint8_t drive_num;
	uint8_t win_nt_flags;
	uint8_t signature;
	uint32_t volume_id;
	char volume_label[11];
	char system_id[8];
	uint8_t code[448];
	uint16_t boot_partition_sig;
}__attribute__((packed)) fat1_EBR_t;

typedef struct fat16_layout{
	uint32_t fat_start_lba;
	uint32_t root_dir_lba;
	uint32_t root_dir_sectors;
	uint32_t data_start_lba;
	uint32_t sectors_per_cluster;
	uint16_t sector_size;
} fat16_layout_t;

typedef struct fat_dir_entry {
	char name[8];
	char ext[3];
	uint8_t attributes;
	uint8_t reserved;
	uint8_t creation_time_tenths;
	uint16_t creation_time;
	uint16_t creation_date;
	uint16_t last_access_date;
	uint16_t first_cluster_high;
	uint16_t write_time;
	uint16_t write_date;
	uint16_t first_cluster_low;
	uint32_t file_size;
} __attribute__((packed)) fat_dir_entry_t;

fat_BS_t *read_fat_boot_section(struct storage_device dev);
void fat16_compute_layout(const fat_BS_t *bpb, fat16_layout_t *out);
uint16_t fat16_read_fat_entry(const struct storage_device *device, const fat16_layout_t *layout, uint16_t cluster);
bool fat16_is_end_of_chain(uint16_t entry);
bool fat16_read_root_dir(const struct storage_device *device, const fat16_layout_t *layout,
			 fat_dir_entry_t *entries, size_t max_entries);
bool fat16_dir_entry_is_unused(const fat_dir_entry_t *entry);
bool fat16_dir_entry_is_deleted(const fat_dir_entry_t *entry);
bool fat16_decode_83_name(const fat_dir_entry_t *entry, char *out, size_t out_size);
