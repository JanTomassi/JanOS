#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
	fatptr_t (*alloc)(size_t req);
	void (*free)(fatptr_t);
} allocator_t;

allocator_t get_gpa_allocator();

typedef void (*slab_ctor_t)(void *obj);
typedef void (*slab_dtor_t)(void *obj);

typedef struct slab_cache slab_cache_t;

slab_cache_t *slab_create(const char *name, size_t obj_size, size_t align, slab_ctor_t ctor, slab_dtor_t dtor);
fatptr_t slab_alloc_obj(slab_cache_t *cache);
bool slab_free_obj(slab_cache_t *cache, fatptr_t obj);
void slab_destroy(slab_cache_t *cache);

struct mem_malloc_tag;
struct mem_phy_mem_link;
struct mem_phy_mem_tag;

void slab_init_tag_caches(struct mem_malloc_tag *malloc_tags, size_t malloc_tag_count, struct mem_phy_mem_tag *phy_tags, size_t phy_tag_count,
			  struct mem_phy_mem_link *phy_links, size_t phy_link_count);
slab_cache_t *slab_get_malloc_tag_cache(void);
slab_cache_t *slab_get_phy_mem_tag_cache(void);
slab_cache_t *slab_get_phy_mem_link_cache(void);

void init_slab_allocator(void);
allocator_t get_slab_allocator(void);
