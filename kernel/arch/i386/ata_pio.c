#include <stdint.h>
#include <kernel/display.h>
#include "port.h"
#include "ata_pio.h"

enum ATA_SR {
	ATA_SR_BSY = 0x80,  // Busy
	ATA_SR_DRDY = 0x40, // Drive ready
	ATA_SR_DF = 0x20,   // Drive write fault
	ATA_SR_DSC = 0x10,  // Drive seek complete
	ATA_SR_DRQ = 0x08,  // Data request ready
	ATA_SR_CORR = 0x04, // Corrected data
	ATA_SR_IDX = 0x02,  // Index
	ATA_SR_ERR = 0x01,  // Error
};

enum ATA_ER {
	ATA_ER_BBK = 0x80,   // Bad block
	ATA_ER_UNC = 0x40,   // Uncorrectable data
	ATA_ER_MC = 0x20,    // Media changed
	ATA_ER_IDNF = 0x10,  // ID mark not found
	ATA_ER_MCR = 0x08,   // Media change request
	ATA_ER_ABRT = 0x04,  // Command aborted
	ATA_ER_TK0NF = 0x02, // Track 0 not found
	ATA_ER_AMNF = 0x01,  // No address mark
};

enum ATA_CMD {
	ATA_CMD_READ_PIO = 0x20,
	ATA_CMD_READ_PIO_EXT = 0x24,
	ATA_CMD_READ_DMA = 0xC8,
	ATA_CMD_READ_DMA_EXT = 0x25,
	ATA_CMD_WRITE_PIO = 0x30,
	ATA_CMD_WRITE_PIO_EXT = 0x34,
	ATA_CMD_WRITE_DMA = 0xCA,
	ATA_CMD_WRITE_DMA_EXT = 0x35,
	ATA_CMD_CACHE_FLUSH = 0xE7,
	ATA_CMD_CACHE_FLUSH_EXT = 0xEA,
	ATA_CMD_PACKET = 0xA0,
	ATA_CMD_IDENTIFY_PACKET = 0xA1,
	ATA_CMD_IDENTIFY = 0xEC,
};

enum ATA_IDENT {
	ATA_IDENT_DEVICETYPE = 0,
	ATA_IDENT_CYLINDERS = 2,
	ATA_IDENT_HEADS = 6,
	ATA_IDENT_SECTORS = 12,
	ATA_IDENT_SERIAL = 20,
	ATA_IDENT_MODEL = 54,
	ATA_IDENT_CAPABILITIES = 98,
	ATA_IDENT_FIELDVALID = 106,
	ATA_IDENT_MAX_LBA = 120,
	ATA_IDENT_COMMANDSETS = 164,
	ATA_IDENT_MAX_LBA_EXT = 200,
};

enum ATA_DEVICE {
	ATA_MASTER = 0x00,
	ATA_SLAVE = 0x01,
};

#define ATA_MASTER 0x00
#define ATA_SLAVE 0x01

enum ata_channel {
	ATA_PRIMARY = 0,
	ATA_SECONDARY = 1,
};

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_SECONDARY_IO 0x170
#define ATA_SECONDARY_CTRL 0x376

struct IDEChannelRegisters {
	unsigned short base;  // I/O Base.
	unsigned short ctrl;  // Control Base
	unsigned short bmide; // Bus Master IDE
	unsigned char nIEN;   // nIEN (No Interrupt);
} channels[2] = {
	{ .base = ATA_PRIMARY_IO, .ctrl = ATA_PRIMARY_CTRL, .bmide = 0, .nIEN = 0 },
	{ .base = ATA_SECONDARY_IO, .ctrl = ATA_SECONDARY_CTRL, .bmide = 0, .nIEN = 0 },
};

static void wait_device(uint8_t channel)
{
	uint16_t ctrl = channels[channel].ctrl;
	inb(ctrl); /* wait 400ns for drive select to work */
	inb(ctrl);
	inb(ctrl);
	inb(ctrl);
}

