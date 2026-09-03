#include "calc.h"

#define INT32_MIN (-2147483647 - 1)
#define INT32_MAX 2147483647

struct parser {
	const char *input;
	calc_size_t length;
	calc_size_t position;
};

static void skip_space(struct parser *parser)
{
	while (parser->position < parser->length) {
		char c = parser->input[parser->position];
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
			break;
		++parser->position;
	}
}

static enum calc_status parse_expression(struct parser *parser, unsigned depth, calc_int32_t *result)
{
	skip_space(parser);
	if (parser->position == parser->length)
		return CALC_ERR_OPERAND;
	if (depth > CALC_MAX_DEPTH)
		return CALC_ERR_DEPTH;

	char token = parser->input[parser->position];
	int minus_is_number = token == '-' && parser->position + 1 < parser->length && parser->input[parser->position + 1] >= '0' &&
			      parser->input[parser->position + 1] <= '9';
	if ((token == '+' || token == '-' || token == '*' || token == '/') && !minus_is_number) {
		++parser->position;
		calc_int32_t left, right;
		enum calc_status status = parse_expression(parser, depth + 1, &left);
		if (status != CALC_OK)
			return status;
		status = parse_expression(parser, depth + 1, &right);
		if (status != CALC_OK)
			return status;
		long long value;
		switch (token) {
		case '+':
			value = (long long)left + right;
			break;
		case '-':
			value = (long long)left - right;
			break;
		case '*':
			value = (long long)left * right;
			break;
		case '/':
			if (right == 0)
				return CALC_ERR_DIV_ZERO;
			if (left == INT32_MIN && right == -1)
				return CALC_ERR_OVERFLOW;
			value = left / right;
			break;
		default:
			return CALC_ERR_TOKEN;
		}
		if (value < INT32_MIN || value > INT32_MAX)
			return CALC_ERR_OVERFLOW;
		*result = (calc_int32_t)value;
		return CALC_OK;
	}

	int negative = 0;
	if (token == '-') {
		negative = 1;
		++parser->position;
	}
	if (parser->position == parser->length || parser->input[parser->position] < '0' || parser->input[parser->position] > '9')
		return CALC_ERR_TOKEN;
	calc_uint32_t magnitude = 0;
	calc_uint32_t limit = negative ? 2147483648u : 2147483647u;
	while (parser->position < parser->length) {
		char c = parser->input[parser->position];
		if (c < '0' || c > '9')
			break;
		calc_uint32_t digit = (calc_uint32_t)(c - '0');
		if (magnitude > (limit - digit) / 10)
			return CALC_ERR_OVERFLOW;
		magnitude = magnitude * 10 + digit;
		++parser->position;
	}
	if (parser->position < parser->length) {
		char c = parser->input[parser->position];
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
			return CALC_ERR_TOKEN;
	}
	*result = negative ? (magnitude == 2147483648u ? INT32_MIN : -(calc_int32_t)magnitude) : (calc_int32_t)magnitude;
	return CALC_OK;
}

enum calc_status calc_eval(const char *input, calc_size_t length, calc_int32_t *result)
{
	if (input == 0 || result == 0 || length == 0)
		return CALC_ERR_EMPTY;
	if (length > CALC_MAX_INPUT)
		return CALC_ERR_TOKEN;
	struct parser parser = { input, length, 0 };
	enum calc_status status = parse_expression(&parser, 0, result);
	if (status != CALC_OK)
		return status;
	skip_space(&parser);
	return parser.position == parser.length ? CALC_OK : CALC_ERR_TRAILING;
}

const char *calc_status_string(enum calc_status status)
{
	switch (status) {
	case CALC_ERR_EMPTY:
		return "empty expression";
	case CALC_ERR_TOKEN:
		return "invalid token";
	case CALC_ERR_OPERAND:
		return "expected operand";
	case CALC_ERR_TRAILING:
		return "trailing input";
	case CALC_ERR_DEPTH:
		return "expression too deep";
	case CALC_ERR_DIV_ZERO:
		return "division by zero";
	case CALC_ERR_OVERFLOW:
		return "integer overflow";
	default:
		return "unknown error";
	}
}
