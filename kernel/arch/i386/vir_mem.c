#include <kernel/vir_mem.h>
#include <kernel/phy_mem.h>
#include <kernel/display.h>
#include <string.h>
#include <list.h>

#define PRESENT_BIT (1 << 0)
#define READ_WRITE_BIT (1 << 1)
#define USER_SUPER_BIT (1 << 2)
#define WRITE_THROUGH_BIT (1 << 3)
#define CACHE_DISABLE_BIT (1 << 4)
#define ACCESSED_DISABLE_BIT (1 << 5)
#define DIRTY (1 << 6)
#define PAGE_SIZE_BIT (1 << 7)
#define GLOBAL_BIT (1 << 8)
#define PAGE_ATTRIBUTE_BIT (1 << 12)
#define RESERVED_BIT (1 << 21)

#define AVAILABLE_4K_BIT ((1 << 6) | (0b1111 << 8))
#define LOCATION_4K_BITS (0xfffff << 12)

#define LOCATION_4M_LOW_BITS (0x3ff << 22)
#define LOCATION_4M_HIGHT_BITS (0xff << 13)

#define page_directory_addr (0xFFFFF000)
#define PAGE_SIZE (4096)
#define round_up_to_page(x) ((x) + (-(x) % PAGE_SIZE))
#define round_down_to_page(x) \
	(((x)-PAGE_SIZE - 1) + (-((x)-PAGE_SIZE - 1) % PAGE_SIZE))

extern size_t HIGHER_HALF;

struct vmm_entry {
	void *ptr;
	size_t size;
	uint16_t flags;
	struct list_head list;
};

static void invalidate(void *addr)
{
	__asm__ volatile("invlpg (%0)" : : "r"((size_t)addr) : "memory");
}

void *get_physaddr(void *virtualaddr)
{
	size_t pd_idx = (size_t)virtualaddr >> 22;
	size_t pt_idx = (size_t)virtualaddr >> 12 & 0x03FF;

	// I'm assuming that the pd will alwais be valid
	size_t *pd = (size_t *)page_directory_addr;

	// No mapping in this range
	if (!(pd[pd_idx] & PRESENT_BIT))
		return nullptr;
	else if ((pd[pd_idx] & PAGE_SIZE_BIT))
		return (void *)((pd[pd_idx] & LOCATION_4M_LOW_BITS) +
				((size_t)virtualaddr & ~LOCATION_4M_LOW_BITS));

	size_t *pt = ((size_t *)0xFFC00000) + (0x400 * pd_idx);
	if ((pt[pt_idx] & PRESENT_BIT) == 0)
		return nullptr;

	return (void *)((pt[pt_idx] & LOCATION_4K_BITS) +
			((size_t)virtualaddr & 0xFFF));
}

bool map_page(void *physaddr, void *virtualaddr, uint16_t flags)
{
	size_t pd_idx = (size_t)virtualaddr >> 22;
	size_t pt_idx = (size_t)virtualaddr >> 12 & 0x03FF;

	size_t *pd = (size_t *)0xFFFFF000;
	size_t *pt = ((size_t *)0xFFC00000) + (0x400 * pd_idx);

	if ((pd[pd_idx] & PRESENT_BIT) == 0) {
		pd[pd_idx] = (size_t)phy_mem_alloc(PAGE_SIZE);
		pd[pd_idx] |= 1;
		memset(pt, 0, PAGE_SIZE);
	}

	pd[pd_idx] |= flags & READ_WRITE_BIT;

	if ((flags & PRESENT_BIT) && !(pt[pt_idx] & PRESENT_BIT))
		pt[pt_idx] = (size_t)phy_mem_alloc(PAGE_SIZE);
	else if (!(flags & PRESENT_BIT) && (pt[pt_idx] & PRESENT_BIT))
		phy_mem_free((void *)(pt[pt_idx] & ~0xFFF));

	pt[pt_idx] = ((size_t)physaddr) | (flags & 0xFFF);

	invalidate(virtualaddr);
}

