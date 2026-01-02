#include <kernel/elf32.h>
#include <kernel/vir_mem.h>
#include <kernel/phy_mem.h>
#include <kernel/display.h>
#include <string.h>
#include <list.h>

#define page_directory_addr (0xFFFFF000)

MODULE("Virt Memory Manager");

extern size_t HIGHER_HALF;

static void invalidate(void *addr)
{
	__asm__ volatile("invlpg (%0)" : : "r"((size_t)addr) : "memory");
}

void *get_phy_addr(void *vir_addr)
{
	size_t pd_idx = (size_t)vir_addr >> 22;
	size_t pt_idx = (size_t)vir_addr >> 12 & 0x03FF;

	// I'm assuming that the pd will alwais be valid
	size_t *pd = (size_t *)page_directory_addr;

	// No mapping in this range
	if (!(pd[pd_idx] & VMM_ENTRY_PRESENT_BIT))
		return nullptr;
	else if ((pd[pd_idx] & VMM_ENTRY_PAGE_SIZE_BIT))
		return (void *)((pd[pd_idx] & VMM_ENTRY_LOCATION_4M_LOW_BITS) + ((size_t)vir_addr & ~VMM_ENTRY_LOCATION_4M_LOW_BITS));

	size_t *pt = ((size_t *)0xFFC00000) + (0x400 * pd_idx);
	if ((pt[pt_idx] & VMM_ENTRY_PRESENT_BIT) == 0)
		return nullptr;

	return (void *)((pt[pt_idx] & VMM_ENTRY_LOCATION_4K_BITS) + ((size_t)vir_addr & 0xFFF));
}

void *get_vir_addr(void *phy_addr)
{
	size_t *pd = (size_t *)page_directory_addr;

	for (size_t pd_idx = 0; pd_idx < 1024; pd_idx++) {
		if (!(pd[pd_idx] & VMM_ENTRY_PRESENT_BIT))
			continue;

		size_t *pt = ((size_t *)0xFFC00000) + (0x400 * pd_idx);
		for (size_t pt_idx = 0; pt_idx < 1024; pt_idx++) {
			if (!(pt[pt_idx] & VMM_ENTRY_PRESENT_BIT) || (pt[pt_idx] & VMM_ENTRY_LOCATION_4K_BITS) != phy_addr)
				continue;

			return (void *)((pd_idx << 22 | pt_idx << 12) + ((size_t)phy_addr & 0xFFF));
		}
	}

	return nullptr;
}

