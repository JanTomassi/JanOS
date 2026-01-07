#pragma once

#include <kernel/vir_mem.h>
#include <kernel/phy_mem.h>
#include <stdint.h>

struct dma_buffer {
	fatptr_t phys;
	struct vmm_entry *virt_entry;
	void *virt;
	size_t size;
};

struct dma_buffer dma_alloc(size_t size);
void dma_free(struct dma_buffer *buffer);
