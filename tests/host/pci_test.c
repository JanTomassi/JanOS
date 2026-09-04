#include "test.h"

#include <arch/i386/pci.h>

#include <stdint.h>
#include <string.h>

/*
 * A PCI function exposes 256 bytes of configuration space.  The first dword
 * contains the vendor and device IDs, class information lives at offsets
 * 0x09-0x0b, BARs start at 0x10, and the command/status dword is at 0x04.
 * This fake stores those bytes exactly as a real little-endian PCI function
 * would, so the production PCI driver can be tested unchanged.
 */
#define CONFIG_SPACE_SIZE 256u
#define MAX_FAKE_FUNCTIONS 16u

struct fake_function {
	bool present;
	uint8_t bus;
	uint8_t device;
	uint8_t function;
	uint8_t config[CONFIG_SPACE_SIZE];
};

struct pci_fake {
	struct fake_function functions[MAX_FAKE_FUNCTIONS];
	size_t function_count;
	size_t read_count;
	size_t write_count;
	uint8_t last_bus;
	uint8_t last_device;
	uint8_t last_function;
	uint8_t last_offset;
	uint32_t last_write;
};

static void put16(uint8_t *config, uint8_t offset, uint16_t value)
{
	config[offset] = (uint8_t)value;
	config[offset + 1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *config, uint8_t offset, uint32_t value)
{
	config[offset] = (uint8_t)value;
	config[offset + 1] = (uint8_t)(value >> 8);
	config[offset + 2] = (uint8_t)(value >> 16);
	config[offset + 3] = (uint8_t)(value >> 24);
}

static uint16_t get16(const uint8_t *config, uint8_t offset)
{
	return (uint16_t)config[offset] | ((uint16_t)config[offset + 1] << 8);
}

static uint32_t get32(const uint8_t *config, uint8_t offset)
{
	return (uint32_t)config[offset] |
		((uint32_t)config[offset + 1] << 8) |
		((uint32_t)config[offset + 2] << 16) |
		((uint32_t)config[offset + 3] << 24);
}

static struct fake_function *fake_find(struct pci_fake *fake, uint8_t bus,
	uint8_t device, uint8_t function)
{
	for (size_t i = 0; i < fake->function_count; ++i) {
		struct fake_function *candidate = &fake->functions[i];
		if (candidate->present && candidate->bus == bus &&
			candidate->device == device && candidate->function == function)
			return candidate;
	}
	return NULL;
}

static struct fake_function *fake_add(struct pci_fake *fake, uint8_t bus,
	uint8_t device, uint8_t function, uint16_t vendor, uint16_t device_id,
	uint8_t class_id, uint8_t subclass, uint8_t prog_if, uint8_t header_type,
	uint8_t irq_line)
{
	TEST_ASSERT(fake->function_count < MAX_FAKE_FUNCTIONS);
	struct fake_function *result = &fake->functions[fake->function_count++];
	*result = (struct fake_function){
		.present = true,
		.bus = bus,
		.device = device,
		.function = function,
	};
	memset(result->config, 0, sizeof(result->config));
	put16(result->config, 0x00, vendor);
	put16(result->config, 0x02, device_id);
	result->config[0x09] = prog_if;
	result->config[0x0A] = subclass;
	result->config[0x0B] = class_id;
	result->config[0x0E] = header_type;
	result->config[0x3C] = irq_line;
	return result;
}

static uint32_t fake_read_dword(uint8_t bus, uint8_t device, uint8_t function,
	uint8_t offset, void *context)
{
	struct pci_fake *fake = context;
	TEST_ASSERT(fake != NULL);
	TEST_ASSERT((offset & 3u) == 0);
	TEST_ASSERT(offset < CONFIG_SPACE_SIZE - sizeof(uint32_t) + 1);
	fake->read_count++;
	fake->last_bus = bus;
	fake->last_device = device;
	fake->last_function = function;
	fake->last_offset = offset;
	struct fake_function *entry = fake_find(fake, bus, device, function);
	return entry == NULL ? UINT32_MAX : get32(entry->config, offset);
}

static void fake_write_dword(uint8_t bus, uint8_t device, uint8_t function,
	uint8_t offset, uint32_t value, void *context)
{
	struct pci_fake *fake = context;
	TEST_ASSERT(fake != NULL);
	TEST_ASSERT((offset & 3u) == 0);
	TEST_ASSERT(offset < CONFIG_SPACE_SIZE - sizeof(uint32_t) + 1);
	fake->write_count++;
	fake->last_bus = bus;
	fake->last_device = device;
	fake->last_function = function;
	fake->last_offset = offset;
	fake->last_write = value;
	struct fake_function *entry = fake_find(fake, bus, device, function);
	if (entry != NULL)
		put32(entry->config, offset, value);
}

static void install_fake(struct pci_fake *fake)
{
	pci_set_config_ops(&(struct pci_config_ops){
		.read_dword = fake_read_dword,
		.write_dword = fake_write_dword,
		.context = fake,
	});
}

static void test_config_access_widths(void)
{
	struct pci_fake fake = { 0 };
	struct fake_function *function = fake_add(&fake, 2, 7, 3, 0x1234, 0x5678,
		0, 0, 0, 0, 0);
	put32(function->config, 0x08, 0xAABBCCDD);
	install_fake(&fake);

	/* Word and byte reads select a lane from the dword returned by PCI CFC. */
	TEST_ASSERT(pci_read_config_dword(2, 7, 3, 0x02) == 0x56781234u);
	TEST_ASSERT(fake.last_offset == 0x00);
	TEST_ASSERT(pci_read_config_word(2, 7, 3, 0x02) == 0x5678u);
	TEST_ASSERT(pci_read_config_byte(2, 7, 3, 0x09) == 0xCCu);
	TEST_ASSERT(fake.last_offset == 0x08);

	/* Writes also align the register offset before reaching the transport. */
	pci_write_config_dword(2, 7, 3, 0x13, 0x11223344u);
	TEST_ASSERT(fake.last_offset == 0x10 && fake.last_write == 0x11223344u);
	TEST_ASSERT(get32(function->config, 0x10) == 0x11223344u);
}

static void test_read_device(void)
{
	struct pci_fake fake = { 0 };
	struct fake_function *function = fake_add(&fake, 4, 5, 2, 0x8086, 0x2922,
		PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA, PCI_PROG_IF_AHCI,
		0x00, 11);
	for (uint8_t bar = 0; bar < 6; ++bar)
		put32(function->config, (uint8_t)(0x10 + bar * 4),
			0x10000000u + (uint32_t)bar * 0x1000u);
	install_fake(&fake);

	struct pci_device device;
	TEST_ASSERT(pci_read_device(4, 5, 2, &device));
	TEST_ASSERT(device.bus == 4 && device.device == 5 && device.function == 2);
	TEST_ASSERT(device.vendor_id == 0x8086 && device.device_id == 0x2922);
	TEST_ASSERT(device.class_id == PCI_CLASS_MASS_STORAGE &&
		device.subclass == PCI_SUBCLASS_SATA &&
		device.prog_if == PCI_PROG_IF_AHCI);
	TEST_ASSERT(device.header_type == 0 && device.irq_line == 11);
	for (uint8_t bar = 0; bar < 6; ++bar)
		TEST_ASSERT(device.bar[bar] == 0x10000000u + (uint32_t)bar * 0x1000u);

	/* A vendor value of 0xffff means that no function exists at this address. */
	struct pci_device unchanged = { .vendor_id = 0xBEEF };
	TEST_ASSERT(!pci_read_device(4, 5, 1, &unchanged));
	TEST_ASSERT(unchanged.vendor_id == 0xBEEF);
	TEST_ASSERT(pci_read_device(4, 5, 2, NULL));
}

static void test_class_matching_and_wildcards(void)
{
	struct pci_fake fake = { 0 };
	fake_add(&fake, 0, 1, 0, 0x1111, 0x0001, 0x02, 0x00, 0x00, 0, 5);
	fake_add(&fake, 0, 2, 0, 0x2222, 0x0002, PCI_CLASS_MASS_STORAGE,
		PCI_SUBCLASS_SATA, 0x02, 0, 9);
	install_fake(&fake);

	struct pci_device found = { 0 };
	TEST_ASSERT(!pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA,
		PCI_PROG_IF_AHCI, &found));
	TEST_ASSERT(pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA,
		0xFF, &found));
	TEST_ASSERT(found.device == 2 && found.prog_if == 0x02);
	TEST_ASSERT(pci_find_storage_device(PCI_SUBCLASS_SATA, NULL));

	/* The output object is only modified after a match is found. */
	struct pci_device sentinel = { .vendor_id = 0xCAFE };
	TEST_ASSERT(!pci_find_class(0xFF, 0xFF, 0xFF, &sentinel));
	TEST_ASSERT(sentinel.vendor_id == 0xCAFE);
}

