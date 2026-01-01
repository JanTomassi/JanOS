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

void init_slab_allocator(void);
allocator_t get_slab_allocator(void);
