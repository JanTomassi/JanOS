#pragma once

#include <stdint.h>
#include <stdbool.h>

void lapic_enable(void);
void lapic_eoi(void);
void lapic_send_ipi(uint8_t apic_id, uint8_t vector, uint8_t delivery_mode);
uint8_t lapic_get_id(void);
