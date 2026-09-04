#include <kernel/init.h>

void kernel_main(unsigned int magic, unsigned long mbi_addr)
{
	kernel_boot(magic, mbi_addr, nullptr);
}
