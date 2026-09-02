#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_IDE 0x01
#define PCI_SUBCLASS_SATA 0x06

#define PCI_PROG_IF_AHCI 0x01

struct pci_device {
	uint8_t bus;
	uint8_t device;
	uint8_t function;

	uint16_t vendor_id;
	uint16_t device_id;
	uint8_t class_id;
	uint8_t subclass;
	uint8_t prog_if;
	uint8_t header_type;
	uint8_t irq_line;

	uint32_t bar[6];
};

uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_read_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t pci_read_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

bool pci_read_device(uint8_t bus, uint8_t device, uint8_t function, struct pci_device *out);
bool pci_find_class(uint8_t class_id, uint8_t subclass, uint8_t prog_if, struct pci_device *out);
bool pci_find_storage_device(uint8_t subclass, struct pci_device *out);

void pci_enable_bus_mastering(const struct pci_device *device);
