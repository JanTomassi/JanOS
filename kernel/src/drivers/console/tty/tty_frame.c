#include <arch/i386/tty/tty_frame.h>
#include <stdbool.h>
#include <string.h>

static uint8_t *buffer;
static size_t pitch, columns, rows, cursor_column, cursor_row;
static uint8_t bytes_per_pixel;
static const uint8_t *font_data;
static size_t font_header_size, font_glyph_count, font_glyph_size;
static size_t font_width = 5, font_height = 7, cell_width = 6, cell_height = 8;
static size_t screen_width, screen_height;

/* Small built-in font: enough to keep the early console independent of BIOS. */
static const uint8_t font[37][7] = {
 [0]={0}, [1]={30,5,30,20,20,30},[2]={31,20,31,20,20,31},[3]={15,16,16,16,16,15},
 [4]={30,21,21,21,21,30},[5]={31,16,30,16,16,31},[6]={31,16,30,16,16,16},
 [7]={15,16,23,17,17,15},[8]={17,17,31,17,17,17},[9]={31,4,4,4,4,31},
 [10]={1,1,1,17,17,14},[11]={17,18,28,18,17,17},[12]={16,16,16,16,16,31},
 [13]={17,27,21,21,17,17},[14]={17,25,21,19,17,17},[15]={14,17,17,17,17,14},
 [16]={30,17,17,30,16,16},[17]={14,17,17,21,18,13},[18]={30,17,17,30,18,17},
 [19]={15,16,14,1,1,30},[20]={31,4,4,4,4,4},[21]={17,17,17,17,17,14},
 [22]={17,17,17,17,10,4},[23]={17,17,21,21,27,17},[24]={17,10,4,10,17,17},
 [25]={17,10,4,4,4,4},[26]={31,2,4,8,16,31},[27]={14,17,19,21,25,14},
 [28]={4,12,4,4,4,14},[29]={14,17,2,4,8,31},[30]={30,1,6,1,17,14},
 [31]={2,6,10,18,31,2},[32]={31,16,30,1,17,14},[33]={6,8,16,30,17,14},
 [34]={31,1,2,4,8,8},[35]={14,17,14,17,17,14},[36]={14,17,15,1,2,12}
};

static const uint8_t *glyph(char ch)
{
	static const uint8_t colon[7] = { 0, 4, 4, 0, 4, 4, 0 };
	static const uint8_t dash[7] = { 0, 0, 0, 31, 0, 0, 0 };
	static const uint8_t dot[7] = { 0, 0, 0, 0, 0, 4, 0 };
	static const uint8_t slash[7] = { 1, 2, 2, 4, 8, 8, 16 };
	static const uint8_t greater[7] = { 16, 8, 4, 2, 4, 8, 16 };
	static const uint8_t equals[7] = { 0, 0, 31, 0, 31, 0, 0 };
	if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
	if (ch >= 'A' && ch <= 'Z') return font[1 + ch - 'A'];
	if (ch >= '0' && ch <= '9') return font[27 + ch - '0'];
	if (ch == ':') return colon;
	if (ch == '-') return dash;
	if (ch == '.') return dot;
	if (ch == '/') return slash;
	if (ch == '>') return greater;
	if (ch == '=') return equals;
	return font[0];
}

static uint32_t font_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void pixel(size_t x, size_t y, uint32_t color)
{
 uint8_t *p = buffer + y * pitch + x * bytes_per_pixel;
 if (bytes_per_pixel == 4) *(uint32_t *)p = color;
 else { p[0] = color; p[1] = color >> 8; p[2] = color >> 16; }
}

static void draw(char ch, size_t x, size_t y)
{
	if (font_data != nullptr) {
		size_t index = (unsigned char)ch;
		if (index >= font_glyph_count)
			index = 0;
		const uint8_t *bits = font_data + font_header_size + index * font_glyph_size;
		for (size_t row = 0; row < font_height; ++row)
			for (size_t col = 0; col < font_width; ++col)
				pixel(x + col, y + row, (bits[row * ((font_width + 7) / 8) + col / 8] &
					(0x80u >> (col & 7))) ? 0xffffff : 0);
		return;
	}
	const uint8_t *bits = glyph(ch);
	for (size_t row = 0; row < cell_height; ++row)
	 for (size_t col = 0; col < cell_width; ++col)
	  pixel(x + col, y + row, row < font_height && col < font_width && (bits[row] & (1u << (4 - col))) ? 0xffffff : 0);
}

static void putc_frame(char ch)
{
 if (ch == '\n') { cursor_column = 0; ++cursor_row; }
  else if (ch == '\b') { if (cursor_column) --cursor_column; draw(' ', cursor_column * cell_width, cursor_row * cell_height); }
  else { draw(ch, cursor_column * cell_width, cursor_row * cell_height); if (++cursor_column >= columns) { cursor_column = 0; ++cursor_row; } }
  if (cursor_row >= rows) {
   memmove(buffer, buffer + pitch * cell_height, pitch * (rows * cell_height - cell_height));
   memset(buffer + pitch * (rows * cell_height - cell_height), 0, pitch * cell_height);
  cursor_row = rows - 1;
 }
}

static void puts_frame(const char *str) { while (*str) putc_frame(*str++); }

display_t tty_frame_initialize(size_t buffer_addr, size_t fb_pitch, size_t width,
 size_t height, uint8_t bit_per_pixel)
{
 if (!buffer_addr || !fb_pitch || width < 6 || height < 8 || (bit_per_pixel != 24 && bit_per_pixel != 32)) return (display_t){};
  buffer = (uint8_t *)buffer_addr; pitch = fb_pitch; bytes_per_pixel = bit_per_pixel / 8;
  screen_width = width; screen_height = height;
  columns = width / cell_width; rows = height / cell_height; cursor_column = cursor_row = 0;
  for (size_t y = 0; y < rows * cell_height; ++y) memset(buffer + y * pitch, 0, columns * cell_width * bytes_per_pixel);
  return (display_t){ .width = columns, .height = rows, .putc = putc_frame, .puts = puts_frame };
}

bool tty_frame_set_font(const void *data, size_t size)
{
	const uint8_t *font = data;
	if (font == nullptr || size < 32 || font_u32(font) != 0x864AB572u)
		return false;
	if (font_u32(font + 4) != 0 || font_u32(font + 12) > 1)
		return false;
	size_t header = font_u32(font + 8), count = font_u32(font + 16), glyph_size = font_u32(font + 20);
	size_t width = font_u32(font + 28), height = font_u32(font + 24);
	if (header < 32 || header > size || count == 0 || glyph_size == 0 || width == 0 || width > 32 ||
		height == 0 || height > 64 || count > (size - header) / glyph_size || header + count * glyph_size != size ||
		glyph_size < height * ((width + 7) / 8))
		return false;
	if (buffer != nullptr && (width + 2 > screen_width || height > screen_height))
		return false;
	font_data = font;
	font_header_size = header;
        font_glyph_count = count;
        font_glyph_size = glyph_size;
	font_width = width;
        font_height = height;
        cell_width = width - 2;
        cell_height = height;
	if (buffer != nullptr) {
		columns = screen_width / cell_width;
		rows = screen_height / cell_height;
		cursor_column = cursor_row = 0;
	}
	return true;
}
