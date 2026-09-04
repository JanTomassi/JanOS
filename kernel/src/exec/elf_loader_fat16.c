#include <kernel/elf_loader.h>

#include <kernel/allocator.h>
#include <kernel/fat16.h>

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
