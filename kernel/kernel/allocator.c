#include <kernel/allocator.h>
#include <kernel/mem_allocs.h>
#include <kernel/vir_mem.h>
#include <kernel/phy_mem.h>
#include <kernel/display.h>

typedef mem_malloc_tag_t malloc_tag_t;
typedef mem_phy_mem_link_t phy_mem_link_t;
typedef mem_phy_mem_tag_t phy_mem_tag_t;

extern void mem_debug_lists(void);

static bool gpa_initialized = false;
static malloc_tag_t *gpa_allocs = nullptr;

MODULE("Allocator");

static void gpa_make_new_space(malloc_tag_t *tag, size_t req)
{
	size_t req_align = round_up_to_page(req);

	struct vmm_entry *vir_mem = vir_mem_alloc(
		req_align, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);

	mem_set_ptr_tag(tag, vir_mem->ptr);
	mem_set_size_tag(tag, vir_mem->size);
	mem_set_used_tag(tag, req);
	mem_set_vmm_tag(tag, vir_mem);
	struct list_head *tag_chain = mem_get_chain_tag(tag);

	for (void *vir_ptr = mem_get_ptr_tag(tag);
	     vir_ptr < mem_get_ptr_tag(tag) + mem_get_size_tag(tag);
	     vir_ptr += PAGE_SIZE) {
		fatptr_t phy_mem = phy_mem_alloc(PAGE_SIZE);
		struct vmm_entry vir_info = {
			.ptr = vir_ptr,
			.size = PAGE_SIZE,
			.flags = vir_mem->flags,
		};
		RESET_LIST_ITEM(&vir_info.list);

		map_pages(&phy_mem, &vir_info);

		phy_mem_tag_t *phy_tag = mem_get_phy_mem_tag();
		mem_set_fatptr_phy_mem(phy_tag, phy_mem);
		mem_set_refcnt_phy_mem(phy_tag, 1);

		mem_insert_phy_mem_tag(phy_tag, tag_chain, false);
	}
}

static malloc_tag_t *gpa_use_space(malloc_tag_t *tag, size_t req)
{
	malloc_tag_t *mem = mem_get_tag();
	void *mem_ptr = mem_get_ptr_tag(tag) + mem_get_used_tag(tag);
	size_t tag_free = mem_get_size_tag(tag) - mem_get_used_tag(tag);

	struct list_head *tag_chain = mem_get_chain_tag(tag);

	mem_set_ptr_tag(mem, mem_ptr);
	mem_set_size_tag(mem, tag_free);
	mem_set_used_tag(mem, req);
	mem_set_vmm_tag(mem, mem_get_vmm_tag(tag));
	struct list_head *mem_chain = mem_get_chain_tag(mem);

	size_t len_acc = 0;
	list_for_each(tag_chain) {
		phy_mem_tag_t *cur = mem_get_phy_tag_from_link(it);
		fatptr_t cur_phy = mem_get_fatptr_phy_mem(cur);

		size_t mem_s = (size_t)mem_get_ptr_tag(mem);
		size_t mem_e =
			(size_t)mem_get_ptr_tag(mem) + mem_get_size_tag(mem);

		size_t tag_s = (size_t)mem_get_ptr_tag(tag) + len_acc;
		size_t tag_e = (size_t)tag_s + cur_phy.len;

		if ((tag_s <= mem_e) && (mem_s <= tag_e)) {
			size_t rang_s = mem_s > tag_s ? mem_s : tag_s;
			size_t rang_e = mem_e < tag_e ? mem_e : tag_e;

			mem_set_refcnt_phy_mem(cur,
					       mem_get_refcnt_phy_mem(cur) + 1);
			mem_insert_phy_mem_tag(cur, mem_chain, false);
		}

		len_acc += cur_phy.len;
	}

	mem_set_size_tag(tag, mem_get_used_tag(tag));

	return mem;
}

