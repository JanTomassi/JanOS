#include <string.h>
#include <stdint.h>

#include <stddef.h>
#include <stdint.h>

void *memmove(void *dstptr, const void *srcptr, size_t size)
{
	uint8_t *dst = dstptr;
	const uint8_t *src = srcptr;

	if (dstptr == srcptr || size == 0)
		return dstptr;

	uintptr_t dst_addr = (uintptr_t)dst;
	uintptr_t src_addr = (uintptr_t)src;

	if (dst_addr < src_addr) {
		size_t i = 0;
		/*
		 * Only perform uint64_t accesses when both addresses
		 * are suitably aligned.
		 */
		while (i < size &&
		       (((uintptr_t)(dst + i) & 7) != 0 ||
		        ((uintptr_t)(src + i) & 7) != 0)) {
			dst[i] = src[i];
			i++;
		}
		for (; i + 8 <= size; i += 8) {
			*(uint64_t *)(dst + i) =
				*(const uint64_t *)(src + i);
		}
		for (; i < size; i++)
			dst[i] = src[i];
	} else {
		size_t i = size;
		/*
		 * Align the end before doing backward uint64_t copies.
		 */
		while (i > 0 &&
		       (((uintptr_t)(dst + i) & 7) != 0 ||
		        ((uintptr_t)(src + i) & 7) != 0)) {
			i--;
			dst[i] = src[i];
		}
		while (i >= 8) {
			i -= 8;
			*(uint64_t *)(dst + i) =
				*(const uint64_t *)(src + i);
		}
		while (i > 0) {
			i--;
			dst[i] = src[i];
		}
	}
	return dstptr;
}
