#include <string.h>

int strcmp(const void *aptr, const void *bptr)
{
	const unsigned char *a = (const unsigned char *)aptr;
	const unsigned char *b = (const unsigned char *)bptr;

	const size_t a_len = strlen(a)+1;
	const size_t b_len = strlen(b)+1;
	const size_t min_len = a_len <= b_len ? a_len : b_len;

	for (size_t i = 0; i < min_len; i++) {
		const char diff = b[i] - a[i];
		if (diff) return diff;
	}
	return 0;
}
