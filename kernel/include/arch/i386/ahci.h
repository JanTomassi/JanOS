#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * AHCI command opcodes (ATA command set).
 * These are the commands issued through the H2D Register FIS when using AHCI.
 */
enum ahci_command {
	AHCI_CMD_NOP = 0x00,		  // No operation.
	AHCI_CMD_DEVICE_RESET = 0x08,	  // Reset the selected device.
	AHCI_CMD_READ_PIO = 0x20,	  // Read sectors using PIO (28-bit LBA).
	AHCI_CMD_READ_PIO_EXT = 0x24,	  // Read sectors using PIO (48-bit LBA).
	AHCI_CMD_READ_DMA = 0xC8,	  // Read sectors using DMA (28-bit LBA).
	AHCI_CMD_READ_DMA_EXT = 0x25,	  // Read sectors using DMA (48-bit LBA).
	AHCI_CMD_WRITE_PIO = 0x30,	  // Write sectors using PIO (28-bit LBA).
	AHCI_CMD_WRITE_PIO_EXT = 0x34,	  // Write sectors using PIO (48-bit LBA).
	AHCI_CMD_WRITE_DMA = 0xCA,	  // Write sectors using DMA (28-bit LBA).
	AHCI_CMD_WRITE_DMA_EXT = 0x35,	  // Write sectors using DMA (48-bit LBA).
	AHCI_CMD_CACHE_FLUSH = 0xE7,	  // Flush device write cache.
	AHCI_CMD_CACHE_FLUSH_EXT = 0xEA,  // Flush device write cache (48-bit).
	AHCI_CMD_IDENTIFY = 0xEC,	  // Identify ATA device.
	AHCI_CMD_PACKET = 0xA0,		  // Send ATAPI packet command.
	AHCI_CMD_IDENTIFY_PACKET = 0xA1,  // Identify ATAPI device.
	AHCI_CMD_SET_FEATURES = 0xEF,	  // Modify device features (DMA enable, etc).
	AHCI_CMD_SECURITY_FREEZE = 0xF5,  // Freeze security state.
	AHCI_CMD_SECURITY_DISABLE = 0xF6, // Disable security.
	AHCI_CMD_DOWNLOAD_MICROCODE = 0x92, // Download device microcode.
	AHCI_CMD_STANDBY_IMMEDIATE = 0xE0,   // Immediate standby.
	AHCI_CMD_IDLE_IMMEDIATE = 0xE1,      // Immediate idle.
	AHCI_CMD_STANDBY = 0xE2,	      // Enter standby mode.
	AHCI_CMD_IDLE = 0xE3,		      // Enter idle mode.
	AHCI_CMD_CHECK_POWER = 0xE5,	      // Check power mode.
	AHCI_CMD_SLEEP = 0xE6,		      // Enter sleep mode.
	AHCI_CMD_READ_VERIFY = 0x40,	      // Verify sectors (28-bit).
	AHCI_CMD_READ_VERIFY_EXT = 0x42,     // Verify sectors (48-bit).
	AHCI_CMD_SEEK = 0x70,		      // Seek (legacy).
	AHCI_CMD_EXECUTE_DEVICE_DIAG = 0x90, // Execute device diagnostics.
	AHCI_CMD_MEDIA_EJECT = 0xED,	      // Eject removable media.
};

#define AHCI_MAX_PORTS 32
#define AHCI_CMD_SLOT_COUNT 32

enum ahci_fis_type {
	AHCI_FIS_TYPE_REG_H2D = 0x27, // Register FIS - host to device.
	AHCI_FIS_TYPE_REG_D2H = 0x34, // Register FIS - device to host.
	AHCI_FIS_TYPE_DMA_ACT = 0x39, // DMA activate FIS.
	AHCI_FIS_TYPE_DMA_SETUP = 0x41, // DMA setup FIS.
	AHCI_FIS_TYPE_DATA = 0x46,	 // Data FIS.
	AHCI_FIS_TYPE_BIST = 0x58,	 // BIST activate FIS.
	AHCI_FIS_TYPE_PIO_SETUP = 0x5F, // PIO setup FIS.
	AHCI_FIS_TYPE_DEV_BITS = 0xA1,  // Set device bits FIS.
};

/**
 * Host Bus Adapter memory registers.
 * The HBA exposes these at BAR5 and each port's registers at offset 0x100.
 */
