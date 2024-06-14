#include <kernel/display.h>
#include <string.h>

#include "tty_text.h"
#include "../vga.h"

uint16_t *tty_buffer;

size_t cursor_row;
size_t cursor_column;

size_t tty_width;
size_t tty_height;

uint8_t tty_color;
uint8_t tty_border_color;

static inline size_t buffer_offset(size_t x, size_t y, size_t stripe)
{
	return y * stripe + x;
}

static inline void tty_putentryat(char c, uint8_t color, size_t x, size_t y)
{
	const size_t index = buffer_offset(x, y, tty_width);
	tty_buffer[index] = vga_entry(c, color);
}

static void scroll_up()
{
	for (size_t y = 2; y < tty_height - 1; y++)
		for (size_t x = 2; x < tty_width - 2; x++) {
			const size_t new_index = buffer_offset(x, y, tty_width);
			const size_t old_index =
				buffer_offset(x, y - 1, tty_width);
			tty_buffer[old_index] = tty_buffer[new_index];
		}

	for (size_t x = 2; x < tty_width - 2; x++) {
		tty_putentryat(' ', tty_color, x, tty_height - 2);
	}
}

static void advanced_one()
{
	if (++cursor_column >= tty_width) {
		cursor_column = 0;
		if (++cursor_row >= tty_height - 1) {
			cursor_row = tty_height - 2;
			scroll_up();
		}
	}
}

static void tty_setcolor(uint8_t color)
{
	tty_color = color;
}

void tty_putchar(char c)
{
	if (c == '\n') {
#ifdef TTY_SLOW_MODE
		{
			size_t tick_to_wait = GLOBAL_TICK + 2;
			while (GLOBAL_TICK < tick_to_wait)
				;
		}
#endif
		cursor_column = 0;
		if (++cursor_row >= tty_height - 1) {
			--cursor_row;
			scroll_up();
		}
		return;
	}

	if (cursor_column == tty_width - 2) {
		tty_putentryat(' ', tty_border_color, cursor_column,
			       cursor_row);
		advanced_one();

		tty_putentryat('|', tty_border_color, cursor_column,
			       cursor_row);
		advanced_one();
	}
	if (cursor_column == 0) {
		tty_putentryat('|', tty_border_color, cursor_column,
			       cursor_row);
		advanced_one();

		tty_putentryat(' ', tty_border_color, cursor_column,
			       cursor_row);
		advanced_one();
	}

	tty_putentryat(c, tty_color, cursor_column, cursor_row);
	advanced_one();
}

static void tty_write(const char *data, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		tty_putchar(data[i]);
	}
}

void tty_write_str(const char *data)
{
	tty_write(data, strlen(data));
}

static void tty_reset()
{
	for (size_t y = 0; y < tty_height; y++)
		for (size_t x = 0; x < tty_width; x++) {
			if (y == 0 && y == tty_height - 1) {
				tty_putentryat('-', tty_border_color, x, y);
			} else if (x == 0 && x == tty_width - 1) {
				tty_putentryat('|', tty_border_color, x, y);
			} else if (x == 1 && x == tty_width - 2) {
				tty_putentryat(' ', tty_border_color, x, y);
			} else {
				tty_putentryat(' ', tty_color, x, y);
			}
		}
}

display_t tty_text_initialize(size_t buffer_addr, size_t pitch, size_t width,
			      size_t height, uint8_t bit_per_pixel)
{
	tty_buffer = (void *)buffer_addr;

	cursor_row = 1;
	cursor_column = 0;

	tty_width = width;
	tty_height = height;

	tty_color = vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
	tty_border_color = vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);

	tty_reset();

	return (display_t){ .width = tty_width,
			    .height = tty_height,
			    .putc = tty_putchar,
			    .puts = tty_write_str };
}