static void soft_reset(uint8_t channel)
{
	wait_device(channel);

	outb(channels[channel].ctrl, 0x4);

	wait_device(channel);

	outb(channels[channel].ctrl, 0x0);

	wait_device(channel);
}

/* on Primary bus: ctrl->base =0x1F0, ctrl->dev_ctl =0x3F6. REG_CYL_LO=4, REG_CYL_HI=5, REG_DEVSEL=6 */
uint32_t ata_pio_detect_devtype(uint8_t channel, uint8_t drive)
{
	uint8_t REG_CYL_LO = 4;
	uint8_t REG_CYL_HI = 5;
	uint8_t REG_DEVSEL = 6;

	if (channel > ATA_SECONDARY)
		return ATADEV_UNKNOWN;

	soft_reset(channel); /* waits until drive is ready again */
	outb(channels[channel].ctrl + 1, drive);
	wait_device(channel);

	outb(channels[channel].base + REG_DEVSEL, 0xA0 | drive << 4);
	wait_device(channel);

	unsigned cl = inb(channels[channel].base + REG_CYL_LO); /* get the "signature bytes" */
	unsigned ch = inb(channels[channel].base + REG_CYL_HI);

	/* differentiate ATA, ATAPI, SATA and SATAPI */
	if (cl == 0x14 && ch == 0xEB)
		return ATADEV_PATAPI;
	if (cl == 0x69 && ch == 0x96)
		return ATADEV_SATAPI;
	if (cl == 0 && ch == 0)
		return ATADEV_PATA;
	if (cl == 0x3c && ch == 0xc3)
		return ATADEV_SATA;
	return ATADEV_UNKNOWN;
}

/*
  Send 0xE0 for the "master" or 0xF0 for the "slave", ORed with the highest 4 bits of the LBA to port 0x1F6: outb(0x1F6, 0xE0 | (slavebit << 4) | ((LBA >> 24) & 0x0F))
  Send a NULL byte to port 0x1F1, if you like (it is ignored and wastes lots of CPU time): outb(0x1F1, 0x00)
  Send the sectorcount to port 0x1F2: outb(0x1F2, (unsigned char) count)
  Send the low 8 bits of the LBA to port 0x1F3: outb(0x1F3, (unsigned char) LBA))
  Send the next 8 bits of the LBA to port 0x1F4: outb(0x1F4, (unsigned char)(LBA >> 8))
  Send the next 8 bits of the LBA to port 0x1F5: outb(0x1F5, (unsigned char)(LBA >> 16))
  Send the "READ SECTORS" command (0x20) to port 0x1F7: outb(0x1F7, 0x20)
  Wait for an IRQ or poll.
  Transfer 256 16-bit values, a uint16_t at a time, into your buffer from I/O port 0x1F0. (In assembler, REP INSW works well for this.)
  Then loop back to waiting for the next IRQ (or poll again -- see next note) for each successive sector.
*/
bool ata_pio_28_read(uint8_t channel, uint8_t drive, uint32_t lba_addr, uint16_t sector_count, void *dest)
{
	if (channel > ATA_SECONDARY)
		return false;

	outb(channels[channel].ctrl, 2);

	if (sector_count >= 256)
		panic("Trying to read more then 256 sector from ata_pio_28");
	// soft_reset(dev_ctl); /* waits until master drive is ready again */

	uint16_t base = channels[channel].base;
	outb(base + 6, 0xE0 | (drive << 4) | ((lba_addr >> 24) & 0x0f));
	outb(base + 2, sector_count);
	outb(base + 3, (uint8_t)lba_addr);
	outb(base + 4, (uint8_t)(lba_addr >> 8));
	outb(base + 5, (uint8_t)(lba_addr >> 16));
	outb(base + 7, 0x20);

	if (sector_count == 0)
		sector_count = 256;

	size_t dest_idx = 0;
	while (sector_count--) {
		while (inb(base + 7) & 0x80)
			;
		if (inb(base + 7) & 0x21)
			panic("ERR or DF set, error reg is %x", inb(base + 1));

		/* in al, dx		; grab a status byte */
		/* test al, 0x80	; BSY flag set? */
		/* jne short .pior_l	; (all other flags are meaningless if BSY is set) */
		/* test al, 0x21	; ERR or DF set? */
		/* jne short .fail */
		uint16_t word_to_trans = 256;
		while (word_to_trans--) {
			uint16_t data = inw(base);
			((uint8_t *)dest)[dest_idx++] = (uint8_t)data;
			((uint8_t *)dest)[dest_idx++] = (uint8_t)(data >> 8);
		}
	}
	outb(channels[channel].ctrl, 0);
	return true;
}

