#include "port.h"

static inline void play_sound(size_t freq, size_t len)
{
	unsigned int divisor = 1193180 / freq;
	outb(0x43, 0xB6); // Command byte for PIT
	outb(0x42, divisor & 0xFF); // Low byte of divisor
	outb(0x42, (divisor >> 8) & 0xFF); // High byte of divisor
	outb(0x61, inb(0x61) | 3); // Turn on the speaker

	for (size_t i = 0; i < len * 10000000; i++)
		;

	outb(0x61, inb(0x61) & 0xFC); // Turn off the speaker

	for (size_t i = 0; i < len * 10000000; i++)
		;
}
