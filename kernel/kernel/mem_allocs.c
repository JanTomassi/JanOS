#include <kernel/vir_mem.h>
#include <kernel/phy_mem.h>
#include <kernel/display.h>
#include <kernel/mem_allocs.h>

#include <stddef.h>
#include <string.h>
#include <list.h>

MODULE("Memory Allocation")

typedef mem_malloc_tag_t malloc_tag_t;
typedef mem_phy_mem_link_t phy_mem_link_t;
typedef mem_phy_mem_tag_t phy_mem_tag_t;

#ifdef DEBUG
typedef enum {
	MANAGER,
	USED,
	FREE,
} type_struct_t;
#endif

struct mem_malloc_tag {
	void *ptr; // Virtual address for specific allocaiton
	size_t size; // Total block size
	size_t used; // Sized used by this block
	struct vmm_entry *vmm;
	struct mem_malloc_tag *tag_manager;

	// head of type struct phy_mem_link
	struct list_head phy_chain;
	// entry of type struct malloc_tag
	struct list_head list;

#ifdef DEBUG
	type_struct_t type;
#endif
};

struct mem_phy_mem_link {
	struct mem_phy_mem_tag *phy_mem;
	struct mem_malloc_tag *tag_manager;

	// entry of type struct phy_mem_link
	struct list_head list;

#ifdef DEBUG
	type_struct_t type;
#endif
};

struct mem_phy_mem_tag {
	fatptr_t phy_mem;
	size_t ref_cnt;

	struct mem_malloc_tag *tag_manager;

#ifdef DEBUG
	type_struct_t type;
#endif
};

fatptr_t mem_get_fatptr_phy_mem(const phy_mem_tag_t *ptr)
{
	return ptr->phy_mem;
}
size_t mem_get_refcnt_phy_mem(const phy_mem_tag_t *ptr)
{
	return ptr->ref_cnt;
}
fatptr_t mem_set_fatptr_phy_mem(phy_mem_tag_t *ptr, fatptr_t val)
{
	fatptr_t old = ptr->phy_mem;
	ptr->phy_mem = val;
	return old;
}
size_t mem_set_refcnt_phy_mem(phy_mem_tag_t *ptr, size_t val)
{
	size_t old = ptr->ref_cnt;
	ptr->ref_cnt = val;
	return old;
}

void *mem_get_ptr_tag(const malloc_tag_t *ptr)
{
	return ptr->ptr;
}
size_t mem_get_size_tag(const malloc_tag_t *ptr)
{
	return ptr->size;
}
size_t mem_get_used_tag(const malloc_tag_t *ptr)
{
	return ptr->used;
}
struct vmm_entry *mem_get_vmm_tag(const malloc_tag_t *ptr)
{
	return ptr->vmm;
}
struct list_head *mem_get_chain_tag(const malloc_tag_t *ptr)
{
	return &ptr->phy_chain;
}
void *mem_set_ptr_tag(malloc_tag_t *ptr, void *val)
{
	void *old = ptr->ptr;
	ptr->ptr = val;
	return old;
}
size_t mem_set_size_tag(malloc_tag_t *ptr, size_t val)
{
	size_t old = ptr->size;
	ptr->size = val;
	return old;
}
size_t mem_set_used_tag(malloc_tag_t *ptr, size_t val)
{
	size_t old = ptr->used;
	ptr->used = val;
	return old;
}
struct vmm_entry *mem_set_vmm_tag(malloc_tag_t *ptr, struct vmm_entry *val)
{
	struct vmm_entry *old = ptr->vmm;
	ptr->vmm = val;
	return old;
}

static LIST_HEAD(tags_list);

static LIST_HEAD(free_tags_list);
static LIST_HEAD(free_phy_tags_list);
static LIST_HEAD(free_phy_links_list);

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
	}
}

