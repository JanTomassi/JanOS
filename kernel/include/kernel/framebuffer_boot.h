#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <arch/i386/context.h>

struct block_device;

bool framebuffer_boot_services(const void *multiboot_info,
                               size_t multiboot_info_size,
                               const struct block_device *device,
                               struct i386_context *initial_context);
bool framebuffer_boot_clear(void);
uint32_t framebuffer_boot_endpoint(void);
size_t framebuffer_console_write(const char *buffer, size_t length);
size_t framebuffer_console_read(char *buffer, size_t length);
void framebuffer_console_enable(void);
void framebuffer_console_flush(void);
