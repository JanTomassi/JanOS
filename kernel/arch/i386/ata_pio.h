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

enum ATA_DEVICE {
	ATA_MASTER = 0x00,
	ATA_SLAVE = 0x01,
};

enum ATA_CHANNEL {
	ATA_PRIMARY = 0,
	ATA_SECONDARY = 1,
};

char *ata_pio_debug_devtype(enum ATADEV dev_type);

uint32_t ata_pio_detect_devtype(uint8_t channel, uint8_t slavebit);

bool ata_pio_28_read(uint8_t channel, uint8_t slavebit, uint32_t lba_addr, uint16_t sector_count, char *dest);
bool ata_pio_28_write(uint8_t channel, uint8_t slavebit, uint32_t lba_addr, uint16_t sector_count, const char *src);

void ide_initialize(unsigned int BAR0, unsigned int BAR1, unsigned int BAR2, unsigned int BAR3, unsigned int BAR4);
