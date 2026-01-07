#include <kernel/storage.h>

#include <kernel/display.h>

#include "../arch/i386/ahci.h"
#include "../arch/i386/ata_pio.h"
#include "../arch/i386/pci.h"

MODULE("storage")

storage_read_handler_t storage_read_handler = nullptr;
storage_write_handler_t storage_write_handler = nullptr;

static uint8_t storage_channel = 0;
static uint8_t storage_drive = 0;

static bool storage_pio_read(uint32_t lba_addr, uint16_t sector_count, void *dest)
{
	return ata_pio_28_read(storage_channel, storage_drive, lba_addr, sector_count, dest);
}

static bool storage_pio_write(uint32_t lba_addr, uint16_t sector_count, const void *src)
{
	return ata_pio_28_write(storage_channel, storage_drive, lba_addr, sector_count, src);
}

void storage_init(void)
{
	if (ahci_probe()) {
		ahci_init();
		storage_read_handler = ahci_read28;
		storage_write_handler = ahci_write28;
		mprint("Storage: AHCI enabled\n");
		return;
	}

	struct pci_device ide = { 0 };
	if (!pci_find_storage_device(PCI_SUBCLASS_IDE, &ide)) {
		mprint("Storage: no IDE controller found\n");
		return;
	}

	pci_enable_bus_mastering(&ide);
	ide_initialize(ide.bar[0], ide.bar[1], ide.bar[2], ide.bar[3], ide.bar[4]);

	uint8_t channels[] = { 0, 0, 1, 1 };
	uint8_t drives[] = { 0, 1, 0, 1 };
	for (size_t i = 0; i < sizeof(channels); i++) {
		uint32_t devtype = ata_pio_detect_devtype(channels[i], drives[i]);
		if (devtype != ATADEV_UNKNOWN) {
			storage_channel = channels[i];
			storage_drive = drives[i];
			break;
		}
	}

	storage_read_handler = storage_pio_read;
	storage_write_handler = storage_pio_write;
	mprint("Storage: ATA PIO enabled (ch=%u drv=%u)\n", storage_channel, storage_drive);
}

bool storage_read(uint32_t lba_addr, uint16_t sector_count, void *dest)
{
	if (storage_read_handler == nullptr)
		return false;
	return storage_read_handler(lba_addr, sector_count, dest);
}

bool storage_write(uint32_t lba_addr, uint16_t sector_count, const void *src)
{
	if (storage_write_handler == nullptr)
		return false;
	return storage_write_handler(lba_addr, sector_count, src);
}
