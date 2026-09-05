#include "test.h"

#include <kernel/boot_log.h>

#include <string.h>

static struct boot_log log;
static char input[KERNEL_BOOT_LOG_CAPACITY + 3u];
static char output[KERNEL_BOOT_LOG_CAPACITY];

static void test_write_and_read(void)
{
	const char early_output[] = "early kernel output\n";
	char buffer[sizeof(early_output)];

	boot_log_init(&log);
	TEST_ASSERT(boot_log_available(&log) == 0);
	TEST_ASSERT(boot_log_read(&log, buffer, sizeof(buffer)) == 0);
	TEST_ASSERT(boot_log_write(&log, NULL, 1) == 0);
	TEST_ASSERT(boot_log_write(&log, early_output, sizeof(early_output) - 1) ==
		sizeof(early_output) - 1);
	TEST_ASSERT(boot_log_available(&log) == sizeof(early_output) - 1);
	TEST_ASSERT(boot_log_read(&log, buffer, sizeof(buffer)) == sizeof(early_output) - 1);
	TEST_ASSERT(memcmp(buffer, early_output, sizeof(early_output) - 1) == 0);
	TEST_ASSERT(boot_log_available(&log) == 0);
}

static void test_wrap_and_overwrite(void)
{
	boot_log_init(&log);
	for (size_t i = 0; i < sizeof(input); ++i)
		input[i] = (char)(i & 0x7f);
	TEST_ASSERT(boot_log_write(&log, input, sizeof(input)) == sizeof(input));
	TEST_ASSERT(boot_log_available(&log) == KERNEL_BOOT_LOG_CAPACITY);
	TEST_ASSERT(boot_log_read(&log, output, sizeof(output)) == sizeof(output));
	for (size_t i = 0; i < sizeof(output); ++i)
		TEST_ASSERT(output[i] == input[i + 3u]);
	TEST_ASSERT(boot_log_available(&log) == 0);
}

static void test_partial_reads(void)
{
	const char first[] = "123";
	const char second[] = "456";
	char buffer[sizeof(first) + sizeof(second)];

	boot_log_init(&log);
	TEST_ASSERT(boot_log_write(&log, first, sizeof(first) - 1) == sizeof(first) - 1);
	TEST_ASSERT(boot_log_read(&log, buffer, 2) == 2);
	TEST_ASSERT(memcmp(buffer, "12", 2) == 0);
	TEST_ASSERT(boot_log_write(&log, second, sizeof(second) - 1) == sizeof(second) - 1);
	TEST_ASSERT(boot_log_read(&log, buffer, sizeof(buffer)) == 4);
	TEST_ASSERT(memcmp(buffer, "3456", 4) == 0);
}

int main(void)
{
	test_write_and_read();
	test_wrap_and_overwrite();
	test_partial_reads();
	return 0;
}