void mem_debug_lists(void)
{
	mprint("debug_lists | malloc_tags_list:\n");
	list_for_each(&tags_list) {
		malloc_tag_t *tag = list_entry(it, malloc_tag_t, list);
		mprint("%x) ptr: %x | size: %x | used: %x | vmm: %x | manager: %x | type: %s\n",
		       tag, tag->ptr, tag->size, tag->used, tag->vmm,
		       (size_t)tag->tag_manager,
		       get_str_type_struct(tag->type));

		list_for_each(&tag->phy_chain) {
			phy_mem_tag_t *phy_tag =
				list_entry(it, phy_mem_link_t, list)->phy_mem;
			mprint("	%x) ptr: %x | size: %x | ref_cnt: %x | manager: %x | type: %s\n",
			       phy_tag, phy_tag->phy_mem.ptr,
			       phy_tag->phy_mem.len, phy_tag->ref_cnt,
			       (size_t)tag->tag_manager,
			       get_str_type_struct(tag->type));
		}
	}

	size_t i = 0;
	mprint("debug_lists | malloc_free_tags_list:\n");
	list_for_each(&free_tags_list) {
		i += 1;
	}
	mprint("    %u unused malloc_tag\n", i);

	i = 0;
	mprint("debug_lists | malloc_free_phy_tags_list:\n");
	list_for_each(&free_phy_tags_list) {
		i += 1;
	}
	mprint("    %u unused phy_mem_tag\n", i);

	i = 0;
	mprint("debug_lists | malloc_free_phy_links_list:\n");
	list_for_each(&free_phy_links_list) {
		i += 1;
	}
	mprint("    %u unused phy_mem_link\n", i);
}
#endif

// Increment ref_cnt of phy mem
static void register_to_manager(malloc_tag_t *);
// Decrement ref_cnt of phy mem
static void unregister_to_manager(malloc_tag_t *);

// New allocated phy_mem_tag will be added to free_phy_tags_list
static void alloc_phy_mem_tags(void);
// Free allocated phy_mem_tags
static void free_phy_mem_tags(malloc_tag_t *);
// Insert phy_mem_tag sorted by ptr in the chain
void mem_insert_phy_mem_tag(phy_mem_tag_t *tag, struct list_head *chain,
			    bool sort);
// Give the phy_mem_tag back to free_phy_tags_list
void mem_give_phy_mem_tag(phy_mem_tag_t *);
// Get the phy_mem_tag from free_phy_tags_list
phy_mem_tag_t *mem_get_phy_mem_tag();

// New allocated phy_mem_link will be added to free_phy_links_list
static void alloc_phy_mem_links(void);
// Free allocated phy_mem_links
static void free_phy_mem_links(malloc_tag_t *);
// Give phy_mem_link* back to free_phy_links_list
void mem_give_phy_mem_link(phy_mem_link_t *);
// Get phy_mem_link* from free_phy_links_list
phy_mem_link_t *mem_get_phy_mem_link();
// Get the phy mem tag from a list of phy_mem_link
phy_mem_tag_t *mem_get_phy_tag_from_link(struct list_head *link)
{
	return list_entry(link, phy_mem_link_t, list)->phy_mem;
}

// New allocated malloc_tag will be added to free_tags_list
static void alloc_tags(void);
// Free allocated malloc_tags
static void free_tags(malloc_tag_t *);
// Insert malloc_tag sorted by ptr in the chain
void mem_insert_tag(malloc_tag_t *tag, struct list_head *list);
// Give malloc_tag* back to free_tags_list
void mem_give_tag(malloc_tag_t *);
// Get malloc_tag* from free_tags_list
malloc_tag_t *mem_get_tag();

static void register_to_manager(malloc_tag_t *manager)
{
	if (manager == nullptr)
		BUG("manager must not be nullptr")

	if (manager->phy_chain.next == &manager->phy_chain) {
		BUG("manager does not manage memory")
	}

	phy_mem_tag_t *phy_mem =
		mem_get_phy_tag_from_link(manager->phy_chain.next);
	phy_mem->ref_cnt += 1;
}

static void unregister_to_manager(malloc_tag_t *manager)
{
	if (manager == nullptr)
		BUG("manager must not be nullptr")

	if (manager->phy_chain.next == &manager->phy_chain) {
		BUG("manager does not manage memory")
	}

	phy_mem_tag_t *phy_mem =
		mem_get_phy_tag_from_link(manager->phy_chain.next);
	phy_mem->ref_cnt -= 1;

	bool rec_manager = manager->tag_manager == manager;

	if (rec_manager && phy_mem->ref_cnt <= 1) {
		mem_give_tag(manager);
	} else if (!rec_manager && phy_mem->ref_cnt == 0) {
		mem_give_tag(manager);
	}
}

