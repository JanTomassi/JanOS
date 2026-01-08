#include <kernel/display.h>
#include <string.h>

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

static display_t disps[DISPLAY_MAX_DISPS];
static uint8_t _last_register = 0;
static uint8_t current = 0;
static bool enabled = false;

void __kprintf_va_list(char *str, va_list ap);

MODULE("DISPLAY");

static uint8_t unumber_len(uintmax_t num)
{
	uint8_t num_len = 0;
	if (num == 0)
		return 1;
	for ( ; num != 0; num /= 10)
		num_len++;
	return num_len;
}

static uint8_t number_len(intmax_t num)
{
	uint8_t num_len = 0;
	if (num < 0) {
		num_len++;
		num *= -1;
	}
	return num_len + unumber_len(num);
}

static uint8_t hex_len(uintmax_t num)
{
	uint8_t num_len = 0;

	if (num == 0)
		return 1;

	for ( ; num != 0; num /= 16)
		num_len++;

	return num_len;
}

static size_t unumber_to_str(uintmax_t num, char *str, size_t str_len)
{
	if (str_len < 2)
		return 2 - str_len;

	if (num == 0) {
		str[0] = '0';
		str[1] = '\0';
		return 0;
	}

	uint8_t num_len = unumber_len(num);

	if ((num_len + 1) > str_len)
		return (num_len + 1) - str_len;

	str[num_len] = '\0';

	for (size_t digit = num % 10, i = num_len - 1; num != 0; (--i, num /= 10, digit = num % 10))
		str[i] = '0' + digit;

	return 0;
}

static size_t number_to_str(intmax_t num, char *str, size_t str_len)
{
	if (str_len < 3)
		return 3 - str_len;
	if (num < 0) {
		str[0] = '-';
		num *= -1;
		return unumber_to_str(num, &str[1], str_len - 1);
	}
	return unumber_to_str(num, &str[0], str_len);
}

static size_t hex_to_str(uintmax_t num, char *str, size_t str_len)
{
	if (str_len < 4)
		return 4 - str_len;

	*str++ = '0';
	*str++ = 'x';
	str_len -= 2;

	if (num == 0) {
		str[0] = '0';
		str[1] = '\0';
		return 0;
	}

	uint8_t num_len = hex_len(num);

	if ((num_len + 1) > str_len)
		return (num_len + 1) - str_len;

	str[num_len] = '\0';

	for (size_t digit = num % 16, i = num_len - 1; num != 0; (--i, num /= 16, digit = num % 16)) {
		if (digit <= 9)
			str[i] = '0' + digit;
		else if (digit >= 0xA && digit <= 0xF)
			str[i] = 'A' + (digit - 0xA);
		else
			str[i] = '?';
	}

	return 0;
}

void __mprintf(char *m, ...)
{
	va_list ap;
	va_start(ap, m);
	kprintf("[%s]: ", m);
	char *fmt = va_arg(ap, char *);
	__kprintf_va_list(fmt, ap);
}

int kprintf(const char *str, ...)
{
	if (!str)
		return 0;
	va_list ap;
	va_start(ap, str);
	__kprintf_va_list((char *)str, ap);
	return 1;
}

void __kprintf_va_list(char *str, va_list ap)
{
	if (!enabled)
		return;
	char *s = 0;
	for (size_t i = 0; i < strlen((const char *)str); i++) {
		if (str[i] == '%') {
			switch (str[i + 1]) {
			// string
			case 's':
				s = va_arg(ap, char *);
				disps[current].puts(s);
				i++;
				continue;
			// unsigned
			case 'u': {
				uint32_t c = va_arg(ap, uint32_t);
				char str[32] = { 0 };
				unumber_to_str(c, str, 32);
				disps[current].puts(str);
				i++;
				continue;
			}
			// decimal
			case 'd': {
				int32_t c = va_arg(ap, int32_t);
				char str[32] = { 0 };
				number_to_str(c, str, 32);
				disps[current].puts(str);
				i++;
				continue;
			}
			// hex
			case 'x': {
				uint32_t c = va_arg(ap, uint32_t);
				char str[32] = { 0 };
				hex_to_str(c, str, 32);
				disps[current].puts(str);
				i++;
				continue;
			}
			// character
			case 'c': {
				// char gets promoted to int for va_arg!
				// clean it.
				char c = (char)(va_arg(ap, int) & ~0xFFFFFF00);
				disps[current].putc(c);
				i++;
				continue;
			}
			default:
				break;
			}
		} else {
			disps[current].putc(str[i]);
		}
	}
	va_end(ap);
}

/* Registers the display interface and returns its ID */
uint8_t display_register(display_t d)
{
	enabled = true;
	disps[_last_register] = d;
	//dispis[_last_register]->onregister();
	return _last_register++;
}
/* Sets it as current display */
bool display_setcurrent(uint8_t id)
{
	if (current == id && id < _last_register)
		return false;
	current = id;
	return true;
}
/* returns current DISPLAY */
display_t *display_getcurrent()
{
	return &disps[current];
}
