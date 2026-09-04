#include "test.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void test_memset_and_memcpy(void)
{
	uint8_t source[32];
	uint8_t destination[32];
	for (size_t i = 0; i < sizeof(source); ++i)
		source[i] = (uint8_t)(i * 3u + 1u);
	memset(destination, 0xa5, sizeof(destination));
	for (size_t size = 0; size <= sizeof(source); ++size) {
		memset(destination, 0xa5, sizeof(destination));
		TEST_ASSERT(memcpy(destination, source, size) == destination);
		TEST_ASSERT(memcmp(destination, source, size) == 0);
		for (size_t i = size; i < sizeof(destination); ++i)
			TEST_ASSERT(destination[i] == 0xa5);
	}
}

static void test_memmove_overlap(void)
{
	uint8_t bytes[16];
	for (size_t i = 0; i < sizeof(bytes); ++i)
		bytes[i] = (uint8_t)i;
	TEST_ASSERT(memmove(bytes + 2, bytes, 10) == bytes + 2);
	for (size_t i = 0; i < 10; ++i)
		TEST_ASSERT(bytes[i + 2] == i);
	for (size_t i = 0; i < sizeof(bytes); ++i)
		bytes[i] = (uint8_t)i;
	TEST_ASSERT(memmove(bytes, bytes + 2, 10) == bytes);
	for (size_t i = 0; i < 10; ++i)
		TEST_ASSERT(bytes[i] == i + 2);
}

static void test_string_helpers(void)
{
	TEST_ASSERT(strlen("") == 0);
	TEST_ASSERT(strlen("JanOS") == 5);
	TEST_ASSERT(memcmp("abc", "abc", 3) == 0);
	TEST_ASSERT(memcmp("abc", "abd", 3) < 0);
	TEST_ASSERT(memcmp("abd", "abc", 3) > 0);
}

int main(void)
{
	test_memset_and_memcpy();
	test_memmove_overlap();
	test_string_helpers();
	return 0;
}
