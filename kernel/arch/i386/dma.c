#include <kernel/phy_mem.h>
#include <kernel/vir_mem.h>
#include <string.h>

#include "dma.h"

static size_t page_align(size_t len)
{
	return round_up_to_page(len);
}

bool dma_alloc(size_t len, struct dma_buffer *out)
{
	if (!out)
		return false;

	size_t alloc_len = page_align(len);
	fatptr_t phys = phy_mem_alloc(alloc_len);
	if (!phys.ptr)
		return false;

	struct vmm_entry *virt = vir_mem_alloc(alloc_len, VMM_PAGE_FLAG_PRESENT_BIT | VMM_PAGE_FLAG_READ_WRITE_BIT);
	if (!virt || !virt->ptr) {
		phy_mem_free(phys);
		return false;
	}

	if (!map_pages(&phys, virt)) {
		phy_mem_free(phys);
		vir_mem_free(virt->ptr);
		return false;
	}

	out->virt = virt->ptr;
	out->phys = (uint32_t)(uintptr_t)phys.ptr;
	out->len = alloc_len;
	memset(out->virt, 0, out->len);
	return true;
}

void dma_free(struct dma_buffer *buf)
{
	if (!buf || !buf->virt)
		return;

	vir_mem_free(buf->virt);
	phy_mem_free((fatptr_t){ .ptr = (void *)(uintptr_t)buf->phys, .len = buf->len });
	buf->virt = NULL;
	buf->phys = 0;
	buf->len = 0;
}
