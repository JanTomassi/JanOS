#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <kernel/phy_mem.h>

struct address_space;

struct address_space *address_space_create(void);
void address_space_destroy(struct address_space *space);

/* MVP: this is the active page directory frame and is not owned by the space. */
const fatptr_t *address_space_page_directory(const struct address_space *space);

/* Map a range at an automatically selected user address. */
bool address_space_map(struct address_space *space, size_t size, uint16_t flags,
                       void **address);

/* Map a page-aligned range at exactly address in the active address space. */
bool address_space_map_at(struct address_space *space, uintptr_t address,
                          size_t size, uint16_t flags, void **mapped);

/* These operations record ownership and update the active address space. */
bool address_space_unmap(struct address_space *space, void *address);
