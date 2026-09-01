#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <kernel/phy_mem.h>

struct address_space;

struct address_space *address_space_create(void);
void address_space_destroy(struct address_space *space);

/* The frame is owned by the address space and must not be freed by callers. */
const fatptr_t *address_space_page_directory(const struct address_space *space);

/* These operations record ownership and map the active address space. */
bool address_space_map(struct address_space *space, size_t size, uint16_t flags,
                       void **address);
bool address_space_unmap(struct address_space *space, void *address);
