#include "dma.h"

#include <string.h>

struct dma_buffer dma_alloc(size_t size)
{
	if (size == 0)
		return (struct dma_buffer){ 0 };

	size_t aligned = round_up_to_page(size);
	fatptr_t phys = phy_mem_alloc(aligned);
	if (phys.ptr == nullptr)
		return (struct dma_buffer){ 0 };

	struct vmm_entry *virt = vmm_alloc(aligned, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);
	if (virt == nullptr) {
		phy_mem_free(phys);
		return (struct dma_buffer){ 0 };
	}

	map_pages(&phys, virt);
	memset(virt->ptr, 0, aligned);

	return (struct dma_buffer){
		.phys = phys,
		.virt_entry = virt,
		.virt = virt->ptr,
		.size = size,
	};
}

void dma_free(struct dma_buffer *buffer)
{
	if (buffer == nullptr || buffer->virt_entry == nullptr)
		return;

	unmap_pages(&buffer->phys, buffer->virt_entry);
	vmm_free(buffer->virt_entry->ptr);
	*buffer = (struct dma_buffer){ 0 };
}
