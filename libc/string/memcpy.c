#include <string.h>

void *memcpy(void *__restrict destptr, const void *__restrict srcptr,
	     size_t size)
{
	unsigned char *dest = (unsigned char *)destptr;
	unsigned char *src = (unsigned char *)srcptr;

	for (; 0 != size % sizeof(size_t); (dest++, src++, size--)) {
		*dest = *src;
	}
	for (size_t i = 0; i < size; i += 4) {
		dest[i + 0] = src[i + 0];
		dest[i + 1] = src[i + 1];
		dest[i + 2] = src[i + 2];
		dest[i + 3] = src[i + 3];
	}
	return dest;
}
