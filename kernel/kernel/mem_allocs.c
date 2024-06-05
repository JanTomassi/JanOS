#include <kernel/vir_mem.h>
#include <kernel/phy_mem.h>
#include <kernel/display.h>

#include <stddef.h>
#include <string.h>
#include <list.h>

struct malloc_tag {
	void *ptr; // Virtual address for specific allocaiton
	size_t size; // Total block size
	size_t used; // Sized used by this block
	struct malloc_tag *tag_manager;

	struct list_head phy_mem;
	struct list_head list;
};
struct phy_mem_tag {
	fatptr_t phy_mem;
	size_t ref_cnt;
	struct malloc_tag *tag_manager;

	struct list_head list;
};

static LIST_HEAD(malloc_tags_list);

static LIST_HEAD(malloc_free_tags_list);
static LIST_HEAD(malloc_phy_mem_tags_list);

static void debug_lists(void)
{
	size_t i = 0;
	kprintf("debug_lists | malloc_tags_list:\n");
	list_for_each(&malloc_tags_list) {
		struct malloc_tag *tag =
			list_entry(it, struct malloc_tag, list);
		kprintf("%u) ptr: %x | size: %x | used: %x | manager: %x\n",
			i++, tag->ptr, tag->size, tag->used,
			(size_t)tag->tag_manager);
	}

	i = 0;
	kprintf("debug_lists | malloc_free_tags_list:\n");
	list_for_each(&malloc_free_tags_list) {
		i += 1;
	}
	kprintf("    %u unused malloc_tag\n", i);

	i = 0;
	kprintf("debug_lists | malloc_phy_mem_tags_list:\n");
	list_for_each(&malloc_phy_mem_tags_list) {
		i += 1;
	}
	kprintf("    %u unused phy_mem_tag\n", i);
}

static void malloc_alloc_phy_mem_tag(void);
static void malloc_alloc_tag(void);
static void malloc_insert_tag(struct malloc_tag *tag);

static void give_phy_mem_tag(struct phy_mem_tag *);
static void give_malloc_tag(struct malloc_tag *);

__attribute__((malloc, malloc(give_phy_mem_tag, 1))) static struct phy_mem_tag *
get_phy_mem_tag();
__attribute__((malloc, malloc(give_malloc_tag, 1))) static struct malloc_tag *
get_malloc_tag();

static void give_phy_mem_tag(struct phy_mem_tag *ptr)
{
	if (ptr->ref_cnt > 1)
		ptr->ref_cnt -= 1;
	memset(ptr, 0, sizeof(struct phy_mem_tag));
}

static void give_malloc_tag(struct malloc_tag *ptr)
{
	list_for_each(&ptr->phy_mem) {
		struct phy_mem_tag *cur =
			list_entry(it, struct phy_mem_tag, list);

		if (cur->ref_cnt > 1)
			cur->ref_cnt -= 1;
	}
	memset(ptr, 0, sizeof(struct malloc_tag));
}

__attribute__((malloc, malloc(give_phy_mem_tag, 1))) static struct phy_mem_tag *
get_phy_mem_tag()
{
	struct list_head *tag_phy_elm = list_pop(&malloc_phy_mem_tags_list);

	if (tag_phy_elm == nullptr) {
		malloc_alloc_phy_mem_tag();
		tag_phy_elm = list_pop(&malloc_phy_mem_tags_list);
	}
	struct phy_mem_tag *tag_phy =
		list_entry(tag_phy_elm, struct phy_mem_tag, list);

	if (tag_phy->tag_manager != nullptr) {
		struct phy_mem_tag *tag_phy_mem =
			list_entry(&tag_phy->tag_manager->phy_mem,
				   struct phy_mem_tag, list);
		tag_phy_mem->ref_cnt += 1;
	}

	return tag_phy;
}

__attribute__((malloc, malloc(give_malloc_tag, 1))) static struct malloc_tag *
get_malloc_tag()
{
	struct list_head *tag_elm = list_pop(&malloc_free_tags_list);

	if (tag_elm == nullptr) {
		malloc_alloc_tag();
		tag_elm = list_pop(&malloc_free_tags_list);
	}
	struct malloc_tag *tag = list_entry(tag_elm, struct malloc_tag, list);

	if (tag->tag_manager != nullptr) {
		struct phy_mem_tag *tag_phy_mem = list_entry(
			&tag->tag_manager->phy_mem, struct phy_mem_tag, list);
		tag_phy_mem->ref_cnt += 1;
	}

	return tag;
}