bool map_page(void *phy_addr, void *virt_addr, uint16_t virt_flags)
{
	size_t pd_idx = (size_t)virt_addr >> 22;
	size_t pt_idx = (size_t)virt_addr >> 12 & 0x03FF;

	size_t *pd = (size_t *)0xFFFFF000;
	size_t *pt = ((size_t *)0xFFC00000) + (0x400 * pd_idx);

	if ((pd[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0) {
		pd[pd_idx] = (size_t)phy_mem_alloc(PAGE_SIZE).ptr;
		pd[pd_idx] |= VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT;
		memset(pt, 0, PAGE_SIZE);
	}

	pt[pt_idx] = ((size_t)phy_addr) | (virt_flags & 0xFFF);

	invalidate(virt_addr);

	return true;
}

bool map_pages(fatptr_t *phy_mem, struct vmm_entry *virt_mem)
{
	if (phy_mem->len != virt_mem->size)
		panic("Physical and Virtual size not equal:\n"
		      " - phy_size: %x\n"
		      " - virt_size %x\n",
		      phy_mem->len, virt_mem->size);

	void *virt_addr = virt_mem->ptr;
	void *phy_addr = phy_mem->ptr;

	while (virt_addr < virt_mem->ptr + virt_mem->size) {
		map_page(phy_addr, virt_addr, virt_mem->flags);

		virt_addr += PAGE_SIZE;
		phy_addr += PAGE_SIZE;
	}
	return true;
}

static bool is_page_table_empty(size_t *pt)
{
	for (size_t i = 0; i < 1024; i++) {
		if (pt[i] & VMM_ENTRY_PRESENT_BIT)
			return false;
	}

	return true;
}

static void unmap_pages(const struct vmm_entry *virt_mem)
{
	if (virt_mem->size == 0)
		return;

	size_t start_addr = (size_t)virt_mem->ptr;
	size_t end_addr = round_up_to_page(start_addr + virt_mem->size);
	size_t last_addr = end_addr - 1;

	size_t first_pd_idx = start_addr >> 22;
	size_t last_pd_idx = last_addr >> 22;

	size_t *pd = (size_t *)page_directory_addr;

	for (size_t pd_idx = first_pd_idx; pd_idx <= last_pd_idx; pd_idx++) {
		if ((pd[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0 || (pd[pd_idx] & VMM_ENTRY_PAGE_SIZE_BIT))
			continue;

		size_t *pt = ((size_t *)0xFFC00000) + (0x400 * pd_idx);

		size_t pt_start = 0;
		size_t pt_end = 1023;

		if (pd_idx == first_pd_idx)
			pt_start = (start_addr >> 12) & 0x3FF;
		if (pd_idx == last_pd_idx)
			pt_end = (last_addr >> 12) & 0x3FF;

		for (size_t pt_idx = pt_start; pt_idx <= pt_end; pt_idx++) {
			if ((pt[pt_idx] & VMM_ENTRY_PRESENT_BIT) == 0)
				continue;

			pt[pt_idx] = 0;
			invalidate((void *)((pd_idx << 22) | (pt_idx << 12)));
		}

		if (is_page_table_empty(pt)) {
			fatptr_t table_frame = {
				.ptr = (void *)(pd[pd_idx] & VMM_ENTRY_LOCATION_4K_BITS),
				.len = PAGE_SIZE,
			};

			pd[pd_idx] = 0;
			phy_mem_free(table_frame);
		}
	}
}


static void invalidate_low_range(void)
{
	size_t *pd = (size_t *)page_directory_addr;
	for (size_t i = 0; i < 768; i++) {
		if ((pd[i] & VMM_ENTRY_PRESENT_BIT) == 1)
			pd[i] = 0;
	}
	__asm__ volatile("push %eax;"
			 "mov  %cr3, %eax;"
			 "mov  %eax, %cr3;"
			 "pop  %eax;");
}

static inline void print_elf_sector(Elf32_Shdr *elf_sec, char *elf_sec_str, size_t i)
{
#ifdef DEBUG
	mprint("Section (%s): [Address: %x, Size: %x, Type: %x, flags: %x]\n", &elf_sec_str[elf_sec[i].sh_name], elf_sec[i].sh_addr, elf_sec[i].sh_size,
	       elf_sec[i].sh_type, elf_sec[i].sh_flags);
#endif
}

static void recreate_vir_mem(const struct multiboot_tag_elf_sections *elf_tag, const struct vmm_entry *preserved_entries, size_t preserved_entry_count)
{
	LIST_HEAD(vmm_used_list);
	struct vmm_entry usable_entry[64] = { 0 };
	const size_t usable_capacity = sizeof(usable_entry) / sizeof(usable_entry[0]);
	size_t used_entris = 0;

	const Elf32_Shdr *elf_sec = (const Elf32_Shdr *)elf_tag->sections;
	const char *elf_sec_str = (char *)elf_sec[elf_tag->shndx].sh_addr;
	size_t required_entries = preserved_entry_count;

	for (size_t i = 0; i < elf_tag->num; i++) {
		if ((elf_sec[i].sh_flags & 0x2) == 0 || elf_sec[i].sh_addr < &HIGHER_HALF)
			continue;

		required_entries++;
	}

	if (required_entries > usable_capacity)
		panic("Not enough space to register kernel sections and preserved ranges\n");

	for (size_t i = 0; i < elf_tag->num; i++) {
		if ((elf_sec[i].sh_flags & 0x2) == 0) {
#ifdef DEBUG
			mprint("Section (%s) dosn't allocate memory at runtime\n", &elf_sec_str[elf_sec[i].sh_name]);
#endif
			continue;
		} else if (elf_sec[i].sh_addr < &HIGHER_HALF) {
#ifdef DEBUG
			mprint("Section (%s) isn't part of the higher half kernel\n", &elf_sec_str[elf_sec[i].sh_name]);
#endif
			continue;
		} else {
			print_elf_sector(elf_sec, elf_sec_str, i);
		}

		size_t elf_s = elf_sec[i].sh_addr;
		size_t elf_e = round_up_to_page(elf_sec[i].sh_addr + elf_sec[i].sh_size);

		struct vmm_entry entry = {
			.ptr = (void *)elf_s,
			.size = elf_e - elf_s,
			.flags = (elf_sec[i].sh_flags & 0x1) * VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT,
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
	fatptr_t pd = phy_mem_alloc(PAGE_SIZE);
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

		for (void *virt_addr = cur->ptr; virt_addr < (cur->ptr + cur->size); virt_addr += PAGE_SIZE) {
			if (((size_t)virt_addr & 0xfff) != 0)
				panic("address is not 4k aligned");

			map_pages(&pd, &tmp_virt);

			size_t pd_idx = (size_t)virt_addr >> 22;
			size_t pt_idx = (size_t)virt_addr >> 12 & 0x03FF;

			size_t *px = (size_t *)tmp_virt.ptr;

			if ((px[pd_idx] & VMM_ENTRY_PRESENT_BIT) == 0) {
				px[pd_idx] = (size_t)phy_mem_alloc(PAGE_SIZE).ptr;

				px[pd_idx] |= 1 | cur->flags | VMM_ENTRY_READ_WRITE_BIT;

				map_pages(&(fatptr_t){ .ptr = (void *)(px[pd_idx] & ~0xfff), .len = PAGE_SIZE }, &tmp_virt);
				memset(tmp_virt.ptr, 0, PAGE_SIZE);
			} else {
				px[pd_idx] |= cur->flags | VMM_ENTRY_READ_WRITE_BIT;

				map_pages(&(fatptr_t){ .ptr = (void *)(px[pd_idx] & ~0xfff), .len = PAGE_SIZE }, &tmp_virt);
			}

			px[pt_idx] = ((size_t)get_phy_addr((void *)virt_addr)) | (cur->flags & 0xFFF);
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
LIST_HEAD(vmm_tags_list);

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

	i = 0;
	mprint("debug_vmm_list | vmm_tags_list:\n");
	list_for_each(&vmm_tags_list) {
		i++;
	}
	mprint("    %u unused vmm_tags\n", i);
}
#endif

static void debug_vmm_list(struct list_head *v){
	mprint("debug_vmm_list:\n");
	size_t i = 0;
	list_for_each(v) {
		struct vmm_entry *tag = list_entry(it, struct vmm_entry, list);
		mprint("    %u) ptr: %x | size: %x | flags: %x\n", i++, tag->ptr, tag->size, tag->flags);
	}
}

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

struct vmm_entry *vir_mem_alloc(size_t req_size, uint8_t flags)
{
	if (req_size > 0 && req_size & 0xfff)
		BUG("Virtual Memory allocation must be page aligned: %d", req_size & 0xfff);

	struct vmm_entry *free_chunk = nullptr;

	list_for_each(&vmm_free_list) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);

		size_t cur_free = cur->size;
		bool update_chunk_sel = cur_free >= req_size && (free_chunk == nullptr || (cur_free < free_chunk->size));

		if (update_chunk_sel) {
			free_chunk = cur;
		}
	}

	struct vmm_entry *tag = list_entry(list_pop(&vmm_tags_list), struct vmm_entry, list);

	*tag = (struct vmm_entry){
		.ptr = free_chunk->ptr,
		.size = req_size,
		.flags = flags,
	};

	free_chunk->ptr += req_size;
	free_chunk->size -= req_size;

	list_add(&tag->list, vir_mem_find_prev_used_chunk(tag)->prev);

#ifdef DEBUG
	debug_vmm_lists();
#endif

	return tag;
}

static struct vmm_entry *vir_mem_find_prev_free_chunk(struct vmm_entry *to_free)
{
	struct vmm_entry *prev_chunk = nullptr;

	list_for_each(&vmm_free_list) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);

		if (prev_chunk == nullptr || prev_chunk->ptr > cur->ptr && prev_chunk->ptr < to_free->ptr)
			prev_chunk = cur;
	}
	return prev_chunk;
}

static void vir_mem_free_coalesce(struct vmm_entry *mid)
{
	struct vmm_entry *prev = list_entry(mid->list.prev, struct vmm_entry, list);
	struct vmm_entry *next = list_entry(mid->list.next, struct vmm_entry, list);

	if (mid->ptr + mid->size == next->ptr) {
		mid->size += next->size;
		list_rm(&next->list);
	}
	if (prev->ptr + prev->size == mid->ptr) {
		prev->size += mid->size;
		list_rm(&mid->list);
	}
}

void vir_mem_free(void *ptr)
{
	list_for_each(&vmm_used_list) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);

		if (cur->ptr == ptr) {
			struct vmm_entry *prev_chunk = vir_mem_find_prev_free_chunk(cur);

			list_rm(&cur->list);
			list_add(&cur->list, &prev_chunk->list);

			vir_mem_free_coalesce(cur);

			break;
		}
	}

#ifdef DEBUG
	debug_vmm_lists();
#endif
}

static struct vmm_entry init_find_free_chunk(struct list_head *vmm_init_list)
{
	struct vmm_entry *free_block = nullptr;
	list_for_each(vmm_init_list) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);
		if (cur->size >= PAGE_SIZE) {
			free_block = cur;
			break;
		}
	}

	struct vmm_entry tag = (struct vmm_entry){
		.ptr = free_block->ptr,
		.size = PAGE_SIZE,
		.flags = VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_PRESENT_BIT,
	};
	RESET_LIST_ITEM(&tag.list);

	free_block->ptr += PAGE_SIZE;
	free_block->size -= PAGE_SIZE;

	return tag;
}

