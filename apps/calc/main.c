#include "calc.h"
#include <string.h>
#include <unistd.h>

static void print(const char *text)
{
	(void)write(1, text, strlen(text));
}

static void print_integer(calc_int32_t value)
{
	char buffer[12];
	calc_size_t position = sizeof(buffer);
	calc_uint32_t magnitude = value < 0 ? (calc_uint32_t)(-(long long)value) : (calc_uint32_t)value;
	if (magnitude == 0)
		buffer[--position] = '0';
	while (magnitude != 0) {
		buffer[--position] = (char)('0' + magnitude % 10);
		magnitude /= 10;
	}
	if (value < 0)
		buffer[--position] = '-';
	(void)write(1, buffer + position, sizeof(buffer) - position);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	char input[CALC_MAX_INPUT];
	print("jan> ");
	for (;;) {
		calc_int32_t bytes = (calc_int32_t)read(0, input, sizeof(input));
		if (bytes <= 0)
			break;
		calc_int32_t result;
		enum calc_status status = calc_eval(input, (calc_size_t)bytes, &result);
		if (status == CALC_OK) {
			print_integer(result);
			print("\n");
		} else {
			print("error: ");
			print(calc_status_string(status));
			print("\n");
		}
		print("jan> ");
	}
	return 0;
}
