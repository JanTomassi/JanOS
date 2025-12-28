#pragma once

#include <stdint.h>

void lapic_set_base(uint32_t base);
uint8_t lapic_get_id(void);
void lapic_enable(void);
void lapic_send_init(uint8_t apic_id);
void lapic_send_sipi(uint8_t apic_id, uint8_t vector);
void lapic_ack(void);

