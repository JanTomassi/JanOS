#include <kernel/vir_mem.h>
#include <kernel/display.h>
#include <string.h>
#include <stdlib.h>

#include "mmio.h"

static size_t page_align_up(size_t addr)
{
	return round_up_to_page(addr);
}

static size_t page_align_down(size_t addr)
{
	return round_down_to_page(addr + PAGE_SIZE) - PAGE_SIZE;
}

fatptr_t mmio_map_region(fatptr_t phys_region, uint16_t flags)
{
	if (!phys_region.ptr || !phys_region.len)
		return (fatptr_t){ .ptr = NULL, .len = 0 };

	uintptr_t phys_addr = (uintptr_t)phys_region.ptr;
	uintptr_t phys_start = page_align_down((size_t)phys_addr);
	uintptr_t phys_end = page_align_up((size_t)phys_addr + phys_region.len);
	size_t map_len = phys_end - phys_start;

	struct vmm_entry *virt = vir_mem_alloc(map_len, flags);
	if (!virt || !virt->ptr)
		return (fatptr_t){ .ptr = NULL, .len = 0 };

	uint8_t *virt_page = (uint8_t *)virt->ptr;
	uintptr_t phys_page = phys_start;

	while (phys_page < phys_end) {
		if (!map_page((void *)phys_page, virt_page, flags)) {
			vir_mem_free(virt->ptr);
			return (fatptr_t){ .ptr = NULL, .len = 0 };
		}

		phys_page += PAGE_SIZE;
		virt_page += PAGE_SIZE;
	}

	size_t offset = (size_t)(phys_addr - phys_start);
	return (fatptr_t){
		.ptr = (uint8_t *)virt->ptr + offset,
		.len = phys_region.len,
	};
}

void mmio_unmap_region(fatptr_t virt_region)
{
	if (!virt_region.ptr || !virt_region.len)
		return;

	uintptr_t virt_start = page_align_down((size_t)virt_region.ptr);
	uintptr_t virt_end = page_align_up((size_t)virt_region.ptr + virt_region.len);
	size_t map_len = virt_end - virt_start;

	/* vir_mem_free will unmap the range that begins at virt_start because
	 * mmio_map_region allocated a dedicated contiguous area. */
	vir_mem_free((void *)virt_start);
	(void)map_len; /* kept for clarity; currently unused. */
}
