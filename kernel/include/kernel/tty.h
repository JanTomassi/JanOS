#pragma once

#include <stddef.h>
#include <stdint.h>

#include <kernel/display.h>

void tty_reset();
display_t tty_initialize(size_t buffer_addr, size_t pitch, size_t width,
			 size_t height, uint8_t bit_per_pixel, bool is_text);

void tty_setcolor(uint8_t color);

/* void tty_putchar(char c); */
/* void tty_write(const char *data, size_t size); */

/* void tty_write_hex(uintmax_t num); */
/* void tty_write_unumber(uintmax_t num); */
/* void tty_write_number(intmax_t num); */
/* void tty_write_str(const char *data); */
