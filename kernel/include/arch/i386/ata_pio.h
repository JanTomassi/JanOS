#pragma once

#include <stdbool.h>
#include <stdint.h>

enum ATADEV {
	ATADEV_PATAPI = 0,
	ATADEV_SATAPI = 1,
	ATADEV_PATA = 2,
	ATADEV_SATA = 3,
	ATADEV_UNKNOWN = 4,
};

char *ata_pio_debug_devtype(enum ATADEV dev_type);

uint32_t ata_pio_detect_devtype(uint8_t channel, uint8_t drive);

bool ata_pio_28_read(uint8_t channel, uint8_t drive, uint32_t lba_addr, uint16_t sector_count, void *dest);
bool ata_pio_28_write(uint8_t channel, uint8_t drive, uint32_t lba_addr, uint16_t sector_count, const void *src);

void ide_initialize(unsigned int BAR0, unsigned int BAR1, unsigned int BAR2, unsigned int BAR3, unsigned int BAR4);
