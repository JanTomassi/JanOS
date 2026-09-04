#include <janos/framebuffer.h>
#include <janos/ipc.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

struct fb_state {
	uint8_t *buffer;
	uint32_t buffer_size;
	uint32_t pitch;
	uint32_t width;
	uint32_t height;
	uint32_t bytes_per_pixel;
	uint32_t columns;
	uint32_t rows;
	uint32_t cursor_column;
	uint32_t cursor_row;
	const uint8_t *font;
	uint32_t font_size;
	uint32_t font_header_size;
	uint32_t font_glyph_count;
	uint32_t font_glyph_size;
	uint32_t font_width;
	uint32_t font_height;
};

static void print(const char *text)
{
	(void)write(1, text, strlen(text));
}

static void print_uint(uint32_t value)
{
	char buffer[11];
	uint32_t position = sizeof(buffer);
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

static uint32_t font_u32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool init_font(struct fb_state *state, const uint8_t *font, uint32_t size)
{
	if (font == 0 || size < 32 || font_u32(font) != 0x864ab572u ||
		font_u32(font + 4) != 0 || font_u32(font + 12) > 1)
		return false;
	uint32_t header = font_u32(font + 8);
	uint32_t flags = font_u32(font + 12);
	uint32_t count = font_u32(font + 16);
	uint32_t glyph_size = font_u32(font + 20);
	uint32_t height = font_u32(font + 24);
	uint32_t width = font_u32(font + 28);
	uint32_t row_bytes = (width + 7) / 8;
	uint64_t data_size = (uint64_t)count * glyph_size;
	if (header < 32 || header > size || count == 0 || glyph_size == 0 ||
		width == 0 || width > 32 || height == 0 || height > 64 ||
		glyph_size < (uint64_t)row_bytes * height || data_size > size - header ||
		(flags == 0 && data_size != size - header))
		return false;
	state->font = font;
	state->font_size = size;
	state->font_header_size = header;
	state->font_glyph_count = count;
	state->font_glyph_size = glyph_size;
	state->font_width = width;
	state->font_height = height;
	return true;
}

static bool init_framebuffer(struct fb_state *state,
	const struct janos_framebuffer_info *info)
{
	if (info == 0 || info->address == 0 || info->size == 0 ||
		info->type != 1 || (info->bpp != 24 && info->bpp != 32) ||
		info->pitch == 0 || info->width == 0 || info->height == 0)
		return false;
	uint32_t bytes_per_pixel = info->bpp / 8;
	uint64_t row_bytes = (uint64_t)info->width * bytes_per_pixel;
	uint64_t buffer_bytes = (uint64_t)info->pitch * info->height;
	if (row_bytes > info->pitch || buffer_bytes > info->size ||
		info->address > 0xffffffffu - info->size)
		return false;
	*state = (struct fb_state){
		.buffer = (uint8_t *)(uintptr_t)info->address,
		.buffer_size = info->size,
		.pitch = info->pitch,
		.width = info->width,
		.height = info->height,
		.bytes_per_pixel = bytes_per_pixel,
	};
	return true;
}

static bool pixel(struct fb_state *state, uint32_t x, uint32_t y, uint32_t color)
{
	if (x >= state->width || y >= state->height)
		return false;
	uint64_t offset = (uint64_t)y * state->pitch +
		(uint64_t)x * state->bytes_per_pixel;
	if (offset + state->bytes_per_pixel > state->buffer_size)
		return false;
	uint8_t *destination = state->buffer + offset;
	if (state->bytes_per_pixel == 4) {
		*(uint32_t *)destination = color;
	} else {
		destination[0] = (uint8_t)color;
		destination[1] = (uint8_t)(color >> 8);
		destination[2] = (uint8_t)(color >> 16);
	}
	return true;
}

static void draw_glyph(struct fb_state *state, uint32_t character,
	uint32_t x, uint32_t y)
{
	if (character >= state->font_glyph_count)
		character = 0;
	const uint8_t *glyph = state->font + state->font_header_size +
		character * state->font_glyph_size;
	uint32_t row_bytes = (state->font_width + 7) / 8;
	for (uint32_t row = 0; row < state->font_height; ++row)
		for (uint32_t column = 0; column < state->font_width; ++column) {
			bool set = (glyph[row * row_bytes + column / 8] &
				(0x80u >> (column & 7))) != 0;
			(void)pixel(state, x + column, y + row, set ? 0xffffffu : 0);
		}
}

static void clear(struct fb_state *state)
{
	for (uint32_t row = 0; row < state->height; ++row)
		memset(state->buffer + (size_t)row * state->pitch, 0, state->pitch);
	state->cursor_column = 0;
	state->cursor_row = 0;
}

static void scroll(struct fb_state *state, uint32_t rows)
{
	if (rows == 0)
		return;
	if (rows >= state->rows) {
		clear(state);
		return;
	}
	size_t lines = (size_t)rows * state->font_height;
	size_t bytes = (size_t)state->height * state->pitch;
	memmove(state->buffer, state->buffer + lines * state->pitch,
		bytes - lines * state->pitch);
	memset(state->buffer + bytes - lines * state->pitch, 0, lines * state->pitch);
	state->cursor_row = state->rows - rows;
}

static void advance(struct fb_state *state)
{
	if (++state->cursor_column < state->columns)
		return;
	state->cursor_column = 0;
	if (++state->cursor_row >= state->rows)
		scroll(state, 1);
}

static void putc_frame(struct fb_state *state, uint32_t character)
{
	if (character == '\n') {
		state->cursor_column = 0;
		if (++state->cursor_row >= state->rows)
			scroll(state, 1);
		return;
	}
	if (character == '\r') {
		state->cursor_column = 0;
		return;
	}
	if (character == '\b') {
		if (state->cursor_column != 0)
			--state->cursor_column;
		draw_glyph(state, ' ', state->cursor_column * state->font_width,
			state->cursor_row * state->font_height);
		return;
	}
	draw_glyph(state, character, state->cursor_column * state->font_width,
		state->cursor_row * state->font_height);
	advance(state);
}

static int32_t request_status(struct fb_state *state,
	const struct janos_ipc_message *request)
{
	if (request->header.type == JANOS_FB_MSG_PUTC) {
		if (request->header.length != sizeof(struct janos_fb_putc))
			return JANOS_FB_STATUS_INVALID;
		struct janos_fb_putc operation;
		memcpy(&operation, request->payload, sizeof(operation));
		if (operation.value > 0xffu)
			return JANOS_FB_STATUS_INVALID;
		putc_frame(state, operation.value);
		return JANOS_FB_STATUS_OK;
	}
	if (request->header.type == JANOS_FB_MSG_PUTS) {
		if (request->header.length < sizeof(uint32_t))
			return JANOS_FB_STATUS_INVALID;
		struct janos_fb_puts operation = { 0 };
		memcpy(&operation, request->payload, request->header.length);
		if (operation.length > JANOS_FB_PUTS_MAX ||
			request->header.length != sizeof(uint32_t) + operation.length)
			return JANOS_FB_STATUS_INVALID;
		for (uint32_t i = 0; i < operation.length; ++i)
			putc_frame(state, (uint8_t)operation.text[i]);
		return JANOS_FB_STATUS_OK;
	}
	if (request->header.type == JANOS_FB_MSG_CURSOR) {
		if (request->header.length != sizeof(struct janos_fb_cursor))
			return JANOS_FB_STATUS_INVALID;
		struct janos_fb_cursor operation;
		memcpy(&operation, request->payload, sizeof(operation));
		if (operation.column >= state->columns || operation.row >= state->rows)
			return JANOS_FB_STATUS_INVALID;
		state->cursor_column = operation.column;
		state->cursor_row = operation.row;
		return JANOS_FB_STATUS_OK;
	}
	if (request->header.type == JANOS_FB_MSG_CLEAR) {
		if (request->header.length != 0)
			return JANOS_FB_STATUS_INVALID;
		clear(state);
		return JANOS_FB_STATUS_OK;
	}
	if (request->header.type == JANOS_FB_MSG_SCROLL) {
		if (request->header.length != sizeof(struct janos_fb_scroll))
			return JANOS_FB_STATUS_INVALID;
		struct janos_fb_scroll operation;
		memcpy(&operation, request->payload, sizeof(operation));
		scroll(state, operation.rows);
		return JANOS_FB_STATUS_OK;
	}
	return JANOS_FB_STATUS_INVALID;
}

static int32_t drain_console(struct fb_state *state)
{
	char buffer[JANOS_FRAMEBUFFER_OUTPUT_CHUNK];
	ssize_t count = janos_framebuffer_read(buffer, sizeof(buffer));
	if (count < 0)
		return count;
	for (ssize_t i = 0; i < count; ++i)
		putc_frame(state, (uint8_t)buffer[i]);
	return count;
}

static void reply_status(uint32_t endpoint,
	const struct janos_ipc_message *request, int32_t status)
{
	struct janos_ipc_message response = { .header = {
		.type = request->header.type,
		.flags = JANOS_IPC_REPLY,
		.length = sizeof(struct janos_fb_reply),
		.request_id = request->header.request_id,
	} };
	struct janos_fb_reply operation = { .status = status };
	memcpy(response.payload, &operation, sizeof(operation));
	(void)janos_ipc_reply(endpoint, request->header.request_id, &response);
}

static void reply_info(uint32_t endpoint, const struct janos_ipc_message *request,
	const struct fb_state *state)
{
	struct janos_ipc_message response = { .header = {
		.type = request->header.type,
		.flags = JANOS_IPC_REPLY,
		.length = sizeof(struct janos_fb_info_reply),
		.request_id = request->header.request_id,
	} };
	struct janos_fb_info_reply info = {
		.status = JANOS_FB_STATUS_OK,
		.width = state->width,
		.height = state->height,
		.pitch = state->pitch,
		.bpp = state->bytes_per_pixel * 8,
		.type = 1,
		.columns = state->columns,
		.rows = state->rows,
		.font_size = state->font_size,
		.font_header_size = state->font_header_size,
		.font_glyph_count = state->font_glyph_count,
		.font_glyph_size = state->font_glyph_size,
		.font_width = state->font_width,
		.font_height = state->font_height,
	};
	memcpy(response.payload, &info, sizeof(info));
	(void)janos_ipc_reply(endpoint, request->header.request_id, &response);
}

static int server(int argc, char **argv)
{
	if (argc < 12) {
		print("fbserver: framebuffer capability arguments missing\n");
		return 1;
	}
	struct janos_framebuffer_info info = {
		.address = parse_uint(argv[3]),
		.size = parse_uint(argv[4]),
		.pitch = parse_uint(argv[5]),
		.width = parse_uint(argv[6]),
		.height = parse_uint(argv[7]),
		.bpp = parse_uint(argv[8]),
		.type = parse_uint(argv[9]),
	};
	struct fb_state state;
	if (!init_framebuffer(&state, &info) ||
		!init_font(&state, (const uint8_t *)(uintptr_t)parse_uint(argv[10]),
			parse_uint(argv[11])) || state.font_width == 0 || state.font_height == 0)
		return 1;
	state.columns = state.width / state.font_width;
	state.rows = state.height / state.font_height;
	if (state.columns == 0 || state.rows == 0)
		return 1;
	uint32_t endpoint = parse_uint(argv[2]);
	clear(&state);
	print("FBSERVER_READY cpu=");
	print_uint((uint32_t)janos_cpu_get());
	print("\n");
	for (;;) {
		int32_t output = drain_console(&state);
		if (output < 0)
			return 1;
		if (output > 0)
			continue;
		struct janos_ipc_message request;
		int32_t result = janos_ipc_receive(endpoint, &request,
			JANOS_IPC_TIMEOUT_INFINITE);
		if (result < 0) {
			if (result == -JANOS_ENOMSG || result == -JANOS_EAGAIN)
				continue;
			return 1;
		}
		if (request.header.type == JANOS_FB_MSG_INFO) {
			if (request.header.length == 0)
				reply_info(endpoint, &request, &state);
			else
				reply_status(endpoint, &request, JANOS_FB_STATUS_INVALID);
			continue;
		}
		int32_t status = request_status(&state, &request);
		reply_status(endpoint, &request, status);
	}
}

int main(int argc, char **argv)
{
	if (argc >= 2 && argv[1][0] == 's' && argv[1][1] == 'e')
		return server(argc, argv);
	print("usage: fbserver server <endpoint> <framebuffer> <size> <pitch> <width> <height> <bpp> <type> <font> <font-size>\n");
	return 1;
}
