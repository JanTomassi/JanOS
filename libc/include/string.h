#pragma once

#include <sys/cdefs.h>
#include <stddef.h>
#include <stdint.h>

int memcmp(const void *, const void *, size_t);
void *memcpy(void *__restrict, const void *__restrict, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, uint8_t, size_t);
size_t strlen(const char *);
int strcmp(const void *, const void *);
int strncmp(const void *, const void *, size_t);
