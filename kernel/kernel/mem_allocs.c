#include <kernel/vir_mem.h>
#include <kernel/phy_mem.h>
#include <kernel/display.h>
#include <kernel/mem_allocs.h>
#include <kernel/allocator.h>

#include <stddef.h>
#include <string.h>
#include <list.h>

MODULE("Memory Allocation")

fatptr_t mem_get_fatptr_phy_mem(const mem_phy_mem_tag_t *ptr)
{
	return ptr->phy_mem;
}
size_t mem_get_refcnt_phy_mem(const mem_phy_mem_tag_t *ptr)
{
	return ptr->ref_cnt;
}
fatptr_t mem_set_fatptr_phy_mem(mem_phy_mem_tag_t *ptr, fatptr_t val)
{
	fatptr_t old = ptr->phy_mem;
	ptr->phy_mem = val;
	return old;
}
size_t mem_set_refcnt_phy_mem(mem_phy_mem_tag_t *ptr, size_t val)
{
	size_t old = ptr->ref_cnt;
	ptr->ref_cnt = val;
	return old;
}

void *mem_get_ptr_tag(const mem_malloc_tag_t *ptr)
{
	return ptr->ptr;
}
size_t mem_get_size_tag(const mem_malloc_tag_t *ptr)
{
	return ptr->size;
}
size_t mem_get_used_tag(const mem_malloc_tag_t *ptr)
{
	return ptr->used;
}
struct vmm_entry *mem_get_vmm_tag(const mem_malloc_tag_t *ptr)
{
	return ptr->vmm;
}
struct list_head *mem_get_chain_tag(const mem_malloc_tag_t *ptr)
{
	return &ptr->phy_chain;
}
void *mem_set_ptr_tag(mem_malloc_tag_t *ptr, void *val)
{
	void *old = ptr->ptr;
	ptr->ptr = val;
	return old;
}
size_t mem_set_size_tag(mem_malloc_tag_t *ptr, size_t val)
{
	size_t old = ptr->size;
	ptr->size = val;
	return old;
}
size_t mem_set_used_tag(mem_malloc_tag_t *ptr, size_t val)
{
	size_t old = ptr->used;
	ptr->used = val;
	return old;
}
struct vmm_entry *mem_set_vmm_tag(mem_malloc_tag_t *ptr, struct vmm_entry *val)
{
	struct vmm_entry *old = ptr->vmm;
	ptr->vmm = val;
	return old;
}

static LIST_HEAD(tags_list);
static slab_cache_t *malloc_tag_cache = nullptr;
static slab_cache_t *phy_mem_tag_cache = nullptr;
static slab_cache_t *phy_mem_link_cache = nullptr;

#ifdef DEBUG
char *get_str_type_struct(type_struct_t type)
{
	switch (type) {
	case MANAGER:
		return "Manager";
	case USED:
		return "Used";
	case FREE:
		return "Free";
	default:
		panic("unreachable");
	}
}

void mem_debug_lists(void)
{
	mprint("debug_lists | malloc_tags_list:\n");
	list_for_each(&tags_list) {
		mem_malloc_tag_t *tag = list_entry(it, mem_malloc_tag_t, list);
		mprint("%x) ptr: %x | size: %x | used: %x | vmm: %x | type: %s\n", tag, tag->ptr, tag->size, tag->used, tag->vmm,
		       get_str_type_struct(tag->type));

		list_for_each(&tag->phy_chain) {
			mem_phy_mem_tag_t *phy_tag = list_entry(it, mem_phy_mem_link_t, list)->phy_mem;
			mprint("	%x) ptr: %x | size: %x | ref_cnt: %x | type: %s\n", phy_tag, phy_tag->phy_mem.ptr, phy_tag->phy_mem.len,
			       phy_tag->ref_cnt, get_str_type_struct(tag->type));
		}
	}
}
#endif

// Insert phy_mem_tag sorted by ptr in the chain
void mem_insert_phy_mem_tag(mem_phy_mem_tag_t *tag, struct list_head *chain, bool sort);
// Give the phy_mem_tag back to slab cache
void mem_give_phy_mem_tag(mem_phy_mem_tag_t *);
// Get the phy_mem_tag from slab cache
mem_phy_mem_tag_t *mem_get_phy_mem_tag();

