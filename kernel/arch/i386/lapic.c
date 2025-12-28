#include <kernel/display.h>
#include <kernel/smp.h>

#include "./port.h"

#include <stdint.h>
MODULE("LAPIC");

#define IA32_APIC_BASE_MSR 0x1B

static volatile uint32_t *lapic_base = (volatile uint32_t *)0xFEE00000;

static inline uint64_t rdmsr(uint32_t msr)
{
	uint32_t low, high;
	__asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");
	return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value)
{
	uint32_t low = (uint32_t)value;
	uint32_t high = (uint32_t)(value >> 32);
	__asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

static inline uint32_t lapic_read(uint32_t reg)
{
	return lapic_base[reg / 4];
}

static inline void lapic_write(uint32_t reg, uint32_t value)
{
	lapic_base[reg / 4] = value;
	(void)lapic_read(0x20); // enforce ordering
}

void lapic_set_base(uint32_t base)
{
	lapic_base = (volatile uint32_t *)(uintptr_t)base;
}

uint8_t lapic_get_id(void)
{
	return (uint8_t)(lapic_read(0x20) >> 24);
}

void lapic_enable(void)
{
	uint64_t apic_base = rdmsr(IA32_APIC_BASE_MSR);
	apic_base |= (1ULL << 11); // enable xAPIC
	wrmsr(IA32_APIC_BASE_MSR, apic_base);

	lapic_set_base((uint32_t)(apic_base & 0xFFFFF000));

	// spurious interrupt vector register: enable bit + vector 0xFF
	lapic_write(0xF0, lapic_read(0xF0) | 0x100 | 0xFF);
}

static void lapic_wait_for_icr(void)
{
	while (lapic_read(0x300) & (1 << 12))
		;
}

void lapic_send_init(uint8_t apic_id)
{
	lapic_wait_for_icr();
	lapic_write(0x310, ((uint32_t)apic_id) << 24);
	lapic_write(0x300, 0x00004500); // INIT | level | assert
	lapic_wait_for_icr();
}

void lapic_send_sipi(uint8_t apic_id, uint8_t vector)
{
	lapic_wait_for_icr();
	lapic_write(0x310, ((uint32_t)apic_id) << 24);
	lapic_write(0x300, 0x00004600 | vector);
	lapic_wait_for_icr();
}

void lapic_ack(void)
{
	lapic_write(0xB0, 0);
}