static void test_multifunction_enumeration(void)
{
	struct pci_fake fake = { 0 };
	/* Function zero advertises bit 7 in header type, so functions 1-7 are read. */
	fake_add(&fake, 0, 3, 0, 0x3333, 0x0003, 0x02, 0x00, 0x00, 0x80, 3);
	struct fake_function *target = fake_add(&fake, 0, 3, 2, 0x3333, 0x0004,
		PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_IDE, 0x80, 0, 4);
	install_fake(&fake);

	struct pci_device found;
	TEST_ASSERT(pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_IDE,
		0xFF, &found));
	TEST_ASSERT(found.function == 2 && found.device_id == 0x0004);
	TEST_ASSERT(fake_find(&fake, 0, 3, 2) == target);

	/* Without the multifunction bit, a matching function one is ignored. */
	fake = (struct pci_fake){ 0 };
	fake_add(&fake, 0, 4, 0, 0x4444, 0x0001, 0x02, 0x00, 0x00, 0, 5);
	fake_add(&fake, 0, 4, 1, 0x4444, 0x0002, PCI_CLASS_MASS_STORAGE,
		PCI_SUBCLASS_IDE, 0x00, 0, 6);
	install_fake(&fake);
	TEST_ASSERT(!pci_find_storage_device(PCI_SUBCLASS_IDE, NULL));
}

