#pragma once

#include <stddef.h>

typedef void (*kernel_boot_test_fn)(const void *multiboot_info,
                                    size_t multiboot_info_size);

void kernel_boot(unsigned int magic, unsigned long mbi_addr,
                 kernel_boot_test_fn boot_test);
