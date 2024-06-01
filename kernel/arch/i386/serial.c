#include <kernel/serial.h>
#include <kernel/display.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "./port.h"

#define PORT 0x3f8 // COM1

static void write_char(char c)
{
	if (c == '\n')
		outb(PORT + 0, '\r');
	outb(PORT + 0, c);
}

static void serial_write_str(const char *str)
{
	size_t len = strlen(str);
	for (size_t i = 0; i < len; i++) {
		if (str[i] == '\n') {
			write_char('\r');
			write_char('\n');
		} else {
			write_char(str[i]);
		}
	}
}

display_t init_serial()
{
	outb(PORT + 1, 0x00); // Disable all interrupts
	outb(PORT + 3, 0x80); // Enable DLAB (set baud rate divisor)
	outb(PORT + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
	outb(PORT + 1, 0x00); //                  (hi byte)
	outb(PORT + 3, 0x03); // 8 bits, no parity, one stop bit
	outb(PORT + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
	outb(PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
	outb(PORT + 4, 0x1E); // Set in loopback mode, test the serial chip
	outb(PORT + 0,
	     0xAE); // Test serial chip (send byte 0xAE and check if serial returns same byte)

	// Check if serial is faulty (i.e: not same byte as sent)
	if (inb(PORT + 0) != 0xAE)
		return (display_t){ .width = 0,
				    .height = 0,
				    .putc = nullptr,
				    .puts = nullptr };

	// If serial is not faulty set it in normal operation mode
	// (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
	outb(PORT + 4, 0x0F);

	return (display_t){ .width = 0,
			    .height = 0,
			    .putc = write_char,
			    .puts = serial_write_str };
}
