#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kernel/allocator.h>
#include <kernel/fat16.h>
#include <kernel/psf2.h>

#define PSF2_MAGIC 0x864AB572u
#define PSF2_HEADER_SIZE 32u
#define PSF2_MAX_SIZE (1024u * 1024u)

static uint32_t psf2_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool psf2_validate(const uint8_t *data, size_t size)
{
	if (data == nullptr || size < PSF2_HEADER_SIZE || psf2_u32(data) != PSF2_MAGIC)
		return false;

	uint32_t version = psf2_u32(data + 4);
	uint32_t header_size = psf2_u32(data + 8);
	uint32_t flags = psf2_u32(data + 12);
	uint32_t glyph_count = psf2_u32(data + 16);
	uint32_t glyph_size = psf2_u32(data + 20);
	uint32_t height = psf2_u32(data + 24);
	uint32_t width = psf2_u32(data + 28);
	if (version != 0 || flags > 1 || header_size < PSF2_HEADER_SIZE || header_size > size || glyph_count == 0 ||
		glyph_size == 0 || width == 0 || height == 0 || width > 32 || height > 64)
		return false;
	if ((size_t)glyph_count > (size - header_size) / glyph_size)
		return false;
	return header_size + (size_t)glyph_count * glyph_size == size;
}

bool psf2_load_from_device(const struct block_device *device, const char *name,
	void **out_data, size_t *out_size)
{
	if (out_data == nullptr || out_size == nullptr)
		return false;
	*out_data = nullptr;
	*out_size = 0;

	struct fat16_file file;
	if (!fat16_file_open(device, name, &file) || file.entry.file_size < PSF2_HEADER_SIZE ||
		file.entry.file_size > PSF2_MAX_SIZE)
		return false;
	allocator_t alloc = get_gpa_allocator();
	size_t size = file.entry.file_size;
	fatptr_t data = alloc.alloc(size);
	if (data.ptr == nullptr)
		return false;
	size_t bytes = 0;
	if (!fat16_file_read_at(&file, 0, data.ptr, size, &bytes) || bytes != size ||
		!psf2_validate(data.ptr, size)) {
		alloc.free(data);
		return false;
	}
	*out_data = data.ptr;
	*out_size = size;
	return true;
}
