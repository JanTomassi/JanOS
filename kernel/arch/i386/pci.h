#pragma once

#include <stdbool.h>
#include <stdint.h>

struct pci_device_info {
	uint8_t bus;
	uint8_t slot;
	uint8_t func;
	uint16_t vendor_id;
	uint16_t device_id;
	uint8_t class_code;
	uint8_t subclass;
	uint8_t prog_if;
	uint32_t bar[6];
	uint8_t irq_line;
};

uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void pci_enable_busmaster(uint8_t bus, uint8_t slot, uint8_t func);

bool pci_find_storage(uint8_t class_code, uint8_t subclass, struct pci_device_info *out);
