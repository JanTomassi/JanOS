#pragma once

#include <stdint.h>
#include <stdbool.h>

#define LAPIC_DEFAULT_BASE 0xFEE00000

enum lapic_reg {
	LAPIC_REG_ID = 0x20,
	LAPIC_REG_VERSION = 0x30,
	LAPIC_REG_TPR = 0x80,
	LAPIC_REG_EOI = 0xB0,
	LAPIC_REG_SVR = 0xF0,
	LAPIC_REG_ICR_LOW = 0x300,
	LAPIC_REG_ICR_HIGH = 0x310,
};

enum lapic_delivery_mode {
	LAPIC_DM_INIT = 0x5,
	LAPIC_DM_STARTUP = 0x6,
};

bool lapic_init(void);
uint32_t lapic_get_id(void);
void lapic_send_ipi(uint32_t apic_id, enum lapic_delivery_mode mode, uint8_t vector);
void lapic_eoi(void);
