#include <kernel/process/address_space.h>

#include <kernel/allocator.h>
#include <kernel/vir_mem.h>
#include <list.h>

struct address_mapping {
	struct vmm_entry *virtual_range;
	struct list_head physical_pages;
	struct list_head list;
};

struct address_page {
	fatptr_t physical;
	struct list_head list;
};

struct address_space {
	fatptr_t page_directory;
	struct list_head mappings;
	uintptr_t next_user_address;
};

#define USER_ADDRESS_START 0x00100000u
#define USER_ADDRESS_LIMIT 0xc0000000u

static allocator_t object_allocator;
static bool allocator_ready;

static void ensure_allocator(void)
{
	if (!allocator_ready) {
		object_allocator = get_slab_allocator();
		allocator_ready = true;
	}
}

static void *object_alloc(size_t size)
{
	ensure_allocator();
	return object_allocator.alloc(size).ptr;
}

static void object_free(void *ptr, size_t size)
{
	if (ptr != nullptr)
		object_allocator.free((fatptr_t){ .ptr = ptr, .len = size });
}

struct address_space *address_space_create(void)
{
	struct address_space *space = object_alloc(sizeof(*space));
	if (space == nullptr)
		return nullptr;

	uintptr_t page_directory;
	__asm__ volatile("mov %%cr3, %0" : "=r"(page_directory) : : "memory");
	if (page_directory == 0) {
		object_free(space, sizeof(*space));
		return nullptr;
	}
	space->page_directory = (fatptr_t){ .ptr = (void *)page_directory,
		.len = PAGE_SIZE };
	RESET_LIST_ITEM(&space->mappings);
	space->next_user_address = USER_ADDRESS_START;
	return space;
}

const fatptr_t *address_space_page_directory(const struct address_space *space)
{
	return space == nullptr ? nullptr : &space->page_directory;
}

bool address_space_map(struct address_space *space, size_t size, uint16_t flags,
                       void **address)
{
	if (space == nullptr || address == nullptr || size == 0 ||
	    (size & (PAGE_SIZE - 1)) != 0)
		return false;

	uintptr_t candidate = round_up_to_page(space->next_user_address);
	if (candidate < USER_ADDRESS_START || size > USER_ADDRESS_LIMIT - candidate)
		return false;
	if (!address_space_map_at(space, candidate, size, flags, address))
		return false;
	space->next_user_address = candidate + size;
	return true;
}

static bool mapping_overlaps(const struct address_space *space, uintptr_t start,
	                            uintptr_t end)
{
	list_for_each(&space->mappings) {
		const struct address_mapping *mapping =
			list_entry(it, const struct address_mapping, list);
		uintptr_t mapping_start = (uintptr_t)mapping->virtual_range->ptr;
		uintptr_t mapping_end = mapping_start + mapping->virtual_range->size;
		if (start < mapping_end && mapping_start < end)
			return true;
	}
	return false;
}

bool address_space_map_at(struct address_space *space, uintptr_t address,
                          size_t size, uint16_t flags, void **mapped)
{
	if (space == nullptr || mapped == nullptr || size == 0 ||
	    (address & (PAGE_SIZE - 1)) != 0 || (size & (PAGE_SIZE - 1)) != 0 ||
	    (flags & VMM_ENTRY_USER_SUPER_BIT) == 0 ||
	    address < PAGE_SIZE || address >= USER_ADDRESS_LIMIT ||
	    size > USER_ADDRESS_LIMIT - address ||
	    mapping_overlaps(space, address, address + size))
		return false;

	struct address_mapping *mapping = object_alloc(sizeof(*mapping));
	if (mapping == nullptr)
		return false;
	mapping->virtual_range = object_alloc(sizeof(*mapping->virtual_range));
	if (mapping->virtual_range == nullptr) {
		object_free(mapping, sizeof(*mapping));
		return false;
	}
	*mapping->virtual_range = (struct vmm_entry){
		.ptr = (void *)address,
		.size = size,
		.flags = flags | VMM_ENTRY_PRESENT_BIT,
	};
	RESET_LIST_ITEM(&mapping->virtual_range->list);
	*mapped = mapping->virtual_range->ptr;
	RESET_LIST_ITEM(&mapping->physical_pages);
	struct address_page *page = nullptr;
	size_t mapped_pages = 0;
	for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
		page = object_alloc(sizeof(*page));
		if (page == nullptr)
			goto fail;
		page->physical = phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH);
		if (page->physical.ptr == nullptr)
			goto fail;
		struct vmm_entry one_page = { .ptr = (void *)(address + offset), .size = PAGE_SIZE,
			.flags = flags | VMM_ENTRY_PRESENT_BIT };
		RESET_LIST_ITEM(&one_page.list);
		map_pages(&page->physical, &one_page);
		list_add(&page->list, &mapping->physical_pages);
		++mapped_pages;
		page = nullptr;
	}
	list_add(&mapping->list, &space->mappings);
	return true;

fail:
	if (page != nullptr && page->physical.ptr != nullptr)
		phy_mem_free(page->physical);
	if (page != nullptr)
		object_free(page, sizeof(*page));
	for (uintptr_t mapped = address; mapped < address + mapped_pages * PAGE_SIZE;
	     mapped += PAGE_SIZE)
		unmap_page(nullptr, (void *)mapped);
	while (mapping->physical_pages.next != &mapping->physical_pages) {
		struct address_page *old = list_pop_entry(&mapping->physical_pages,
			struct address_page, list);
		phy_mem_free(old->physical);
		object_free(old, sizeof(*old));
	}
	object_free(mapping->virtual_range, sizeof(*mapping->virtual_range));
	object_free(mapping, sizeof(*mapping));
	return false;
}

bool address_space_unmap(struct address_space *space, void *address)
{
	if (space == nullptr || address == nullptr)
		return false;
	for (struct list_head *it = space->mappings.next; it != &space->mappings;
	     it = it->next) {
		struct address_mapping *mapping = list_entry(it, struct address_mapping, list);
		if (mapping->virtual_range->ptr != address)
			continue;
		list_rm(&mapping->list);
		uintptr_t mapped = (uintptr_t)address;
		while (mapping->physical_pages.next != &mapping->physical_pages) {
			struct address_page *page = list_pop_entry(&mapping->physical_pages,
				struct address_page, list);
			unmap_page(nullptr, (void *)mapped);
			phy_mem_free(page->physical);
			object_free(page, sizeof(*page));
			mapped += PAGE_SIZE;
		}
		object_free(mapping->virtual_range, sizeof(*mapping->virtual_range));
		object_free(mapping, sizeof(*mapping));
		return true;
	}
	return false;
}

void address_space_destroy(struct address_space *space)
{
	if (space == nullptr)
		return;
	while (space->mappings.next != &space->mappings) {
		struct address_mapping *mapping = list_first_entry(&space->mappings,
			struct address_mapping, list);
		address_space_unmap(space, mapping->virtual_range->ptr);
	}
	object_free(space, sizeof(*space));
}