struct hba_port {
	volatile uint32_t clb;	 // Command list base address (low).
	volatile uint32_t clbu;	 // Command list base address (high).
	volatile uint32_t fb;	 // FIS base address (low).
	volatile uint32_t fbu;	 // FIS base address (high).
	volatile uint32_t is;	 // Interrupt status.
	volatile uint32_t ie;	 // Interrupt enable.
	volatile uint32_t cmd;	 // Command and status.
	volatile uint32_t reserved0;
	volatile uint32_t tfd;	 // Task file data.
	volatile uint32_t sig;	 // Signature.
	volatile uint32_t ssts;	 // SATA status (SCR0).
	volatile uint32_t sctl;	 // SATA control (SCR2).
	volatile uint32_t serr;	 // SATA error (SCR1).
	volatile uint32_t sact;	 // SATA active (SCR3).
	volatile uint32_t ci;	 // Command issue.
	volatile uint32_t sntf;	 // SATA notification (SCR4).
	volatile uint32_t fbs;	 // FIS-based switching control.
	volatile uint32_t reserved1[11];
	volatile uint32_t vendor[4];
} __attribute__((packed));

struct hba_mem {
	volatile uint32_t cap;	    // Host capability.
	volatile uint32_t ghc;	    // Global host control.
	volatile uint32_t is;	    // Interrupt status.
	volatile uint32_t pi;	    // Ports implemented.
	volatile uint32_t vs;	    // AHCI version.
	volatile uint32_t ccc_ctl;  // Command completion coalescing control.
	volatile uint32_t ccc_pts;  // Command completion coalescing ports.
	volatile uint32_t em_loc;   // Enclosure management location.
	volatile uint32_t em_ctl;   // Enclosure management control.
	volatile uint32_t cap2;	    // Host capabilities extended.
	volatile uint32_t bohc;	    // BIOS/OS handoff control and status.
	uint8_t reserved[0xA0 - 0x2C];
	uint8_t vendor[0x100 - 0xA0];
	struct hba_port ports[AHCI_MAX_PORTS];
} __attribute__((packed));

/**
 * Command header entry (32 bytes each).
 * One command list contains 32 headers, one per slot.
 */
struct hba_cmd_header {
	uint8_t cfl : 5;	// Command FIS length in DWORDS.
	uint8_t a : 1;		// ATAPI flag.
	uint8_t w : 1;		// Write (1) or read (0).
	uint8_t p : 1;		// Prefetchable.
	uint8_t r : 1;		// Reset.
	uint8_t b : 1;		// BIST.
	uint8_t c : 1;		// Clear busy upon R_OK.
	uint8_t reserved0 : 1;
	uint8_t pmp : 4;	// Port multiplier port.
	uint16_t prdtl;		// Physical region descriptor table length.
	volatile uint32_t prdbc; // PRD byte count transferred.
	uint32_t ctba;		// Command table base address.
	uint32_t ctbau;		// Command table base address upper 32 bits.
	uint32_t reserved1[4];
} __attribute__((packed));

/**
 * Physical Region Descriptor Table entry.
 * Each PRDT entry maps a DMA buffer region (up to 4MiB).
 */
struct hba_prdt_entry {
	uint32_t dba;	// Data base address.
	uint32_t dbau;	// Data base address upper.
	uint32_t reserved0;
	uint32_t dbc : 22; // Byte count, 0-based (max 4MiB).
	uint32_t reserved1 : 9;
	uint32_t i : 1; // Interrupt on completion.
} __attribute__((packed));

/**
 * Command table.
 * Contains the command FIS, ATAPI command (if any), and PRDT entries.
 */
struct hba_cmd_table {
	uint8_t cfis[64];  // Command FIS.
	uint8_t acmd[16];  // ATAPI command.
	uint8_t reserved[48];
	struct hba_prdt_entry prdt_entry[1];
} __attribute__((packed));

/**
 * Register - Host to Device FIS (FIS type 0x27).
 * Used to send ATA commands over AHCI.
 */
struct fis_reg_h2d {
	uint8_t fis_type; // FIS type: AHCI_FIS_TYPE_REG_H2D.
	uint8_t pmport : 4;
	uint8_t reserved0 : 3;
	uint8_t c : 1;	 // Command/control bit.
	uint8_t command;	 // ATA command.
	uint8_t featurel;	 // Feature low byte.
	uint8_t lba0;		 // LBA low byte.
	uint8_t lba1;
	uint8_t lba2;
	uint8_t device;	 // Device register.
	uint8_t lba3;
	uint8_t lba4;
	uint8_t lba5;
	uint8_t featureh;	 // Feature high byte.
	uint8_t countl;	 // Sector count low.
	uint8_t counth;	 // Sector count high.
	uint8_t icc;		 // Isochronous command completion.
	uint8_t control;	 // Control register.
	uint8_t reserved1[4];
} __attribute__((packed));

bool ahci_probe(void);
void ahci_init(void);
bool ahci_port_is_active(uint8_t port_index);
bool ahci_read28_port(uint8_t port_index, uint32_t lba_addr, uint16_t sector_count, void *dest);
bool ahci_write28_port(uint8_t port_index, uint32_t lba_addr, uint16_t sector_count, const void *src);
