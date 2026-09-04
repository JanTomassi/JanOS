#include <kernel/elf32.h>
#include <kernel/vir_mem.h>
#include <kernel/phy_mem.h>
#include <kernel/display.h>
#include <kernel/allocator.h>
#include <stdalign.h>
#include <stdbool.h>
#include <string.h>
#include <list.h>

#define page_directory_addr VMM_RECURSIVE_PD_ADDR
#define page_table_addr VMM_RECURSIVE_PT_BASE

MODULE("Virt Memory Manager");

extern size_t HIGHER_HALF;

static fatptr_t kernel_page_directory;

static bool is_page_table_empty(size_t *pt);

static void invalidate(const void *addr)
{
	__asm__ volatile("invlpg (%0)" : : "r"((size_t)addr) : "memory");
}

uintptr_t round_up_to_page(uintptr_t x)
{
	if (x > UINTPTR_MAX - (PAGE_SIZE - 1))
		return 0;
	return (x + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}
uintptr_t round_down_to_page(uintptr_t x)
{
	return x & ~(PAGE_SIZE - 1);
}


void *vmm_phy_addr(const void *vir_addr)
{
	size_t pd_idx = (size_t)vir_addr >> 22;
	size_t pt_idx = (size_t)vir_addr >> 12 & 0x03FF;

	size_t *pd = (size_t *)page_directory_addr;

	if (!(pd[pd_idx] & VMM_ENTRY_PRESENT_BIT))
		return nullptr;
	else if ((pd[pd_idx] & VMM_ENTRY_PAGE_SIZE_BIT))
		return (void *)((pd[pd_idx] & VMM_ENTRY_LOCATION_4M_LOW_BITS) + ((size_t)vir_addr & ~VMM_ENTRY_LOCATION_4M_LOW_BITS));

	size_t *pt = ((size_t *)page_table_addr) + (0x400 * pd_idx);
	if ((pt[pt_idx] & VMM_ENTRY_PRESENT_BIT) == 0)
		return nullptr;

	return (void *)((pt[pt_idx] & VMM_ENTRY_LOCATION_4K_BITS) + ((size_t)vir_addr & 0xFFF));
}

void *vmm_vir_addr(const void *phy_addr)
{
	size_t *pd = (size_t *)page_directory_addr;

	for (size_t pd_idx = 0; pd_idx < 1024; pd_idx++) {
		if (!(pd[pd_idx] & VMM_ENTRY_PRESENT_BIT))
			continue;

		size_t *pt = ((size_t *)page_table_addr) + (0x400 * pd_idx);
		for (size_t pt_idx = 0; pt_idx < 1024; pt_idx++) {
			if (!(pt[pt_idx] & VMM_ENTRY_PRESENT_BIT) || (void *)(pt[pt_idx] & VMM_ENTRY_LOCATION_4K_BITS) != phy_addr)
				continue;

			return (void *)((pd_idx << 22 | pt_idx << 12) + ((size_t)phy_addr & 0xFFF));
		}
	}

	return nullptr;
}

void map_page(const void *phy_addr, const void *virt_addr, uint16_t virt_flags)
{
	if (((uintptr_t)phy_addr & (PAGE_SIZE - 1)) != 0 ||
	    ((uintptr_t)virt_addr & (PAGE_SIZE - 1)) != 0)
		BUG("map_page requires page-aligned addresses");
	if ((uintptr_t)virt_addr >= VMM_RECURSIVE_PT_BASE)
		BUG("map_page cannot modify recursive mapping window");

	size_t pd_idx = (size_t)virt_addr >> 22;
	size_t pt_idx = (size_t)virt_addr >> 12 & 0x03FF;

	size_t *pd = (size_t *)page_directory_addr;
	size_t *pt = ((size_t *)page_table_addr) + (0x400 * pd_idx);

	if ((pd[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0) {
		fatptr_t table = phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH);
		if (table.ptr == nullptr)
			panic("Failed to allocate page table\n");
		pd[pd_idx] = (size_t)table.ptr;
		pd[pd_idx] |= VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT |
			((virt_flags & VMM_ENTRY_USER_SUPER_BIT) != 0 ? VMM_ENTRY_USER_SUPER_BIT : 0);
		memset(pt, 0, PAGE_SIZE);
		invalidate(pt);
	} else if (pd[pd_idx] & VMM_ENTRY_PAGE_SIZE_BIT) {
		const size_t huge = pd[pd_idx];
		const size_t table = (size_t)phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH).ptr;
		if (table == 0)
			panic("Failed to split huge page mapping\n");

		pd[pd_idx] = table | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT;
		for (size_t i = 0; i < 1024; i++)
			pt[i] = ((huge & VMM_ENTRY_LOCATION_4M_LOW_BITS) + i * PAGE_SIZE) |
				(huge & 0xFFF & ~VMM_ENTRY_PAGE_SIZE_BIT);
		invalidate(pt);
	}
	if (virt_flags & VMM_ENTRY_USER_SUPER_BIT)
		pd[pd_idx] |= VMM_ENTRY_USER_SUPER_BIT;

	const size_t old_pte = pt[pt_idx];
	const size_t new_pte = ((size_t)phy_addr) | (virt_flags & 0xFFF);
	if ((old_pte & VMM_ENTRY_PRESENT_BIT) != 0 &&
	    (old_pte & VMM_ENTRY_LOCATION_4K_BITS) != (new_pte & VMM_ENTRY_LOCATION_4K_BITS)) {
		BUG("map_page would replace an existing mapping");
	}
	pt[pt_idx] = new_pte;

	invalidate(virt_addr);
}

static bool mapping_present(const void *virt_addr)
{
	size_t pd_idx = (size_t)virt_addr >> 22;
	size_t pt_idx = (size_t)virt_addr >> 12 & 0x03FF;
	size_t *pd = (size_t *)page_directory_addr;
	if ((pd[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0)
		return false;
	if (pd[pd_idx] & VMM_ENTRY_PAGE_SIZE_BIT)
		return true;
	return (((size_t *)page_table_addr) + (0x400 * pd_idx))[pt_idx] &
		VMM_ENTRY_PRESENT_BIT;
}

void map_pages(const fatptr_t *phy_mem, const struct vmm_entry *virt_mem)
{
	if (phy_mem == nullptr || virt_mem == nullptr)
		BUG("map_pages requires non-null ranges");
	if (((uintptr_t)phy_mem->ptr & (PAGE_SIZE - 1)) != 0 ||
	    ((uintptr_t)virt_mem->ptr & (PAGE_SIZE - 1)) != 0 ||
	    (phy_mem->len & (PAGE_SIZE - 1)) != 0 ||
	    (virt_mem->size & (PAGE_SIZE - 1)) != 0)
		BUG("map_pages requires page-aligned ranges");
	if (phy_mem->len != virt_mem->size)
		panic("Physical and Virtual size not equal:\n"
		      " - phy_size: %x\n"
		      " - virt_size %x\n",
		      phy_mem->len, virt_mem->size);

	void *virt_addr = virt_mem->ptr;
	void *phy_addr = phy_mem->ptr;

	while (virt_addr < virt_mem->ptr + virt_mem->size) {
		/* Boot paging starts with identity-backed huge pages.  A range handed
		 * out by the VMM is free in the allocator even when that bootstrap
		 * translation still exists, so remove only the translation here. */
		void *existing = vmm_phy_addr(virt_addr);
		if (mapping_present(virt_addr) && existing != phy_addr)
			unmap_page(nullptr, virt_addr);
		map_page(phy_addr, virt_addr, virt_mem->flags);

		virt_addr += PAGE_SIZE;
		phy_addr += PAGE_SIZE;
	}
}

static bool is_page_table_empty(size_t *pt)
{
	for (size_t i = 0; i < 1024; i++) {
		if (pt[i] & VMM_ENTRY_PRESENT_BIT)
			return false;
	}

	return true;
}

void unmap_page(const void* phy_mem, const void *virt_addr)
{
	if (phy_mem != nullptr)
		phy_mem_free((const fatptr_t){.ptr = phy_mem, .len = PAGE_SIZE});

	size_t pd_idx = (size_t)virt_addr >> 22;
	size_t pt_idx = (size_t)virt_addr >> 12 & 0x03FF;

	size_t *pd = (size_t *)page_directory_addr;
	size_t *pt = ((size_t *)page_table_addr) + (0x400 * pd_idx);

	if ((pd[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0)
		return;
	if (pd[pd_idx] & VMM_ENTRY_PAGE_SIZE_BIT) {
		const size_t huge = pd[pd_idx];
		fatptr_t table = phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH);
		if (table.ptr == nullptr)
			panic("Failed to allocate page table while unmapping\n");
		pd[pd_idx] = (size_t)table.ptr | VMM_ENTRY_READ_WRITE_BIT |
			VMM_ENTRY_PRESENT_BIT | (huge & VMM_ENTRY_USER_SUPER_BIT);
		for (size_t i = 0; i < 1024; i++)
			pt[i] = ((huge & VMM_ENTRY_LOCATION_4M_LOW_BITS) + i * PAGE_SIZE) |
				(huge & 0xFFF & ~VMM_ENTRY_PAGE_SIZE_BIT);
		invalidate(pt);
	}

	pt[pt_idx] = 0;

	if (is_page_table_empty(pt)) {
		fatptr_t table_frame = {
			.ptr = (void *)(pd[pd_idx] & VMM_ENTRY_LOCATION_4K_BITS),
			.len = PAGE_SIZE,
		};

		pd[pd_idx] = 0;
		phy_mem_free(table_frame);
	}

	invalidate(virt_addr);
}

void unmap_pages(const fatptr_t *phy_mem, const struct vmm_entry *virt_mem)
{
	if (virt_mem == nullptr || ((uintptr_t)virt_mem->ptr & (PAGE_SIZE - 1)) != 0 ||
	    (virt_mem->size & (PAGE_SIZE - 1)) != 0)
		BUG("unmap_pages requires a page-aligned range");
	if (phy_mem != nullptr){
		phy_mem_free(*phy_mem);
	}
	if (virt_mem->size == 0){
		return;
	}

	void *start_addr = virt_mem->ptr;
	void *end_addr = (void*)round_up_to_page((uintptr_t)start_addr + virt_mem->size);

	for (void *virt_addr = start_addr; virt_addr < end_addr; virt_addr += PAGE_SIZE) {
		unmap_page(nullptr, virt_addr);
	}
}

static struct vmm_entry *temporary_mapping(const fatptr_t *physical)
{
	struct vmm_entry *temporary = vmm_alloc(PAGE_SIZE,
		VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);
	if (temporary == nullptr)
		return nullptr;
	map_pages(physical, temporary);
	return temporary;
}

static void release_temporary_mapping(struct vmm_entry *temporary)
{
	if (temporary == nullptr)
		return;
	unmap_page(nullptr, temporary->ptr);
	vmm_free(temporary->ptr);
}

static uint32_t current_page_directory(void)
{
	uint32_t directory;
	__asm__ volatile("mov %%cr3, %0" : "=r"(directory) : : "memory");
	return directory & VMM_ENTRY_LOCATION_4K_BITS;
}

bool vmm_page_directory_is_active(const fatptr_t *page_directory)
{
	return page_directory != nullptr && page_directory->ptr != nullptr &&
		current_page_directory() == (uint32_t)(uintptr_t)page_directory->ptr;
}

bool vmm_page_directory_create(fatptr_t *page_directory)
{
	if (page_directory == nullptr || kernel_page_directory.ptr == nullptr)
		return false;
	fatptr_t directory = phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH);
	if (directory.ptr == nullptr)
		return false;
	struct vmm_entry *mapped = temporary_mapping(&directory);
	if (mapped == nullptr) {
		phy_mem_free(directory);
		return false;
	}
	uint32_t *new_pd = mapped->ptr;
	memset(new_pd, 0, PAGE_SIZE);
	uint32_t *kernel_pd = (uint32_t *)(uintptr_t)kernel_page_directory.ptr;
	for (size_t i = 768; i < 1023; ++i)
		new_pd[i] = kernel_pd[i] & ~VMM_ENTRY_USER_SUPER_BIT;
	new_pd[1023] = (uint32_t)(uintptr_t)directory.ptr |
		VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT;
	release_temporary_mapping(mapped);
	*page_directory = directory;
	return true;
}

bool vmm_page_directory_destroy(fatptr_t page_directory)
{
	if (page_directory.ptr == nullptr || vmm_page_directory_is_active(&page_directory))
		return false;
	struct vmm_entry *mapped = temporary_mapping(&page_directory);
	if (mapped == nullptr)
		return false;
	uint32_t *pd = mapped->ptr;
	for (size_t i = 0; i < 768; ++i) {
		if ((pd[i] & VMM_ENTRY_PRESENT_BIT) == 0)
			continue;
		if (pd[i] & VMM_ENTRY_PAGE_SIZE_BIT)
			continue;
		fatptr_t table = { .ptr = (void *)(uintptr_t)(pd[i] & VMM_ENTRY_LOCATION_4K_BITS),
			.len = PAGE_SIZE };
		pd[i] = 0;
		phy_mem_free(table);
	}
	release_temporary_mapping(mapped);
	phy_mem_free(page_directory);
	return true;
}

bool vmm_page_directory_map(const fatptr_t *page_directory, const fatptr_t *physical,
	                         uintptr_t virtual_address, uint16_t flags)
{
	if (page_directory == nullptr || physical == nullptr || page_directory->ptr == nullptr ||
		physical->ptr == nullptr || physical->len != PAGE_SIZE ||
		(virtual_address & (PAGE_SIZE - 1)) != 0 || virtual_address >= VMM_RECURSIVE_PT_BASE)
		return false;
	struct vmm_entry *pd_mapping = temporary_mapping(page_directory);
	if (pd_mapping == nullptr)
		return false;
	uint32_t *pd = pd_mapping->ptr;
	size_t pd_idx = virtual_address >> 22;
	size_t pt_idx = (virtual_address >> 12) & 0x3ff;
	fatptr_t table = { 0 };
	bool new_table = false;
	if ((pd[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0) {
		table = phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH);
		if (table.ptr == nullptr) {
			release_temporary_mapping(pd_mapping);
			return false;
		}
		struct vmm_entry *table_mapping = temporary_mapping(&table);
		if (table_mapping == nullptr) {
			phy_mem_free(table);
			release_temporary_mapping(pd_mapping);
			return false;
		}
		memset(table_mapping->ptr, 0, PAGE_SIZE);
		release_temporary_mapping(table_mapping);
		pd[pd_idx] = (uint32_t)(uintptr_t)table.ptr | VMM_ENTRY_PRESENT_BIT |
			VMM_ENTRY_READ_WRITE_BIT;
		new_table = true;
	} else if (pd[pd_idx] & VMM_ENTRY_PAGE_SIZE_BIT) {
		release_temporary_mapping(pd_mapping);
		return false;
	}
	if (flags & VMM_ENTRY_USER_SUPER_BIT)
		pd[pd_idx] |= VMM_ENTRY_USER_SUPER_BIT;
	table.ptr = (void *)(uintptr_t)(pd[pd_idx] & VMM_ENTRY_LOCATION_4K_BITS);
	table.len = PAGE_SIZE;
	struct vmm_entry *table_mapping = temporary_mapping(&table);
	if (table_mapping == nullptr) {
		if (new_table) {
			pd[pd_idx] = 0;
			phy_mem_free(table);
		}
		release_temporary_mapping(pd_mapping);
		return false;
	}
	uint32_t *pt = table_mapping->ptr;
	uint32_t old = pt[pt_idx];
	uint32_t entry = (uint32_t)(uintptr_t)physical->ptr | (flags & 0xfff);
	if ((old & VMM_ENTRY_PRESENT_BIT) != 0 &&
		(old & VMM_ENTRY_LOCATION_4K_BITS) != (entry & VMM_ENTRY_LOCATION_4K_BITS)) {
		release_temporary_mapping(table_mapping);
		release_temporary_mapping(pd_mapping);
		return false;
	}
	pt[pt_idx] = entry;
	release_temporary_mapping(table_mapping);
	release_temporary_mapping(pd_mapping);
	if (vmm_page_directory_is_active(page_directory))
		invalidate((void *)virtual_address);
	return true;
}

bool vmm_page_directory_unmap(const fatptr_t *page_directory, uintptr_t virtual_address)
{
	if (page_directory == nullptr || page_directory->ptr == nullptr ||
		(virtual_address & (PAGE_SIZE - 1)) != 0 || virtual_address >= VMM_RECURSIVE_PT_BASE)
		return false;
	struct vmm_entry *pd_mapping = temporary_mapping(page_directory);
	if (pd_mapping == nullptr)
		return false;
	uint32_t *pd = pd_mapping->ptr;
	size_t pd_idx = virtual_address >> 22;
	size_t pt_idx = (virtual_address >> 12) & 0x3ff;
	if ((pd[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0 ||
		(pd[pd_idx] & VMM_ENTRY_PAGE_SIZE_BIT)) {
		release_temporary_mapping(pd_mapping);
		return false;
	}
	fatptr_t table = { .ptr = (void *)(uintptr_t)(pd[pd_idx] & VMM_ENTRY_LOCATION_4K_BITS),
		.len = PAGE_SIZE };
	struct vmm_entry *table_mapping = temporary_mapping(&table);
	if (table_mapping == nullptr) {
		release_temporary_mapping(pd_mapping);
		return false;
	}
	uint32_t *pt = table_mapping->ptr;
	pt[pt_idx] = 0;
	bool empty = is_page_table_empty(pt);
	release_temporary_mapping(table_mapping);
	if (empty) {
		pd[pd_idx] = 0;
		phy_mem_free(table);
	}
	release_temporary_mapping(pd_mapping);
	if (vmm_page_directory_is_active(page_directory))
		invalidate((void *)virtual_address);
	return true;
}

bool vmm_page_directory_protect(const fatptr_t *page_directory,
								uintptr_t virtual_address, uint16_t flags)
{
	if (page_directory == nullptr || page_directory->ptr == nullptr ||
		(virtual_address & (PAGE_SIZE - 1)) != 0 || virtual_address >= VMM_RECURSIVE_PT_BASE)
		return false;
	struct vmm_entry *pd_mapping = temporary_mapping(page_directory);
	if (pd_mapping == nullptr)
		return false;
	uint32_t *pd = pd_mapping->ptr;
	size_t pd_idx = virtual_address >> 22;
	size_t pt_idx = (virtual_address >> 12) & 0x3ff;
	if ((pd[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0 ||
		(pd[pd_idx] & VMM_ENTRY_PAGE_SIZE_BIT)) {
		release_temporary_mapping(pd_mapping);
		return false;
	}
	fatptr_t table = { .ptr = (void *)(uintptr_t)(pd[pd_idx] & VMM_ENTRY_LOCATION_4K_BITS),
		.len = PAGE_SIZE };
	struct vmm_entry *table_mapping = temporary_mapping(&table);
	if (table_mapping == nullptr) {
		release_temporary_mapping(pd_mapping);
		return false;
	}
	uint32_t *pte = &((uint32_t *)table_mapping->ptr)[pt_idx];
	if ((*pte & VMM_ENTRY_PRESENT_BIT) == 0) {
		release_temporary_mapping(table_mapping);
		release_temporary_mapping(pd_mapping);
		return false;
	}
	*pte = (*pte & VMM_ENTRY_LOCATION_4K_BITS) |
		(flags & 0xfff) | VMM_ENTRY_PRESENT_BIT;
	release_temporary_mapping(table_mapping);
	release_temporary_mapping(pd_mapping);
	if (vmm_page_directory_is_active(page_directory))
		invalidate((void *)virtual_address);
	return true;
}

bool vmm_page_directory_get_flags(const fatptr_t *page_directory,
	                                uintptr_t virtual_address, uint16_t *flags)
{
	if (page_directory == nullptr || page_directory->ptr == nullptr || flags == nullptr ||
	    (virtual_address & (PAGE_SIZE - 1)) != 0 || virtual_address >= VMM_RECURSIVE_PT_BASE)
		return false;
	struct vmm_entry *pd_mapping = temporary_mapping(page_directory);
	if (pd_mapping == nullptr)
		return false;
	uint32_t *pd = pd_mapping->ptr;
	size_t pd_idx = virtual_address >> 22;
	size_t pt_idx = (virtual_address >> 12) & 0x3ff;
	if ((pd[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0 ||
	    (pd[pd_idx] & VMM_ENTRY_PAGE_SIZE_BIT)) {
		release_temporary_mapping(pd_mapping);
		return false;
	}
	fatptr_t table = { .ptr = (void *)(uintptr_t)(pd[pd_idx] & VMM_ENTRY_LOCATION_4K_BITS),
		.len = PAGE_SIZE };
	struct vmm_entry *table_mapping = temporary_mapping(&table);
	if (table_mapping == nullptr) {
		release_temporary_mapping(pd_mapping);
		return false;
	}
	uint32_t pte = ((uint32_t *)table_mapping->ptr)[pt_idx];
	uint32_t effective = pte & 0xfff;
	if ((pd[pd_idx] & VMM_ENTRY_USER_SUPER_BIT) == 0)
		effective &= ~VMM_ENTRY_USER_SUPER_BIT;
	if ((pd[pd_idx] & VMM_ENTRY_READ_WRITE_BIT) == 0)
		effective &= ~VMM_ENTRY_READ_WRITE_BIT;
	*flags = (uint16_t)effective;
	release_temporary_mapping(table_mapping);
	release_temporary_mapping(pd_mapping);
	return (pte & VMM_ENTRY_PRESENT_BIT) != 0;
}

bool vmm_page_directory_activate(const fatptr_t *page_directory)
{
	if (page_directory == nullptr || page_directory->ptr == nullptr ||
		((uintptr_t)page_directory->ptr & (PAGE_SIZE - 1)) != 0)
		return false;
	__asm__ volatile("mov %0, %%cr3" : : "r"(page_directory->ptr) : "memory");
	return true;
}

const fatptr_t *vmm_kernel_page_directory(void)
{
	return kernel_page_directory.ptr == nullptr ? nullptr : &kernel_page_directory;
}

static void invalidate_low_range(void)
{
	/* Keep identity mappings until the physical allocator metadata is mapped high. */
}

static inline void print_elf_sector(const Elf32_Shdr *elf_sec, const char *elf_sec_str, const size_t i)
{
#ifdef DEBUG
	mprint("Section (%s): [Address: %x, Size: %x, Type: %x, flags: %x]\n", &elf_sec_str[elf_sec[i].sh_name], elf_sec[i].sh_addr, elf_sec[i].sh_size,
	       elf_sec[i].sh_type, elf_sec[i].sh_flags);
#endif
}

static void recreate_vir_mem(const struct multiboot_tag_elf_sections *elf_tag, const struct vmm_entry *preserved_entries, size_t preserved_entry_count)
{
	/* Boot paging already maps the complete low and higher-half address space. */
	(void)elf_tag;
	(void)preserved_entries;
	(void)preserved_entry_count;
	return;

	LIST_HEAD(vmm_used_list);
	struct vmm_entry usable_entry[64] = { 0 };
	const size_t usable_capacity = sizeof(usable_entry) / sizeof(usable_entry[0]);
	size_t used_entris = 0;

	const Elf32_Shdr *elf_sec = (const Elf32_Shdr *)elf_tag->sections;
	const char *elf_sec_str = (char *)elf_sec[elf_tag->shndx].sh_addr;
	size_t required_entries = preserved_entry_count;

	for (size_t i = 0; i < elf_tag->num; i++) {
		if ((elf_sec[i].sh_flags & ELF_SHF_ALLOC) == 0 || (void*)elf_sec[i].sh_addr < (void*)&HIGHER_HALF)
			continue;

		required_entries++;
	}

	if (required_entries > usable_capacity)
		panic("Not enough space to register kernel sections and preserved ranges\n");

	for (size_t i = 0; i < elf_tag->num; i++) {
		if ((elf_sec[i].sh_flags & ELF_SHF_ALLOC) == 0) {
#ifdef DEBUG
			mprint("Section (%s) dosn't allocate memory at runtime\n", &elf_sec_str[elf_sec[i].sh_name]);
#endif
			continue;
		} else if ((void*)elf_sec[i].sh_addr < (void*)&HIGHER_HALF) {
#ifdef DEBUG
			mprint("Section (%s) isn't part of the higher half kernel\n", &elf_sec_str[elf_sec[i].sh_name]);
#endif
			continue;
		} else {
			print_elf_sector(elf_sec, elf_sec_str, i);
		}

		size_t elf_s = round_down_to_page(elf_sec[i].sh_addr);
		size_t elf_e = round_up_to_page(elf_sec[i].sh_addr + elf_sec[i].sh_size);

		struct vmm_entry entry = {
			.ptr = (void *)elf_s,
			.size = elf_e - elf_s,
			.flags = (elf_sec[i].sh_flags & ELF_SHF_WRITE) * VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT,
		};
		RESET_LIST_ITEM(&entry.list);

		usable_entry[used_entris] = entry;
		list_add(&usable_entry[used_entris].list, &vmm_used_list);
		used_entris++;
	}

	for (size_t i = 0; i < preserved_entry_count && used_entris < usable_capacity; i++) {
		struct vmm_entry entry = preserved_entries[i];
		RESET_LIST_ITEM(&entry.list);
		usable_entry[used_entris] = entry;
		list_add(&usable_entry[used_entris].list, &vmm_used_list);
		used_entris++;
	}

	invalidate_low_range();

	/**
	 * Temporanialiy bind the address 1000 to the new pd
	 */
	fatptr_t pd = phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH);
	struct vmm_entry tmp_virt = (struct vmm_entry){
		.ptr = (void *)0x1000,
		.size = PAGE_SIZE,
		.flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT,
	};

	map_pages(&pd, &tmp_virt);
	memset(tmp_virt.ptr, 0, tmp_virt.size);
	((uint32_t *)tmp_virt.ptr)[1023] = (size_t)pd.ptr | VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT;

	list_for_each(&vmm_used_list) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);

		size_t mapped_pages = 0;
		for (void *virt_addr = cur->ptr; virt_addr < (cur->ptr + cur->size); virt_addr += PAGE_SIZE) {
			if (++mapped_pages > 1024)
				panic("virtual range is unexpectedly large\n");

			map_pages(&pd, &tmp_virt);

			size_t pd_idx = (size_t)virt_addr >> 22;
			size_t pt_idx = (size_t)virt_addr >> 12 & 0x03FF;

			size_t *px = (size_t *)tmp_virt.ptr;

			if ((px[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0) {
				px[pd_idx] = (size_t)phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH).ptr;

				px[pd_idx] |= 1 | cur->flags | VMM_ENTRY_READ_WRITE_BIT;

				map_pages(&(fatptr_t){ .ptr = (void *)(px[pd_idx] & ~0xfff), .len = PAGE_SIZE }, &tmp_virt);
				memset(tmp_virt.ptr, 0, PAGE_SIZE);
			} else {
				px[pd_idx] |= cur->flags | VMM_ENTRY_READ_WRITE_BIT;

				map_pages(&(fatptr_t){ .ptr = (void *)(px[pd_idx] & ~0xfff), .len = PAGE_SIZE }, &tmp_virt);
			}

			void *physical = vmm_phy_addr((void *)virt_addr);
			if (physical == nullptr)
				panic("missing physical mapping for kernel range\n");
			px[pt_idx] = ((size_t)physical) | (cur->flags & 0xFFF);
		}
	}

	void *old_pd_loc = 0;
	__asm__ volatile("mov %%cr3, %0" : "=g"(old_pd_loc) : : "memory");

	__asm__ volatile("mov %0, %%cr3" : : "r"(pd.ptr) : "memory");

	map_pages(&(fatptr_t){ .ptr = old_pd_loc, .len = PAGE_SIZE }, &tmp_virt);

	memcpy((void *)0x1000, (void *)0xfffff000, PAGE_SIZE);

	((uint32_t *)0x1000)[1023] = (size_t)old_pd_loc | VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT;

	__asm__ volatile("mov %0, %%cr3" : : "r"(old_pd_loc) : "memory");

	tmp_virt.flags = 0;
	map_pages(&(fatptr_t){ .ptr = old_pd_loc, .len = PAGE_SIZE }, &tmp_virt);

	phy_mem_free(pd);
}

LIST_HEAD(vmm_free_list);
LIST_HEAD(vmm_used_list);

static slab_cache_t *vmm_entry_cache = nullptr;
static bool vmm_allocator_initialized = false;

#define EARLY_VMM_ENTRY_CAPACITY (32)
static uint32_t early_vmm_bitmap = 0;
static struct vmm_entry early_vmm_entries[EARLY_VMM_ENTRY_CAPACITY * sizeof(struct vmm_entry)] = { 0 };

static bool is_early_vmm_entry(const struct vmm_entry *entry)
{
	return entry >= early_vmm_entries && entry < early_vmm_entries + EARLY_VMM_ENTRY_CAPACITY;
}

static struct vmm_entry *early_vmm_alloc(void){
	if (vmm_allocator_initialized)
		kerror("Called early alloc with vmm initialized");

	for (uint16_t bit = 0; bit < EARLY_VMM_ENTRY_CAPACITY; bit++) {
		uint32_t mask = 1u << bit;
		if ((early_vmm_bitmap & mask) != 0)
			continue;

		early_vmm_bitmap |= mask;
		struct vmm_entry *entry = &early_vmm_entries[bit];
		*entry = (struct vmm_entry){ 0 };
		RESET_LIST_ITEM(&entry->list);
		return entry;
	}

	return nullptr;
}

static void early_vmm_free(struct vmm_entry *entry)
{
	if (!is_early_vmm_entry(entry)){
		kerror("Called on a non early entry");
		return;
	}

	size_t idx = (size_t)(entry - early_vmm_entries);
	early_vmm_bitmap &= ~(1u << idx);
}

static struct vmm_entry *vmm_entry_alloc(void)
{
	if (vmm_allocator_initialized) {
		fatptr_t entry = slab_alloc_obj(vmm_entry_cache);
		if (entry.ptr == nullptr)
			return nullptr;
		return entry.ptr;
	} else{
		return early_vmm_alloc();
	}
}

static void vmm_entry_free(struct vmm_entry *entry)
{
	if (entry == nullptr)
		return;

	if (vmm_allocator_initialized && !is_early_vmm_entry(entry)) {
		slab_free_obj(vmm_entry_cache, (fatptr_t){ .ptr = entry, .len = sizeof(*entry) });
		return;
	}

	early_vmm_free(entry);
}

#ifdef DEBUG
static void debug_vmm_lists(void)
{
	size_t i = 0;
	mprint("debug_vmm_list | vmm_free_list:\n");
	list_for_each(&vmm_free_list) {
		struct vmm_entry *tag = list_entry(it, struct vmm_entry, list);
		mprint("    %u) ptr: %x | size: %x | flags: %x\n", i++, tag->ptr, tag->size, tag->flags);
	}

	i = 0;
	mprint("debug_vmm_list | vmm_used_list:\n");
	list_for_each(&vmm_used_list) {
		struct vmm_entry *tag = list_entry(it, struct vmm_entry, list);
		mprint("    %u) ptr: %x | size: %x | flags: %x\n", i++, tag->ptr, tag->size, tag->flags);
	}

}

static void debug_vmm_list(struct list_head *v){
	mprint("debug_vmm_list:\n");
	size_t i = 0;
	list_for_each(v) {
		struct vmm_entry *tag = list_entry(it, struct vmm_entry, list);
		mprint("    %u) ptr: %x | size: %x | flags: %x\n", i++, tag->ptr, tag->size, tag->flags);
	}
}
#endif

static struct list_head *vir_mem_find_prev_used_chunk(struct vmm_entry *to_alloc)
{
	struct list_head *next_chunk = &vmm_used_list;

	list_for_each(&vmm_used_list) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);

		if (cur->ptr < to_alloc->ptr && (next_chunk == &vmm_used_list || cur->ptr > list_entry(next_chunk, struct vmm_entry, list)->ptr))
			next_chunk = &cur->list;
	}
	return next_chunk;
}

struct vmm_entry *vmm_alloc(size_t req_size, uint8_t flags)
{
	if (req_size == 0 || (req_size & (PAGE_SIZE - 1)) != 0)
		BUG("Virtual memory allocation must be non-zero and page aligned");
	uint32_t lock_flags = allocator_lock_acquire();

	struct vmm_entry *free_chunk = nullptr;

	size_t free_count = 0;
	list_for_each(&vmm_free_list) {
		if (++free_count > 256)
			panic("Corrupt virtual free list\n");
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);

		size_t cur_free = cur->size;
		bool update_chunk_sel = cur_free >= req_size && (free_chunk == nullptr || (cur_free < free_chunk->size));

		if (update_chunk_sel) {
			free_chunk = cur;
		}
	}

	if (free_chunk == nullptr) {
		allocator_lock_release(lock_flags);
		return nullptr;
	}

	struct vmm_entry *tag = vmm_entry_alloc();
	if (tag == nullptr) {
		allocator_lock_release(lock_flags);
		return nullptr;
	}

	*tag = (struct vmm_entry){
		.ptr = free_chunk->ptr,
		.size = req_size,
		.flags = flags,
	};

	if (free_chunk->size == req_size) {
		list_rm(&free_chunk->list);
		vmm_entry_free(free_chunk);
	} else {
		free_chunk->ptr += req_size;
		free_chunk->size -= req_size;
	}

	list_add(&tag->list, vir_mem_find_prev_used_chunk(tag)->prev);

#ifdef DEBUG
	debug_vmm_lists();
#endif

	allocator_lock_release(lock_flags);
	return tag;
}

static struct list_head *vir_mem_find_free_insert_pos(uintptr_t address)
{
	list_for_each(&vmm_free_list) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);
		if ((uintptr_t)cur->ptr > address)
			return it->prev;
	}
	return vmm_free_list.prev;
}

static void vir_mem_free_coalesce(struct vmm_entry *mid)
{
	struct vmm_entry *prev = list_is_first(&mid->list, &vmm_free_list) ? nullptr : list_entry(mid->list.prev, struct vmm_entry, list);
	struct vmm_entry *next = list_is_last(&mid->list, &vmm_free_list) ? nullptr : list_entry(mid->list.next, struct vmm_entry, list);

	if (next != nullptr && mid->ptr + mid->size == next->ptr) {
		mid->size += next->size;
		list_rm(&next->list);
		vmm_entry_free(next);
	}
	if (prev != nullptr && prev->ptr + prev->size == mid->ptr) {
		prev->size += mid->size;
		list_rm(&mid->list);
		vmm_entry_free(mid);
	}
}

void vmm_free(const void *ptr)
{
	if (ptr == nullptr || ((uintptr_t)ptr & (PAGE_SIZE - 1)) != 0)
		BUG("vmm_free requires a page-aligned allocation address");
	uint32_t lock_flags = allocator_lock_acquire();

	list_for_each(&vmm_used_list) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);

		if (cur->ptr == ptr) {
			list_rm(&cur->list);
			list_add(&cur->list, vir_mem_find_free_insert_pos((uintptr_t)cur->ptr));
			vir_mem_free_coalesce(cur);
			break;
		}
	}

#ifdef DEBUG
	debug_vmm_lists();
#endif
	allocator_lock_release(lock_flags);
}

void vmm_release_init(void)
{
	extern uint8_t __sinit;
	extern uint8_t __einit;
	const uintptr_t start = round_down_to_page((uintptr_t)&__sinit);
	const uintptr_t end = round_up_to_page((uintptr_t)&__einit);

	for (struct list_head *it = vmm_used_list.next; it != &vmm_used_list;) {
		struct list_head *next = it->next;
		struct vmm_entry *entry = list_entry(it, struct vmm_entry, list);
		uintptr_t entry_start = (uintptr_t)entry->ptr;
		uintptr_t entry_end = entry_start + entry->size;
		uintptr_t release_start = entry_start > start ? entry_start : start;
		uintptr_t release_end = entry_end < end ? entry_end : end;

		if (release_start < release_end) {
			for (uintptr_t page = release_start; page < release_end;
			     page += PAGE_SIZE) {
				void *physical = vmm_phy_addr((void *)page);
				if (physical != nullptr)
					unmap_page(physical, (void *)page);
			}

			struct vmm_entry *free_entry = vmm_entry_alloc();
			if (free_entry == nullptr)
				panic("Failed to allocate reclaimed VMM range\n");
			*free_entry = (struct vmm_entry){
				.ptr = (void *)release_start,
				.size = release_end - release_start,
			};
			RESET_LIST_ITEM(&free_entry->list);
			list_add(&free_entry->list,
				 vir_mem_find_free_insert_pos(release_start));
			vir_mem_free_coalesce(free_entry);

			if (release_start == entry_start && release_end == entry_end) {
				list_rm(&entry->list);
				vmm_entry_free(entry);
			} else if (release_start == entry_start) {
				entry->ptr = (void *)release_end;
				entry->size = entry_end - release_end;
			} else if (release_end == entry_end) {
				entry->size = release_start - entry_start;
			} else {
				struct vmm_entry *right = vmm_entry_alloc();
				if (right == nullptr)
					panic("Failed to split reclaimed VMM range\n");
				*right = (struct vmm_entry){
					.ptr = (void *)release_end,
					.size = entry_end - release_end,
					.flags = entry->flags,
				};
				RESET_LIST_ITEM(&right->list);
				entry->size = release_start - entry_start;
				list_add(&right->list, &entry->list);
			}
		}
		it = next;
	}
}

static void migrate_tags_to_slab(void)
{
	if (vmm_allocator_initialized)
		return;

	if (vmm_entry_cache == nullptr)
		vmm_entry_cache = slab_create("vmm_entry", sizeof(struct vmm_entry), alignof(struct vmm_entry), nullptr, nullptr);

	if (vmm_entry_cache == nullptr)
		panic("Failed to create slab cache for vmm entries\n");
	slab_set_cache_reserve(vmm_entry_cache, 10);

	fatptr_t tag_s = slab_alloc_obj(vmm_entry_cache);

	vmm_allocator_initialized = true;

	LIST_HEAD(migrated_free);
	LIST_HEAD(migrated_used);

	for (struct list_head *it = vmm_free_list.next; it != &vmm_free_list; ) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);
		it = it->next;
		list_rm(&cur->list);

		if (is_early_vmm_entry(cur)) {
			struct vmm_entry *replacement = vmm_entry_alloc();
			if (replacement == nullptr)
				panic("Failed to migrate vmm entry to slab allocator\n");

			*replacement = *cur;
			RESET_LIST_ITEM(&replacement->list);
			list_add(&replacement->list, &migrated_free);
			early_vmm_free(cur);
		} else {
			list_add(&cur->list, &migrated_free);
		}
	}

	for (struct list_head *it = vmm_used_list.next; it != &vmm_used_list; ) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);
		it = it->next;
		list_rm(&cur->list);

		if (is_early_vmm_entry(cur)) {
			struct vmm_entry *replacement = vmm_entry_alloc();
			if (replacement == nullptr)
				panic("Failed to migrate vmm entry to slab allocator\n");

			*replacement = *cur;
			RESET_LIST_ITEM(&replacement->list);
			list_add(&replacement->list, &migrated_used);
			early_vmm_free(cur);
		} else {
			list_add(&cur->list, &migrated_used);
		}
	}

	slab_free_obj(vmm_entry_cache, tag_s);

	RESET_LIST_ITEM(&vmm_free_list);
	RESET_LIST_ITEM(&vmm_used_list);

	struct list_head *it = migrated_free.next;
	while(&migrated_free != it) {
		list_mv(it, vmm_free_list.prev);
		it = migrated_free.next;
	}

	it = migrated_used.next;
	while(&migrated_used != it) {
		list_mv(it, vmm_used_list.prev);
		it = migrated_used.next;
	}
}

