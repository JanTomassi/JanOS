#include <kernel/elf_loader.h>

#include <kernel/elf32.h>
#include <kernel/fat16.h>
#include <kernel/allocator.h>
#include <kernel/vir_mem.h>
#include <string.h>

#define ELF32_CLASS 1
#define ELF32_DATA_LSB 1
#define ELF32_TYPE_EXEC 2
#define ELF32_MACHINE_I386 3
#define ELF32_PT_LOAD 1
#define ELF32_PF_X 1
#define ELF32_PF_W 2
#define ELF32_PF_R 4
#define ELF32_PT_DYNAMIC 2
#define ELF32_PT_INTERP 3
#define ELF32_PT_TLS 7
#define ELF32_PT_PHDR 6
#define ELF32_PT_GNU_STACK 0x6474e551
#define ELF32_MAX_LOAD_SEGMENTS 128

struct elf32_header {
	unsigned char ident[16]; uint16_t type, machine; uint32_t version, entry;
	uint32_t phoff, shoff, flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));
struct elf32_program_header {
	uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} __attribute__((packed));
struct loaded_range { uintptr_t start, end; };

static bool range_ok(uint32_t start, uint32_t length, size_t limit, size_t *end)
{
	uint64_t finish = (uint64_t)start + length;
	if (finish > 0xc0000000u || finish < start || finish > limit)
		return false;
	*end = (size_t)finish;
	return true;
}

static void unmap_ranges(const struct elf_load_ops *ops,
			const struct loaded_range *ranges, size_t count)
{
	if (ops->unmap == nullptr)
		return;
	for (size_t i = 0; i < count; ++i)
		ops->unmap(ranges[i].start, ranges[i].end - ranges[i].start, ops->context);
}

bool elf32_load(const void *image, size_t image_size,
		const struct elf_load_ops *ops, struct elf_load_result *result)
{
	if (image == nullptr || ops == nullptr || ops->map == nullptr || result == nullptr ||
		image_size < sizeof(struct elf32_header))
		return false;
	const struct elf32_header *header = image;
	if (header->ident[0] != 0x7f || header->ident[1] != 'E' || header->ident[2] != 'L' ||
		header->ident[3] != 'F' || header->ident[4] != ELF32_CLASS || header->ident[5] != ELF32_DATA_LSB ||
		header->ident[6] != 1 || header->type != ELF32_TYPE_EXEC ||
		header->machine != ELF32_MACHINE_I386 || header->version != 1 ||
		header->ehsize != sizeof(struct elf32_header) ||
		header->phentsize != sizeof(struct elf32_program_header) || header->phnum == 0)
		return false;
	uint64_t ph_end = (uint64_t)header->phoff + (uint64_t)header->phnum * header->phentsize;
	if (ph_end > image_size || header->phnum > ELF32_MAX_LOAD_SEGMENTS)
		return false;
	*result = (struct elf_load_result){ .entry = header->entry, .lowest_address = UINTPTR_MAX };
	struct loaded_range ranges[ELF32_MAX_LOAD_SEGMENTS];
	size_t range_count = 0;
	size_t mapped_count = 0;
	bool entry_valid = false;
	for (uint16_t i = 0; i < header->phnum; ++i) {
		const struct elf32_program_header *ph = (const void *)((const uint8_t *)image + header->phoff + i * header->phentsize);
		if (ph->type == ELF32_PT_DYNAMIC || ph->type == ELF32_PT_INTERP ||
			ph->type == ELF32_PT_TLS)
			goto fail;
		if (ph->type != ELF32_PT_LOAD)
			continue;
		if (ph->memsz < ph->filesz || (ph->align > 1 && (ph->align & (ph->align - 1)) != 0) ||
			(ph->align > 1 && ((ph->offset & (ph->align - 1)) != (ph->vaddr & (ph->align - 1)))) ||
			ph->vaddr < PAGE_SIZE || ph->vaddr >= 0xc0000000u) {
			unmap_ranges(ops, ranges, mapped_count);
			return false;
		}
		size_t file_end, mem_end;
		if (!range_ok(ph->offset, ph->filesz, image_size, &file_end) ||
			!range_ok(ph->vaddr, ph->memsz, 0xc0000000u, &mem_end) || ph->memsz == 0) {
			unmap_ranges(ops, ranges, mapped_count);
			return false;
		}
		uintptr_t start = round_down_to_page(ph->vaddr);
		uintptr_t end = round_up_to_page(mem_end);
		if ((ph->flags & ELF32_PF_X) != 0 && header->entry >= ph->vaddr && header->entry < mem_end)
			entry_valid = true;
		for (size_t j = 0; j < range_count; ++j)
			if (start < ranges[j].end && ranges[j].start < end) {
				unmap_ranges(ops, ranges, mapped_count);
				return false;
			}
		ranges[range_count++] = (struct loaded_range){ .start = start, .end = end };
		uint32_t flags = ELF_LOAD_READ;
		if (ph->flags & ELF32_PF_W) flags |= ELF_LOAD_WRITE;
		if (ph->flags & ELF32_PF_X) flags |= ELF_LOAD_EXEC;
		/* Loading always needs writable pages; permissions are reduced only after
		 * file data and BSS have been installed. */
		void *mapped = ops->map(start, end - start, flags | ELF_LOAD_WRITE, ops->context);
		if (mapped == nullptr) {
			unmap_ranges(ops, ranges, mapped_count);
			return false;
		}
		++mapped_count;
		bool copied = ops->copy != nullptr
			? ops->copy(ph->vaddr, (const uint8_t *)image + ph->offset, ph->filesz, ops->context)
			: (memcpy((uint8_t *)mapped + (ph->vaddr - start),
				(const uint8_t *)image + ph->offset, ph->filesz), true);
		bool zeroed = ops->zero != nullptr
			? ops->zero(ph->vaddr + ph->filesz, ph->memsz - ph->filesz, ops->context)
			: (memset((uint8_t *)mapped + (ph->vaddr - start) + ph->filesz,
				0, ph->memsz - ph->filesz), true);
		if (!copied || !zeroed || (ops->protect != nullptr &&
			!ops->protect(start, end - start, flags, ops->context)))
			goto fail;
		if (start < result->lowest_address) result->lowest_address = start;
		if (end > result->highest_address) result->highest_address = end;
		++result->segment_count;
	}
	if (result->segment_count == 0 || !entry_valid) {
		unmap_ranges(ops, ranges, mapped_count);
		return false;
	}
	return true;

fail:
	unmap_ranges(ops, ranges, mapped_count);
	return false;
}

bool elf32_load_fat16(const struct block_device *device, const char *name,
			const struct elf_load_ops *ops, struct elf_load_result *result)
{
	if (device == nullptr || name == nullptr || ops == nullptr || result == nullptr)
		return false;
	struct fat16_file file;
	if (!fat16_file_open(device, name, &file) || file.entry.file_size == 0)
		return false;
	allocator_t allocator = get_gpa_allocator();
	fatptr_t image = allocator.alloc(file.entry.file_size);
	if (image.ptr == nullptr)
		return false;
	size_t bytes = 0;
	bool ok = fat16_file_read_at(&file, 0, image.ptr, file.entry.file_size, &bytes) &&
		bytes == file.entry.file_size && elf32_load(image.ptr, bytes, ops, result);
	allocator.free(image);
	return ok;
}
