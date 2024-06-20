#pragma once

#include <stddef.h>
#include <stdlib.h>

fatptr_t mem_gpa_alloc(size_t req);
void mem_gpa_free(fatptr_t);
