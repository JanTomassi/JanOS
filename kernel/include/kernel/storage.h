#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef bool (*storage_read_handler_t)(uint32_t lba_addr, uint16_t sector_count, void *dest);
typedef bool (*storage_write_handler_t)(uint32_t lba_addr, uint16_t sector_count, const void *src);

extern storage_read_handler_t storage_read_handler;
extern storage_write_handler_t storage_write_handler;

void storage_init(void);
bool storage_read(uint32_t lba_addr, uint16_t sector_count, void *dest);
bool storage_write(uint32_t lba_addr, uint16_t sector_count, const void *src);
