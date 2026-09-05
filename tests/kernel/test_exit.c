#include <kernel/test.h>

#include <kernel/display.h>
#include <arch/i386/port.h>

void kernel_test_marker(const char *name, bool passed)
{
	kprintf("JANOS:TEST:%s:%s\n", name, passed ? "PASS" : "FAIL");
}

[[noreturn]] void kernel_test_finish(uint32_t status)
{
	kprintf("JANOS:TEST:EXIT:%u\n", (unsigned)status);
	outd(0xf4, status);
	for (;;)
	__asm__ volatile("cli; hlt" ::: "memory", "cc");
}
