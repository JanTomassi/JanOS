#include <janos/input.h>
#include <janos/ipc.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

struct input_modifiers {
	bool shift;
	bool ctrl;
	bool alt;
	bool capslock;
};

static void print(const char *text)
{
	(void)write(1, text, strlen(text));
}

static void print_uint(uint32_t value)
{
	char buffer[11];
	size_t position = sizeof(buffer);
	buffer[--position] = '\0';
	if (value == 0)
		buffer[--position] = '0';
	while (value != 0) {
		buffer[--position] = (char)('0' + value % 10);
		value /= 10;
	}
	print(buffer + position);
}

static uint32_t parse_uint(const char *text)
{
	uint32_t value = 0;
	while (*text >= '0' && *text <= '9') {
		value = value * 10u + (uint32_t)(*text - '0');
		++text;
	}
	return value;
}

static char scan_character(uint8_t scan_code)
{
	switch (scan_code) {
	case 0x02: return '1';
	case 0x03: return '2';
	case 0x04: return '3';
	case 0x05: return '4';
	case 0x06: return '5';
	case 0x07: return '6';
	case 0x08: return '7';
	case 0x09: return '8';
	case 0x0a: return '9';
	case 0x0b: return '0';
	case 0x0c: return '-';
	case 0x0d: return '=';
	case 0x10: return 'q';
	case 0x11: return 'w';
	case 0x12: return 'e';
	case 0x13: return 'r';
	case 0x14: return 't';
	case 0x15: return 'y';
	case 0x16: return 'u';
	case 0x17: return 'i';
	case 0x18: return 'o';
	case 0x19: return 'p';
	case 0x1a: return '[';
	case 0x1b: return ']';
	case 0x1e: return 'a';
	case 0x1f: return 's';
	case 0x20: return 'd';
	case 0x21: return 'f';
	case 0x22: return 'g';
	case 0x23: return 'h';
	case 0x24: return 'j';
	case 0x25: return 'k';
	case 0x26: return 'l';
	case 0x27: return ';';
	case 0x28: return '\'';
	case 0x29: return '`';
	case 0x2b: return '\\';
	case 0x2c: return 'z';
	case 0x2d: return 'x';
	case 0x2e: return 'c';
	case 0x2f: return 'v';
	case 0x30: return 'b';
	case 0x31: return 'n';
	case 0x32: return 'm';
	case 0x33: return ',';
	case 0x34: return '.';
	case 0x35: return '/';
	case 0x39: return ' ';
	default: return '\0';
	}
}

static char shifted_character(uint8_t scan_code, char character)
{
	switch (scan_code) {
	case 0x02: return '!';
	case 0x03: return '@';
	case 0x04: return '#';
	case 0x05: return '$';
	case 0x06: return '%';
	case 0x07: return '^';
	case 0x08: return '&';
	case 0x09: return '*';
	case 0x0a: return '(';
	case 0x0b: return ')';
	case 0x0c: return '_';
	case 0x0d: return '+';
	case 0x1a: return '{';
	case 0x1b: return '}';
	case 0x27: return ':';
	case 0x28: return '"';
	case 0x29: return '~';
	case 0x2b: return '|';
	case 0x33: return '<';
	case 0x34: return '>';
	case 0x35: return '?';
	default:
		return character >= 'a' && character <= 'z' ?
			(char)(character - 'a' + 'A') : character;
	}
}

static uint32_t special_key(uint8_t scan_code, bool extended)
{
	if (extended) {
		switch (scan_code) {
		case 0x1c: return JANOS_INPUT_KEY_ENTER;
		case 0x1d: return JANOS_INPUT_KEY_CTRL;
		case 0x38: return JANOS_INPUT_KEY_ALT;
		case 0x47: return JANOS_INPUT_KEY_HOME;
		case 0x48: return JANOS_INPUT_KEY_ARROW_UP;
		case 0x4b: return JANOS_INPUT_KEY_ARROW_LEFT;
		case 0x4d: return JANOS_INPUT_KEY_ARROW_RIGHT;
		case 0x4f: return JANOS_INPUT_KEY_END;
		case 0x50: return JANOS_INPUT_KEY_ARROW_DOWN;
		case 0x53: return JANOS_INPUT_KEY_DELETE;
		default: return JANOS_INPUT_KEY_NONE;
		}
	}
	switch (scan_code) {
	case 0x01: return JANOS_INPUT_KEY_ESCAPE;
	case 0x0e: return JANOS_INPUT_KEY_BACKSPACE;
	case 0x0f: return JANOS_INPUT_KEY_TAB;
	case 0x1c: return JANOS_INPUT_KEY_ENTER;
	case 0x1d: return JANOS_INPUT_KEY_CTRL;
	case 0x2a:
	case 0x36: return JANOS_INPUT_KEY_SHIFT;
	case 0x38: return JANOS_INPUT_KEY_ALT;
	case 0x3a: return JANOS_INPUT_KEY_CAPSLOCK;
	default: return JANOS_INPUT_KEY_NONE;
	}
}

