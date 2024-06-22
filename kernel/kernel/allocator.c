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

static void gpa_make_new_space(malloc_tag_t *tag, size_t req)
{
	size_t req_align = round_up_to_page(req);

	struct vmm_entry *vir_mem = vir_mem_alloc(
		req_align, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);

	mem_set_ptr_tag(tag, vir_mem->ptr);
	mem_set_size_tag(tag, vir_mem->size);
	mem_set_used_tag(tag, req);
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

		mem_insert_phy_mem_tag(phy_tag, tag_chain);
	}
}

fatptr_t mem_gpa_alloc(size_t req)
{
	malloc_tag_t *mem = mem_find_best_fit(req);
	if (mem == nullptr) {
		mem = mem_get_tag();
		gpa_make_new_space(mem, req);
		mem_register_tag(mem);
	}
	mem_debug_lists();

	return (fatptr_t){
		.ptr = mem_get_ptr_tag(mem),
		.len = mem_get_used_tag(mem),
	};
}

void mem_gpa_free(fatptr_t freeing)
{
	panic("TODO");
}