// Give phy_mem_link* back to slab cache
void mem_give_phy_mem_link(mem_phy_mem_link_t *);
// Get phy_mem_link* from slab cache
mem_phy_mem_link_t *mem_get_phy_mem_link();
// Get the phy mem tag from a list of phy_mem_link
mem_phy_mem_tag_t *mem_get_phy_tag_from_link(struct list_head *link)
{
	mem_phy_mem_link_t *pml = list_entry(link, mem_phy_mem_link_t, list);
	return pml->phy_mem;
}

// Insert malloc_tag sorted by ptr in the chain
void mem_insert_tag(mem_malloc_tag_t *tag, struct list_head *list);
// Give malloc_tag* back to slab cache
void mem_give_tag(mem_malloc_tag_t *);
// Get malloc_tag* from slab cache
mem_malloc_tag_t *mem_get_tag();

// Align a byte offset for the tag slab layout.
// Assumes align is a power of two.
static size_t tag_slab_align_offset(size_t val, size_t align)
{
	return (val + (align - 1)) & ~(align - 1);
}

// Compute the tag slab object stride after alignment and minimum size checks.
// Assumes obj_size is non-zero.
static size_t tag_slab_obj_stride(size_t obj_size, size_t align)
{
	size_t real_align = align < sizeof(void *) ? sizeof(void *) : align;
	size_t aligned_size = tag_slab_align_offset(obj_size, real_align);
	if (aligned_size < sizeof(void *))
		aligned_size = sizeof(void *);
	return aligned_size;
}

void init_mem_alloc_tag_slabs(void)
{
	struct vmm_entry *tags_virt = vmm_alloc(PAGE_SIZE, VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT);
	if (tags_virt == nullptr)
		BUG("failed to allocate tag slab page");

	fatptr_t tags_phy = phy_mem_alloc(tags_virt->size);
	if (tags_phy.ptr == nullptr)
		BUG("failed to allocate tag slab physical memory");

	map_pages(&tags_phy, tags_virt);
	memset(tags_virt->ptr, 0, tags_virt->size);

	size_t per_section = tags_virt->size / 3;
	uint8_t *base = tags_virt->ptr;
	size_t offset = 0;

	size_t malloc_tag_size = tag_slab_obj_stride(sizeof(mem_malloc_tag_t), _Alignof(mem_malloc_tag_t));
	offset = tag_slab_align_offset(offset, _Alignof(mem_malloc_tag_t));
	size_t malloc_tag_count = per_section / malloc_tag_size;
	mem_malloc_tag_t *malloc_tags = (mem_malloc_tag_t *)(base + offset);
	offset += malloc_tag_count * malloc_tag_size;

	size_t phy_mem_tag_size = tag_slab_obj_stride(sizeof(mem_phy_mem_tag_t), _Alignof(mem_phy_mem_tag_t));
	offset = tag_slab_align_offset(offset, _Alignof(mem_phy_mem_tag_t));
	size_t phy_mem_tag_count = per_section / phy_mem_tag_size;
	mem_phy_mem_tag_t *phy_mem_tags = (mem_phy_mem_tag_t *)(base + offset);
	offset += phy_mem_tag_count * phy_mem_tag_size;

	size_t phy_mem_link_size = tag_slab_obj_stride(sizeof(mem_phy_mem_link_t), _Alignof(mem_phy_mem_link_t));
	offset = tag_slab_align_offset(offset, _Alignof(mem_phy_mem_link_t));
	size_t remaining = tags_virt->size - offset;
	size_t phy_mem_link_count = remaining / phy_mem_link_size;
	mem_phy_mem_link_t *phy_mem_links = (mem_phy_mem_link_t *)(base + offset);

	if (malloc_tag_count == 0 || phy_mem_tag_count == 0 || phy_mem_link_count == 0)
		BUG("tag slab page is too small for bootstrap caches");

	slab_init_tag_caches(malloc_tags, malloc_tag_count, phy_mem_tags, phy_mem_tag_count, phy_mem_links, phy_mem_link_count);

	malloc_tag_cache = slab_get_malloc_tag_cache();
	phy_mem_tag_cache = slab_get_phy_mem_tag_cache();
	phy_mem_link_cache = slab_get_phy_mem_link_cache();

	if (malloc_tag_cache == nullptr || phy_mem_tag_cache == nullptr || phy_mem_link_cache == nullptr)
		BUG("tag slab caches failed to initialize");
}