void mem_give_tag(malloc_tag_t *tag)
{
	struct list_head *rm_chain_elm;
	while (rm_chain_elm = list_pop(&tag->phy_chain),
	       rm_chain_elm != nullptr) {
		phy_mem_link_t *link =
			list_entry(rm_chain_elm, phy_mem_link_t, list);
		mem_give_phy_mem_link(link);
	}

	unregister_to_manager(tag->tag_manager);

	list_rm(&tag->list);

	malloc_tag_t *manager = tag->tag_manager;

	memset(tag, 0, sizeof(malloc_tag_t));

	tag->tag_manager = manager;

#ifdef DEBUG
	tag->type = FREE;
#endif

	list_add(&tag->list, &free_tags_list);
}

malloc_tag_t *mem_get_tag()
{
	struct list_head *tag_elm = list_pop(&free_tags_list);

	if (tag_elm == nullptr) {
		alloc_tags();
		tag_elm = list_pop(&free_tags_list);
	}

	malloc_tag_t *tag = list_entry(tag_elm, malloc_tag_t, list);
	RESET_LIST_ITEM(&tag->phy_chain);

#ifdef DEBUG
	tag->type = USED;
#endif

	if (tag->tag_manager != nullptr) {
		register_to_manager(tag->tag_manager);
	}

	return tag;
}

void mem_give_phy_mem_link(phy_mem_link_t *link)
{
	if (link->phy_mem != nullptr) {
		phy_mem_tag_t *phy_mem = link->phy_mem;
		mem_give_phy_mem_tag(phy_mem);
	}

	unregister_to_manager(link->tag_manager);

	list_rm(&link->list);

	list_add(&link->list, &free_phy_links_list);

#ifdef DEBUG
	link->type = FREE;
#endif
}

phy_mem_link_t *mem_get_phy_mem_link()
{
	struct list_head *tag_elm = list_pop(&free_phy_links_list);

	if (tag_elm == nullptr) {
		alloc_phy_mem_links();
		tag_elm = list_pop(&free_phy_links_list);
	}

	phy_mem_link_t *tag = list_entry(tag_elm, phy_mem_link_t, list);

	if (tag->tag_manager != nullptr) {
		register_to_manager(tag->tag_manager);
	}

#ifdef DEBUG
	tag->type = USED;
#endif

	return tag;
}

void mem_give_phy_mem_tag(phy_mem_tag_t *tag)
{
	tag->ref_cnt -= 1;
	if (tag->ref_cnt != 0)
		return;

	unregister_to_manager(tag->tag_manager);

	phy_mem_link_t *link = mem_get_phy_mem_link();
	link->phy_mem = tag;

	list_add(&link->list, &free_phy_tags_list);

#ifdef DEBUG
	tag->type = FREE;
#endif
}

phy_mem_tag_t *mem_get_phy_mem_tag()
{
	struct list_head *tag_elm = list_pop(&free_phy_tags_list);

	if (tag_elm == nullptr) {
		alloc_phy_mem_tags();
		tag_elm = list_pop(&free_phy_tags_list);
	}

	phy_mem_link_t *tag_link = list_entry(tag_elm, phy_mem_link_t, list);
	phy_mem_tag_t *tag = tag_link->phy_mem;

	tag_link->phy_mem = nullptr;
	mem_give_phy_mem_link(tag_link);

	if (tag->tag_manager != nullptr) {
		register_to_manager(tag->tag_manager);
	}

#ifdef DEBUG
	tag->type = USED;
#endif

	return tag;
}

void mem_insert_phy_mem_tag(phy_mem_tag_t *tag, struct list_head *chain,
			    bool sort)
{
	phy_mem_link_t *link = mem_get_phy_mem_link();
	link->phy_mem = tag;

	if (!sort) {
		list_add(&link->list, chain->prev);
		return;
	}

	phy_mem_link_t *prev = nullptr;

	list_for_each(chain) {
		phy_mem_link_t *cur = list_entry(it, phy_mem_link_t, list);
		fatptr_t cur_mem = cur->phy_mem->phy_mem;
		fatptr_t tag_mem = tag->phy_mem;
		if (cur_mem.ptr < tag_mem.ptr &&
		    (prev == nullptr ||
		     (prev != nullptr &&
		      cur_mem.ptr > prev->phy_mem->phy_mem.ptr)))
			prev = cur;
	}
	if (prev == nullptr)
		list_add(&link->list, chain);
	else
		list_add(&link->list, &prev->list);
}

void mem_insert_tag(malloc_tag_t *tag, struct list_head *list)
{
	malloc_tag_t *prev = nullptr;
	list_for_each(list) {
		malloc_tag_t *cur = list_entry(it, malloc_tag_t, list);
		if (cur->ptr < tag->ptr &&
		    (prev == nullptr ||
		     (prev != nullptr && cur->ptr > prev->ptr)))
			prev = cur;
	}
	if (prev == nullptr)
		list_add(&tag->list, list);
	else
		list_add(&tag->list, &prev->list);
}

