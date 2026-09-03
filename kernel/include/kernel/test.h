#pragma once

#include <stddef.h>

/* Run boot-time kernel integration tests, if the test image requests them. */
void kernel_test_boot(const void *multiboot_info, size_t multiboot_info_size);
