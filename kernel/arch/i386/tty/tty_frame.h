#include <stdint.h>
#include <kernel/display.h>

display_t tty_frame_initialize(size_t buffer_addr, size_t pitch, size_t width,
			       size_t height, uint8_t bit_per_pixel);
