#include <kernel/mmio.h>
#include <kernel/vir_mem.h>
#include <kernel/display.h>

#include <string.h>

MODULE("MMIO");

static bool mmio_range_mapped(uintptr_t phys_base, size_t size)
{
	for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
		if (!get_vir_addr((void *)(phys_base + offset)))
			return false;
	}
	return true;
}

void *mmio_map_range(uintptr_t phys_addr, size_t size, uint16_t flags)
{
	if (size == 0)
		return nullptr;

	uintptr_t phys_base = round_down_to_page(phys_addr);
	size_t offset = phys_addr - phys_base;
	size_t map_size = round_up_to_page(offset + size);

	void *virt_base = get_vir_addr((void *)phys_base);

	if (!virt_base || !mmio_range_mapped(phys_base, map_size)) {
		struct vmm_entry *virt = vir_mem_alloc(map_size, flags | VMM_ENTRY_PRESENT_BIT);

		fatptr_t phys = {
			.ptr = (void *)phys_base,
			.len = map_size,
		};

		map_pages(&phys, virt);
		virt_base = virt->ptr;
	}

	return (void *)((uintptr_t)virt_base + offset);
}
