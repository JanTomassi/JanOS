#pragma once
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val)
{
	__asm__ volatile("out %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline void outw(uint16_t port, uint16_t val)
{
	__asm__ volatile("out %w0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline void outd(uint16_t port, uint32_t val)
{
	__asm__ volatile("out %k0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port)
{
	uint8_t ret;
	__asm__ volatile("in %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
	return ret;
}

static inline uint16_t inw(uint16_t port)
{
	uint16_t ret;
	__asm__ volatile("in %w1, %w0" : "=a"(ret) : "Nd"(port) : "memory");
	return ret;
}

static inline uint32_t ind(uint16_t port)
{
	uint32_t ret;
	__asm__ volatile("in %w1, %k0" : "=a"(ret) : "Nd"(port) : "memory");
	return ret;
}

static inline void io_wait(void)
{
	outb(0x80, 0);
}
