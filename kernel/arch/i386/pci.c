#include "pci.h"
#include "port.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_HEADER_TYPE 0x0E
#define PCI_BAR0 0x10
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_COMMAND 0x04

uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
	uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
	outd(PCI_CONFIG_ADDRESS, address);
	return ind(PCI_CONFIG_DATA);
}

uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
	uint32_t value = pci_config_read_dword(bus, slot, func, offset & 0xFC);
	uint8_t shift = (offset & 2) * 8;
	return (uint16_t)((value >> shift) & 0xFFFF);
}

uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
	uint32_t value = pci_config_read_dword(bus, slot, func, offset & 0xFC);
	uint8_t shift = (offset & 3) * 8;
	return (uint8_t)((value >> shift) & 0xFF);
}

void pci_config_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value)
{
	uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
	outd(PCI_CONFIG_ADDRESS, address);
	uint32_t original = ind(PCI_CONFIG_DATA);
	uint8_t shift = (offset & 2) * 8;
	uint32_t mask = 0xFFFFu << shift;
	uint32_t new_value = (original & ~mask) | ((uint32_t)value << shift);
	outd(PCI_CONFIG_DATA, new_value);
}

void pci_enable_busmaster(uint8_t bus, uint8_t slot, uint8_t func)
{
	uint16_t command = pci_config_read_word(bus, slot, func, PCI_COMMAND);
	uint16_t new_command = command | (1u << 2) | (1u << 1);
	if (new_command != command)
		pci_config_write_word(bus, slot, func, PCI_COMMAND, new_command);
}

static bool is_multifunction(uint8_t bus, uint8_t slot)
{
	uint32_t header = pci_config_read_dword(bus, slot, 0, PCI_HEADER_TYPE);
	return (header >> 16) & 0x80;
}

static bool match_class(uint8_t bus, uint8_t slot, uint8_t func, uint8_t class_code, uint8_t subclass)
{
	uint32_t class_reg = pci_config_read_dword(bus, slot, func, 0x08);
	uint8_t cls = (uint8_t)(class_reg >> 24);
	uint8_t sub = (uint8_t)(class_reg >> 16);
	return cls == class_code && sub == subclass;
}

bool pci_find_storage(uint8_t class_code, uint8_t subclass, struct pci_device_info *out)
{
	if (!out)
		return false;

	for (uint16_t bus = 0; bus < 256; bus++) {
		uint8_t bus_id = (uint8_t)bus;
		for (uint8_t slot = 0; slot < 32; slot++) {
			uint32_t vendor_device = pci_config_read_dword(bus_id, slot, 0, 0x00);
			if (vendor_device == 0xFFFFFFFF)
				continue;

			uint8_t max_func = is_multifunction(bus_id, slot) ? 8 : 1;
			for (uint8_t func = 0; func < max_func; func++) {
				vendor_device = pci_config_read_dword(bus_id, slot, func, 0x00);
				if (vendor_device == 0xFFFFFFFF)
					continue;

				if (!match_class(bus_id, slot, func, class_code, subclass))
					continue;

				out->bus = bus_id;
				out->slot = slot;
				out->func = func;
				out->vendor_id = (uint16_t)(vendor_device & 0xFFFF);
				out->device_id = (uint16_t)(vendor_device >> 16);

				uint32_t class_reg = pci_config_read_dword(bus_id, slot, func, 0x08);
				out->class_code = (uint8_t)(class_reg >> 24);
				out->subclass = (uint8_t)(class_reg >> 16);
				out->prog_if = (uint8_t)(class_reg >> 8);

				for (int bar = 0; bar < 6; bar++)
					out->bar[bar] = pci_config_read_dword(bus_id, slot, func, PCI_BAR0 + bar * 4);

				out->irq_line = pci_config_read_byte(bus_id, slot, func, PCI_INTERRUPT_LINE);
				return true;
			}
		}
	}

	return false;
}
