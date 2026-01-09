#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum storage_backend {
	STORAGE_BACKEND_AHCI,
	STORAGE_BACKEND_ATA_PIO,
};

struct storage_device {
	enum storage_backend backend;
	uint8_t ahci_port;
	uint8_t channel;
	uint8_t drive;
};

void storage_init(void);
size_t storage_device_count(void);
bool storage_get_device(size_t device_index, struct storage_device *out_device);
bool storage_read_device(const struct storage_device *device, uint32_t lba_addr, uint16_t sector_count, void *dest);
bool storage_write_device(const struct storage_device *device, uint32_t lba_addr, uint16_t sector_count, const void *src);