bool mem_remove_tag(malloc_tag_t *tag, struct list_head *list)
{
	list_for_each(list) {
		malloc_tag_t *cur = list_entry(it, malloc_tag_t, list);
		if (cur->ptr == tag->ptr && cur->size == tag->size &&
		    cur->used == tag->used) {
			mem_give_tag(cur);
			return false;
		}
	}
	return true;
}

static void alloc_phy_mem_tags(void)
{
	struct vmm_entry *new_tags_virt = vir_mem_alloc(
		PAGE_SIZE, VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT);
	size_t new_tags_virt_count =
		new_tags_virt->size / sizeof(phy_mem_tag_t);

	fatptr_t new_tags_phy = phy_mem_alloc(new_tags_virt->size);

	map_pages(&new_tags_phy, new_tags_virt);

	// Adding all the allocated tags to the chain
	for (size_t i = 0; i < new_tags_virt_count; i++) {
		phy_mem_tag_t new_tag = { 0 };
#ifdef DEBUG
		new_tag.type = FREE;
#endif
		((phy_mem_tag_t *)new_tags_virt->ptr)[i] = new_tag;

		phy_mem_link_t *tag_link = mem_get_phy_mem_link();
		tag_link->phy_mem = &((phy_mem_tag_t *)new_tags_virt->ptr)[i];

		list_add(&tag_link->list, &free_phy_tags_list);
	}

	// Get one unused tag
	malloc_tag_t *tag = mem_get_tag();
	phy_mem_tag_t *tag_phy = mem_get_phy_mem_tag();
	phy_mem_link_t *tag_link = mem_get_phy_mem_link();

	// Copy the info about the tag chunk
	tag->ptr = new_tags_virt->ptr;
	tag->size = new_tags_virt->size;
	tag->used = new_tags_virt_count * sizeof(phy_mem_tag_t);
	tag->vmm = new_tags_virt;

	tag_phy->phy_mem = new_tags_phy;
	tag_phy->ref_cnt = &tag >= tag->ptr && tag < tag->ptr + tag->size;

	tag_link->phy_mem = tag_phy;
	list_add(&tag_link->list, &tag->phy_chain);

	mem_insert_tag(tag, &tags_list);

	for (size_t i = 0; i < new_tags_virt_count; i++) {
		(((phy_mem_tag_t *)new_tags_virt->ptr)[i]).tag_manager = tag;
	}
}

static void alloc_phy_mem_links(void)
{
	struct vmm_entry *new_tags_virt = vir_mem_alloc(
		PAGE_SIZE, VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT);
	size_t new_tags_virt_count =
		new_tags_virt->size / sizeof(phy_mem_link_t);

	fatptr_t new_tags_phy = phy_mem_alloc(new_tags_virt->size);

	map_pages(&new_tags_phy, new_tags_virt);

	// Adding all the allocated tags to the chain
	for (size_t i = 0; i < new_tags_virt_count; i++) {
		phy_mem_link_t new_tag = { nullptr };
#ifdef DEBUG
		new_tag.type = FREE;
#endif
		((phy_mem_link_t *)new_tags_virt->ptr)[i] = new_tag;

		list_add(&((phy_mem_link_t *)new_tags_virt->ptr)[i].list,
			 free_phy_links_list.prev);
	}

	// Get one unused tag
	malloc_tag_t *tag = mem_get_tag();
	phy_mem_tag_t *tag_phy = mem_get_phy_mem_tag();
	phy_mem_link_t *tag_link = mem_get_phy_mem_link();

	// Copy the info about the tag chunk
	tag->ptr = new_tags_virt->ptr;
	tag->size = new_tags_virt->size;
	tag->used = new_tags_virt_count * sizeof(phy_mem_link_t);
	tag->vmm = new_tags_virt;

	tag_phy->phy_mem = new_tags_phy;
	tag_phy->ref_cnt = &tag >= tag->ptr && tag < tag->ptr + tag->size;

	tag_link->phy_mem = tag_phy;
	list_add(&tag_link->list, &tag->phy_chain);

	mem_insert_tag(tag, &tags_list);

	for (size_t i = 0; i < new_tags_virt_count; i++) {
		(((phy_mem_link_t *)new_tags_virt->ptr)[i]).tag_manager = tag;
	}
}