static void invalidate_low_range(void)
{
	size_t *pd = (size_t *)page_directory_addr;
	for (size_t i = 0; i < 768; i++) {
		if ((pd[i] & PRESENT_BIT) == 1)
			pd[i] = 0;
	}
	__asm__ volatile("push %eax;"
			 "mov  %cr3, %eax;"
			 "mov  %eax, %cr3;"
			 "pop  %eax;");
}

static inline void print_elf_sector(Elf32_Shdr *elf_sec, char *elf_sec_str,
				    size_t i)
{
	kprintf("Section (%s): [Address: %x, Size: %x, Type: %x, flags: %x]\n",
		&elf_sec_str[elf_sec[i].sh_name], elf_sec[i].sh_addr,
		elf_sec[i].sh_size, elf_sec[i].sh_type, elf_sec[i].sh_flags);
}

static void recreate_vir_mem(multiboot_elf_section_header_table_t elf)
{
	LIST_HEAD(vmm_used_list);
	struct vmm_entry usable_entry[32] = { 0 };
	size_t used_entris = 0;

	Elf32_Shdr *elf_sec = elf.addr;
	char *elf_sec_str = (char *)elf_sec[elf.shndx].sh_addr;

	for (size_t i = 0; i < elf.num; i++) {
		if ((elf_sec[i].sh_flags & 0x2) == 0) {
			kprintf("Section (%s) dosn't allocate memory at runtime\n",
				&elf_sec_str[elf_sec[i].sh_name]);
			continue;
		} else if (elf_sec[i].sh_addr < &HIGHER_HALF) {
			kprintf("Section (%s) isn't part of the higher half kernel\n",
				&elf_sec_str[elf_sec[i].sh_name]);
			continue;
		} else {
			print_elf_sector(elf_sec, elf_sec_str, i);
		}

		size_t elf_s = elf_sec[i].sh_addr;
		size_t elf_e = round_up_to_page(elf_sec[i].sh_addr +
						elf_sec[i].sh_size);

		struct vmm_entry entry = {
			.ptr = (void *)elf_s,
			.size = elf_e - elf_s,
			.flags = (elf_sec[i].sh_flags & 0x1) * READ_WRITE_BIT |
				 PRESENT_BIT,
		};
		RESET_LIST_ITEM(&entry.list);

		usable_entry[used_entris] = entry;
		list_add(&usable_entry[used_entris].list, &vmm_used_list);
		used_entris++;
	}

	invalidate_low_range();

	/**
	 * Temporanialiy bind the address 0 to the new pd
	 */
	void *pd_loc = phy_mem_alloc(PAGE_SIZE);

	map_page(pd_loc, (void *)0, PRESENT_BIT | READ_WRITE_BIT);
	memset((void *)0, 0, PAGE_SIZE);
	((uint32_t *)0)[1023] = (size_t)pd_loc | PRESENT_BIT | READ_WRITE_BIT;

	list_for_each(&vmm_used_list) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);

		for (void *virt_addr = cur->ptr;
		     virt_addr < (cur->ptr + cur->size);
		     virt_addr += PAGE_SIZE) {
			if (((size_t)virt_addr & 0xfff) != 0)
				panic("address is not 4k aligned");

			map_page(pd_loc, (void *)0, PRESENT_BIT);

			size_t pd_idx = (size_t)virt_addr >> 22;
			size_t pt_idx = (size_t)virt_addr >> 12 & 0x03FF;

			size_t *px = (size_t *)0x0;

			if ((px[pd_idx] & PRESENT_BIT) == 0) {
				px[pd_idx] = (size_t)phy_mem_alloc(PAGE_SIZE);

				px[pd_idx] |= 1;
				px[pd_idx] |= cur->flags & READ_WRITE_BIT;

				map_page((void *)(px[pd_idx] & ~0xfff),
					 (void *)0, PRESENT_BIT);
				memset((void *)0, 0, PAGE_SIZE);
			} else {
				px[pd_idx] |= cur->flags & READ_WRITE_BIT;
				map_page((void *)(px[pd_idx] & ~0xfff),
					 (void *)0, PRESENT_BIT);
			}

			px[pt_idx] = ((size_t)get_physaddr((void *)virt_addr)) |
				     (cur->flags & 0xFFF);
		}
	}

	void *old_pd_loc = 0;
	__asm__ volatile("mov %%cr3, %0" : "=g"(old_pd_loc) : : "memory");

	__asm__ volatile("mov %0, %%cr3" : : "r"(pd_loc) : "memory");

	map_page(old_pd_loc, 0, PRESENT_BIT | READ_WRITE_BIT);

	memcpy((void *)0x0, (void *)0xfffff000, PAGE_SIZE);

	((uint32_t *)0)[1023] = (size_t)old_pd_loc | PRESENT_BIT |
				READ_WRITE_BIT;

	__asm__ volatile("mov %0, %%cr3" : : "r"(old_pd_loc) : "memory");

	map_page(old_pd_loc, 0, 0);
}

