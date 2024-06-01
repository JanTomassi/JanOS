#pragma once
#include <kernel/multiboot.h>

#include <stddef.h>
#include <stdint.h>

extern uint32_t initial_page_dir[1024];

enum VMM_PAGE_FLAGS {
	VMM_PAGE_FLAG_PRESENT_BIT = (1 << 0),
	VMM_PAGE_FLAG_READ_WRITE_BIT = (1 << 1),
	VMM_PAGE_FLAG_USER_SUPER_BIT = (1 << 2),
	VMM_PAGE_FLAG_WRITE_THROUGH_BIT = (1 << 3),
	VMM_PAGE_FLAG_CACHE_DISABLE_BIT = (1 << 4),
	VMM_PAGE_FLAG_ACCESSED_DISABLE_BIT = (1 << 5),
	VMM_PAGE_FLAG_DIRTY = (1 << 6),
	VMM_PAGE_FLAG_PAGE_SIZE_BIT = (1 << 7),
	VMM_PAGE_FLAG_GLOBAL_BIT = (1 << 8),
	VMM_PAGE_FLAG_PAGE_ATTRIBUTE_BIT = (1 << 12)
};

void init_vir_mem(multiboot_info_t *mbd);
bool map_page(void *physaddr, void *virtualaddr, uint16_t flags);
bool unmap_page(void *physaddr, void *virtualaddr, unsigned int flags);

void *kmalloc(size_t);
void *kcalloc(size_t, size_t);
void kfree(void *);
