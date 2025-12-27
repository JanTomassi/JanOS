#include "storage.h"
#include "ahci.h"
#include "pci.h"

static struct storage_driver active_driver = {
	.type = STORAGE_DRIVER_NONE,
	.ahci = {0},
	.detect_devtype = 0,
	.read28 = 0,
	.write28 = 0,
};

const char *storage_driver_name(enum storage_driver_type type)
{
	switch (type) {
	case STORAGE_DRIVER_ATA_PIO:
		return "ata-pio";
	case STORAGE_DRIVER_AHCI:
		return "ahci";
	case STORAGE_DRIVER_NONE:
	default:
		return "none";
	}
}

void storage_init(void)
{
	struct ahci_controller ctrl = {0};
	if (ahci_probe(&ctrl) && ahci_init(&ctrl)) {
		active_driver.type = STORAGE_DRIVER_AHCI;
		active_driver.ahci = ctrl;
		active_driver.detect_devtype = 0;
		active_driver.read28 = ahci_read28;
		active_driver.write28 = ahci_write28;
		return;
	}

	struct pci_device_info ide_dev = {0};
	if (pci_find_storage(0x01, 0x01, &ide_dev)) {
		ide_initialize(ide_dev.bar[0], ide_dev.bar[1], ide_dev.bar[2],
			       ide_dev.bar[3], ide_dev.bar[4]);
	} else {
		ide_initialize(0, 0, 0, 0, 0);
	}
	active_driver.type = STORAGE_DRIVER_ATA_PIO;
	active_driver.detect_devtype = ata_pio_detect_devtype;
	active_driver.read28 = ata_pio_28_read;
	active_driver.write28 = ata_pio_28_write;
}

const struct storage_driver *storage_get_driver(void)
{
	return &active_driver;
}
