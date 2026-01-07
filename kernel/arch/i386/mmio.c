#include "mmio.h"

#include <kernel/phy_mem.h>

struct mmio_region mmio_map(uintptr_t phys_addr, size_t size)
{
	if (size == 0)
		return (struct mmio_region){ 0 };

	size_t aligned = round_up_to_page(phys_addr + size) - round_down_to_page(phys_addr);
	struct vmm_entry *virt = vmm_alloc(aligned, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT);
	if (virt == nullptr)
		return (struct mmio_region){ 0 };

	fatptr_t phys = {
		.ptr = (void *)round_down_to_page(phys_addr),
		.len = aligned,
	};

	map_pages(&phys, virt);

	return (struct mmio_region){
		.phys = phys,
		.virt_entry = virt,
		.virt = (uint8_t *)virt->ptr + (phys_addr - (uintptr_t)phys.ptr),
		.size = size,
	};
}

void mmio_unmap(struct mmio_region *region)
{
	if (region == nullptr || region->virt_entry == nullptr)
		return;

	unmap_pages(&region->phys, region->virt_entry);
	vmm_free(region->virt_entry->ptr);
	*region = (struct mmio_region){ 0 };
}