void mem_give_tag(mem_malloc_tag_t *tag)
{
	if (malloc_tag_cache == nullptr)
		BUG("malloc tag cache not initialized");

	struct list_head *rm_chain_elm;
	while (rm_chain_elm = list_pop(&tag->phy_chain), rm_chain_elm != nullptr) {
		mem_phy_mem_link_t *link = list_entry(rm_chain_elm, mem_phy_mem_link_t, list);
		mem_give_phy_mem_link(link);
	}

	list_rm(&tag->list);

	memset(tag, 0, sizeof(mem_malloc_tag_t));

#ifdef DEBUG
	tag->type = FREE;
#endif
	slab_free_obj(malloc_tag_cache, (fatptr_t){ .ptr = tag, .len = sizeof(*tag) });
}

mem_malloc_tag_t *mem_get_tag()
{
	if (malloc_tag_cache == nullptr)
		BUG("malloc tag cache not initialized");

	fatptr_t tag_obj = slab_alloc_obj(malloc_tag_cache);
	if (tag_obj.ptr == nullptr)
		return nullptr;

	mem_malloc_tag_t *tag = tag_obj.ptr;
	RESET_LIST_ITEM(&tag->phy_chain);
	RESET_LIST_ITEM(&tag->list);

#ifdef DEBUG
	tag->type = USED;
#endif

	return tag;
}

void mem_give_phy_mem_link(mem_phy_mem_link_t *link)
{
	if (phy_mem_link_cache == nullptr)
		BUG("phy mem link cache not initialized");

	if (link->phy_mem != nullptr) {
		mem_phy_mem_tag_t *phy_mem = link->phy_mem;
		mem_give_phy_mem_tag(phy_mem);
	}

	list_rm(&link->list);
	slab_free_obj(phy_mem_link_cache, (fatptr_t){ .ptr = link, .len = sizeof(*link) });

#ifdef DEBUG
	link->type = FREE;
#endif
}

mem_phy_mem_link_t *mem_get_phy_mem_link()
{
	if (phy_mem_link_cache == nullptr)
		BUG("phy mem link cache not initialized");

	fatptr_t link_obj = slab_alloc_obj(phy_mem_link_cache);
	if (link_obj.ptr == nullptr)
		return nullptr;

	mem_phy_mem_link_t *tag = link_obj.ptr;
	RESET_LIST_ITEM(&tag->list);

#ifdef DEBUG
	tag->type = USED;
#endif

	return tag;
}

void mem_give_phy_mem_tag(mem_phy_mem_tag_t *tag)
{
	if (phy_mem_tag_cache == nullptr)
		BUG("phy mem tag cache not initialized");

	tag->ref_cnt -= 1;
	if (tag->ref_cnt != 0)
		return;
	slab_free_obj(phy_mem_tag_cache, (fatptr_t){ .ptr = tag, .len = sizeof(*tag) });

#ifdef DEBUG
	tag->type = FREE;
#endif
}

mem_phy_mem_tag_t *mem_get_phy_mem_tag()
{
	if (phy_mem_tag_cache == nullptr)
		BUG("phy mem tag cache not initialized");

	fatptr_t tag_obj = slab_alloc_obj(phy_mem_tag_cache);
	if (tag_obj.ptr == nullptr)
		return nullptr;

	mem_phy_mem_tag_t *tag = tag_obj.ptr;

#ifdef DEBUG
	tag->type = USED;
#endif

	return tag;
}

void mem_insert_phy_mem_tag(mem_phy_mem_tag_t *tag, struct list_head *chain, bool sort)
{
	mem_phy_mem_link_t *link = mem_get_phy_mem_link();
	link->phy_mem = tag;

	if (!sort) {
		list_add(&link->list, chain->prev);
		return;
	}

	mem_phy_mem_link_t *prev = nullptr;

	list_for_each(chain) {
		mem_phy_mem_link_t *cur = list_entry(it, mem_phy_mem_link_t, list);
		fatptr_t cur_mem = cur->phy_mem->phy_mem;
		fatptr_t tag_mem = tag->phy_mem;
		if (cur_mem.ptr < tag_mem.ptr && (prev == nullptr || (prev != nullptr && cur_mem.ptr > prev->phy_mem->phy_mem.ptr)))
			prev = cur;
	}
	if (prev == nullptr)
		list_add(&link->list, chain);
	else
		list_add(&link->list, &prev->list);
}

