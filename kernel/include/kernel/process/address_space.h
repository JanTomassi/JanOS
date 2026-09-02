#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <kernel/phy_mem.h>

struct address_space;

struct address_space *address_space_create(void);
void address_space_destroy(struct address_space *space);

/* The process-owned page directory frame. */
const fatptr_t *address_space_page_directory(const struct address_space *space);

/* Activate this address space on the current CPU. */
bool address_space_activate(struct address_space *space);

/* Map a range at an automatically selected user address. */
bool address_space_map(struct address_space *space, size_t size, uint16_t flags,
                       void **address);

/* Map a page-aligned range at exactly address in this address space. */
bool address_space_map_at(struct address_space *space, uintptr_t address,
                          size_t size, uint16_t flags, void **mapped);
bool address_space_protect(struct address_space *space, uintptr_t address,
                            size_t size, uint16_t flags);
bool address_space_validate(const struct address_space *space, uintptr_t address,
                            size_t size, uint16_t required_flags);
bool address_space_copy_from(struct address_space *space, void *destination,
                             uintptr_t address, size_t size);
bool address_space_copy_to(struct address_space *space, uintptr_t address,
                            const void *source, size_t size);
bool address_space_zero(struct address_space *space, uintptr_t address,
                        size_t size);

/* These operations record ownership and update this address space. */
bool address_space_unmap(struct address_space *space, void *address);
