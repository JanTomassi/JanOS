#include <kernel/memblock.h>
#include <string.h>
#include <kernel/memblock.h>
#include <kernel/multiboot.h>
#include <kernel/elf32.h>
#include <string.h>
#include <stdbool.h>

static inline size_t memblock_align_up(size_t addr, size_t align)
{
	if (align == 0)
		return addr;

	size_t mask = align - 1;
	return (addr + mask) & ~mask;
}

static inline size_t memblock_align_down(size_t addr, size_t align)
{
	if (align == 0)
		return addr;

	size_t mask = align - 1;
	return addr & ~mask;
}

static inline bool memblock_overlaps(size_t base1, size_t size1, size_t base2, size_t size2)
{
	if (size1 == 0 || size2 == 0)
		return false;

	size_t end1 = base1 + size1;
	size_t end2 = base2 + size2;

	if (end1 < base1)
		end1 = SIZE_MAX;
	if (end2 < base2)
		end2 = SIZE_MAX;

	return (base1 < end2) && (base2 < end1);
}

static void memblock_insert_region(struct memblock_type *type, size_t base, size_t size, unsigned long flags)
{
	if (size == 0 || type->cnt >= MEMBLOCK_MAX_REGIONS)
		return;

	unsigned int idx = 0;
	while (idx < type->cnt && type->regions[idx].base <= base)
		idx++;

	for (unsigned int move = type->cnt; move > idx; move--)
		type->regions[move] = type->regions[move - 1];

	type->regions[idx] = (struct memblock_region){
		.base = base,
		.size = size,
		.flags = flags,
	};
	type->cnt++;
}

static void memblock_merge_regions(struct memblock_type *type)
{
	if (type->cnt < 2)
		return;

	unsigned int i = 0;
	while (i + 1 < type->cnt) {
		struct memblock_region *cur = &type->regions[i];
		struct memblock_region *next = &type->regions[i + 1];
		size_t cur_end = cur->base + cur->size;
		size_t next_end = next->base + next->size;

		if (cur_end < cur->base)
			cur_end = SIZE_MAX;
		if (next_end < next->base)
			next_end = SIZE_MAX;

		if ((cur->flags == next->flags) && cur_end >= next->base) {
			if (next_end > cur_end)
				cur->size = next_end - cur->base;
			for (unsigned int move = i + 1; move + 1 < type->cnt; move++)
				type->regions[move] = type->regions[move + 1];
			type->cnt--;
			continue;
		}
		i++;
	}
}

static void memblock_add_region(struct memblock_type *type, size_t base, size_t size, unsigned long flags)
{
	memblock_insert_region(type, base, size, flags);
	memblock_merge_regions(type);
}

static void memblock_remove_range(struct memblock_type *type, size_t base, size_t size)
{
	if (size == 0 || type->cnt == 0)
		return;

	size_t end = base + size;
	if (end < base)
		end = SIZE_MAX;

	for (unsigned int i = 0; i < type->cnt;) {
		struct memblock_region *cur = &type->regions[i];
		size_t cur_end = cur->base + cur->size;

		if (cur_end < cur->base)
			cur_end = SIZE_MAX;

		if (!memblock_overlaps(base, size, cur->base, cur->size)) {
			i++;
			continue;
		}

		if (base <= cur->base && end >= cur_end) {
			for (unsigned int move = i; move + 1 < type->cnt; move++)
				type->regions[move] = type->regions[move + 1];
			type->cnt--;
			continue;
		}

		if (base > cur->base && end < cur_end) {
			if (type->cnt >= MEMBLOCK_MAX_REGIONS)
				return;
			struct memblock_region tail = {
				.base = end,
				.size = cur_end - end,
				.flags = cur->flags,
			};
			cur->size = base - cur->base;
			for (unsigned int move = type->cnt; move > i + 1; move--)
				type->regions[move] = type->regions[move - 1];
			type->regions[i + 1] = tail;
			type->cnt++;
			i += 2;
			continue;
		}

		if (base <= cur->base) {
			cur->base = end;
			cur->size = cur_end - end;
			i++;
			continue;
		}

		cur->size = base - cur->base;
		i++;
	}
}

static size_t memblock_find_bottom_up(const struct memblock *block, size_t size, size_t align, size_t start, size_t end)
{
	for (unsigned int i = 0; i < block->memory.cnt; i++) {
		const struct memblock_region *region = &block->memory.regions[i];
		size_t region_start = region->base;
		size_t region_end = region->base + region->size;

		if (region_end < region->base)
			region_end = SIZE_MAX;

		if (region_start < start)
			region_start = start;
		if (region_end > end)
			region_end = end;
		if (region_start >= region_end)
			continue;

		size_t candidate = memblock_align_up(region_start, align);

		while (candidate + size <= region_end) {
			bool overlap = false;
			for (unsigned int j = 0; j < block->reserved.cnt; j++) {
				const struct memblock_region *res = &block->reserved.regions[j];
				if (res->base + res->size <= candidate)
					continue;
				if (res->base >= candidate + size)
					break;
				overlap = true;
				candidate = memblock_align_up(res->base + res->size, align);
				break;
			}
			if (!overlap)
				return candidate;
			if (candidate + size > region_end)
				break;
		}
	}

	return MEMBLOCK_ALLOC_FAIL;
}

