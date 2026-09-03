#pragma once

#include <stdint.h>
#include <stdbool.h>

void lapic_enable(void);
void lapic_eoi(void);
void lapic_send_ipi(uint8_t apic_id, uint8_t vector, uint8_t delivery_mode);
uint8_t lapic_get_id(void);
void lapic_set_base(uintptr_t phys);
void lapic_timer_init(void);

void lapic_start_ap(uint8_t apic_id, uint8_t startup_vector);

void lapic_clear_error(void);

void lapic_set_icr_cpu(uint8_t apic_id);

void lapic_trigger_init();

void lapic_trigger_deassert();

void lapic_trigger_startup(uint8_t vector);

void lapic_wait_delivery();
