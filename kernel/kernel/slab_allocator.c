#include <kernel/allocator.h>
#include <kernel/display.h>
#include <kernel/mem_allocs.h>
#include <kernel/phy_mem.h>
#include <kernel/vir_mem.h>

#include <list.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

MODULE("Slab Allocator");

typedef mem_malloc_tag_t malloc_tag_t;
typedef mem_phy_mem_tag_t phy_mem_tag_t;

fatptr_t mem_gpa_alloc(size_t req);
void mem_gpa_free(fatptr_t freeing);

struct slab_object {
	struct slab_object *next;
};

struct slab {
	void *mem;
	size_t len;
	size_t in_use;
	size_t capacity;
	struct slab_object *free_list;
	malloc_tag_t *tag;
	struct list_head list;
	struct slab_cache *cache;
};

struct slab_cache {
	const char *name;
	size_t obj_size;
	size_t align;
	size_t objs_per_slab;
	size_t free_objs;
	size_t reserve_free;
	slab_ctor_t ctor;
	slab_dtor_t dtor;
	bool release_empty;

	struct list_head partial;
	struct list_head full;
	struct list_head empty;
	struct list_head list;
};

static struct list_head slab_caches = { .next = &slab_caches, .prev = &slab_caches };
static bool slab_initialized = false;
static bool tag_caches_ready = false;

static slab_cache_t malloc_tag_cache = { 0 };
static slab_cache_t phy_mem_tag_cache = { 0 };
static slab_cache_t phy_mem_link_cache = { 0 };

static struct slab malloc_tag_slab = { 0 };
static struct slab phy_mem_tag_slab = { 0 };
static struct slab phy_mem_link_slab = { 0 };

void slab_set_cache_reserve(slab_cache_t *cache, size_t reserve_free);

static void *slab_take_cache_obj_no_grow(slab_cache_t *cache);
static void slab_free_pages(malloc_tag_t *tag);
static void slab_init_bootstrap_cache(slab_cache_t *cache, struct slab *slab, const char *name, size_t obj_size, size_t align,
				      void *buffer, size_t obj_count, bool release_empty);

static size_t align_up(size_t val, size_t align)
{
	return (val + (align - 1)) & ~(align - 1);
}

static malloc_tag_t *slab_alloc_pages(size_t req)
{
	malloc_tag_t *tag = slab_take_cache_obj_no_grow(&malloc_tag_cache);
	if (tag == nullptr)
		BUG("malloc tag reserve exhausted");
	if (tag == nullptr)
		return nullptr;

	size_t req_align = round_up_to_page(req);
	struct vmm_entry *vir_mem = vmm_alloc(req_align, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);
	if (vir_mem == nullptr)
		goto fail_tag;

	mem_set_ptr_tag(tag, vir_mem->ptr);
	mem_set_size_tag(tag, vir_mem->size);
	mem_set_used_tag(tag, req_align);
	mem_set_vmm_tag(tag, vir_mem);
	struct list_head *tag_chain = mem_get_chain_tag(tag);

	mem_register_tag(tag);

	for (void *vir_ptr = mem_get_ptr_tag(tag); vir_ptr < mem_get_ptr_tag(tag) + mem_get_size_tag(tag); vir_ptr += PAGE_SIZE) {
		fatptr_t phy_mem = phy_mem_alloc(PAGE_SIZE);
		if (phy_mem.ptr == nullptr)
			goto fail_mem;

		struct vmm_entry vir_info = {
			.ptr = vir_ptr,
			.size = PAGE_SIZE,
			.flags = vir_mem->flags,
		};
		RESET_LIST_ITEM(&vir_info.list);

		map_pages(&phy_mem, &vir_info);
		memset(vir_info.ptr, 0, vir_info.size);

		phy_mem_tag_t *phy_tag = mem_get_phy_mem_tag();
		mem_set_fatptr_phy_mem(phy_tag, phy_mem);
		mem_set_refcnt_phy_mem(phy_tag, 1);

		mem_insert_phy_mem_tag(phy_tag, tag_chain, false);
	}

	return tag;
fail_mem:
	slab_free_pages(tag);
	return nullptr;
fail_tag:
	mem_give_tag(tag);
	return nullptr;
}