static void init_vir_manager(struct list_head *vmm_init_list)
{
	RESET_LIST_ITEM(&vmm_free_list);
	RESET_LIST_ITEM(&vmm_used_list);

	// Add all the virtual memory mapping to the kmalloc known block
	size_t count = 0;
	list_for_each(vmm_init_list) {
		if (++count > 64)
			panic("Corrupt initial virtual memory list\n");
		struct vmm_entry *vmm_cur = list_entry(it, struct vmm_entry, list);

		struct vmm_entry *vmm_tag = vmm_entry_alloc();
		if (vmm_tag == nullptr)
			panic("Failed to allocate vmm entry from early pool during init\n");

		*vmm_tag = *vmm_cur;

		list_add(&vmm_tag->list, vmm_free_list.prev);
	}
}

static void vmm_init_used_range(void *ptr, size_t size, uint8_t flags)
{
	if (size == 0)
		return;
	if (((uintptr_t)ptr & (PAGE_SIZE - 1)) != 0 || (size & (PAGE_SIZE - 1)) != 0)
		panic("Unaligned VMM used range during init\n");

	struct vmm_entry *tag = vmm_entry_alloc();
	if (tag == nullptr)
		panic("Failed to allocate vmm used entry during init\n");

	*tag = (struct vmm_entry){
		.ptr = ptr,
		.size = size,
		.flags = flags,
	};

	list_add(&tag->list, vir_mem_find_prev_used_chunk(tag)->prev);
}