static void alloc_tags(void)
{
	struct vmm_entry *new_tags_virt = vir_mem_alloc(
		PAGE_SIZE, VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT);
	size_t new_tags_virt_count = new_tags_virt->size / sizeof(malloc_tag_t);

	fatptr_t new_tags_phy = phy_mem_alloc(new_tags_virt->size);

	map_pages(&new_tags_phy, new_tags_virt);

	// Adding all the allocated tags to the chain
	for (size_t i = 0; i < new_tags_virt_count; i++) {
		malloc_tag_t new_tag = { nullptr };
#ifdef DEBUG
		new_tag.type = FREE;
#endif
		((malloc_tag_t *)new_tags_virt->ptr)[i] = new_tag;

		list_add(&((malloc_tag_t *)new_tags_virt->ptr)[i].list,
			 free_tags_list.prev);
	}

	// Get one unused tag
	malloc_tag_t *tag = mem_get_tag();
	phy_mem_tag_t *tag_phy = mem_get_phy_mem_tag();
	phy_mem_link_t *tag_link = mem_get_phy_mem_link();

	// Copy the info about the tag chunk
	tag->ptr = new_tags_virt->ptr;
	tag->size = new_tags_virt->size;
	tag->used = new_tags_virt_count * sizeof(malloc_tag_t);
	tag->vmm = new_tags_virt;

	tag_phy->phy_mem = new_tags_phy;
	tag_phy->ref_cnt = &tag >= tag->ptr && tag < tag->ptr + tag->size;

	tag_link->phy_mem = tag_phy;

	list_add(&tag_link->list, &tag->phy_chain);

	mem_insert_tag(tag, &tags_list);

	for (size_t i = 0; i < new_tags_virt_count; i++) {
		(((malloc_tag_t *)new_tags_virt->ptr)[i]).tag_manager = tag;
	}
}

static void init_tags(malloc_tag_t *manager)
{
	struct vmm_entry *new_tags_virt = vir_mem_alloc(
		PAGE_SIZE, VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT);
	size_t new_tags_virt_count = new_tags_virt->size / sizeof(malloc_tag_t);

	fatptr_t new_tags_phy = phy_mem_alloc(new_tags_virt->size);

	map_pages(&new_tags_phy, new_tags_virt);

	manager->ptr = new_tags_virt->ptr;
	manager->size = new_tags_virt->size;
	manager->used = new_tags_virt_count * sizeof(malloc_tag_t);
	manager->vmm = new_tags_virt;
	phy_mem_link_t *link =
		list_entry(manager->phy_chain.next, phy_mem_link_t, list);
	link->phy_mem->phy_mem = new_tags_phy;

#ifdef DEBUG
	manager->type = MANAGER;
	link->type = USED;
	link->phy_mem->type = USED;
#endif

	// Adding all the allocated tags to the chain
	for (size_t i = 0; i < new_tags_virt_count; i++) {
		malloc_tag_t new_tag = { nullptr };
		new_tag.tag_manager = manager;
#ifdef DEBUG
		new_tag.type = FREE;
#endif
		((malloc_tag_t *)new_tags_virt->ptr)[i] = new_tag;

		list_add(&((malloc_tag_t *)new_tags_virt->ptr)[i].list,
			 free_tags_list.prev);
	}
}

static void init_phy_mem_links(malloc_tag_t *manager)
{
	struct vmm_entry *new_tags_virt = vir_mem_alloc(
		PAGE_SIZE, VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT);
	size_t new_tags_virt_count =
		new_tags_virt->size / sizeof(phy_mem_link_t);

	fatptr_t new_tags_phy = phy_mem_alloc(new_tags_virt->size);

	map_pages(&new_tags_phy, new_tags_virt);

	manager->ptr = new_tags_virt->ptr;
	manager->size = new_tags_virt->size;
	manager->used = new_tags_virt_count * sizeof(phy_mem_link_t);
	manager->vmm = new_tags_virt;
	phy_mem_link_t *link =
		list_entry(manager->phy_chain.next, phy_mem_link_t, list);
	link->phy_mem->phy_mem = new_tags_phy;

#ifdef DEBUG
	manager->type = MANAGER;
	link->type = USED;
	link->phy_mem->type = USED;
#endif

	// Adding all the allocated tags to the chain
	for (size_t i = 0; i < new_tags_virt_count; i++) {
		phy_mem_link_t new_tag = { nullptr };
		new_tag.tag_manager = manager;
#ifdef DEBUG
		new_tag.type = FREE;
#endif
		((phy_mem_link_t *)new_tags_virt->ptr)[i] = new_tag;

		list_add(&((phy_mem_link_t *)new_tags_virt->ptr)[i].list,
			 free_phy_links_list.prev);
	}
}