static void slab_free_pages(malloc_tag_t *tag)
{
	if (tag == nullptr)
		return;

	void *tag_ptr = mem_get_ptr_tag(tag);
	struct list_head *chain = mem_get_chain_tag(tag);

	list_for_each(chain) {
		phy_mem_tag_t *phy_tag = mem_get_phy_tag_from_link(it);
		fatptr_t fatptr = mem_get_fatptr_phy_mem(phy_tag);
		switch (mem_get_refcnt_phy_mem(phy_tag)) {
		case 0:
			BUG("phy_mem_tag with a zero refcnt\n");
			break;
		case 1: {
			struct vmm_entry vir_mem = {
				.ptr = (void *)((size_t)tag_ptr & 0xfffff000),
				.size = fatptr.len,
				.flags = 0,
			};
			RESET_LIST_ITEM(&vir_mem.list);

			map_pages(&fatptr, &vir_mem);
			phy_mem_free(fatptr);
			break;
		}
		default:
			break;
		}
		tag_ptr += fatptr.len;
	}

	bool same_size = mem_get_vmm_tag(tag)->size == mem_get_size_tag(tag);
	bool same_ptr = mem_get_vmm_tag(tag)->ptr == mem_get_ptr_tag(tag);

	if (same_size && same_ptr) {
		vmm_free(mem_get_vmm_tag(tag)->ptr);
	}

	mem_unregister_tag(tag);
}

static struct slab *slab_new_slab(slab_cache_t *cache)
{
	if (cache == nullptr)
		return nullptr;

	malloc_tag_t *tag = slab_alloc_pages(PAGE_SIZE);
	if (tag == nullptr)
		return nullptr;

	struct slab *slab = mem_gpa_alloc(sizeof(*slab)).ptr;
	if (slab == nullptr) {
		slab_free_pages(tag);
		return nullptr;
	}

	slab->mem = mem_get_ptr_tag(tag);
	slab->len = mem_get_size_tag(tag);
	slab->tag = tag;
	slab->in_use = 0;
	slab->capacity = cache->objs_per_slab;
	slab->cache = cache;
	RESET_LIST_ITEM(&slab->list);

	slab->free_list = nullptr;

	uint8_t *walker = slab->mem;
	for (size_t i = 0; i < cache->objs_per_slab; i++) {
		struct slab_object *obj = (struct slab_object *)(walker + (i * cache->obj_size));
		obj->next = slab->free_list;
		slab->free_list = obj;
	}

	cache->free_objs += cache->objs_per_slab;
	list_add(&slab->list, &cache->empty);
	return slab;
}

static void slab_release_slab(struct slab *slab)
{
	if (slab == nullptr)
		return;

	if (slab->tag == nullptr)
		return;

	if (slab->list.next != nullptr && slab->list.prev != nullptr)
		list_rm(&slab->list);
	slab_free_pages(slab->tag);
	mem_gpa_free((fatptr_t){ .ptr = slab, .len = sizeof(*slab) });
}

static struct slab *slab_find_for_ptr(slab_cache_t *cache, void *ptr)
{
	list_for_each(&cache->partial) {
		struct slab *slab = list_entry(it, struct slab, list);
		uint8_t *base = slab->mem;
		if ((uint8_t *)ptr >= base && (uint8_t *)ptr < base + slab->len)
			return slab;
	}
	list_for_each(&cache->full) {
		struct slab *slab = list_entry(it, struct slab, list);
		uint8_t *base = slab->mem;
		if ((uint8_t *)ptr >= base && (uint8_t *)ptr < base + slab->len)
			return slab;
	}
	list_for_each(&cache->empty) {
		struct slab *slab = list_entry(it, struct slab, list);
		uint8_t *base = slab->mem;
		if ((uint8_t *)ptr >= base && (uint8_t *)ptr < base + slab->len)
			return slab;
	}
	return nullptr;
}

