#include <stdlib.h>
#include <string.h>

#include <kernel/allocator.h>
#include <kernel/fat16.h>


fat_BS_t *read_fat_boot_section(struct storage_device dev){
	allocator_t gpa_alloc = get_gpa_allocator();
	fatptr_t boot_sector = gpa_alloc.alloc(512);

	fat_BS_t *boot_sector_p = boot_sector.ptr;
	memset(boot_sector_p, 0, 512);

	storage_read_device(&dev, 0, 1, boot_sector_p);

	return boot_sector_p;
}
