#include <kernel/storage.h>

#include <kernel/allocator.h>
#include <kernel/display.h>

#include "../arch/i386/ahci.h"
#include "../arch/i386/ata_pio.h"
#include "../arch/i386/pci.h"

#include <list.h>

MODULE("storage")

#define STORAGE_MAX_DEVICES (AHCI_MAX_PORTS + 4u)

struct storage_device_entry {
	struct storage_device device;
	struct list_head list;
};

static slab_cache_t *storage_device_cache = nullptr;
static LIST_HEAD(storage_devices_list);
static size_t storage_devices_count = 0;

static void storage_clear_devices(void)
{
	if (storage_device_cache == nullptr)
		return;

	struct storage_device_entry *entry = nullptr;
	while ((entry = list_pop_entry(&storage_devices_list, struct storage_device_entry, list)) != nullptr) {
		slab_free_obj(storage_device_cache, (fatptr_t){ .ptr = entry, .len = sizeof(*entry) });
	}

	storage_devices_count = 0;
}

static bool storage_register_device(struct storage_device device)
{
	if (storage_devices_count >= STORAGE_MAX_DEVICES)
		return false;

	if (storage_device_cache == nullptr)
		return false;

	fatptr_t entry_ptr = slab_alloc_obj(storage_device_cache);
	if (entry_ptr.ptr == nullptr)
		return false;

	struct storage_device_entry *entry = entry_ptr.ptr;
	entry->device = device;
	RESET_LIST_ITEM(&entry->list);
	list_add(&entry->list, &storage_devices_list);
	storage_devices_count++;
	return true;
}

static void storage_init_cache()
{
	if (storage_device_cache == nullptr) {
		storage_device_cache = slab_create("storage_device", sizeof(struct storage_device_entry),
						   alignof(struct storage_device_entry), nullptr, nullptr);
		if (storage_device_cache == nullptr) {
			mprint("Storage: failed to allocate device cache\n");
			return;
		}
	}
}

static void storage_try_ahci_init()
{
	if (!ahci_probe()) {
		return;
	}

	ahci_init();
	for (uint8_t port = 0; port < AHCI_MAX_PORTS; port++) {
		if (!ahci_port_is_active(port))
			continue;

		struct storage_device device = {
			.backend = STORAGE_BACKEND_AHCI,
			.ahci_port = port,
		};
		storage_register_device(device);
	}
}

static void storage_try_register_ide_dev()
{
	uint8_t channels[] = { 0, 0, 1, 1 };
	uint8_t drives[] = { 0, 1, 0, 1 };
	for (size_t i = 0; i < sizeof(channels); i++) {
		uint32_t devtype = ata_pio_detect_devtype(channels[i], drives[i]);
		if (devtype == ATADEV_UNKNOWN)
			continue;

		struct storage_device device = {
			.backend = STORAGE_BACKEND_ATA_PIO,
			.channel = channels[i],
			.drive = drives[i],
		};
		storage_register_device(device);
	}
}

void storage_init(void)
{
	storage_init_cache();

	storage_clear_devices();

	storage_try_ahci_init();
	if (storage_devices_count > 0) {
		mprint("Storage: AHCI enabled (%u drives)\n", storage_devices_count);
		return;
	}

	struct pci_device ide = { 0 };
	if (!pci_find_storage_device(PCI_SUBCLASS_IDE, &ide)) {
		mprint("Storage: no IDE controller found\n");
		return;
	}

	pci_enable_bus_mastering(&ide);
	ide_initialize(ide.bar[0], ide.bar[1], ide.bar[2], ide.bar[3], ide.bar[4]);

	storage_try_register_ide_dev();
	if (storage_devices_count > 0) {
		mprint("Storage: ATA PIO enabled (%zu drives)\n", storage_devices_count);
		return;
	}

	mprint("Storage: no devices found\n");
}

size_t storage_device_count(void)
{
	return storage_devices_count;
}

bool storage_get_device(size_t device_index, struct storage_device *out_device)
{
	if (device_index >= storage_devices_count)
		return false;
	if (out_device == nullptr)
		return false;

	size_t current = 0;
	list_for_each(&storage_devices_list) {
		struct storage_device_entry *entry = list_entry(it, struct storage_device_entry, list);
		if (current == device_index) {
			*out_device = entry->device;
			return true;
		}
		current++;
	}

	return false;
}

bool storage_read_device(const struct storage_device *device, uint32_t lba_addr, uint16_t sector_count, void *dest)
{
	if (device == nullptr)
		return false;

	switch (device->backend) {
	case STORAGE_BACKEND_AHCI:
		return ahci_read28_port(device->ahci_port, lba_addr, sector_count, dest);
	case STORAGE_BACKEND_ATA_PIO:
		return ata_pio_28_read(device->channel, device->drive, lba_addr, sector_count, dest);
	default:
		return false;
	}
}

bool storage_write_device(const struct storage_device *device, uint32_t lba_addr, uint16_t sector_count, const void *src)
{
	if (device == nullptr)
		return false;

	switch (device->backend) {
	case STORAGE_BACKEND_AHCI:
		return ahci_write28_port(device->ahci_port, lba_addr, sector_count, src);
	case STORAGE_BACKEND_ATA_PIO:
		return ata_pio_28_write(device->channel, device->drive, lba_addr, sector_count, src);
	default:
		return false;
	}
}
