#include "calc.h"
#include <stdbool.h>
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

static int command_is(const char *line, const char *command)
{
	calc_size_t i = 0;
	while (command[i] != '\0' && line[i] == command[i])
		++i;
	return command[i] == '\0' && (line[i] == '\0' || line[i] == ' ' || line[i] == '\t');
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	char input[CALC_MAX_INPUT + 1];
	char last_input[CALC_MAX_INPUT + 1];
	bool has_last_input = false;
	print("jan> ");
	for (;;) {
		calc_int32_t bytes = (calc_int32_t)read(0, input, CALC_MAX_INPUT);
		if (bytes <= 0)
			break;
		while (bytes > 0 && (input[bytes - 1] == '\n' || input[bytes - 1] == '\r'))
			--bytes;
		input[bytes] = '\0';
		if (bytes == 0 && has_last_input) {
			memcpy(input, last_input, strlen(last_input) + 1);
			bytes = (calc_int32_t)strlen(input);
		} else if (bytes > 0) {
			memcpy(last_input, input, (size_t)bytes + 1);
			has_last_input = true;
		}
		if (command_is(input, "help")) {
			print("help  show commands\ninfo  show process information\ncpu   show current CPU\ncalc  evaluate an expression\n");
			print("jan> ");
			continue;
		}
		if (command_is(input, "info")) {
			print("JanOS MVP shell: one foreground process, PS/2 input, console output\n");
			print("Use calc <expression> or enter an expression directly.\n");
			print("jan> ");
			continue;
		}
		if (command_is(input, "cpu")) {
			print("calc cpu=");
			print_integer(janos_cpu_get());
			print("\njan> ");
			continue;
		}
		const char *expression = input;
		if (command_is(input, "calc")) {
			expression = input + 4;
			while (*expression == ' ' || *expression == '\t')
				++expression;
		}
		calc_int32_t result;
		enum calc_status status = calc_eval(expression, (calc_size_t)strlen(expression), &result);
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
