#pragma once

#include <stdbool.h>

#include <arch/i386/context.h>

struct block_device;

bool stage5_boot_services(const struct block_device *device,
                          struct i386_context *initial_context,
                          bool *initial_context_ready);
