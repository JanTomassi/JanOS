#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum block_device_backend {
	BLOCK_DEVICE_BACKEND_AHCI,
	BLOCK_DEVICE_BACKEND_ATA_PIO,
};

struct block_device {
	enum block_device_backend backend;
	uint32_t sector_size;
	uint64_t sector_count;
	char name[16];
	uint8_t ahci_port;
	uint8_t channel;
	uint8_t drive;
};

void block_device_init(void);
size_t block_device_count(void);
bool block_device_get(size_t index, struct block_device *out_device);
bool block_device_find(const char *name, struct block_device *out_device);
bool block_device_read(const struct block_device *device, uint64_t lba,
	uint32_t sector_count, void *destination);
bool block_device_write(const struct block_device *device, uint64_t lba,
	uint32_t sector_count, const void *source);
