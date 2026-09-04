#include <kernel/block_device.h>

#include <kernel/allocator.h>
#include <kernel/display.h>

#include <arch/i386/ahci.h>
#include <arch/i386/ata_pio.h>
#include <arch/i386/pci.h>

#include <list.h>
#include <string.h>

MODULE("storage")

#define BLOCK_DEVICE_MAX_DEVICES (AHCI_MAX_PORTS + 4u)

struct block_device_entry {
	struct block_device device;
	struct list_head list;
};

static slab_cache_t *block_device_cache = nullptr;
static LIST_HEAD(block_devices_list);
static size_t block_devices_count = 0;

static void block_device_set_name(char *name, size_t size, const char *prefix,
	uint8_t first, uint8_t second, bool has_second)
{
	if (size < 7)
		return;
	name[0] = prefix[0];
	name[1] = prefix[1];
	name[2] = prefix[2];
	name[3] = has_second ? (char)('0' + first) : (char)('0' + first);
	if (has_second) {
		name[4] = '.';
		name[5] = (char)('0' + second);
		name[6] = '\0';
	} else {
		name[4] = '\0';
	}
}

static void block_device_clear(void)
{
	if (block_device_cache == nullptr)
		return;

	struct block_device_entry *entry = nullptr;
	while ((entry = list_pop_entry(&block_devices_list, struct block_device_entry, list)) != nullptr) {
		slab_free_obj(block_device_cache, (fatptr_t){ .ptr = entry, .len = sizeof(*entry) });
	}

	block_devices_count = 0;
}

static bool block_device_register(struct block_device device)
{
	if (block_devices_count >= BLOCK_DEVICE_MAX_DEVICES)
		return false;

	if (block_device_cache == nullptr)
		return false;

	fatptr_t entry_ptr = slab_alloc_obj(block_device_cache);
	if (entry_ptr.ptr == nullptr)
		return false;

	struct block_device_entry *entry = entry_ptr.ptr;
	entry->device = device;
	RESET_LIST_ITEM(&entry->list);
	list_add(&entry->list, &block_devices_list);
	block_devices_count++;
	return true;
}

static void block_device_init_cache()
{
	if (block_device_cache == nullptr) {
		block_device_cache = slab_create("block_device", sizeof(struct block_device_entry),
						   alignof(struct block_device_entry), nullptr, nullptr);
		if (block_device_cache == nullptr) {
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

		struct block_device device = {
			.backend = BLOCK_DEVICE_BACKEND_AHCI,
			.ahci_port = port,
		};
		device.sector_size = 512;
		device.sector_count = UINT64_MAX;
		device.name[0] = 's';
		device.name[1] = 'a';
		device.name[2] = 't';
		device.name[3] = 'a';
		if (port < 10) {
			device.name[4] = (char)('0' + port);
			device.name[5] = '\0';
		} else {
			device.name[4] = (char)('0' + port / 10);
			device.name[5] = (char)('0' + port % 10);
			device.name[6] = '\0';
		}
		block_device_register(device);
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

		struct block_device device = {
			.backend = BLOCK_DEVICE_BACKEND_ATA_PIO,
			.sector_size = 512,
			.sector_count = UINT64_MAX,
			.channel = channels[i],
			.drive = drives[i],
		};
		block_device_set_name(device.name, sizeof(device.name), "ata", channels[i], drives[i], true);
		block_device_register(device);
	}
}

void block_device_init(void)
{
	block_device_init_cache();

	block_device_clear();

	storage_try_ahci_init();
	if (block_devices_count > 0) {
		mprint("Storage: AHCI enabled (%u drives)\n", block_devices_count);
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
	if (block_devices_count > 0) {
		mprint("Storage: ATA PIO enabled (%u drives)\n", (unsigned)block_devices_count);
		return;
	}

	mprint("Storage: no devices found\n");
}

size_t block_device_count(void)
{
	return block_devices_count;
}

bool block_device_get(size_t device_index, struct block_device *out_device)
{
	if (device_index >= block_devices_count)
		return false;
	if (out_device == nullptr)
		return false;

	size_t current = 0;
	list_for_each(&block_devices_list) {
		struct block_device_entry *entry = list_entry(it, struct block_device_entry, list);
		if (current == device_index) {
			*out_device = entry->device;
			return true;
		}
		current++;
	}

	return false;
}

bool block_device_find(const char *name, struct block_device *out_device)
{
	if (name == nullptr || out_device == nullptr)
		return false;
	list_for_each(&block_devices_list) {
		struct block_device_entry *entry = list_entry(it, struct block_device_entry, list);
		if (strlen(name) < sizeof(entry->device.name) &&
			strlen(entry->device.name) == strlen(name) &&
			memcmp(entry->device.name, name, strlen(name)) == 0) {
			*out_device = entry->device;
			return true;
		}
	}
	return false;
}

bool block_device_read(const struct block_device *device, uint64_t lba, uint32_t sector_count, void *dest)
{
	if (device == nullptr || dest == nullptr || sector_count == 0 || lba > UINT32_MAX ||
		sector_count > UINT16_MAX || device->sector_size != 512)
		return false;
	if (device->read_fn != nullptr)
		return device->read_fn(device->context, lba, sector_count, dest);

	switch (device->backend) {
	case BLOCK_DEVICE_BACKEND_AHCI:
		return ahci_read28_port(device->ahci_port, (uint32_t)lba, (uint16_t)sector_count, dest);
	case BLOCK_DEVICE_BACKEND_ATA_PIO:
		return ata_pio_28_read(device->channel, device->drive, (uint32_t)lba, (uint16_t)sector_count, dest);
	default:
		return false;
	}
}

bool block_device_write(const struct block_device *device, uint64_t lba, uint32_t sector_count, const void *src)
{
	if (device == nullptr || src == nullptr || sector_count == 0 || lba > UINT32_MAX ||
		sector_count > UINT16_MAX || device->sector_size != 512)
		return false;
	if (device->write_fn != nullptr)
		return device->write_fn(device->context, lba, sector_count, src);

	switch (device->backend) {
	case BLOCK_DEVICE_BACKEND_AHCI:
		return ahci_write28_port(device->ahci_port, (uint32_t)lba, (uint16_t)sector_count, src);
	case BLOCK_DEVICE_BACKEND_ATA_PIO:
		return ata_pio_28_write(device->channel, device->drive, (uint32_t)lba, (uint16_t)sector_count, src);
	default:
		return false;
	}
}
