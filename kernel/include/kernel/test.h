#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Run boot-time kernel integration tests, if the test image requests them. */
void kernel_test_boot(const void *multiboot_info, size_t multiboot_info_size);

/* Test-only serial markers and deterministic QEMU completion. */
void kernel_test_marker(const char *name, bool passed);
[[noreturn]] void kernel_test_finish(uint32_t status);