static void init_vir_manager(struct list_head *vmm_init_list)
{
	struct vmm_entry tags_chunk = init_find_free_chunk(vmm_init_list);

	fatptr_t phy_tag_mem = phy_mem_alloc(PAGE_SIZE);
	map_pages(&phy_tag_mem, &tags_chunk);

	for (size_t i = 0; i < tags_chunk.size / sizeof(struct vmm_entry); i++) {
		struct vmm_entry new_tag = { nullptr };
		((struct vmm_entry *)tags_chunk.ptr)[i] = new_tag;

		list_add(&((struct vmm_entry *)tags_chunk.ptr)[i].list, vmm_tags_list.prev);
	}

	// Get one unused tag
	struct vmm_entry *tag = list_entry(list_pop(&vmm_tags_list), struct vmm_entry, list);

	// Copy the info about the tag chunk
	*tag = tags_chunk;
	RESET_LIST_ITEM(&tag->list);

	list_add(&tag->list, &vmm_used_list);

	// Add all the virtual memory mapping to the kmalloc known block
	list_for_each(vmm_init_list) {
		struct vmm_entry *vmm_cur = list_entry(it, struct vmm_entry, list);

		struct vmm_entry *vmm_tag = list_entry(list_pop(&vmm_tags_list), struct vmm_entry, list);

		*vmm_tag = *vmm_cur;

		list_add(&vmm_tag->list, vmm_free_list.prev);
	}
}