static size_t memblock_find_top_down(const struct memblock *block, size_t size, size_t align, size_t start, size_t end)
{
	for (int i = (int)block->memory.cnt - 1; i >= 0; i--) {
		const struct memblock_region *region = &block->memory.regions[i];
		size_t region_start = region->base;
		size_t region_end = region->base + region->size;

		if (region_end < region->base)
			region_end = SIZE_MAX;

		if (region_start < start)
			region_start = start;
		if (region_end > end)
			region_end = end;
		if (region_start >= region_end || region_end - region_start < size)
			continue;

		if (region_end < size)
			continue;

		size_t candidate = memblock_align_down(region_end - size, align);
		if (candidate < region_start)
			continue;

		while (candidate >= region_start) {
			bool overlap = false;
			for (int j = (int)block->reserved.cnt - 1; j >= 0; j--) {
				const struct memblock_region *res = &block->reserved.regions[j];
				if (res->base >= candidate + size)
					continue;
				if (res->base + res->size <= candidate)
					break;
				overlap = true;
				if (res->base < size)
					return MEMBLOCK_ALLOC_FAIL;
				candidate = memblock_align_down(res->base - size, align);
				break;
			}
			if (!overlap)
				return candidate;
			if (candidate < region_start)
				break;
		}
	}

	return MEMBLOCK_ALLOC_FAIL;
}

void memblock_add(struct memblock *block, size_t base, size_t size)
{
	memblock_add_region(&block->memory, base, size, MEMBLOCK_NONE);
}

void memblock_reserve(struct memblock *block, size_t base, size_t size)
{
	memblock_add_region(&block->reserved, base, size, MEMBLOCK_RESERVED);
}

void memblock_remove(struct memblock *block, size_t base, size_t size)
{
	memblock_remove_range(&block->memory, base, size);
}

size_t memblock_alloc_range(struct memblock *block, size_t size, size_t align, size_t start, size_t end)
{
	if (size == 0 || block == nullptr)
		return MEMBLOCK_ALLOC_FAIL;

	if (align == 0)
		align = 1;
	if (end == 0)
		end = SIZE_MAX;

	size_t addr = block->bottom_up
		? memblock_find_bottom_up(block, size, align, start, end)
		: memblock_find_top_down(block, size, align, start, end);

	if (addr != MEMBLOCK_ALLOC_FAIL)
		memblock_reserve(block, addr, size);

	return addr;
}

void memblock_free(struct memblock *block, size_t base, size_t size)
{
	memblock_remove_range(&block->reserved, base, size);
}

struct memblock block = {0};
extern void HIGHER_HALF;
const uintptr_t HIGHER_HALF_ADDR = (uintptr_t)&HIGHER_HALF;

struct rsdp_descriptor {
	char signature[8];
	uint8_t checksum;
	char oem_id[6];
	uint8_t revision;
	uint32_t rsdt_address;
	uint32_t length;
	uint64_t xsdt_address;
	uint8_t extended_checksum;
	uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char oem_id[6];
	char oem_table_id[8];
	uint32_t oem_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
} __attribute__((packed));

struct madt_header {
	struct acpi_sdt_header header;
	uint32_t lapic_addr;
	uint32_t flags;
	uint8_t entries[];
} __attribute__((packed));

struct madt_ioapic_info {
	uintptr_t phys_addr;
	uint32_t gsi_base;
	uint8_t ioapic_id;
};

struct mbi_info{
	struct multiboot_tag_mmap *mmap_tag;
	struct multiboot_tag_elf_sections *elf_sec_tag;
	struct multiboot_tag *acpi_tag;
	uintptr_t kernel_start_addr;
	uintptr_t kernel_end_addr;
};

inline static uintptr_t round_up_to_page(uintptr_t x){
	return (x + 0xFFF) & ~0xFFF;
}

static void parse_madt(void *phys_addr)
{
	static bool ioapic_found = false;

	struct madt_header *madt = (struct madt_header *)phys_addr;
	size_t madt_length = madt->header.length;
	memblock_remove(&block, (size_t)madt, madt_length);
	size_t offset = sizeof(struct madt_header);

	memblock_remove(&block, madt->lapic_addr, 0x1000);

	while (offset + 2 <= madt->header.length) {
		uint8_t *entry = ((uint8_t *)madt) + offset;
		uint8_t type = entry[0];
		uint8_t length = entry[1];
		if (length < 2)
			break;

		if (type == 1 && length >= 12 && !ioapic_found) {
			memblock_remove(&block, *(uint32_t *)(entry + 4), 0x40);
			ioapic_found = true;
		}

		offset += length;
	}
}