struct malloc_tag_entry {
	void *ptr; // Virtual address for specific allocaiton
	void *page; // Physical page address
	size_t size; // Total block size
	size_t used; // Sized used by this block

	struct list_head list;
};
LIST_HEAD(malloc_tags_list);
LIST_HEAD(free_malloc_tags_list);

static void alloc_malloc_tag()
{
	struct malloc_tag_entry *free_chunk = nullptr;
	list_for_each(&malloc_tags_list) {
		struct malloc_tag_entry *cur =
			list_entry(it, struct malloc_tag_entry, list);
		if ((free_chunk == nullptr ||
		     (free_chunk != nullptr &&
		      (cur->size - cur->used <
		       free_chunk->size - free_chunk->used))) &&
		    cur->size - cur->used >= PAGE_SIZE)
			free_chunk = cur;
	}

	struct malloc_tag_entry *new_tag_ptr = (void *)free_chunk->ptr;
	free_chunk->ptr += PAGE_SIZE;
	free_chunk->size -= PAGE_SIZE;

	void *new_phy_mem = phy_mem_alloc(PAGE_SIZE);
	map_page(new_phy_mem, new_tag_ptr, READ_WRITE_BIT | PRESENT_BIT);

	size_t new_tag_count = PAGE_SIZE / sizeof(struct malloc_tag_entry);

	for (size_t i = 0; i < new_tag_count; i++) {
		struct malloc_tag_entry new_tag = { nullptr };
		new_tag_ptr[i] = new_tag;

		list_add(&new_tag_ptr[i].list, &free_malloc_tags_list);
	}

	struct malloc_tag_entry *tag =
		list_entry(list_pop(&free_malloc_tags_list),
			   struct malloc_tag_entry, list);

	tag->ptr = new_tag_ptr;
	tag->page = new_phy_mem;
	tag->size = PAGE_SIZE;
	tag->used = (PAGE_SIZE / sizeof(struct malloc_tag_entry)) *
		    sizeof(struct malloc_tag_entry);
	list_add(&tag->list, free_chunk->list.prev);
}

static struct malloc_tag_entry *malloc_find_free_space(size_t req_size)
{
	struct malloc_tag_entry *free_chunk = nullptr;
	list_for_each(&malloc_tags_list) {
		struct malloc_tag_entry *cur =
			list_entry(it, struct malloc_tag_entry, list);

		size_t cur_free = cur->size - cur->used;
		bool update_chunk_sel =
			cur_free >= req_size &&
			(free_chunk == nullptr ||
			 (cur_free < free_chunk->size - free_chunk->used));

		if (update_chunk_sel) {
			free_chunk = cur;
		}
	}

	return free_chunk;
}

