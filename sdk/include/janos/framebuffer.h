#pragma once

#include <stdint.h>
#include <janos/syscall.h>

/* The framebuffer service is an IPC protocol, not a kernel console. */
#define JANOS_FRAMEBUFFER_PROTOCOL_VERSION 1u
#define JANOS_FB_PUTS_MAX (JANOS_IPC_PAYLOAD_SIZE - sizeof(uint32_t))

enum janos_framebuffer_message_type {
	JANOS_FB_MSG_PUTC = 0x46420001u,
	JANOS_FB_MSG_PUTS = 0x46420002u,
	JANOS_FB_MSG_CURSOR = 0x46420003u,
	JANOS_FB_MSG_CLEAR = 0x46420004u,
	JANOS_FB_MSG_SCROLL = 0x46420005u,
	JANOS_FB_MSG_INFO = 0x46420006u,
};

enum janos_framebuffer_status {
	JANOS_FB_STATUS_OK = 0,
	JANOS_FB_STATUS_INVALID = JANOS_EINVAL,
	JANOS_FB_STATUS_UNSUPPORTED = 95,
};

/* All of these structures are payloads and therefore fit the fixed IPC ABI. */
struct janos_fb_putc {
	uint32_t value;
};

struct janos_fb_puts {
	uint32_t length;
	char text[JANOS_FB_PUTS_MAX];
};

struct janos_fb_cursor {
	uint32_t column;
	uint32_t row;
};

struct janos_fb_scroll {
	uint32_t rows;
};

struct janos_fb_reply {
	int32_t status;
};

struct janos_fb_info_reply {
	int32_t status;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t bpp;
	uint32_t type;
	uint32_t columns;
	uint32_t rows;
	uint32_t font_size;
	uint32_t font_header_size;
	uint32_t font_glyph_count;
	uint32_t font_glyph_size;
	uint32_t font_width;
	uint32_t font_height;
};

/* Address is the user virtual address in the framebuffer server. */
struct janos_framebuffer_info {
	uint32_t address;
	uint32_t size;
	uint32_t pitch;
	uint32_t width;
	uint32_t height;
	uint32_t bpp;
	uint32_t type;
};

_Static_assert(sizeof(struct janos_fb_puts) == JANOS_IPC_PAYLOAD_SIZE,
	"framebuffer puts payload must fit IPC");
_Static_assert(sizeof(struct janos_fb_reply) <= JANOS_IPC_PAYLOAD_SIZE,
	"framebuffer reply must fit IPC");
_Static_assert(sizeof(struct janos_fb_info_reply) <= JANOS_IPC_PAYLOAD_SIZE,
	"framebuffer info reply must fit IPC");
_Static_assert(sizeof(struct janos_framebuffer_info) == 28,
	"framebuffer metadata ABI changed");
