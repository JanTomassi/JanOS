#pragma once

/* Compatibility include for code that has not moved to the block-device API. */
#include <kernel/block_device.h>

typedef struct block_device storage_device;

#define storage_init block_device_init
#define storage_device_count block_device_count
#define storage_get_device block_device_get
#define storage_read_device block_device_read
#define storage_write_device block_device_write