static struct malloc_tag_entry *get_free_malloc_tag(void)
{
	struct list_head *free_tag_entry = list_pop(&free_malloc_tags_list);
	if (free_tag_entry == nullptr) {
		alloc_malloc_tag();
		free_tag_entry = list_pop(&free_malloc_tags_list);
	}

	return list_entry(free_tag_entry, struct malloc_tag_entry, list);
}

static void free_useless_tag(struct malloc_tag_entry *tag)
{
	if (tag->size == 0) {
		list_rm(&tag->list);
		memset(tag, 0, sizeof(struct malloc_tag_entry));
		list_add(&tag->list, &free_malloc_tags_list);
	}
}

void *kmalloc(size_t req_size)
{
	if (req_size == 0 || req_size > PAGE_SIZE)
		panic("Request size in kmalloc is not valid: value [%u]",
		      req_size);

	struct malloc_tag_entry *free_chunk = malloc_find_free_space(req_size);
	struct malloc_tag_entry *free_tag = get_free_malloc_tag();

	// Chunk if free_chunk is unused virtaul memory
	if (free_chunk->size >= PAGE_SIZE && free_chunk->page == nullptr) {
		if (free_chunk->ptr !=
		    round_up_to_page((size_t)free_chunk->ptr))
			panic("Unused Virtual memory should alaways be 4KiB aligned");

		free_tag->ptr = free_chunk->ptr;
		free_tag->size = PAGE_SIZE;
		free_tag->used = 0;
		free_tag->page = phy_mem_alloc(PAGE_SIZE);

		// Put it before the free_chunk to mantain sorting
		list_add(&free_tag->list, free_chunk->list.prev);

		map_page(free_tag->page, free_tag->ptr,
			 READ_WRITE_BIT | PRESENT_BIT);

		free_chunk->ptr += PAGE_SIZE;
		free_chunk->size -= PAGE_SIZE;

		free_useless_tag(free_chunk);

		// Update the free_chunk with the new allocated chunk
		free_chunk = free_tag;
		// Get a new free tag
		free_tag = get_free_malloc_tag();
	}

	free_tag->ptr = free_chunk->ptr + free_chunk->used;
	free_tag->size = free_chunk->size - free_chunk->used;
	free_tag->used = req_size;
	free_tag->page = free_chunk->page;
	list_add(&free_tag->list, &free_chunk->list);

	// There is no more usable space after free_chunk because
	// free_tag is imidiatly after
	free_chunk->size = free_chunk->used;
	free_useless_tag(free_chunk);

	// kprintf("Used malloc tags list\n");
	// size_t i = 0;
	// list_for_each(&malloc_tags_list) {
	// 	struct malloc_tag_entry *tag =
	// 		list_entry(it, struct malloc_tag_entry, list);
	// 	kprintf("\033[1;32m%u) ptr: %x | page: %x | size: %x | used: %x\033[0m\n", i++,
	// 		tag->ptr, tag->page, tag->size, tag->used);
	// }

	return free_tag->ptr;
}

static struct malloc_tag_entry *find_tag_to_free(void *ptr)
{
	struct malloc_tag_entry *tag_to_free = nullptr;

	list_for_each(&malloc_tags_list) {
		struct malloc_tag_entry *cur =
			list_entry(it, struct malloc_tag_entry, list);

		if (cur->ptr == ptr) {
			kprintf("Found free page\n"
				"   - ptr: %x | "
				"page: %x | "
				"size: %x | "
				"used: %x\n",
				cur->ptr, cur->page, cur->size, cur->used);

			tag_to_free = cur;
		}
	}

	if (tag_to_free == nullptr) {
		panic("Requested free of an unknown ptr");
	}

	return tag_to_free;
}

