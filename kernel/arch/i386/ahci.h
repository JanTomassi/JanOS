#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Represents the single AHCI controller the kernel binds to after PCI
 * discovery. All fields are PCI-derived and used to configure and drive
 * the HBA:
 *   - bus/device/function: PCI address of the controller.
 *   - bar5: MMIO base address for the AHCI memory space (BAR5, 4 KiB aligned).
 *   - port_map: bitmask of implemented/active ports as reported by the HBA.
 *   - port: the first active port chosen as default for reads/writes.
 *   - irq_line: legacy PIC IRQ line assigned by PCI config space. */
struct ahci_controller {
	uint8_t bus;
	uint8_t device;
	uint8_t function;
	uint32_t bar5;
	uint32_t port_map;
	uint8_t port;
	uint8_t irq_line;
};

enum ahci_command {
	/* Data transfers (28-bit) */
	AHCI_CMD_READ_SECTORS = 0x20,	    /* PIO read sectors */
	AHCI_CMD_READ_SECTORS_EXT = 0x24,   /* PIO read sectors (48-bit) */
	AHCI_CMD_WRITE_SECTORS = 0x30,	    /* PIO write sectors */
	AHCI_CMD_WRITE_SECTORS_EXT = 0x34,  /* PIO write sectors (48-bit) */
	AHCI_CMD_READ_DMA = 0xC8,	    /* 28-bit LBA, multi-sector DMA read */
	AHCI_CMD_WRITE_DMA = 0xCA,	    /* 28-bit LBA, multi-sector DMA write */
	AHCI_CMD_READ_DMA_EXT = 0x25,	    /* 48-bit LBA, multi-sector DMA read */
	AHCI_CMD_WRITE_DMA_EXT = 0x35,	    /* 48-bit LBA, multi-sector DMA write */
	AHCI_CMD_READ_DMA_FPDMA = 0x60,	    /* NCQ read (Native Command Queuing) */
	AHCI_CMD_WRITE_DMA_FPDMA = 0x61,    /* NCQ write (Native Command Queuing) */
	AHCI_CMD_VERIFY_SECTORS = 0x40,	    /* Verify sectors (no data) */
	AHCI_CMD_VERIFY_SECTORS_EXT = 0x42, /* Verify sectors (48-bit, no data) */

	/* Cache / power management */
	AHCI_CMD_FLUSH_CACHE = 0xE7,	   /* Flush write cache (28-bit) */
	AHCI_CMD_FLUSH_CACHE_EXT = 0xEA,   /* Flush write cache (48-bit) */
	AHCI_CMD_STANDBY_IMMEDIATE = 0xE0, /* Enter standby */
	AHCI_CMD_IDLE_IMMEDIATE = 0xE1,	   /* Enter idle */
	AHCI_CMD_SLEEP = 0xE6,		   /* Enter sleep */

	/* Identity / feature control */
	AHCI_CMD_IDENTIFY = 0xEC,	 /* ATA IDENTIFY DEVICE */
	AHCI_CMD_IDENTIFY_PACKET = 0xA1, /* ATAPI identify packet device */
	AHCI_CMD_PACKET = 0xA0,		 /* ATAPI packet command */
	AHCI_CMD_SET_FEATURES = 0xEF,	 /* Set drive features */
	AHCI_CMD_SMART = 0xB0,		 /* SMART feature set (subcommands in feature reg) */

	/* Security */
	AHCI_CMD_SECURITY_SET_PASSWORD = 0xF1,	/* Set drive security password */
	AHCI_CMD_SECURITY_UNLOCK = 0xF2,	/* Unlock drive with password */
	AHCI_CMD_SECURITY_ERASE_PREPARE = 0xF3, /* Prepare for secure erase */
	AHCI_CMD_SECURITY_ERASE_UNIT = 0xF4,	/* Secure erase unit */
	AHCI_CMD_SECURITY_FREEZE_LOCK = 0xF5,	/* Freeze security configuration */

	/* Misc / maintenance */
	AHCI_CMD_DOWNLOAD_MICROCODE = 0x92, /* Firmware download */
	AHCI_CMD_READ_NATIVE_MAX = 0xF8,    /* Read native max address */
	AHCI_CMD_SET_MAX_ADDRESS = 0xF9	    /* Set native max address (HPA) */
};

bool ahci_probe(struct ahci_controller *out);
bool ahci_init(const struct ahci_controller *ctrl);
bool ahci_read28(uint8_t channel, uint8_t slavebit, uint32_t lba_addr, uint16_t sector_count, char *dest);
bool ahci_write28(uint8_t channel, uint8_t slavebit, uint32_t lba_addr, uint16_t sector_count, const char *src);