void mem_insert_tag(mem_malloc_tag_t *tag, struct list_head *list)
{
	mem_malloc_tag_t *prev = nullptr;
	list_for_each(list) {
		mem_malloc_tag_t *cur = list_entry(it, mem_malloc_tag_t, list);
		if (cur->ptr < tag->ptr && (prev == nullptr || (prev != nullptr && cur->ptr > prev->ptr)))
			prev = cur;
	}
	if (prev == nullptr)
		list_add(&tag->list, list);
	else
		list_add(&tag->list, &prev->list);
}

bool mem_remove_tag(mem_malloc_tag_t *tag, struct list_head *list)
{
	list_for_each(list) {
		mem_malloc_tag_t *cur = list_entry(it, mem_malloc_tag_t, list);
		if (cur->ptr == tag->ptr && cur->size == tag->size && cur->used == tag->used) {
			mem_give_tag(cur);
			return false;
		}
	}
	return true;
}

void init_kmalloc(void)
{
	init_mem_alloc_tag_slabs();

#ifdef DEBUG
	mem_debug_lists();
#endif
}

mem_malloc_tag_t *mem_find_best_fit(size_t req)
{
	mem_malloc_tag_t *tag = nullptr;
	list_for_each(&tags_list) {
		mem_malloc_tag_t *it_tag = list_entry(it, mem_malloc_tag_t, list);
		size_t free_space = it_tag->size - it_tag->used;
		if ((tag != nullptr && free_space >= req && free_space < tag->size - tag->used) || (tag == nullptr && free_space >= req)) {
			tag = it_tag;
		}
	}
	return tag;
}

void mem_register_tag(mem_malloc_tag_t *tag)
{
	mem_insert_tag(tag, &tags_list);
}

mem_malloc_tag_t *mem_coalesce_tag(mem_malloc_tag_t *tag)
{
	mem_malloc_tag_t *prev = list_entry(tag->list.prev, mem_malloc_tag_t, list);
	mem_malloc_tag_t *next = list_entry(tag->list.next, mem_malloc_tag_t, list);

	bool coalesce_prev = prev->ptr + prev->size == tag->ptr && prev->vmm == tag->vmm;
	bool coalesce_next = tag->ptr + tag->size == next->ptr && next->used == 0 && next->vmm == tag->vmm;

	mem_malloc_tag_t *mem_to_free = tag;

	tag->used = 0;
	if (coalesce_next) {
		tag->size += next->size;
		list_for_each(&next->phy_chain) {
			mem_phy_mem_link_t *phy_link = list_entry(it, mem_phy_mem_link_t, list);
			mem_phy_mem_tag_t *phy_tag = phy_link->phy_mem;

			list_for_each(&tag->phy_chain) {
				mem_phy_mem_link_t *phy_link2 = list_entry(it, mem_phy_mem_link_t, list);
				mem_phy_mem_tag_t *phy_tag2 = phy_link2->phy_mem;
				if (phy_tag == phy_tag2)
					goto continue_next;
			}

			struct list_head *elm = next->phy_chain.prev;
			list_rm(elm);
			list_add(elm, tag->phy_chain.prev);
continue_next:
		}
	}

	if (coalesce_prev) {
		prev->size += tag->size;
		list_for_each(&tag->phy_chain) {
			mem_phy_mem_link_t *phy_link = list_entry(it, mem_phy_mem_link_t, list);
			mem_phy_mem_tag_t *phy_tag = phy_link->phy_mem;

			list_for_each(&prev->phy_chain) {
				mem_phy_mem_link_t *phy_link2 = list_entry(it, mem_phy_mem_link_t, list);
				mem_phy_mem_tag_t *phy_tag2 = phy_link2->phy_mem;
				if (phy_tag == phy_tag2)
					goto continue_curr;
			}

			struct list_head *elm = next->phy_chain.prev;
			list_rm(elm);
			list_add(elm, tag->phy_chain.prev);
continue_curr:
		}
	}

	if (prev->used == 0) {
		mem_unregister_tag(tag);
		return prev;
	} else {
		return tag;
	}
}

void mem_unregister_tag(mem_malloc_tag_t *tag)
{
	mem_remove_tag(tag, &tags_list);
}