static bool slab_in_free_list(const struct slab *slab, const void *ptr)
{
	for (const struct slab_object *it = slab->free_list; it != nullptr; it = it->next) {
		if (it == ptr)
			return true;
	}
	return false;
}

static void *slab_take_obj(struct slab *slab)
{
	if (slab->free_list == nullptr)
		return nullptr;

	struct slab_object *obj = slab->free_list;
	slab->free_list = obj->next;
	slab->in_use += 1;
	return obj;
}

static void slab_return_obj(struct slab *slab, void *ptr)
{
	struct slab_object *node = ptr;
	node->next = slab->free_list;
	slab->free_list = node;
	slab->in_use -= 1;
}

static void *slab_take_cache_obj_no_grow(slab_cache_t *cache)
{
	if (cache == nullptr)
		return nullptr;

	struct slab *target = nullptr;

	if (cache->partial.next != &cache->partial)
		target = list_entry(cache->partial.next, struct slab, list);
	else if (cache->empty.next != &cache->empty)
		target = list_entry(cache->empty.next, struct slab, list);
	else
		return nullptr;

	void *obj = slab_take_obj(target);
	if (obj == nullptr)
		return nullptr;

	cache->free_objs -= 1;
	if (cache->ctor != nullptr)
		cache->ctor(obj);
	else
		memset(obj, 0, cache->obj_size);

	if (target->in_use == target->capacity)
		list_mv(&target->list, &cache->full);
	else if (list_is_first(&target->list, &cache->empty))
		list_mv(&target->list, &cache->partial);

	return obj;
}

static slab_cache_t *slab_find_or_create_cache(size_t size)
{
	list_for_each(&slab_caches) {
		slab_cache_t *cache = list_entry(it, slab_cache_t, list);
		if (cache->obj_size == size)
			return cache;
	}

	return slab_create("general", size, sizeof(void *), nullptr, nullptr);
}

static void ensure_initialized(void)
{
	if (slab_initialized)
		return;

	slab_initialized = true;
}

slab_cache_t *slab_create(const char *name, size_t obj_size, size_t align, slab_ctor_t ctor, slab_dtor_t dtor)
{
	if (obj_size == 0)
		return nullptr;

	size_t real_align = align == 0 ? sizeof(void *) : align;
	if (real_align < sizeof(void *))
		real_align = sizeof(void *);

	size_t aligned_size = align_up(obj_size, real_align);
	if (aligned_size < sizeof(struct slab_object))
		aligned_size = sizeof(struct slab_object);

	size_t objs_per_slab = PAGE_SIZE / aligned_size;
	if (objs_per_slab == 0)
		return nullptr;

	slab_cache_t *cache = mem_gpa_alloc(sizeof(*cache)).ptr;
	if (cache == nullptr)
		return nullptr;

	cache->name = name;
	cache->obj_size = aligned_size;
	cache->align = real_align;
	cache->objs_per_slab = objs_per_slab;
	cache->free_objs = 0;
	cache->reserve_free = 0;
	cache->ctor = ctor;
	cache->dtor = dtor;
	cache->release_empty = true;

	RESET_LIST_ITEM(&cache->partial);
	RESET_LIST_ITEM(&cache->full);
	RESET_LIST_ITEM(&cache->empty);
	RESET_LIST_ITEM(&cache->list);

	list_add(&cache->list, slab_caches.prev);
	return cache;
}