static void init_phy_mem_tags(malloc_tag_t *manager)
{
	struct vmm_entry *new_tags_virt = vir_mem_alloc(
		PAGE_SIZE, VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT);
	size_t new_tags_virt_count =
		new_tags_virt->size / sizeof(phy_mem_tag_t);

	fatptr_t new_tags_phy = phy_mem_alloc(new_tags_virt->size);

	map_pages(&new_tags_phy, new_tags_virt);

	manager->ptr = new_tags_virt->ptr;
	manager->size = new_tags_virt->size;
	manager->used = new_tags_virt_count * sizeof(phy_mem_tag_t);
	manager->vmm = new_tags_virt;
	phy_mem_link_t *link =
		list_entry(manager->phy_chain.next, phy_mem_link_t, list);
	link->phy_mem->phy_mem = new_tags_phy;

#ifdef DEBUG
	manager->type = MANAGER;
	link->type = USED;
	link->phy_mem->type = USED;
#endif

	// Adding all the allocated tags to the chain
	for (size_t i = 0; i < new_tags_virt_count; i++) {
		phy_mem_tag_t new_tag = { 0 };
		new_tag.tag_manager = manager;
#ifdef DEBUG
		new_tag.type = FREE;
#endif
		((phy_mem_tag_t *)new_tags_virt->ptr)[i] = new_tag;

		phy_mem_link_t *tag_link = mem_get_phy_mem_link();
		tag_link->phy_mem = &((phy_mem_tag_t *)new_tags_virt->ptr)[i];

		list_add(&tag_link->list, &free_phy_tags_list);
	}
}