void init_vir_mem(const struct multiboot_tag_elf_sections *elf_tag, const struct vmm_entry *preserved_entries, size_t preserved_entry_count)
{
	struct vmm_entry init_vmm_entry[PAGE_SIZE / sizeof(struct vmm_entry)] = { 0 };
	const size_t init_vmm_capacity = sizeof(init_vmm_entry) / sizeof(init_vmm_entry[0]);
	size_t init_vmm_entris_used = 0;
	LIST_HEAD(vmm_free_list);

	struct vmm_entry init_entry = {
		.ptr = (void *)&HIGHER_HALF,
		.size = SIZE_MAX - (size_t)&HIGHER_HALF,
		.flags = 0,
	};
	RESET_LIST_ITEM(&init_entry.list);

	init_vmm_entry[init_vmm_entris_used] = init_entry;
	list_add(&init_entry.list, &vmm_free_list);
	init_vmm_entris_used++;

	const Elf32_Shdr *elf_sec = (const Elf32_Shdr *)elf_tag->sections;
	const char *elf_sec_str = (char *)elf_sec[elf_tag->shndx].sh_addr;
	for (size_t i = 0; i < elf_tag->num; i++) {
#ifdef DEBUG
		mprint("Section (%s): [Address: %x, Size: %x, Type: %x, flags: %x]\n", &elf_sec_str[elf_sec[i].sh_name], elf_sec[i].sh_addr, elf_sec[i].sh_size,
		       elf_sec[i].sh_type, elf_sec[i].sh_flags);
#endif

		if ((elf_sec[i].sh_flags & 0x2) == 0) {
#ifdef DEBUG
			mprint("Section (%s) dosn't allocate memory at runtime\n", &elf_sec_str[elf_sec[i].sh_name]);
#endif
			continue;
		}

		list_for_each(&vmm_free_list) {
			struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);

			/*
			 * Intersect the two range, and in case split
			 * the two found entry or remove it
			 */

			void *cur_s = cur->ptr;
			void *cur_e = cur->ptr + cur->size;

			void *elf_s = (void *)elf_sec[i].sh_addr;
			void *elf_e = (void *)elf_sec[i].sh_addr + elf_sec[i].sh_size;

			if (elf_s > cur_e || cur_s >= elf_e)
				continue;

			void *range_s = (void *)round_down_to_page((size_t)cur_s > (size_t)elf_s ? (size_t)cur_s : (size_t)elf_s);
			void *range_e = (void *)round_up_to_page((size_t)cur_e < (size_t)elf_e ? (size_t)cur_e : (size_t)elf_e);

			struct vmm_entry left_entry = {
				.ptr = cur_s,
				.size = range_s - cur_s,
			};
			RESET_LIST_ITEM(&left_entry.list);

			struct vmm_entry right_entry = {
				.ptr = range_e,
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
		}
	}

	for (size_t i = 0; i < preserved_entry_count; i++) {
		if (init_vmm_entris_used >= init_vmm_capacity)
			panic("Not enough space to reserve preserved virtual ranges\n");

		uintptr_t range_s = (uintptr_t)preserved_entries[i].ptr;
		uintptr_t range_e = (uintptr_t)preserved_entries[i].ptr + preserved_entries[i].size;

		list_for_each(&vmm_free_list) {
			struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);

			uintptr_t cur_s = (uintptr_t)cur->ptr;
			uintptr_t cur_e = (uintptr_t)cur->ptr + cur->size;

			if (range_s > cur_e || cur_s >= range_e)
				continue;

			uintptr_t inter_s = round_down_to_page((uintptr_t)(cur_s > range_s ? cur_s : range_s));
			uintptr_t inter_e = round_up_to_page((uintptr_t)(cur_e < range_e ? cur_e : range_e));

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
		}
	}

	recreate_vir_mem(elf_tag, preserved_entries, preserved_entry_count);
	init_vir_manager(&vmm_free_list);
}