fatptr_t slab_alloc_obj(slab_cache_t *cache)
{
	if (cache == nullptr) {
		BUG("cache must not be nullptr");
	}

	if (!slab_initialized)
		ensure_initialized();

	struct slab *target = nullptr;

	if (cache->reserve_free > 0 && cache->free_objs <= cache->reserve_free) {
		target = slab_new_slab(cache);
		if (target == nullptr)
			return (fatptr_t){ .ptr = nullptr, .len = 0 };
	}

	if (target == nullptr && cache->partial.next != &cache->partial)
		target = list_entry(cache->partial.next, struct slab, list);
	else if (target == nullptr && cache->empty.next != &cache->empty)
		target = list_entry(cache->empty.next, struct slab, list);

	if (target == nullptr)
		target = slab_new_slab(cache);

	if (target == nullptr)
		return (fatptr_t){ .ptr = nullptr, .len = 0 };

	void *obj = slab_take_obj(target);
	if (obj == nullptr)
		return (fatptr_t){ .ptr = nullptr, .len = 0 };

	cache->free_objs -= 1;
	if (cache->ctor != nullptr)
		cache->ctor(obj);
	else
		memset(obj, 0, cache->obj_size);

	if (target->in_use == target->capacity)
		list_mv(&target->list, &cache->full);
	else if (list_is_first(&target->list, &cache->empty))
		list_mv(&target->list, &cache->partial);

	return (fatptr_t){ .ptr = obj, .len = cache->obj_size };
}

bool slab_free_obj(slab_cache_t *cache, fatptr_t obj)
{
	if (cache == nullptr || obj.ptr == nullptr)
		return false;

	struct slab *slab = slab_find_for_ptr(cache, obj.ptr);
	if (slab == nullptr) {
		mprint("slab: cross-cache free rejected (%x)\n", obj.ptr);
		return false;
	}

	const size_t offset = (size_t)((uint8_t *)obj.ptr - (uint8_t *)slab->mem);
	if (offset % cache->obj_size != 0) {
		mprint("slab: unaligned free (%x)\n", obj.ptr);
		return false;
	}

	if (slab_in_free_list(slab, obj.ptr)) {
		mprint("slab: double free detected (%x)\n", obj.ptr);
		return false;
	}

	if (cache->dtor != nullptr)
		cache->dtor(obj.ptr);

	slab_return_obj(slab, obj.ptr);
	cache->free_objs += 1;

	if (slab->in_use == 0) {
		list_mv(&slab->list, &cache->empty);
		if (cache->release_empty && cache->free_objs - slab->capacity >= cache->reserve_free) {
			cache->free_objs -= slab->capacity;
			slab_release_slab(slab);
		}
		return true;
	}

	if (list_is_first(&slab->list, &cache->full))
		list_mv(&slab->list, &cache->partial);

	return true;
}

void slab_destroy(slab_cache_t *cache)
{
	if (cache == nullptr)
		return;

	list_rm(&cache->list);

	while (cache->partial.next != &cache->partial) {
		struct slab *slab = list_entry(cache->partial.next, struct slab, list);
		slab_release_slab(slab);
	}
	while (cache->full.next != &cache->full) {
		struct slab *slab = list_entry(cache->full.next, struct slab, list);
		slab_release_slab(slab);
	}
	while (cache->empty.next != &cache->empty) {
		struct slab *slab = list_entry(cache->empty.next, struct slab, list);
		slab_release_slab(slab);
	}

	mem_gpa_free((fatptr_t){ .ptr = cache, .len = sizeof(*cache) });
}

void init_slab_allocator(void)
{
	if (slab_initialized)
		return;

	ensure_initialized();
}

static void slab_init_bootstrap_cache(slab_cache_t *cache, struct slab *slab, const char *name, size_t obj_size, size_t align,
				      void *buffer, size_t obj_count, bool release_empty)
{
	size_t real_align = align == 0 ? sizeof(void *) : align;
	if (real_align < sizeof(void *))
		real_align = sizeof(void *);

	size_t aligned_size = align_up(obj_size, real_align);
	if (aligned_size < sizeof(struct slab_object))
		aligned_size = sizeof(struct slab_object);

	cache->name = name;
	cache->obj_size = aligned_size;
	cache->align = real_align;
	cache->objs_per_slab = obj_count;
	cache->free_objs = obj_count;
	cache->reserve_free = 0;
	cache->ctor = nullptr;
	cache->dtor = nullptr;
	cache->release_empty = release_empty;

	RESET_LIST_ITEM(&cache->partial);
	RESET_LIST_ITEM(&cache->full);
	RESET_LIST_ITEM(&cache->empty);
	RESET_LIST_ITEM(&cache->list);

	slab->mem = buffer;
	slab->len = aligned_size * obj_count;
	slab->in_use = 0;
	slab->capacity = obj_count;
	slab->free_list = nullptr;
	slab->tag = nullptr;
	slab->cache = cache;
	RESET_LIST_ITEM(&slab->list);

	uint8_t *walker = slab->mem;
	for (size_t i = 0; i < obj_count; i++) {
		struct slab_object *obj = (struct slab_object *)(walker + (i * aligned_size));
		obj->next = slab->free_list;
		slab->free_list = obj;
	}

	list_add(&slab->list, &cache->empty);
}

