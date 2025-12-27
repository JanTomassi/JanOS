#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct dma_buffer {
	void *virt;
	uint32_t phys;
	size_t len;
};

bool dma_alloc(size_t len, struct dma_buffer *out);
void dma_free(struct dma_buffer *buf);
