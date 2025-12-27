#include <kernel/display.h>
#include <kernel/vir_mem.h>
#include <stdbool.h>

#include "./mmio.h"
#include "./lapic.h"

static volatile uint32_t *lapic_base = (volatile uint32_t *)LAPIC_DEFAULT_BASE;

static inline uint32_t lapic_read(enum lapic_reg reg)
{
	return lapic_base[reg / 4];
}

static inline void lapic_write(enum lapic_reg reg, uint32_t value)
{
	lapic_base[reg / 4] = value;
	(void)lapic_base[LAPIC_REG_ID / 4];
}

bool lapic_init(void)
{
	/* The LAPIC is memory mapped; if paging has already been set up to map
	 * the default base we do not need to remap it here. If it is not mapped,
	 * mmio_map_region will create a mapping. */
	fatptr_t phys = {
		.ptr = (void *)LAPIC_DEFAULT_BASE,
		.len = 0x1000,
	};
	fatptr_t virt = mmio_map_region(
		phys,
		VMM_PAGE_FLAG_CACHE_DISABLE_BIT | VMM_PAGE_FLAG_READ_WRITE_BIT |
			VMM_PAGE_FLAG_PRESENT_BIT);
	if (!virt.ptr) {
		kprintf("LAPIC mapping failed\n");
		return false;
	}

	lapic_base = (volatile uint32_t *)virt.ptr;

	/* Enable the local APIC in software by setting the spurious interrupt
	 * vector register bit 8. Vector is left at 0xFF (masked) for now. */
	uint32_t svr = lapic_read(LAPIC_REG_SVR);
	lapic_write(LAPIC_REG_SVR, svr | (1 << 8) | 0xFF);

	return true;
}

uint32_t lapic_get_id(void)
{
	return lapic_read(LAPIC_REG_ID) >> 24;
}

void lapic_send_ipi(uint32_t apic_id, enum lapic_delivery_mode mode,
		    uint8_t vector)
{
	/* Bits 56-63 hold destination in ICR high when physical mode. */
	lapic_write(LAPIC_REG_ICR_HIGH, apic_id << 24);
	lapic_write(LAPIC_REG_ICR_LOW,
		    (uint32_t)mode << 8 | (uint32_t)vector | (1 << 14));

	while (lapic_read(LAPIC_REG_ICR_LOW) & (1 << 12))
		;
}

void lapic_eoi(void)
{
	lapic_write(LAPIC_REG_EOI, 0);
}