void init_kmalloc(void)
{
	phy_mem_tag_t tags_manager_phy = {
		.phy_mem = { 0 },
		.ref_cnt = 0,
		.tag_manager = nullptr,
	};
	phy_mem_tag_t links_manager_phy = {
		.phy_mem = { 0 },
		.ref_cnt = 0,
		.tag_manager = nullptr,
	};
	phy_mem_tag_t phy_tags_manager_phy = {
		.phy_mem = { 0 },
		.ref_cnt = 0,
		.tag_manager = nullptr,
	};

	phy_mem_link_t tags_manager_link = {
		.phy_mem = &tags_manager_phy,
		.tag_manager = nullptr,
	};
	RESET_LIST_ITEM(&tags_manager_link.list);

	phy_mem_link_t links_manager_link = {
		.phy_mem = &links_manager_phy,
		.tag_manager = nullptr,
	};
	RESET_LIST_ITEM(&links_manager_link.list);

	phy_mem_link_t phy_tags_manager_link = {
		.phy_mem = &phy_tags_manager_phy,
		.tag_manager = nullptr,
	};
	RESET_LIST_ITEM(&phy_tags_manager_link.list);

	malloc_tag_t tags_manager = {
		.tag_manager = nullptr,
		.phy_chain =
			(struct list_head){
				.next = &tags_manager_link.list,
				.prev = &tags_manager_link.list,
			},
	};
	RESET_LIST_ITEM(&tags_manager.list);

	malloc_tag_t links_manager = {
		.tag_manager = nullptr,
		.phy_chain =
			(struct list_head){
				.next = &links_manager_link.list,
				.prev = &links_manager_link.list,
			},
	};
	RESET_LIST_ITEM(&links_manager.list);

	malloc_tag_t phy_tags_manager = {
		.tag_manager = nullptr,
		.phy_chain =
			(struct list_head){
				.next = &phy_tags_manager_link.list,
				.prev = &phy_tags_manager_link.list,
			},
	};
	RESET_LIST_ITEM(&phy_tags_manager_link.list);

	init_tags(&tags_manager);
	init_phy_mem_links(&links_manager);
	init_phy_mem_tags(&phy_tags_manager);

	malloc_tag_t *tag = mem_get_tag();
	phy_mem_tag_t *tag_phy = mem_get_phy_mem_tag();
	phy_mem_link_t *tag_link = mem_get_phy_mem_link();

	tag->ptr = tags_manager.ptr;
	tag->size = tags_manager.size;
	tag->used = tags_manager.used;
	tag->vmm = tags_manager.vmm;
#ifdef DEBUG
	tag->type = tags_manager.type;
#endif

	tag_phy->phy_mem = tags_manager_phy.phy_mem;
	tag_phy->ref_cnt = tags_manager_phy.ref_cnt;
#ifdef DEBUG
	tag_phy->type = tags_manager_phy.type;
#endif

	tag_link->phy_mem = tag_phy;
#ifdef DEBUG
	tag_link->type = tags_manager_link.type;
#endif

	list_add(&tag_link->list, &tag->phy_chain);
	mem_insert_tag(tag, &tags_list);

	for (size_t i = 0; i < tag->used / sizeof(malloc_tag_t); i++) {
		(((malloc_tag_t *)tag->ptr)[i]).tag_manager = tag;
	}

	tag = mem_get_tag();
	tag_phy = mem_get_phy_mem_tag();
	tag_link = mem_get_phy_mem_link();

	tag->ptr = links_manager.ptr;
	tag->size = links_manager.size;
	tag->used = links_manager.used;
	tag->vmm = links_manager.vmm;
#ifdef DEBUG
	tag->type = links_manager.type;
#endif

	tag_phy->phy_mem = links_manager_phy.phy_mem;
	tag_phy->ref_cnt = links_manager_phy.ref_cnt;
#ifdef DEBUG
	tag_phy->type = links_manager_phy.type;
#endif

	tag_link->phy_mem = tag_phy;
#ifdef DEBUG
	tag_link->type = links_manager_link.type;
#endif

	list_add(&tag_link->list, &tag->phy_chain);
	mem_insert_tag(tag, &tags_list);

	for (size_t i = 0; i < tag->used / sizeof(phy_mem_link_t); i++) {
		(((phy_mem_link_t *)tag->ptr)[i]).tag_manager = tag;
	}

	tag = mem_get_tag();
	tag_phy = mem_get_phy_mem_tag();
	tag_link = mem_get_phy_mem_link();

	tag->ptr = phy_tags_manager.ptr;
	tag->size = phy_tags_manager.size;
	tag->used = phy_tags_manager.used;
	tag->vmm = phy_tags_manager.vmm;
#ifdef DEBUG
	tag->type = phy_tags_manager.type;
#endif

	tag_phy->phy_mem = phy_tags_manager_phy.phy_mem;
	tag_phy->ref_cnt = phy_tags_manager_phy.ref_cnt;
#ifdef DEBUG
	tag_phy->type = phy_tags_manager_phy.type;
#endif

	tag_link->phy_mem = tag_phy;
#ifdef DEBUG
	tag_link->type = phy_tags_manager_link.type;
#endif

	list_add(&tag_link->list, &tag->phy_chain);
	mem_insert_tag(tag, &tags_list);

	for (size_t i = 0; i < tag->used / sizeof(phy_mem_tag_t); i++) {
		(((phy_mem_tag_t *)tag->ptr)[i]).tag_manager = tag;
	}

	mem_debug_lists();
}

