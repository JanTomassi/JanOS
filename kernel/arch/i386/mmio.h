#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Map a physical region (fatptr specifies base and length) into virtual memory
 * with the provided page flags (e.g., VMM_PAGE_FLAG_CACHE_DISABLE_BIT |
 * VMM_PAGE_FLAG_READ_WRITE_BIT). The returned fatptr holds the virtual base and
 * length; ptr will be NULL on failure. */
fatptr_t mmio_map_region(fatptr_t phys_region, uint16_t flags);

/* Release a mapping previously created with mmio_map_region. */
void mmio_unmap_region(fatptr_t virt_region);
