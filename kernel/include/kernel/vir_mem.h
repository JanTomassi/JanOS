#pragma once
#include <kernel/multiboot.h>

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <list.h>

#define VMM_ENTRY_PRESENT_BIT (1 << 0)
#define VMM_ENTRY_READ_WRITE_BIT (1 << 1)
#define VMM_ENTRY_USER_SUPER_BIT (1 << 2)
#define VMM_ENTRY_WRITE_THROUGH_BIT (1 << 3)
#define VMM_ENTRY_CACHE_DISABLE_BIT (1 << 4)
#define VMM_ENTRY_ACCESSED_DISABLE_BIT (1 << 5)
#define VMM_ENTRY_DIRTY (1 << 6)
#define VMM_ENTRY_PAGE_SIZE_BIT (1 << 7)
#define VMM_ENTRY_GLOBAL_BIT (1 << 8)
#define VMM_ENTRY_PAGE_ATTRIBUTE_BIT (1 << 12)
#define VMM_ENTRY_RESERVED_BIT (1 << 21)

#define VMM_ENTRY_AVAILABLE_4K_BIT ((1 << 6) | (0b1111 << 8))
#define VMM_ENTRY_LOCATION_4K_BITS (0xfffff << 12)

#define VMM_ENTRY_LOCATION_4M_LOW_BITS (0x3ff << 22)
#define VMM_ENTRY_LOCATION_4M_HIGHT_BITS (0xff << 13)

#define PAGE_SIZE (4096)
#define round_up_to_page(x) ((x) + (-(x) % PAGE_SIZE))
#define round_down_to_page(x) \
	(((x)-PAGE_SIZE - 1) + (-((x)-PAGE_SIZE - 1) % PAGE_SIZE))

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

struct vmm_entry {
	void *ptr;
	size_t size;
	uint16_t flags;
	struct list_head list;
};

void *get_physaddr(void *virtualaddr);

void init_vir_mem(multiboot_info_t *mbd);
bool map_page(void *physaddr, void *virtualaddr, uint16_t flags);

void *kmalloc(size_t);
void *kcalloc(size_t, size_t);
void kfree(void *);
