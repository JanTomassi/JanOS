#include <kernel/multiboot.h>

#include <kernel/tty.h>
#include <kernel/serial.h>
#include <kernel/display.h>

#include <kernel/phy_mem.h>
#include <kernel/vir_mem.h>
#include <kernel/interrupt.h>

#include <string.h>

#include "../arch/i386/control_register.h"

extern void idt_init(void);
extern void PIC_remap(int offset1, int offset2);
extern void pic_disable(void);

void section_divisor(char *section_name)
{
	if (section_name != NULL)
		kprintf("\n%s", section_name);

	kprintf("---------------------------------------"
		"-------------------------------------\n");
}

void setup_phy_mem(multiboot_info_t *mbd)
{
	phy_mem_reset();

	section_divisor("Multiboot memory map:\n");

	/* Loop through the memory map and display the values */
	for (size_t i = 0; i < mbd->mmap_length;
	     i += sizeof(multiboot_memory_map_t)) {
		multiboot_memory_map_t *mmmt =
			(multiboot_memory_map_t *)(mbd->mmap_addr + i);

		kprintf("Start Addr: %x | Length: %x | Size: %x | Type: %u\n",
			(uint32_t)mmmt->addr, (uint32_t)mmmt->len,
			(uint32_t)mmmt->size, (uint32_t)mmmt->type);

		if (mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {
			phy_mem_add_region(mmmt->addr, mmmt->len);
		}
	}

	section_divisor("ELF section header:\n");

	Elf32_Shdr *elf_sec = mbd->u.elf_sec.addr;
	char *elf_sec_str = (char *)elf_sec[mbd->u.elf_sec.shndx].sh_addr;
	for (size_t i = 0; i < mbd->u.elf_sec.num; i++) {
		kprintf("Section (%s): [Address: %x, Size: %x]\n",
			&elf_sec_str[elf_sec[i].sh_name], elf_sec[i].sh_addr,
			elf_sec[i].sh_size);

		phy_mem_rm_region(elf_sec[i].sh_addr, elf_sec[i].sh_size);
	}

	section_divisor("Physical memory allocator:\n");

	kprintf("   - Number of blocks: %x\n", phy_mem_get_tot_blocks());
	kprintf("   - Number of used blocks: %x\n", phy_mem_get_used_blocks());
	kprintf("   - Number of free blocks: %x\n", phy_mem_get_free_blocks());
}

void phy_memory_test()
{
	section_divisor(
		"1. Physical memory allocation test:\nTesting if they will overlap:\n");

	void *const alloc = phy_mem_alloc(4096 * 1);
	kprintf("Step 1 alloc one block without freeing: %x\n", (size_t)alloc);

	void *const alloc2 = phy_mem_alloc(4096 * 1);
	kprintf("Step 2 alloc one block without freeing: %x\n", (size_t)alloc2);

	kprintf("Step 3 are they equal: %s\n",
		alloc == alloc2 ? "true" : "false");

	section_divisor(
		"2. Physical memory allocation test:\nTesting if reuse work:\n");

	void *const alloc3 = phy_mem_alloc(4096 * 1);
	kprintf("Step 1 alloc one block then free it: %x\n", (size_t)alloc3);
	phy_mem_free(alloc3);

	void *const alloc4 = phy_mem_alloc(4096 * 1);
	kprintf("Step 2 alloc one block then free it: %x\n", (size_t)alloc4);
	phy_mem_free(alloc4);

	kprintf("Step 3 are they equal: %s\n",
		alloc3 == alloc4 ? "true" : "false");

	phy_mem_free(alloc2);
	phy_mem_free(alloc);
}

void kernel_main(multiboot_info_t *mbd, unsigned int magic)
{
	display_t serial_dpy = init_serial();
	display_setcurrent(display_register(serial_dpy));

	/* Make sure the magic number matches for memory mapping*/
	if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		panic("invalid magic number!\n");
	}

	/* Check bit 6 to see if we have a valid memory map */
	if (!(mbd->flags >> 6 & 0x1)) {
		panic("invalid memory map given by GRUB bootloader\n");
	}

	section_divisor("Initializing programable interrupt controller:\n");

	PIC_remap(0x20, 0x28);

	kprintf("    - IRQ Master: start at dec: %u, hex: %x\n"
		"                    end at dec: %u, hex: %x\n",
		0x20, 0x20, 0x20 + 7, 0x20 + 7);
	kprintf("    - IRQ Slave:  start at dec: %u, hex: %x\n"
		"                    end at dec: %u, hex: %x\n",
		0x28, 0x28, 0x28 + 7, 0x28 + 7);

	section_divisor("Initializing interrupt description table:\n");
	idt_init();
	kprintf("IDT initialized\n");

	display_t tty_dpy = tty_initialize(
		mbd->framebuffer_addr, mbd->framebuffer_pitch,
		mbd->framebuffer_width, mbd->framebuffer_height,
		mbd->framebuffer_bpp, mbd->framebuffer_type == 1);

	section_divisor("Screen stats:\n");
	kprintf("    - WIDTH: %d\n", mbd->framebuffer_width);
	kprintf("    - HEIGHT: %d\n", mbd->framebuffer_height);
	section_divisor("Control registers:\n");
	debug_CR_reg();

	setup_phy_mem(mbd);
	phy_memory_test();

	size_t phy_addr_framebuffer = mbd->framebuffer_addr;
	size_t framebuffer_width = mbd->framebuffer_width;
	size_t framebuffer_height = mbd->framebuffer_height;
	size_t framebuffer_pitch = mbd->framebuffer_pitch;
	size_t framebuffer_size =
		mbd->framebuffer_pitch * mbd->framebuffer_height;

	section_divisor("Virtual memory init:\n");
	init_vir_mem(mbd);

	for (size_t i = 0;
	     i < ((framebuffer_pitch * framebuffer_height) / 4096); i++)
		map_page((void *)phy_addr_framebuffer + (i * 4096),
			 (void *)(i * 4096),
			 VMM_PAGE_FLAG_PRESENT_BIT |
				 VMM_PAGE_FLAG_READ_WRITE_BIT);

	for (size_t i = 0; i < framebuffer_height; i++)
		for (size_t j = 0; j < framebuffer_width; j++)
			((uint32_t *)(0))[j + (i * framebuffer_width)] =
				(0x00 << 24) |
				((uint8_t)(i * (255.0 / 800))) << 16 |
				((uint8_t)(j * (255.0 / 1280))) << 8 | 0x00;

	/* tty_reset(); */

	/* Entering graphical mode */
	/* display_setcurrent(display_register(tty_dpy)); */
}

size_t GLOBAL_TICK = 0;
DEFINE_IRQ(32)
{
	++GLOBAL_TICK;
}
