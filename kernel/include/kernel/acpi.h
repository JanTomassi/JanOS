#pragma once

#include <kernel/multiboot.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct acpi_rsdp_descriptor {
	char signature[8];
	uint8_t checksum;
	char oem_id[6];
	uint8_t revision;
	uint32_t rsdt_address;
	// ACPI 2.0+ fields
	uint32_t length;
	uint64_t xsdt_address;
	uint8_t extended_checksum;
	uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char oem_id[6];
	char oem_table_id[8];
	uint32_t oem_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
	struct acpi_sdt_header header;
	uint32_t lapic_address;
	uint32_t flags;
	uint8_t entries[];
} __attribute__((packed));

enum acpi_madt_entry_type {
	ACPI_MADT_LAPIC = 0,
	ACPI_MADT_IOAPIC = 1,
	ACPI_MADT_INT_OVERRIDE = 2,
	ACPI_MADT_NMI_SOURCE = 3,
	ACPI_MADT_LAPIC_NMI = 4,
	ACPI_MADT_LAPIC_OVERRIDE = 5,
	ACPI_MADT_X2APIC = 9,
};

struct acpi_madt_lapic_entry {
	uint8_t entry_type;
	uint8_t length;
	uint8_t acpi_processor_id;
	uint8_t apic_id;
	uint32_t flags;
} __attribute__((packed));

struct acpi_madt_ioapic_entry {
	uint8_t entry_type;
	uint8_t length;
	uint8_t ioapic_id;
	uint8_t reserved;
	uint32_t address;
	uint32_t global_interrupt_base;
} __attribute__((packed));

struct acpi_madt_int_override_entry {
	uint8_t entry_type;
	uint8_t length;
	uint8_t bus_source;
	uint8_t irq_source;
	uint32_t global_interrupt;
	uint16_t flags;
} __attribute__((packed));

struct acpi_madt_lapic_override_entry {
	uint8_t entry_type;
	uint8_t length;
	uint16_t reserved;
	uint64_t lapic_address;
} __attribute__((packed));

struct acpi_madt_lapic_nmi_entry {
	uint8_t entry_type;
	uint8_t length;
	uint8_t acpi_processor_id;
	uint16_t flags;
	uint8_t lint;
} __attribute__((packed));

struct acpi_madt_x2apic_entry {
	uint8_t entry_type;
	uint8_t length;
	uint16_t reserved;
	uint32_t x2apic_id;
	uint32_t flags;
	uint32_t acpi_processor_uid;
} __attribute__((packed));

const struct acpi_rsdp_descriptor *acpi_find_rsdp(const struct multiboot_tag_old_acpi *old_rsdp,
						  const struct multiboot_tag_new_acpi *new_rsdp);
const struct acpi_madt *acpi_find_madt(const struct acpi_rsdp_descriptor *rsdp);
bool acpi_checksum_valid(const void *table, size_t length);

