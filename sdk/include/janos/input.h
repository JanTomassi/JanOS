#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <janos/syscall.h>

#define JANOS_INPUT_PROTOCOL_VERSION 1u

enum janos_input_message_type {
	JANOS_INPUT_MSG_RAW = 0x494e0001u,
	JANOS_INPUT_MSG_KEY = 0x494e0002u,
};

/* Logical keys emitted by the userspace input server. */
enum janos_input_key_code {
	JANOS_INPUT_KEY_NONE = 0,
	JANOS_INPUT_KEY_CHARACTER,
	JANOS_INPUT_KEY_BACKSPACE,
	JANOS_INPUT_KEY_ENTER,
	JANOS_INPUT_KEY_TAB,
	JANOS_INPUT_KEY_ESCAPE,
	JANOS_INPUT_KEY_HOME,
	JANOS_INPUT_KEY_END,
	JANOS_INPUT_KEY_ARROW_UP,
	JANOS_INPUT_KEY_ARROW_DOWN,
	JANOS_INPUT_KEY_ARROW_LEFT,
	JANOS_INPUT_KEY_ARROW_RIGHT,
	JANOS_INPUT_KEY_DELETE,
	JANOS_INPUT_KEY_SHIFT,
	JANOS_INPUT_KEY_CTRL,
	JANOS_INPUT_KEY_ALT,
	JANOS_INPUT_KEY_CAPSLOCK,
};

#define JANOS_INPUT_EVENT_CHARACTER_MASK 0xffu
#define JANOS_INPUT_EVENT_KEY_SHIFT 8u
#define JANOS_INPUT_EVENT_KEY_MASK 0xffu
#define JANOS_INPUT_EVENT_PRESSED (1u << 16)
#define JANOS_INPUT_EVENT_SHIFT (1u << 17)
#define JANOS_INPUT_EVENT_CTRL (1u << 18)
#define JANOS_INPUT_EVENT_ALT (1u << 19)
#define JANOS_INPUT_EVENT_CAPSLOCK (1u << 20)

static inline uint32_t janos_input_event_make(uint32_t key, uint32_t character,
	                                              bool pressed, uint32_t modifiers)
{
	uint32_t value = (character & JANOS_INPUT_EVENT_CHARACTER_MASK) |
		((key & JANOS_INPUT_EVENT_KEY_MASK) << JANOS_INPUT_EVENT_KEY_SHIFT);
	if (pressed)
		value |= JANOS_INPUT_EVENT_PRESSED;
	return value | (modifiers & (JANOS_INPUT_EVENT_SHIFT |
		JANOS_INPUT_EVENT_CTRL | JANOS_INPUT_EVENT_ALT |
		JANOS_INPUT_EVENT_CAPSLOCK));
}

static inline uint32_t janos_input_event_key(uint32_t event)
{
	return (event >> JANOS_INPUT_EVENT_KEY_SHIFT) & JANOS_INPUT_EVENT_KEY_MASK;
}

static inline uint32_t janos_input_event_character(uint32_t event)
{
	return event & JANOS_INPUT_EVENT_CHARACTER_MASK;
}

static inline bool janos_input_event_pressed(uint32_t event)
{
	return (event & JANOS_INPUT_EVENT_PRESSED) != 0;
}
