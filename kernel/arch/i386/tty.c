#include <stddef.h>
#include <stdint.h>

#include <kernel/tty.h>
#include <kernel/timer.h>
#include <kernel/display.h>

#include "vga.h"

extern void idt_init(void);

size_t strlen(const char *str)
{
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
static inline size_t buffer_offset(size_t x, size_t y, size_t stripe)
{
	return y * stripe + x;
}

size_t cursor_row;
size_t cursor_column;

size_t tty_width;
size_t tty_height;

uint16_t *tty_buffer;

uint8_t tty_color;
uint8_t tty_border_color;

void tty_setcolor(uint8_t color)
{
	tty_color = color;
}

static inline void tty_putentryat(char c, uint8_t color, size_t x, size_t y)
{
	const size_t index = buffer_offset(x, y, VGA_WIDTH);
	tty_buffer[index] = vga_entry(c, color);
}

void scroll_up()
{
	for (size_t y = 2; y < VGA_HEIGHT - 1; y++)
		for (size_t x = 2; x < VGA_WIDTH - 2; x++) {
			const size_t new_index = buffer_offset(x, y, VGA_WIDTH);
			const size_t old_index =
				buffer_offset(x, y - 1, VGA_WIDTH);
			tty_buffer[old_index] = tty_buffer[new_index];
		}

	for (size_t x = 2; x < VGA_WIDTH - 2; x++) {
		tty_putentryat(' ', tty_color, x, VGA_HEIGHT - 2);
	}
}

void advanced_one()
{
	if (++cursor_column >= VGA_WIDTH) {
		cursor_column = 0;
		if (++cursor_row >= VGA_HEIGHT - 1) {
			cursor_row = VGA_HEIGHT - 2;
			scroll_up();
		}
	}
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
		if (++cursor_row >= VGA_HEIGHT - 1) {
			--cursor_row;
			scroll_up();
		}
		return;
	}

	if (cursor_column == VGA_WIDTH - 2) {
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

void tty_write(const char *data, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		tty_putchar(data[i]);
	}
}

void tty_write_str(const char *data)
{
	tty_write(data, strlen(data));
}

void tty_reset()
{
	for (size_t y = 0; y < VGA_HEIGHT; y++)
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			switch (x) {
			case 0:
			case VGA_WIDTH - 1:
				tty_putentryat('|', tty_border_color, x, y);
				break;
			case 1:
			case VGA_WIDTH - 2:

				tty_putentryat(' ', tty_border_color, x, y);
				break;
			default:
				tty_putentryat(' ', tty_color, x, y);
				break;
			}

			switch (y) {
			case 0:
			case VGA_HEIGHT - 1:

				tty_putentryat('-', tty_border_color, x, y);
			}
		}
}

display_t tty_initialize(size_t buffer_addr, size_t pitch, size_t width,
			 size_t height, uint8_t bit_per_pixel, bool is_text)
{
	tty_buffer = (void *)buffer_addr;

	cursor_row = 1;
	cursor_column = 0;

	tty_width = width;
	tty_height = height;

	tty_color = vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
	tty_border_color = vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);

	return (display_t){ .width = tty_width,
			    .height = tty_height,
			    .putc = tty_putchar,
			    .puts = tty_write_str };
}
