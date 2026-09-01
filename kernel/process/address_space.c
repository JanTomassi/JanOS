#include <kernel/process/address_space.h>

#include <kernel/allocator.h>
#include <kernel/vir_mem.h>
#include <list.h>
#include <string.h>

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
};

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

	space->page_directory = phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH);
	if (space->page_directory.ptr == nullptr) {
		object_free(space, sizeof(*space));
		return nullptr;
	}
	memset(space->page_directory.ptr, 0, PAGE_SIZE);
	RESET_LIST_ITEM(&space->mappings);
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

	struct address_mapping *mapping = object_alloc(sizeof(*mapping));
	if (mapping == nullptr)
		return false;
	mapping->virtual_range = vmm_alloc(size, flags | VMM_ENTRY_PRESENT_BIT);
	if (mapping->virtual_range == nullptr) {
		object_free(mapping, sizeof(*mapping));
		return false;
	}
	*address = mapping->virtual_range->ptr;
	RESET_LIST_ITEM(&mapping->physical_pages);
	struct address_page *page = nullptr;
	for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
		page = object_alloc(sizeof(*page));
		if (page == nullptr)
			goto fail;
		page->physical = phy_mem_alloc(PAGE_SIZE, PHY_MEM_ALLOC_HIGH);
		if (page->physical.ptr == nullptr)
			goto fail;
		struct vmm_entry one_page = { .ptr = *address + offset, .size = PAGE_SIZE,
			.flags = flags | VMM_ENTRY_PRESENT_BIT };
		RESET_LIST_ITEM(&one_page.list);
		map_pages(&page->physical, &one_page);
		list_add(&page->list, &mapping->physical_pages);
		page = nullptr;
	}
	list_add(&mapping->list, &space->mappings);
	return true;

fail:
	if (page != nullptr && page->physical.ptr != nullptr)
		phy_mem_free(page->physical);
	if (page != nullptr)
		object_free(page, sizeof(*page));
	while (mapping->physical_pages.next != &mapping->physical_pages) {
		struct address_page *old = list_pop_entry(&mapping->physical_pages,
			struct address_page, list);
		phy_mem_free(old->physical);
		object_free(old, sizeof(*old));
	}
	vmm_free(mapping->virtual_range->ptr);
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
		while (mapping->physical_pages.next != &mapping->physical_pages) {
			struct address_page *page = list_pop_entry(&mapping->physical_pages,
				struct address_page, list);
			unmap_page(nullptr, mapping->virtual_range->ptr);
			phy_mem_free(page->physical);
			object_free(page, sizeof(*page));
			mapping->virtual_range->ptr += PAGE_SIZE;
		}
		mapping->virtual_range->ptr = address;
		vmm_free(address);
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
	phy_mem_free(space->page_directory);
	object_free(space, sizeof(*space));
}