void kfree(void *ptr)
{
	struct malloc_tag_entry *chunk = find_tag_to_free(ptr);
	struct malloc_tag_entry *prev_chunk =
		list_prev_entry_circular(chunk, &malloc_tags_list, list);
	struct malloc_tag_entry *next_chunk =
		list_next_entry_circular(chunk, &malloc_tags_list, list);

	if (prev_chunk->page != chunk->page && (size_t)chunk->ptr & 0xfff) {
		panic("Chunk not aligned on page without a prev chunk");
	} else if (prev_chunk->page == chunk->page &&
		   prev_chunk->ptr + prev_chunk->size != chunk->ptr) {
		panic("Prev chunk and chunk should be contigus if on the same page");
	} else if (next_chunk->page == chunk->page &&
		   chunk->ptr + chunk->size != next_chunk->ptr) {
		panic("Chunk and next chunk should be contigus if on the same page");
	}

	if (next_chunk->page == chunk->page &&
	    prev_chunk->page == chunk->page) {
		if (next_chunk->used == 0) {
			chunk->size += next_chunk->size;
			next_chunk->size = 0;
			free_useless_tag(next_chunk);
		}

		prev_chunk->size += chunk->size;
		chunk->size = 0;
		free_useless_tag(chunk);

		if (prev_chunk->size == PAGE_SIZE && prev_chunk->used == 0) {
			map_page(prev_chunk->page, prev_chunk->ptr, 0);
			phy_mem_free(prev_chunk->page);
			prev_chunk->page = nullptr;
		}
	} else if (next_chunk->page != chunk->page &&
		   prev_chunk->page != chunk->page) {
		map_page(chunk->page, chunk->ptr, 0);
		phy_mem_free(chunk->page);
		chunk->page = nullptr;
		chunk->used = 0;
	} else if (next_chunk->page == chunk->page &&
		   prev_chunk->page != chunk->page) {
		if (next_chunk->used == 0) {
			chunk->size += next_chunk->size;
			next_chunk->size = 0;
			free_useless_tag(next_chunk);
		}
		chunk->used = 0;
	} else if (next_chunk->page != chunk->page &&
		   prev_chunk->page == chunk->page) {
		prev_chunk->size += chunk->size;
		chunk->size = 0;
		free_useless_tag(chunk);

		if (prev_chunk->size == PAGE_SIZE && prev_chunk->used == 0) {
			map_page(prev_chunk->page, prev_chunk->ptr, 0);
			phy_mem_free(prev_chunk->page);
			prev_chunk->page = nullptr;
		}
	}

	// kprintf("Used malloc tags list\n");
	// size_t i = 0;
	// list_for_each(&malloc_tags_list) {
	// 	struct malloc_tag_entry *tag =
	// 		list_entry(it, struct malloc_tag_entry, list);
	// 	kprintf("\033[1;31m%u) ptr: %x | page: %x | size: %x | used: %x\033[0m\n", i++,
	// 		tag->ptr, tag->page, tag->size, tag->used);
	// }
}