static void test_full_bus_range(void)
{
	struct pci_fake fake = { 0 };
	fake_add(&fake, 255, 31, 0, 0xFFFF - 1, 0x9999, PCI_CLASS_MASS_STORAGE,
		PCI_SUBCLASS_IDE, 0, 0, 7);
	install_fake(&fake);

	/* bus is intentionally wider than uint8_t in the loop so bus 255 is visited. */
	struct pci_device found;
	TEST_ASSERT(pci_find_storage_device(PCI_SUBCLASS_IDE, &found));
	TEST_ASSERT(found.bus == 255 && found.device == 31);
}

static void test_enable_bus_mastering(void)
{
	struct pci_fake fake = { 0 };
	struct fake_function *function = fake_add(&fake, 1, 6, 0, 0x8086, 0x7010,
		PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_IDE, 0, 0, 14);
	put32(function->config, 0x04, 0xABCD0001u);
	install_fake(&fake);

	struct pci_device device = { .bus = 1, .device = 6, .function = 0 };
	pci_enable_bus_mastering(&device);
	/* Command bit 1 enables memory space; bit 2 enables bus mastering. */
	TEST_ASSERT(fake.write_count == 1);
	TEST_ASSERT(fake.last_offset == 0x04);
	TEST_ASSERT(fake.last_write == 0xABCD0007u);
	TEST_ASSERT(get16(function->config, 0x04) == 0x0007u);

	size_t writes = fake.write_count;
	pci_enable_bus_mastering(NULL);
	TEST_ASSERT(fake.write_count == writes);
}

int main(void)
{
	test_config_access_widths();
	test_read_device();
	test_class_matching_and_wildcards();
	test_multifunction_enumeration();
	test_full_bus_range();
	test_enable_bus_mastering();
	pci_set_config_ops(NULL);
	return 0;
}
