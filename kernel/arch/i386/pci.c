#include "pci.h"

#include "port.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_CONFIG_ENABLE (1u << 31)

static uint32_t pci_make_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
	return PCI_CONFIG_ENABLE | ((uint32_t)bus << 16) | ((uint32_t)device << 11) | ((uint32_t)function << 8) |
	       (offset & 0xFC);
}

uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
	outd(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, function, offset));
	return ind(PCI_CONFIG_DATA);
}

uint16_t pci_read_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
	uint32_t data = pci_read_config_dword(bus, device, function, offset);
	return (uint16_t)((data >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
	uint32_t data = pci_read_config_dword(bus, device, function, offset);
	return (uint8_t)((data >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
	outd(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, function, offset));
	outd(PCI_CONFIG_DATA, value);
}

bool pci_read_device(uint8_t bus, uint8_t device, uint8_t function, struct pci_device *out)
{
	uint16_t vendor_id = pci_read_config_word(bus, device, function, 0x00);
	if (vendor_id == 0xFFFF)
		return false;

	if (out == nullptr)
		return true;

	uint16_t device_id = pci_read_config_word(bus, device, function, 0x02);
	uint8_t class_id = pci_read_config_byte(bus, device, function, 0x0B);
	uint8_t subclass = pci_read_config_byte(bus, device, function, 0x0A);
	uint8_t prog_if = pci_read_config_byte(bus, device, function, 0x09);
	uint8_t header_type = pci_read_config_byte(bus, device, function, 0x0E);
	uint8_t irq_line = pci_read_config_byte(bus, device, function, 0x3C);

	*out = (struct pci_device){
		.bus = bus,
		.device = device,
		.function = function,
		.vendor_id = vendor_id,
		.device_id = device_id,
		.class_id = class_id,
		.subclass = subclass,
		.prog_if = prog_if,
		.header_type = header_type,
		.irq_line = irq_line,
	};

	for (uint8_t bar = 0; bar < 6; bar++)
		out->bar[bar] = pci_read_config_dword(bus, device, function, (uint8_t)(0x10 + (bar * 4)));

	return true;
}

static bool pci_match_class(const struct pci_device *dev, uint8_t class_id, uint8_t subclass, uint8_t prog_if)
{
	if (dev->class_id != class_id)
		return false;
	if (dev->subclass != subclass)
		return false;
	if (prog_if != 0xFF && dev->prog_if != prog_if)
		return false;

	return true;
}

bool pci_find_class(uint8_t class_id, uint8_t subclass, uint8_t prog_if, struct pci_device *out)
{
	for (uint16_t bus = 0; bus < 256; bus++) {
		for (uint8_t device = 0; device < 32; device++) {
			struct pci_device dev = { 0 };
			if (!pci_read_device((uint8_t)bus, device, 0, &dev))
				continue;

			if (pci_match_class(&dev, class_id, subclass, prog_if)) {
				if (out != nullptr)
					*out = dev;
				return true;
			}

			if ((dev.header_type & 0x80) == 0)
				continue;

			for (uint8_t function = 1; function < 8; function++) {
				if (!pci_read_device((uint8_t)bus, device, function, &dev))
					continue;

				if (pci_match_class(&dev, class_id, subclass, prog_if)) {
					if (out != nullptr)
						*out = dev;
					return true;
				}
			}
		}
	}

	return false;
}

bool pci_find_storage_device(uint8_t subclass, struct pci_device *out)
{
	return pci_find_class(PCI_CLASS_MASS_STORAGE, subclass, 0xFF, out);
}

void pci_enable_bus_mastering(const struct pci_device *device)
{
	if (device == nullptr)
		return;

	uint16_t command = pci_read_config_word(device->bus, device->device, device->function, 0x04);
	command |= (1u << 2) | (1u << 1);
	uint32_t new_value = (uint32_t)command | ((uint32_t)pci_read_config_word(device->bus, device->device, device->function, 0x06) << 16);
	pci_write_config_dword(device->bus, device->device, device->function, 0x04, new_value);
}
