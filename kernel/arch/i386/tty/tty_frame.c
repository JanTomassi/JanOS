#include "tty_frame.h"

static uint8_t *tty_buffer;

static size_t cursor_row;
static size_t cursor_column;

static size_t tty_width;
static size_t tty_height;

static uint32_t tty_color;
static uint32_t tty_background;

display_t tty_frame_initialize(size_t buffer_addr, size_t pitch, size_t width,
			       size_t height, uint8_t bit_per_pixel)
{
	tty_buffer = (uint8_t *)buffer_addr;

	cursor_row = 1;
	cursor_column = 0;

	tty_width = width;
	tty_height = height;

	tty_color = 0xffffffff;
	tty_background = 0x000000ff;

	// tty_reset();

	// return (display_t){ .width = tty_width,
	// 		    .height = tty_height,
	// 		    .putc = tty_putchar,
	// 		    .puts = tty_write_str };

	return (display_t){};
}