struct mbi_info init_memblock_from_mbi(uintptr_t mbi_addr, uintptr_t kernel_start_addr){
	struct mbi_info res = {.kernel_start_addr = -1, .kernel_end_addr = 0};

	// Filling the memblock with what ram is accessible
	for (struct multiboot_tag *tag = (struct multiboot_tag *)(mbi_addr + 8);
	     tag->type != MULTIBOOT_TAG_TYPE_END;
	     tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag + ((tag->size + 7) & ~7))) {
		if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
			res.mmap_tag = (struct multiboot_tag_mmap *)tag;
			multiboot_memory_map_t *mmap;
			for (mmap = res.mmap_tag->entries; (multiboot_uint8_t *)mmap < (multiboot_uint8_t *)tag + tag->size;
			     mmap = (multiboot_memory_map_t *)((unsigned long)mmap + res.mmap_tag->entry_size))
				if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE){
					memblock_add(&block, mmap->addr, mmap->len);
				}else if(mmap->type == MULTIBOOT_MEMORY_RESERVED){
					memblock_remove(&block, mmap->addr, mmap->len);
				}
		}
	}

	for (struct multiboot_tag *tag = (struct multiboot_tag *)(mbi_addr + 8); tag->type != MULTIBOOT_TAG_TYPE_END;
	     tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag + ((tag->size + 7) & ~7))) {
		switch (tag->type) {
		case MULTIBOOT_TAG_TYPE_ACPI_OLD:
		case MULTIBOOT_TAG_TYPE_ACPI_NEW: {
			res.acpi_tag = tag;
			struct rsdp_descriptor *rsdp = (struct rsdp_descriptor *)(((struct multiboot_tag_new_acpi *)res.acpi_tag)->rsdp);
			memblock_remove(&block, (size_t)rsdp, sizeof(struct rsdp_descriptor));

			struct acpi_sdt_header *rsdt = (void *)(uintptr_t)rsdp->rsdt_address;
			size_t rsdt_len = rsdt->length;
			memblock_remove(&block, (size_t)rsdt, rsdt_len);

			size_t entry_count = (rsdt->length - sizeof(struct acpi_sdt_header)) / sizeof(uint32_t);
			uint32_t *entries = (uint32_t *)((uint8_t *)rsdt + sizeof(struct acpi_sdt_header));
			for (size_t i = 0; i < entry_count; i++) {
				struct acpi_sdt_header *hdr = (struct acpi_sdt_header *)entries[i];
				size_t hdr_len = hdr->length;
				memblock_remove(&block, (size_t)hdr, hdr_len);
				bool is_apic = true;
				for(size_t i = 0; i<4; i++){
					is_apic = is_apic && hdr->signature[i] == "APIC"[i];
				}

				if (is_apic) {
					parse_madt(hdr);
				}
			}
		}
			break;
		case MULTIBOOT_TAG_TYPE_ELF_SECTIONS: {
			res.elf_sec_tag = (struct multiboot_tag_elf_sections *)tag;
			const Elf32_Shdr *elf_sec = (const Elf32_Shdr *)res.elf_sec_tag->sections;
			const char *elf_sec_str = (char *)(elf_sec[res.elf_sec_tag->shndx].sh_addr);
			Elf32_Shdr string_sec = elf_sec[res.elf_sec_tag->shndx];
			memblock_remove(&block,  string_sec.sh_addr, string_sec.sh_size);
			for (size_t i = 0; i < res.elf_sec_tag->num; i++) {
				if(!(elf_sec[i].sh_flags & ELF_SHF_ALLOC))
					continue;

				uintptr_t section_start = elf_sec[i].sh_addr;
				uintptr_t section_end = elf_sec[i].sh_addr + elf_sec[i].sh_size;

				/* Subtract if it is in the higher kernel */
				if(section_start >= HIGHER_HALF_ADDR){
					section_start -= HIGHER_HALF_ADDR;
				}
				if(section_end >= HIGHER_HALF_ADDR){
					section_end -= HIGHER_HALF_ADDR;
				}

				/* Find start and end of kernel code/data */
				if(section_end > res.kernel_end_addr){
					res.kernel_end_addr = section_end;
				}
				if(section_start < res.kernel_end_addr){
					res.kernel_start_addr = section_start;
				}

				/* Remove from memblock the memory that is being used by the kernel code */
				memblock_remove(&block, section_start, section_end - section_start);
			}
		}
			break;
		}

	}

	/* Remove memory used for the multiboot info from the usable space */
	memblock_remove(&block, mbi_addr, *(unsigned *)mbi_addr);
	return res;
}

void memblock_init(unsigned long mbi_addr, bool bottom_up)
{
	memset(&block, 0, sizeof(block));
	block.bottom_up = bottom_up;

	struct mbi_info mbi_info = init_memblock_from_mbi(mbi_addr, HIGHER_HALF_ADDR);
}
