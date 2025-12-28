#pragma once

#include <kernel/vir_mem.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MMIO_RW_FLAGS (VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT)

/* Map a physical range into virtual memory, reusing an existing mapping if it
 * already covers the requested region. Returns a virtual pointer adjusted by
 * the original offset from the aligned physical base. */
void *mmio_map_range(uintptr_t phys_addr, size_t size, uint16_t flags);

static inline void *mmio_map(uintptr_t phys_addr, size_t size)
{
	return mmio_map_range(phys_addr, size, MMIO_RW_FLAGS);
}
