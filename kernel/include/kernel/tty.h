#pragma once

#include <stddef.h>
#include <stdint.h>

struct i386_trap_frame;

int32_t console_read(char *buffer, size_t length, struct i386_trap_frame *frame);
size_t console_write(const char *buffer, size_t length);