static void gpa_init(malloc_tag_t *mem)
{
	struct vmm_entry *vir_mem = vir_mem_alloc(
		PAGE_SIZE, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);

	mem_set_ptr_tag(mem, vir_mem->ptr);
	mem_set_size_tag(mem, vir_mem->size);
	mem_set_used_tag(mem, PAGE_SIZE);
	mem_set_vmm_tag(mem, vir_mem);
	struct list_head *tag_chain = mem_get_chain_tag(mem);

	for (void *vir_ptr = mem_get_ptr_tag(mem);
	     vir_ptr < mem_get_ptr_tag(mem) + mem_get_size_tag(mem);
	     vir_ptr += PAGE_SIZE) {
		fatptr_t phy_mem = phy_mem_alloc(PAGE_SIZE);
		struct vmm_entry vir_info = {
			.ptr = vir_ptr,
			.size = PAGE_SIZE,
			.flags = vir_mem->flags,
		};
		RESET_LIST_ITEM(&vir_info.list);

		map_pages(&phy_mem, &vir_info);

		phy_mem_tag_t *phy_tag = mem_get_phy_mem_tag();
		mem_set_fatptr_phy_mem(phy_tag, phy_mem);
		mem_set_refcnt_phy_mem(phy_tag, 1);

		mem_insert_phy_mem_tag(phy_tag, tag_chain, false);
	}
}

static void gpa_set_alloc(malloc_tag_t *ptr, const malloc_tag_t *loc)
{
	const size_t num_elm = PAGE_SIZE / sizeof(malloc_tag_t *);
	const malloc_tag_t **array_ptr = mem_get_ptr_tag(loc);

	for (size_t i = 0; i < num_elm - 1; i++) {
		if (array_ptr[i] == nullptr) {
			array_ptr[i] = ptr;
			return;
		}
	}

	if (array_ptr[num_elm - 1] != nullptr)
		gpa_set_alloc(ptr, array_ptr[num_elm - 2]);
	if (array_ptr[num_elm - 1] == nullptr) {
		array_ptr[num_elm - 1] = mem_get_tag();
		gpa_init(array_ptr[num_elm - 1]);
		mem_register_tag(array_ptr[num_elm - 1]);
	}
}

static malloc_tag_t **gpa_get_alloc(void *ptr, const malloc_tag_t *loc)
{
	const size_t num_elm = PAGE_SIZE / sizeof(malloc_tag_t *);
	malloc_tag_t **array_ptr = mem_get_ptr_tag(loc);

	for (size_t i = 0; i < sizeof(malloc_tag_t *); i++) {
		if (mem_get_ptr_tag(array_ptr[i]) == ptr)
			return &array_ptr[i];
	}

	if (array_ptr[num_elm - 2] != nullptr)
		gpa_get_alloc(ptr, array_ptr[num_elm - 1]);
	else
		return nullptr;
}

fatptr_t mem_gpa_alloc(size_t req)
{
	if (!gpa_initialized) {
		gpa_initialized = true;
		gpa_allocs = mem_get_tag();
		gpa_init(gpa_allocs);
		mem_register_tag(gpa_allocs);
	}

	malloc_tag_t *mem = mem_find_best_fit(req);

	if (mem == nullptr) {
		mem = mem_get_tag();
		gpa_make_new_space(mem, req);
		mem_register_tag(mem);
	} else {
		mem = gpa_use_space(mem, req);
		mem_register_tag(mem);
	}

	gpa_set_alloc(mem, gpa_allocs);

	mem_debug_lists();

	return (fatptr_t){
		.ptr = mem_get_ptr_tag(mem),
		.len = mem_get_used_tag(mem),
	};
}

void mem_gpa_free(fatptr_t freeing)
{
	malloc_tag_t **tag = gpa_get_alloc(freeing.ptr, gpa_allocs);
	mem_unregister_tag(*tag);
	tag = nullptr;
	mem_debug_lists();
}
