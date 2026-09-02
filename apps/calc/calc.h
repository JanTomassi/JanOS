#pragma once

typedef __SIZE_TYPE__ calc_size_t;
typedef __INT32_TYPE__ calc_int32_t;
typedef __UINT32_TYPE__ calc_uint32_t;

#define CALC_MAX_INPUT 256
#define CALC_MAX_DEPTH 32

enum calc_status {
	CALC_OK = 0,
	CALC_ERR_EMPTY,
	CALC_ERR_TOKEN,
	CALC_ERR_OPERAND,
	CALC_ERR_TRAILING,
	CALC_ERR_DEPTH,
	CALC_ERR_DIV_ZERO,
	CALC_ERR_OVERFLOW,
};

enum calc_status calc_eval(const char *input, calc_size_t length, calc_int32_t *result);
const char *calc_status_string(enum calc_status status);