void init_vir_mem(multiboot_info_t *mbd)
{
	struct vmm_entry init_vmm_entry[PAGE_SIZE / sizeof(struct vmm_entry)];
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

	Elf32_Shdr *elf_sec = mbd->u.elf_sec.addr;
	char *elf_sec_str = (char *)elf_sec[mbd->u.elf_sec.shndx].sh_addr;

	for (size_t i = 0; i < mbd->u.elf_sec.num; i++) {
		kprintf("Section (%s): [Address: %x, Size: %x, Type: %x, flags: %x]\n",
			&elf_sec_str[elf_sec[i].sh_name], elf_sec[i].sh_addr,
			elf_sec[i].sh_size, elf_sec[i].sh_type,
			elf_sec[i].sh_flags);

		if ((elf_sec[i].sh_flags & 0x2) == 0) {
			kprintf("Section (%s) dosn't allocate memory at runtime\n",
				&elf_sec_str[elf_sec[i].sh_name]);
			continue;
		}

		list_for_each(&vmm_free_list) {
			struct vmm_entry *cur =
				list_entry(it, struct vmm_entry, list);

			/*
			 * Intersect the two range, and in case split
			 * the two found entry or remove it
			 */

			void *cur_s = cur->ptr;
			void *cur_e = cur->ptr + cur->size;

			void *elf_s = (void *)elf_sec[i].sh_addr;
			void *elf_e =
				(void *)elf_sec[i].sh_addr + elf_sec[i].sh_size;

			if (elf_s > cur_e || cur_s > elf_e)
				continue;

			void *range_s = (void *)round_down_to_page(
				(size_t)cur_s > (size_t)elf_s ? (size_t)cur_s :
								(size_t)elf_s);
			void *range_e = (void *)round_up_to_page(
				(size_t)cur_e < (size_t)elf_e ? (size_t)cur_e :
								(size_t)elf_e);

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
				init_vmm_entry[init_vmm_entris_used] =
					left_entry;
				list_add(&init_vmm_entry[init_vmm_entris_used]
						  .list,
					 cur->list.prev);
				init_vmm_entris_used++;
			}
			if (right_entry.size > 0) {
				init_vmm_entry[init_vmm_entris_used] =
					right_entry;
				list_add(&init_vmm_entry[init_vmm_entris_used]
						  .list,
					 &cur->list);
				init_vmm_entris_used++;
			}
			list_rm(&cur->list);
		}
	}

	recreate_vir_mem(mbd->u.elf_sec);

	/* alloc_malloc_tag */
	struct vmm_entry *free_block = nullptr;
	list_for_each(&vmm_free_list) {
		struct vmm_entry *cur = list_entry(it, struct vmm_entry, list);
		if (cur->size >= PAGE_SIZE) {
			free_block = cur;
			break;
		}
	}

	struct malloc_tag_entry *new_tag_ptr =
		(struct malloc_tag_entry *)free_block->ptr;

	void *new_tag_phy_page = phy_mem_alloc(PAGE_SIZE);
	map_page(new_tag_phy_page, new_tag_ptr, READ_WRITE_BIT | PRESENT_BIT);

	size_t new_tag_count = PAGE_SIZE / sizeof(struct malloc_tag_entry);

	free_block->ptr += PAGE_SIZE;
	free_block->size -= PAGE_SIZE;

	for (size_t i = 0; i < new_tag_count; i++) {
		struct malloc_tag_entry new_tag = { nullptr };
		new_tag_ptr[i] = new_tag;

		list_add(&new_tag_ptr[i].list, free_malloc_tags_list.prev);
	}

	struct malloc_tag_entry *tag =
		list_entry(list_pop(&free_malloc_tags_list),
			   struct malloc_tag_entry, list);

	tag->ptr = new_tag_ptr;
	tag->size = PAGE_SIZE;
	tag->used = (PAGE_SIZE / sizeof(struct malloc_tag_entry)) *
		    sizeof(struct malloc_tag_entry);
	tag->page = new_tag_phy_page;

	list_for_each(&vmm_free_list) {
		struct vmm_entry *vmm_cur =
			list_entry(it, struct vmm_entry, list);

		struct malloc_tag_entry *vmm_tag =
			list_entry(list_pop(&free_malloc_tags_list),
				   struct malloc_tag_entry, list);

		vmm_tag->ptr = vmm_cur->ptr;
		vmm_tag->size = vmm_cur->size;
		vmm_tag->used = 0;

		list_add(&vmm_tag->list, malloc_tags_list.prev);
		if (vmm_tag->ptr - PAGE_SIZE == tag->ptr)
			list_add(&tag->list, vmm_tag->list.prev);
	}

	kprintf("Used malloc tags list\n");
	size_t i = 0;
	list_for_each(&malloc_tags_list) {
		struct malloc_tag_entry *tag =
			list_entry(it, struct malloc_tag_entry, list);
		kprintf("%u) ptr: %x | size: %x | used: %x\n", i++, tag->ptr,
			tag->size, tag->used);
	}
}