char *ata_pio_debug_devtype(enum ATADEV dev_type)
{
	switch (dev_type) {
	case ATADEV_PATAPI:
		return "pata-pi";
	case ATADEV_SATAPI:
		return "sata-pi";
	case ATADEV_PATA:
		return "pata";
	case ATADEV_SATA:
		return "sata";
	case ATADEV_UNKNOWN:
		return "unknown";
	default:
		return "unknown dev_type";
	}
}

bool ata_pio_28_write(uint8_t channel, uint8_t drive, uint32_t lba_addr, uint16_t sector_count, const void *src)
{
	if (channel > ATA_SECONDARY)
		return false;

	outb(channels[channel].ctrl, 2);

	if (sector_count >= 256)
		panic("Trying to write more then 256 sector from ata_pio_28");

	uint16_t base = channels[channel].base;
	outb(base + 6, 0xE0 | (drive << 4) | ((lba_addr >> 24) & 0x0f));
	outb(base + 2, sector_count);
	outb(base + 3, (uint8_t)lba_addr);
	outb(base + 4, (uint8_t)(lba_addr >> 8));
	outb(base + 5, (uint8_t)(lba_addr >> 16));
	outb(base + 7, 0x30);

	if (sector_count == 0)
		sector_count = 256;

	size_t src_idx = 0;
	while (sector_count--) {
		while (inb(base + 7) & 0x80)
			;
		if (inb(base + 7) & 0x21)
			panic("ERR or DF set, error reg is %x", inb(base + 1));

		uint16_t word_to_trans = 256;
		while (word_to_trans--) {
			uint16_t data = ((const uint8_t *)src)[src_idx++];
			data |= ((uint16_t)((const uint8_t *)src)[src_idx++]) << 8;
			outw(base, data);
		}
	}

	outb(base + 7, ATA_CMD_CACHE_FLUSH);
	while (inb(base + 7) & ATA_SR_BSY)
		;

	outb(channels[channel].ctrl, 0);
	return true;
}

void ide_initialize(unsigned int BAR0, unsigned int BAR1, unsigned int BAR2, unsigned int BAR3, unsigned int BAR4)
{
	channels[ATA_PRIMARY].base = (BAR0 & 0xFFFFFFFC) ? (BAR0 & 0xFFFFFFFC) : ATA_PRIMARY_IO;
	channels[ATA_PRIMARY].ctrl = (BAR1 & 0xFFFFFFFC) ? (BAR1 & 0xFFFFFFFC) : ATA_PRIMARY_CTRL;
	channels[ATA_SECONDARY].base = (BAR2 & 0xFFFFFFFC) ? (BAR2 & 0xFFFFFFFC) : ATA_SECONDARY_IO;
	channels[ATA_SECONDARY].ctrl = (BAR3 & 0xFFFFFFFC) ? (BAR3 & 0xFFFFFFFC) : ATA_SECONDARY_CTRL;
	channels[ATA_PRIMARY].bmide = (BAR4 & 0xFFFFFFFC);
	channels[ATA_SECONDARY].bmide = (BAR4 & 0xFFFFFFFC) ? (BAR4 & 0xFFFFFFFC) + 8 : 0;
}
