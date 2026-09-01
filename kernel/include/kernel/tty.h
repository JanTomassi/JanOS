#pragma once

#include <stddef.h>
#include <stdint.h>

#include <kernel/display.h>

void tty_reset();
display_t tty_initialize(size_t buffer_addr, size_t pitch, size_t width, size_t height, uint8_t bit_per_pixel, bool is_text);

void tty_setcolor(uint8_t color);

size_t console_read(char *buffer, size_t length);
size_t console_write(const char *buffer, size_t length);
