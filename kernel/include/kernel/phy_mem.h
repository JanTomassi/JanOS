#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <kernel/multiboot.h>
#include <kernel/elf32.h>

typedef bool phy_mem_is_used;

typedef enum phy_mem_alloc_mode {
	PHY_MEM_ALLOC_ANY = 0,      // all memory (high -> low tree scan)
	PHY_MEM_ALLOC_LOW_16BIT,    // only below physical address 64 KiB
	PHY_MEM_ALLOC_LOW_1M,       // only below physical address 1 MiB
	PHY_MEM_ALLOC_HIGH,         // only at or above physical address 64 KiB
} phy_mem_alloc_mode_t;

void phy_mem_free(fatptr_t addr_ptr);
fatptr_t phy_mem_alloc(size_t len, phy_mem_alloc_mode_t mode);

void phy_mem_init();
