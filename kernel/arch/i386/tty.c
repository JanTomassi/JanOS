#include <stddef.h>
#include <stdint.h>

#include <kernel/tty.h>
#include <kernel/timer.h>
#include <kernel/display.h>

#include "vga.h"
#include "tty/tty_text.h"
#include "tty/tty_frame.h"

size_t strlen(const char *str)
{
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}

display_t tty_initialize(size_t buffer_addr, size_t pitch, size_t width,
			 size_t height, uint8_t bit_per_pixel, bool is_text)
{
	if (is_text)
		return tty_text_initialize(buffer_addr, pitch, width, height,
					   bit_per_pixel);
	else
		return tty_frame_initialize(buffer_addr, pitch, width, height,
					    bit_per_pixel);
}
