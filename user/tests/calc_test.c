#include "calc.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void expect(const char *input, enum calc_status status, calc_int32_t value)
{
	calc_int32_t actual = 0;
	assert(calc_eval(input, strlen(input), &actual) == status);
	if (status == CALC_OK)
		assert(actual == value);
}

int main(void)
{
	char deep[CALC_MAX_INPUT];
	int position = 0;
	for (int i = 0; i < CALC_MAX_DEPTH + 2; ++i) {
		deep[position++] = '+';
		deep[position++] = ' ';
		deep[position++] = '1';
		deep[position++] = ' ';
	}
	deep[position++] = '1';
	expect("+ 2 3", CALC_OK, 5);
	expect("\t*\n -2 3 ", CALC_OK, -6);
	expect("/ 7 2", CALC_OK, 3);
	expect("+ -2147483648 0", CALC_OK, -2147483647 - 1);
	expect("2147483647", CALC_OK, 2147483647);
	calc_int32_t deep_result = 0;
	assert(calc_eval(deep, (calc_size_t)position, &deep_result) == CALC_ERR_DEPTH);
	expect("* + 2 3 4", CALC_OK, 20);
	expect("/ -2 2", CALC_OK, -1);
	expect("-2147483648", CALC_OK, -2147483647 - 1);
	expect("+ 2147483647 0", CALC_OK, 2147483647);
	expect("/ -2147483648 -1", CALC_ERR_OVERFLOW, 0);
	expect("+ 1", CALC_ERR_OPERAND, 0);
	expect("/ 8 0", CALC_ERR_DIV_ZERO, 0);
	expect("+ 2147483647 1", CALC_ERR_OVERFLOW, 0);
	expect("- -2147483648 1", CALC_ERR_OVERFLOW, 0);
	expect("+ 1 2 3", CALC_ERR_TRAILING, 0);
	expect("2x", CALC_ERR_TOKEN, 0);
	expect("+ 1 nope", CALC_ERR_TOKEN, 0);
	expect("", CALC_ERR_EMPTY, 0);
	assert(calc_eval(0, 1, &deep_result) == CALC_ERR_EMPTY);
	assert(calc_eval("1", 1, 0) == CALC_ERR_EMPTY);
	return 0;
}
