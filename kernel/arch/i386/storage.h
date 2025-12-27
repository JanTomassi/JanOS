#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ata_pio.h"
#include "ahci.h"

enum storage_driver_type {
	STORAGE_DRIVER_NONE = 0,
	STORAGE_DRIVER_ATA_PIO,
	STORAGE_DRIVER_AHCI,
};

struct storage_driver {
	enum storage_driver_type type;
	struct ahci_controller ahci;
	uint32_t (*detect_devtype)(uint8_t channel, uint8_t slavebit);
	bool (*read28)(uint8_t channel, uint8_t slavebit, uint32_t lba_addr,
		       uint16_t sector_count, char* dest);
	bool (*write28)(uint8_t channel, uint8_t slavebit, uint32_t lba_addr,
			uint16_t sector_count, const char* src);
};

void storage_init(void);
const struct storage_driver *storage_get_driver(void);
const char *storage_driver_name(enum storage_driver_type type);
