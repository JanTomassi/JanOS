#include <arch/i386/tty/tty_frame.h>
#include <string.h>

static uint8_t *buffer;
static size_t pitch, columns, rows, cursor_column, cursor_row;
static uint8_t bytes_per_pixel;

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

static void pixel(size_t x, size_t y, uint32_t color)
{
 uint8_t *p = buffer + y * pitch + x * bytes_per_pixel;
 if (bytes_per_pixel == 4) *(uint32_t *)p = color;
 else { p[0] = color; p[1] = color >> 8; p[2] = color >> 16; }
}

static void draw(char ch, size_t x, size_t y)
{
 const uint8_t *bits = glyph(ch);
 for (size_t row = 0; row < 8; ++row)
  for (size_t col = 0; col < 6; ++col)
   pixel(x + col, y + row, row < 7 && col < 5 && (bits[row] & (1u << (4 - col))) ? 0xffffff : 0);
}

static void putc_frame(char ch)
{
 if (ch == '\n') { cursor_column = 0; ++cursor_row; }
 else if (ch == '\b') { if (cursor_column) --cursor_column; draw(' ', cursor_column * 6, cursor_row * 8); }
 else { draw(ch, cursor_column * 6, cursor_row * 8); if (++cursor_column >= columns) { cursor_column = 0; ++cursor_row; } }
 if (cursor_row >= rows) {
  memmove(buffer, buffer + pitch * 8, pitch * (rows * 8 - 8));
  memset(buffer + pitch * (rows * 8 - 8), 0, pitch * 8);
  cursor_row = rows - 1;
 }
}

static void puts_frame(const char *str) { while (*str) putc_frame(*str++); }

display_t tty_frame_initialize(size_t buffer_addr, size_t fb_pitch, size_t width,
 size_t height, uint8_t bit_per_pixel)
{
 if (!buffer_addr || !fb_pitch || width < 6 || height < 8 || (bit_per_pixel != 24 && bit_per_pixel != 32)) return (display_t){};
 buffer = (uint8_t *)buffer_addr; pitch = fb_pitch; bytes_per_pixel = bit_per_pixel / 8;
 columns = width / 6; rows = height / 8; cursor_column = cursor_row = 0;
 for (size_t y = 0; y < rows * 8; ++y) memset(buffer + y * pitch, 0, columns * 6 * bytes_per_pixel);
 return (display_t){ .width = columns, .height = rows, .putc = putc_frame, .puts = puts_frame };
}
