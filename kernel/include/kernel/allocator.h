#pragma once

#include <stddef.h>
#include <stdlib.h>

typedef struct {
	fatptr_t (*alloc)(size_t req);
	void (*free)(fatptr_t);
} allocator_t;

allocator_t get_gpa_allocator();
