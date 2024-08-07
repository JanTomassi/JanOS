#pragma once

#include <stddef.h>
#include <stdlib.h>

struct mem_malloc_tag;
struct mem_phy_mem_link;
struct mem_phy_mem_tag;

typedef struct mem_malloc_tag mem_malloc_tag_t;
typedef struct mem_phy_mem_link mem_phy_mem_link_t;
typedef struct mem_phy_mem_tag mem_phy_mem_tag_t;

fatptr_t mem_get_fatptr_phy_mem(const mem_phy_mem_tag_t *);
size_t mem_get_refcnt_phy_mem(const mem_phy_mem_tag_t *);
fatptr_t mem_set_fatptr_phy_mem(mem_phy_mem_tag_t *, fatptr_t val);
size_t mem_set_refcnt_phy_mem(mem_phy_mem_tag_t *, size_t val);

void *mem_get_ptr_tag(const mem_malloc_tag_t *);
size_t mem_get_size_tag(const mem_malloc_tag_t *);
size_t mem_get_used_tag(const mem_malloc_tag_t *);
struct vmm_entry *mem_get_vmm_tag(const mem_malloc_tag_t *ptr);
struct list_head *mem_get_chain_tag(const mem_malloc_tag_t *);
void *mem_set_ptr_tag(mem_malloc_tag_t *, void *);
size_t mem_set_size_tag(mem_malloc_tag_t *, size_t);
size_t mem_set_used_tag(mem_malloc_tag_t *, size_t);
struct vmm_entry *mem_set_vmm_tag(mem_malloc_tag_t *ptr, struct vmm_entry *val);

// Find best effort fit for the req return a new allocated
// malloc_tag_t will return nullptr for no match
mem_malloc_tag_t *mem_find_best_fit(size_t req);

// Register a malloc_tag to allocator manager
void mem_register_tag(mem_malloc_tag_t *);
// Coalesce with free near tag this must be call before mem_unregister_tag
mem_malloc_tag_t *mem_coalesce_tag(mem_malloc_tag_t *);
// Unregister a malloc_tag to allocator manager
void mem_unregister_tag(mem_malloc_tag_t *);

// Insert phy_mem_tag sorted by ptr in the chain
void mem_insert_phy_mem_tag(mem_phy_mem_tag_t *tag, struct list_head *chain,
			    bool sort);
// Give the phy_mem_tag back to free_phy_tags_list
void mem_give_phy_mem_tag(mem_phy_mem_tag_t *);
// Get the phy_mem_tag from free_phy_tags_list
mem_phy_mem_tag_t *mem_get_phy_mem_tag();

// Give phy_mem_link* back to free_phy_links_list
void mem_give_phy_mem_link(mem_phy_mem_link_t *);
// Get phy_mem_link* from free_phy_links_list
mem_phy_mem_link_t *mem_get_phy_mem_link();
// Get the phy mem tag from a list of phy_mem_link
mem_phy_mem_tag_t *mem_get_phy_tag_from_link(struct list_head *link);

// Insert malloc_tag sorted by ptr in the chain
void mem_insert_tag(mem_malloc_tag_t *tag, struct list_head *list);
// Remove malloc_tag and coalesce with the near
// return true if the coalesce was successful
bool mem_remove_tag(mem_malloc_tag_t *tag, struct list_head *list);
// Give malloc_tag* back to free_tags_list
void mem_give_tag(mem_malloc_tag_t *);
// Get malloc_tag* from free_tags_list
mem_malloc_tag_t *mem_get_tag();

#ifdef DEBUG
void mem_debug_lists(void);
#endif
