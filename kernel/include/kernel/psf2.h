#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <kernel/block_device.h>

/* Load a validated PSF2 file from the root of a FAT16 block device. */
bool psf2_load_from_device(const struct block_device *device, const char *name,
	void **out_data, size_t *out_size);