static void vmm_init_used_list(const struct multiboot_tag_elf_sections *elf_tag,
			       const struct vmm_entry *preserved_entries,
			       size_t preserved_entry_count)
{
	if (elf_tag == nullptr)
		return;

	const Elf32_Shdr *elf_sec = (const Elf32_Shdr *)elf_tag->sections;

	for (size_t i = 0; i < elf_tag->num; i++) {
		if ((elf_sec[i].sh_flags & ELF_SHF_ALLOC) == 0 || (void*)elf_sec[i].sh_addr < (void*)&HIGHER_HALF)
			continue;

		size_t elf_s = round_down_to_page(elf_sec[i].sh_addr);
		size_t elf_e = round_up_to_page(elf_sec[i].sh_addr + elf_sec[i].sh_size);
		uint8_t flags = (elf_sec[i].sh_flags & ELF_SHF_WRITE) * VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT;

		vmm_init_used_range((void *)elf_s, elf_e - elf_s, flags);
	}

	for (size_t i = 0; i < preserved_entry_count; i++) {
		struct vmm_entry entry = preserved_entries[i];
		vmm_init_used_range(entry.ptr, entry.size, entry.flags);
	}
}

void vmm_init(const struct multiboot_tag_elf_sections *elf_tag, const struct vmm_entry *preserved_entries, size_t preserved_entry_count)
{
	kernel_page_directory = (fatptr_t){
		.ptr = (void *)(uintptr_t)current_page_directory(), .len = PAGE_SIZE };
	struct vmm_entry init_vmm_entry[16 * sizeof(struct vmm_entry)] = { 0 };
	const size_t init_vmm_capacity = sizeof(init_vmm_entry) / sizeof(init_vmm_entry[0]);
	size_t init_vmm_entris_used = 0;
	LIST_HEAD(init_vmm_free_list);

	struct vmm_entry init_entry = {
		.ptr = (void *)round_up_to_page((uintptr_t)&HIGHER_HALF),
		.size = VMM_RECURSIVE_PT_BASE - round_up_to_page((uintptr_t)&HIGHER_HALF),
		.flags = 0,
	};
	if (init_entry.size == 0 || (uintptr_t)init_entry.ptr >= VMM_RECURSIVE_PT_BASE)
		panic("Invalid higher-half VMM range\n");
	RESET_LIST_ITEM(&init_entry.list);

	init_vmm_entry[init_vmm_entris_used] = init_entry;
	list_add(&init_vmm_entry[0].list, &init_vmm_free_list);
	init_vmm_entris_used++;

	const Elf32_Shdr *elf_sec = elf_tag == nullptr ? nullptr : (const Elf32_Shdr *)elf_tag->sections;
	const char *elf_sec_str = elf_tag == nullptr ? nullptr : (char *)elf_sec[elf_tag->shndx].sh_addr;
	for (size_t i = 0; elf_tag != nullptr && i < elf_tag->num; i++) {
#ifdef DEBUG
		mprint("Section (%s): [Address: %x, Size: %x, Type: %x, flags: %x]\n", &elf_sec_str[elf_sec[i].sh_name], elf_sec[i].sh_addr, elf_sec[i].sh_size,
		       elf_sec[i].sh_type, elf_sec[i].sh_flags);
#endif

		if ((elf_sec[i].sh_flags & ELF_SHF_ALLOC) == 0) {
#ifdef DEBUG
			mprint("Section (%s) dosn't allocate memory at runtime\n", &elf_sec_str[elf_sec[i].sh_name]);
#endif
			continue;
		}

		uintptr_t reserve_start = round_down_to_page(elf_sec[i].sh_addr);
		uintptr_t reserve_end = round_up_to_page(elf_sec[i].sh_addr + elf_sec[i].sh_size);
		if (reserve_end <= reserve_start || reserve_start < (uintptr_t)init_entry.ptr ||
		    reserve_start >= VMM_RECURSIVE_PT_BASE)
			continue;
		if (reserve_end > VMM_RECURSIVE_PT_BASE)
			reserve_end = VMM_RECURSIVE_PT_BASE;

		for (struct list_head *it = init_vmm_free_list.next;
		     it != &init_vmm_free_list;) {
			struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);
			struct list_head *next = it->next;

			/*
			 * Intersect the two range, and in case split
			 * the two found entry or remove it
			 */

			uintptr_t cur_s = (uintptr_t)cur->ptr;
			uintptr_t cur_e = cur_s + cur->size;
			if (reserve_start >= cur_e || cur_s >= reserve_end) {
				it = next;
				continue;
			}

			uintptr_t range_s = cur_s > reserve_start ? cur_s : reserve_start;
			uintptr_t range_e = cur_e < reserve_end ? cur_e : reserve_end;

			struct vmm_entry left_entry = {
				.ptr = (void *)cur_s,
				.size = range_s - cur_s,
			};
			RESET_LIST_ITEM(&left_entry.list);

			struct vmm_entry right_entry = {
				.ptr = (void *)range_e,
				.size = cur_e - range_e,
			};
			RESET_LIST_ITEM(&right_entry.list);

			if (left_entry.size > 0) {
				if (init_vmm_entris_used >= init_vmm_capacity)
					panic("Not enough space to track preserved left split\n");
				init_vmm_entry[init_vmm_entris_used] = left_entry;
				list_add(&init_vmm_entry[init_vmm_entris_used].list, cur->list.prev);
				init_vmm_entris_used++;
			}
			if (right_entry.size > 0) {
				if (init_vmm_entris_used >= init_vmm_capacity)
					panic("Not enough space to track preserved right split\n");
				init_vmm_entry[init_vmm_entris_used] = right_entry;
				list_add(&init_vmm_entry[init_vmm_entris_used].list, &cur->list);
				init_vmm_entris_used++;
			}
			list_rm(&cur->list);
			it = next;
		}
	}

	for (size_t i = 0; i < preserved_entry_count; i++) {
		if (init_vmm_entris_used >= init_vmm_capacity)
			panic("Not enough space to reserve preserved virtual ranges\n");

		uintptr_t range_s = round_down_to_page((uintptr_t)preserved_entries[i].ptr);
		uintptr_t range_e = round_up_to_page((uintptr_t)preserved_entries[i].ptr + preserved_entries[i].size);
		if (range_s < (uintptr_t)init_entry.ptr)
			range_s = (uintptr_t)init_entry.ptr;
		if (range_e > VMM_RECURSIVE_PT_BASE)
			range_e = VMM_RECURSIVE_PT_BASE;
		if (range_s >= range_e)
			continue;

		for (struct list_head *it = init_vmm_free_list.next;
		     it != &init_vmm_free_list;) {
			struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);
			struct list_head *next = it->next;

			uintptr_t cur_s = (uintptr_t)cur->ptr;
			uintptr_t cur_e = (uintptr_t)cur->ptr + cur->size;

			if (range_s > cur_e || cur_s >= range_e) {
				it = next;
				continue;
			}

			uintptr_t inter_s = cur_s > range_s ? cur_s : range_s;
			uintptr_t inter_e = cur_e < range_e ? cur_e : range_e;

			struct vmm_entry left_entry = {
				.ptr = (void*)cur_s,
				.size = inter_s - cur_s,
			};
			RESET_LIST_ITEM(&left_entry.list);

			struct vmm_entry right_entry = {
				.ptr = (void*)inter_e,
				.size = cur_e - inter_e,
			};
			RESET_LIST_ITEM(&right_entry.list);

			if (left_entry.size > 0) {
				init_vmm_entry[init_vmm_entris_used] = left_entry;
				list_add(&init_vmm_entry[init_vmm_entris_used].list, cur->list.prev);
				init_vmm_entris_used++;
			}
			if (right_entry.size > 0) {
				init_vmm_entry[init_vmm_entris_used] = right_entry;
				list_add(&init_vmm_entry[init_vmm_entris_used].list, &cur->list);
				init_vmm_entris_used++;
			}

			list_rm(&cur->list);
			it = next;
		}
	}

	recreate_vir_mem(elf_tag, preserved_entries, preserved_entry_count);
	init_vir_manager(&init_vmm_free_list);
}

void vmm_finish_init(const struct multiboot_tag_elf_sections *elf_tag, const struct vmm_entry *preserved_entries, size_t preserved_entry_count)
{
	migrate_tags_to_slab();
	vmm_init_used_list(elf_tag, preserved_entries, preserved_entry_count);
#ifdef DEBUG
	debug_vmm_lists();
#endif
}