malloc_tag_t *mem_find_best_fit(size_t req)
{
	malloc_tag_t *tag = nullptr;
	list_for_each(&tags_list) {
		malloc_tag_t *it_tag = list_entry(it, malloc_tag_t, list);
		size_t free_space = it_tag->size - it_tag->used;
		if ((tag != nullptr && free_space >= req &&
		     free_space < tag->size - tag->used) ||
		    (tag == nullptr && free_space >= req)) {
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
	mem_malloc_tag_t *prev =
		list_entry(tag->list.prev, mem_malloc_tag_t, list);
	mem_malloc_tag_t *next =
		list_entry(tag->list.next, mem_malloc_tag_t, list);

	bool coalesce_prev = prev->ptr + prev->size == tag->ptr &&
			     prev->vmm == tag->vmm;
	bool coalesce_next = tag->ptr + tag->size == next->ptr &&
			     next->used == 0 && next->vmm == tag->vmm;

	mem_malloc_tag_t *mem_to_free = tag;

	tag->used = 0;
	if (coalesce_next) {
		tag->size += next->size;
		list_for_each(&next->phy_chain) {
			mem_phy_mem_link_t *phy_link =
				list_entry(it, mem_phy_mem_link_t, list);
			mem_phy_mem_tag_t *phy_tag = phy_link->phy_mem;

			list_for_each(&tag->phy_chain) {
				mem_phy_mem_link_t *phy_link2 = list_entry(
					it, mem_phy_mem_link_t, list);
				mem_phy_mem_tag_t *phy_tag2 = phy_link->phy_mem;
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
			mem_phy_mem_link_t *phy_link =
				list_entry(it, mem_phy_mem_link_t, list);
			mem_phy_mem_tag_t *phy_tag = phy_link->phy_mem;

			list_for_each(&prev->phy_chain) {
				mem_phy_mem_link_t *phy_link2 = list_entry(
					it, mem_phy_mem_link_t, list);
				mem_phy_mem_tag_t *phy_tag2 = phy_link->phy_mem;
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

/* static struct malloc_tag *kmalloc_alloc_mem(size_t req) */
/* { */
/* 	struct malloc_tag *vir_tag = get_malloc_tag(); */
/* 	RESET_LIST_ITEM(&vir_tag->phy_mem); */

/* 	struct vmm_entry *vir_mem = vir_mem_alloc( */
/* 		req, VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT); */

/* 	vir_tag->ptr = vir_mem->ptr; */
/* 	vir_tag->size = vir_mem->size; */
/* 	vir_tag->used = 0; */

/* 	for (void *vir_ptr = vir_tag->ptr; */
/* 	     vir_ptr < vir_tag->ptr + vir_tag->size; vir_ptr += PAGE_SIZE) { */
/* 		fatptr_t phy_tag = phy_mem_alloc(PAGE_SIZE); */
/* 		struct vmm_entry virt_tag = { */
/* 			.ptr = vir_ptr, */
/* 			.size = PAGE_SIZE, */
/* 			.flags = vir_mem->flags, */
/* 			.list = { .next = &virt_tag.list, */
/* 				  .prev = &virt_tag.list }, */
/* 		}; */

/* 		map_pages(&phy_tag, &virt_tag); */

/* 		struct phy_mem_tag *tag_phy = get_phy_mem_tag(); */
/* 		tag_phy->phy_mem = phy_tag; */
/* 		tag_phy->ref_cnt = 1; */

/* 		list_add(&tag_phy->list, &vir_tag->phy_mem); */
/* 	} */

/* 	malloc_insert_tag(vir_tag, &malloc_tags_list); */

/* 	return vir_tag; */
/* } */

/* static void malloc_use_free_tag_mem(struct malloc_tag *new_tag, */
/* 				    struct malloc_tag *old_tag, size_t req) */
/* { */
/* 	new_tag->ptr = old_tag->ptr + old_tag->used; */
/* 	new_tag->size = old_tag->size - old_tag->used; */
/* 	new_tag->used = req; */

/* 	list_for_each(&old_tag->phy_mem) { */
/* 		struct phy_mem_tag *tag = */
/* 			list_entry(it, struct phy_mem_tag, list); */

/* 		size_t phy_s = (size_t)get_vir_addr(tag->phy_mem.ptr); */
/* 		size_t phy_e = */
/* 			(size_t)get_vir_addr(tag->phy_mem.ptr) + PAGE_SIZE; */

/* 		size_t new_phy_s = (size_t)(new_tag->ptr); */
/* 		size_t new_phy_e = (size_t)(new_tag->ptr + new_tag->size); */

/* 		bool overlap = phy_s < new_phy_e && new_phy_s < phy_e; */

/* 		if (overlap) { */
/* 			tag->ref_cnt += 1; */
/* 			malloc_insert_phy_mem_tag(tag, &new_tag->phy_mem); */
/* 		} */
/* 	} */

/* 	old_tag->size = old_tag->used; */
/* 	if (old_tag->size == 0) */
/* 		give_malloc_tag(old_tag); */
/* } */

/* fatptr_t kmalloc(size_t req) */
/* { */
/* 	if (req == 0) */
/* 		BUG("requested size is 0") */

/* 	struct malloc_tag *vir_tag = kmalloc_find_mem(req); */

/* 	if (vir_tag == nullptr) */
/* 		vir_tag = kmalloc_alloc_mem(req); */

/* 	struct malloc_tag *new_vir_tag = get_malloc_tag(); */

/* 	malloc_use_free_tag_mem(new_vir_tag, vir_tag, req); */

/* 	debug_lists(); */

/* 	return (fatptr_t){ .ptr = new_vir_tag->ptr, .len = new_vir_tag->used }; */
/* } */

/* void kfree(fatptr_t to_free) */
/* { */
/* } */
