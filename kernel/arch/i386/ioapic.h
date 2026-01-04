#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct ioapic_override {
	uint8_t source;
	uint32_t gsi;
	uint16_t flags;
};

void ioapic_init(uintptr_t phys_addr, uint32_t gsi_base, uint8_t apic_id);
void ioapic_register_overrides(const struct ioapic_override *overrides, size_t count);
void ioapic_configure_legacy_irqs(void);

void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);