static void malloc_insert_tag(struct malloc_tag *tag)
{
	// Add all the virtual memory mapping to the kmalloc known block
	struct malloc_tag *prev = nullptr;
	list_for_each(&malloc_tags_list) {
		struct malloc_tag *cur =
			list_entry(it, struct malloc_tag, list);
		if (cur->ptr < tag->ptr &&
		    (prev != nullptr || cur->ptr > prev->ptr))
			prev = cur;
	}
	if (prev == nullptr)
		list_add(&tag->list, &malloc_tags_list);
	else
		list_add(&tag->list, &prev->list);
}

static void malloc_alloc_phy_mem_tag(void)
{
	struct vmm_entry *new_tags_virt = vir_mem_alloc(
		PAGE_SIZE, VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT);
	size_t new_tags_virt_count =
		new_tags_virt->size / sizeof(struct phy_mem_tag);

	fatptr_t new_tags_phy = phy_mem_alloc(new_tags_virt->size);

	map_pages(&new_tags_phy, new_tags_virt);

	// Adding all the allocated tags to the chain
	for (size_t i = 0; i < new_tags_virt_count; i++) {
		struct phy_mem_tag new_tag = { 0 };
		((struct phy_mem_tag *)new_tags_virt->ptr)[i] = new_tag;

		list_add(&((struct phy_mem_tag *)new_tags_virt->ptr)[i].list,
			 malloc_phy_mem_tags_list.prev);
	}

	// Get one unused tag
	struct malloc_tag *tag = get_malloc_tag();
	struct phy_mem_tag *tag_phy = get_phy_mem_tag();

	// Copy the info about the tag chunk
	tag->ptr = new_tags_virt->ptr;
	tag->size = new_tags_virt->size;
	tag->used = new_tags_virt_count * sizeof(struct phy_mem_tag);
	RESET_LIST_ITEM(&tag->phy_mem);

	tag_phy->phy_mem = new_tags_phy;
	tag_phy->ref_cnt = 1;
	list_add(&tag_phy->list, &tag->phy_mem);

	malloc_insert_tag(tag);

	for (size_t i = 0; i < new_tags_virt_count; i++) {
		(((struct phy_mem_tag *)new_tags_virt->ptr)[i]).tag_manager =
			tag;
	}
}

static void malloc_alloc_tag(void)
{
	struct vmm_entry *new_tags_virt = vir_mem_alloc(
		PAGE_SIZE, VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT);
	size_t new_tags_virt_count =
		new_tags_virt->size / sizeof(struct malloc_tag);

	fatptr_t new_tags_phy = phy_mem_alloc(new_tags_virt->size);

	map_pages(&new_tags_phy, new_tags_virt);

	// Adding all the allocated tags to the chain
	for (size_t i = 0; i < new_tags_virt_count; i++) {
		struct malloc_tag new_tag = { nullptr };
		((struct malloc_tag *)new_tags_virt->ptr)[i] = new_tag;

		list_add(&((struct malloc_tag *)new_tags_virt->ptr)[i].list,
			 malloc_free_tags_list.prev);
	}

	// Get one unused tag
	struct malloc_tag *tag = get_malloc_tag();
	struct phy_mem_tag *tag_phy = get_phy_mem_tag();

	// Copy the info about the tag chunk
	tag->ptr = new_tags_virt->ptr;
	tag->size = new_tags_virt->size;
	tag->used = new_tags_virt_count * sizeof(struct malloc_tag);
	RESET_LIST_ITEM(&tag->phy_mem);

	tag_phy->phy_mem = new_tags_phy;
	tag_phy->ref_cnt = 1;
	list_add(&tag_phy->list, &tag->phy_mem);

	malloc_insert_tag(tag);

	for (size_t i = 0; i < new_tags_virt_count; i++) {
		(((struct malloc_tag *)new_tags_virt->ptr)[i]).tag_manager =
			tag;
	}
}

void init_kmalloc(void)
{
	malloc_alloc_tag();
	debug_lists();
}