void slab_init_tag_caches(mem_malloc_tag_t *malloc_tags, size_t malloc_tag_count, mem_phy_mem_tag_t *phy_tags, size_t phy_tag_count,
			  mem_phy_mem_link_t *phy_links, size_t phy_link_count)
{
	if (tag_caches_ready)
		return;

	if (malloc_tags == nullptr || phy_tags == nullptr || phy_links == nullptr)
		BUG("tag slab buffers must not be null");

	if (malloc_tag_count == 0 || phy_tag_count == 0 || phy_link_count == 0)
		BUG("tag slab buffers must not be empty");

	slab_initialized = true;

	slab_init_bootstrap_cache(&malloc_tag_cache, &malloc_tag_slab, "malloc_tag", sizeof(malloc_tag_t), _Alignof(malloc_tag_t), malloc_tags,
				  malloc_tag_count, false);
	slab_init_bootstrap_cache(&phy_mem_tag_cache, &phy_mem_tag_slab, "phy_mem_tag", sizeof(phy_mem_tag_t), _Alignof(phy_mem_tag_t), phy_tags,
				  phy_tag_count, false);
	slab_init_bootstrap_cache(&phy_mem_link_cache, &phy_mem_link_slab, "phy_mem_link", sizeof(mem_phy_mem_link_t), _Alignof(mem_phy_mem_link_t),
				  phy_links, phy_link_count, false);

	slab_set_cache_reserve(&malloc_tag_cache, 1);
	tag_caches_ready = true;
}

void slab_set_cache_reserve(slab_cache_t *cache, size_t reserve_free)
{
	if (cache == nullptr)
		return;

	cache->reserve_free = reserve_free;
	while (cache->free_objs < cache->reserve_free) {
		struct slab *slab = slab_new_slab(cache);
		if (slab == nullptr)
			break;
	}
	if (cache->free_objs < cache->reserve_free)
		BUG("slab cache reserve unmet");
}

slab_cache_t *slab_get_malloc_tag_cache(void)
{
	return tag_caches_ready ? &malloc_tag_cache : nullptr;
}

slab_cache_t *slab_get_phy_mem_tag_cache(void)
{
	return tag_caches_ready ? &phy_mem_tag_cache : nullptr;
}

slab_cache_t *slab_get_phy_mem_link_cache(void)
{
	return tag_caches_ready ? &phy_mem_link_cache : nullptr;
}

static fatptr_t slab_general_alloc(size_t req)
{
	if (!slab_initialized)
		ensure_initialized();

	if (req == 0)
		return (fatptr_t){ .ptr = nullptr, .len = 0 };

	if (req > PAGE_SIZE)
		return mem_gpa_alloc(req);

	slab_cache_t *cache = slab_find_or_create_cache(align_up(req, sizeof(void *)));
	return slab_alloc_obj(cache);
}

static void slab_general_free(fatptr_t fatptr)
{
	if (!slab_initialized)
		ensure_initialized();

	if (fatptr.ptr == nullptr || fatptr.len == 0)
		return;

	if (fatptr.len > PAGE_SIZE) {
		mem_gpa_free(fatptr);
		return;
	}

	slab_cache_t *cache = slab_find_or_create_cache(align_up(fatptr.len, sizeof(void *)));
	slab_free_obj(cache, fatptr);
}

allocator_t get_slab_allocator(void)
{
	ensure_initialized();
	return (allocator_t){ .alloc = slab_general_alloc, .free = slab_general_free };
}
