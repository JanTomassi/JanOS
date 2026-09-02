#pragma once

#include <stdint.h>
#include <kernel/vir_mem.h>

struct mmio_region {
	fatptr_t phys;
	struct vmm_entry *virt_entry;
	void *virt;
	size_t size;
};

struct mmio_region mmio_map(uintptr_t phys_addr, size_t size);
void mmio_unmap(struct mmio_region *region);

static inline uint32_t mmio_read32(const volatile void *addr)
{
	return *(const volatile uint32_t *)addr;
}

static inline void mmio_write32(volatile void *addr, uint32_t value)
{
	*(volatile uint32_t *)addr = value;
}
