#include "lapic.h"
#include <kernel/display.h>
#include <kernel/vir_mem.h>
#include <kernel/interrupt.h>
#include <stddef.h>
#include <stdbool.h>

extern void PIC_remap(int offset1, int offset2);
extern void pic_disable(void);

#define LAPIC_MMIO_BASE 0xFEE00000
#define LAPIC_REG_ID 0x20
#define LAPIC_REG_EOI 0xB0
#define LAPIC_REG_SVR 0xF0
#define LAPIC_REG_ERR_STAT 0x280
#define LAPIC_REG_ICR_LOW 0x300
#define LAPIC_REG_ICR_HIGH 0x310

#define LAPIC_SVR_ENABLE (1 << 8)

MODULE("LAPIC");

static volatile uint32_t *lapic_base = nullptr;

static inline uint32_t lapic_read(uint32_t reg)
{
	return lapic_base[reg / 4];
}

static inline void lapic_write(uint32_t reg, uint32_t val)
{
	lapic_base[reg / 4] = val;
	(void)lapic_read(LAPIC_REG_ID);
}

static void lapic_map_base(void)
{
	if (lapic_base != nullptr)
		return;

	fatptr_t phys = {
		.ptr = (void *)LAPIC_MMIO_BASE,
		.len = PAGE_SIZE,
	};
	struct vmm_entry virt = {
		.ptr = (void *)LAPIC_MMIO_BASE,
		.size = PAGE_SIZE,
		.flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT,
	};

	map_pages(&phys, &virt);
	lapic_base = (uint32_t *)virt.ptr;
}

void lapic_enable(void)
{
	lapic_map_base();

	PIC_remap(0x20, 0x28);
	pic_disable();

	uint32_t svr = 0xFF | LAPIC_SVR_ENABLE;
	lapic_write(LAPIC_REG_SVR, svr);
	mprint("LAPIC enabled with SVR=0x%x\n", svr);
}

void lapic_eoi(void)
{
	if (lapic_base == nullptr)
		return;

	lapic_write(LAPIC_REG_EOI, 0);
}

void lapic_send_ipi(uint8_t apic_id, uint8_t vector, uint8_t delivery_mode)
{
	lapic_map_base();

	const uint32_t dest = ((uint32_t)apic_id) << 24;
	lapic_write(LAPIC_REG_ICR_HIGH, dest);

	uint32_t icr_low = (delivery_mode << 8) | vector;
	lapic_write(LAPIC_REG_ICR_LOW, icr_low);

	while (lapic_read(LAPIC_REG_ICR_LOW) & (1 << 12))
		;
}

uint8_t lapic_get_id(void)
{
	lapic_map_base();
	return (uint8_t)(lapic_read(LAPIC_REG_ID) >> 24);
}