static void update_modifier(struct input_modifiers *modifiers,
	uint32_t key, bool pressed)
{
	switch (key) {
	case JANOS_INPUT_KEY_SHIFT: modifiers->shift = pressed; break;
	case JANOS_INPUT_KEY_CTRL: modifiers->ctrl = pressed; break;
	case JANOS_INPUT_KEY_ALT: modifiers->alt = pressed; break;
	case JANOS_INPUT_KEY_CAPSLOCK:
		if (pressed)
			modifiers->capslock = !modifiers->capslock;
		break;
	default:
		break;
	}
}

static uint32_t modifier_bits(const struct input_modifiers *modifiers)
{
	uint32_t bits = 0;
	if (modifiers->shift)
		bits |= JANOS_INPUT_EVENT_SHIFT;
	if (modifiers->ctrl)
		bits |= JANOS_INPUT_EVENT_CTRL;
	if (modifiers->alt)
		bits |= JANOS_INPUT_EVENT_ALT;
	if (modifiers->capslock)
		bits |= JANOS_INPUT_EVENT_CAPSLOCK;
	return bits;
}

static void handle_scan(uint32_t shell_endpoint, uint8_t scan_code,
	struct input_modifiers *modifiers, bool *extended, uint8_t *pause_bytes)
{
	if (*pause_bytes != 0) {
		--*pause_bytes;
		return;
	}
	if (scan_code == 0xe1) {
		*pause_bytes = 5;
		*extended = false;
		return;
	}
	if (scan_code == 0xe0) {
		*extended = true;
		return;
	}
	bool is_extended = *extended;
	*extended = false;
	bool pressed = (scan_code & 0x80u) == 0;
	uint8_t code = scan_code & 0x7fu;
	uint32_t key = special_key(code, is_extended);
	char character = '\0';
	if (!is_extended && key == JANOS_INPUT_KEY_NONE) {
		character = scan_character(code);
		if (character != '\0' &&
			(modifiers->shift ^ (modifiers->capslock &&
			character >= 'a' && character <= 'z')))
			character = shifted_character(code, character);
		if (character != '\0')
			key = JANOS_INPUT_KEY_CHARACTER;
	}
	if (key == JANOS_INPUT_KEY_NONE)
		return;
	update_modifier(modifiers, key, pressed);
	uint32_t event = janos_input_event_make(key, (uint8_t)character,
		pressed, modifier_bits(modifiers));
	(void)janos_ipc_notify(shell_endpoint, JANOS_INPUT_MSG_KEY, event);
}

static int server(int argc, char **argv)
{
	if (argc < 4) {
		print("input: endpoint capabilities missing\n");
		return 1;
	}
	uint32_t endpoint = parse_uint(argv[2]);
	uint32_t shell_endpoint = parse_uint(argv[3]);
	struct input_modifiers modifiers = { 0 };
	bool extended = false;
	uint8_t pause_bytes = 0;
	print("INPUT_READY cpu=");
	print_uint((uint32_t)janos_cpu_get());
	print("\n");
	for (;;) {
		struct janos_ipc_message message;
		int32_t result = janos_ipc_receive(endpoint, &message,
			JANOS_IPC_TIMEOUT_INFINITE);
		if (result < 0) {
			if (result == -JANOS_EAGAIN || result == -JANOS_ENOMSG)
				continue;
			return 1;
		}
		if (message.header.type != JANOS_INPUT_MSG_RAW ||
			message.header.flags != JANOS_IPC_NOTIFICATION ||
			message.header.length != sizeof(uint32_t))
			continue;
		uint32_t value;
		memcpy(&value, message.payload, sizeof(value));
		handle_scan(shell_endpoint, (uint8_t)value, &modifiers,
			&extended, &pause_bytes);
	}
}

int main(int argc, char **argv)
{
	if (argc >= 2 && argv[1][0] == 's' && argv[1][1] == 'e')
		return server(argc, argv);
	print("usage: input server <endpoint> <shell-endpoint>\n");
	return 1;
}
