#include <kernel/multiboot.h>

#include <kernel/tty.h>
#include <kernel/serial.h>
#include <kernel/display.h>
#include <kernel/elf32.h>

#include <kernel/phy_mem.h>
#include <kernel/vir_mem.h>
#include <kernel/allocator.h>

#include <kernel/interrupt.h>

#include <string.h>
#include "../arch/i386/ata_pio.h"
#include "../arch/i386/control_register.h"
#include "../arch/i386/cpuid.h"
#include "../arch/i386/ioapic.h"
#include "../arch/i386/lapic.h"
#include "../arch/i386/pic.h"
#include "../arch/i386/smp.h"
#include "../arch/i386/port.h"

extern void idt_init(void);
extern void init_kmalloc(void);
extern void *HIGHER_HALF;

size_t GLOBAL_TICK = 0;
DEFINE_IRQ(34)
{
	++GLOBAL_TICK;
}

static void imcr_route_to_apic(void)
{
	outb(0x22, 0x70);
	outb(0x23, 0x01);
}

void section_divisor(char *section_name)
{
	if (section_name != NULL)
		kprintf("\n%s", section_name);

	kprintf("---------------------------------------"
		"-------------------------------------\n");
}

void phy_memory_test()
{
	section_divisor("1. Physical memory allocation test:\nTesting if they will overlap:\n");

	fatptr_t alloc = phy_mem_alloc(4096 * 1);
	kprintf("Step 1 alloc one block without freeing: %x\n", alloc.ptr);

	fatptr_t alloc2 = phy_mem_alloc(4096 * 1);
	kprintf("Step 2 alloc one block without freeing: %x\n", alloc2.ptr);

	kprintf("Step 3 are they equal: %s\n", alloc.ptr == alloc2.ptr ? "true" : "false");

	section_divisor("2. Physical memory allocation test:\nTesting if reuse work:\n");

	fatptr_t alloc3 = phy_mem_alloc(4096 * 1);
	kprintf("Step 1 alloc one block then free it: %x\n", alloc3.ptr);
	phy_mem_free(alloc3);

	fatptr_t alloc4 = phy_mem_alloc(4096 * 1);
	kprintf("Step 2 alloc one block then free it: %x\n", alloc4.ptr);
	phy_mem_free(alloc4);

	kprintf("Step 3 are they equal: %s\n", alloc3.ptr == alloc4.ptr ? "true" : "false");

	phy_mem_free(alloc2);
	phy_mem_free(alloc);
}

void gpa_test(allocator_t gpa_alloc){
	section_divisor("Testing gpa alloc:\n");

	fatptr_t mem1 = gpa_alloc.alloc(4096);
	kprintf("mem1 allocated\n");
	fatptr_t mem2 = gpa_alloc.alloc(128);
	kprintf("mem2 allocated\n");

	gpa_alloc.free(mem2);
	kprintf("mem2 freed\n");
	gpa_alloc.free(mem1);
	kprintf("mem1 freed\n");

	mem1 = gpa_alloc.alloc(4096);
	kprintf("mem1 allocated\n");
	mem2 = gpa_alloc.alloc(512);
	kprintf("mem2 allocated\n");

	gpa_alloc.free(mem1);
	kprintf("mem1 freed\n");
	gpa_alloc.free(mem2);
	kprintf("mem2 freed\n");
}

struct mbi_info{
	struct multiboot_tag_mmap *mmap_tag;
	struct multiboot_tag_elf_sections *elf_sec_tag;
	struct multiboot_tag *acpi_tag;
	uintptr_t kernel_end_addr;
};
struct mbi_info get_mbi_info(uintptr_t mbi_addr, size_t mbi_size, uintptr_t kernel_start_addr){
	struct mbi_info res = {.kernel_end_addr = kernel_start_addr};

	for (struct multiboot_tag *tag = (struct multiboot_tag *)(mbi_addr + 8); tag->type != MULTIBOOT_TAG_TYPE_END;
	     tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag + ((tag->size + 7) & ~7))) {
		kprintf("Tag %d, Size %x\n", tag->type, tag->size);
		switch (tag->type) {
		case MULTIBOOT_TAG_TYPE_CMDLINE:
			kprintf("Command line = %s\n", ((struct multiboot_tag_string *)tag)->string);
			break;
		case MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME:
			kprintf("Boot loader name = %s\n", ((struct multiboot_tag_string *)tag)->string);
			break;
		case MULTIBOOT_TAG_TYPE_MODULE:
			kprintf("Module at %x-%x. Command line %s\n", ((struct multiboot_tag_module *)tag)->mod_start,
				((struct multiboot_tag_module *)tag)->mod_end, ((struct multiboot_tag_module *)tag)->cmdline);
			break;
		case MULTIBOOT_TAG_TYPE_BASIC_MEMINFO:
			kprintf("mem_lower = %uKB, mem_upper = %uKB\n", ((struct multiboot_tag_basic_meminfo *)tag)->mem_lower,
				((struct multiboot_tag_basic_meminfo *)tag)->mem_upper);
			break;
		case MULTIBOOT_TAG_TYPE_BOOTDEV:
			kprintf("Boot device %x,%u,%u\n", ((struct multiboot_tag_bootdev *)tag)->biosdev, ((struct multiboot_tag_bootdev *)tag)->slice,
				((struct multiboot_tag_bootdev *)tag)->part);
			break;
		case MULTIBOOT_TAG_TYPE_ACPI_OLD:
		case MULTIBOOT_TAG_TYPE_ACPI_NEW:
			res.acpi_tag = tag;
			break;
		case MULTIBOOT_TAG_TYPE_MMAP: {
			res.mmap_tag = (struct multiboot_tag_mmap *)tag;
			multiboot_memory_map_t *mmap;
			kprintf("mmap\n");
			for (mmap = res.mmap_tag->entries; (multiboot_uint8_t *)mmap < (multiboot_uint8_t *)tag + tag->size;
			     mmap = (multiboot_memory_map_t *)((unsigned long)mmap + res.mmap_tag->entry_size))
				kprintf(" base_addr = %x%x,"
					" length = %x%x, type = %x\n",
					(unsigned)(mmap->addr >> 32), (unsigned)(mmap->addr & 0xffffffff), (unsigned)(mmap->len >> 32),
					(unsigned)(mmap->len & 0xffffffff), (unsigned)mmap->type);
		} break;
		case MULTIBOOT_TAG_TYPE_ELF_SECTIONS: {
			res.elf_sec_tag = (struct multiboot_tag_elf_sections *)tag;
			const Elf32_Shdr *elf_sec = (const Elf32_Shdr *)res.elf_sec_tag->sections;
			const char *elf_sec_str = (char *)(elf_sec[res.elf_sec_tag->shndx].sh_addr);
			for (size_t i = 0; i < res.elf_sec_tag->num; i++) {
				kprintf("Section (%s): [Address: %x, Size: %x]\n", &elf_sec_str[elf_sec[i].sh_name], elf_sec[i].sh_addr, elf_sec[i].sh_size);
				if ((elf_sec[i].sh_flags & 0x2) == 0 || elf_sec[i].sh_addr < (size_t)&HIGHER_HALF)
					continue;

				size_t section_end = round_up_to_page(elf_sec[i].sh_addr + elf_sec[i].sh_size);
				if (section_end > res.kernel_end_addr)
					res.kernel_end_addr = section_end;
			}
		} break;
		case MULTIBOOT_TAG_TYPE_FRAMEBUFFER: {
			unsigned i;
			struct multiboot_tag_framebuffer *tagfb = (struct multiboot_tag_framebuffer *)tag;
			void *fb = (void *)(unsigned long)tagfb->common.framebuffer_addr;

			switch (tagfb->common.framebuffer_type) {
			case MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED: {
				unsigned best_distance, distance;
				struct multiboot_color *palette;

				palette = tagfb->framebuffer_palette;

				best_distance = 4 * 256 * 256;

				for (i = 0; i < tagfb->framebuffer_palette_num_colors; i++) {
					distance = (0xff - palette[i].blue) * (0xff - palette[i].blue) + palette[i].red * palette[i].red +
						   palette[i].green * palette[i].green;
					if (distance < best_distance) {
						best_distance = distance;
					}
				}
			}
				break;

			case MULTIBOOT_FRAMEBUFFER_TYPE_RGB:
				break;

			case MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT:
				break;

			default:
				break;
			}
			break;
		}break;
		}
	}
	return res;
}

void kernel_main(unsigned int magic, unsigned long mbi_addr)
{
	display_t serial_dpy = init_serial();
	uint8_t serial_dpy_reg = DISPLAY_MAX_DISPS;
	if (serial_dpy.putc != nullptr || serial_dpy.puts != nullptr) {
		serial_dpy_reg = display_register(serial_dpy);
		display_setcurrent(serial_dpy_reg);
	}

	section_divisor("Control registers:\n");
	debug_CR_reg();

	/* Make sure the magic number matches for memory mapping*/
	if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
		panic("invalid magic number!\n");
	}

	if (mbi_addr & 7) {
		kprintf("Unaligned mbi: %x\n", mbi_addr);
		return;
	}

	size_t mbi_size = *(unsigned *)mbi_addr;
	kprintf("Announced mbi size %x\n", mbi_size);

	struct vmm_entry preserved_entries[1] = { 0 };
	size_t preserved_entry_count = 0;
	uintptr_t kernel_start_addr = round_up_to_page((uintptr_t)&HIGHER_HALF);

	struct mbi_info mbi_info = get_mbi_info(mbi_addr, mbi_size, kernel_start_addr);

	section_divisor("Initializing programable interrupt controller:\n");

	bool apic_capable = cpuid_has_apic();
	if (!apic_capable) {
		PIC_remap(0x20, 0x28);
		kprintf("- IRQ Master: start at dec: %u, hex: %x\n"
			"                end at dec: %u, hex: %x\n",
			0x20, 0x20, 0x20 + 7, 0x20 + 7);
		kprintf("- IRQ Slave:  start at dec: %u, hex: %x\n"
			"                end at dec: %u, hex: %x\n",
			0x28, 0x28, 0x28 + 7, 0x28 + 7);
	} else {
		kprintf("APIC detected, routing interrupts via Local APIC/IOAPIC\n");
		imcr_route_to_apic();
	}

	section_divisor("Initializing interrupt description table:\n");
	idt_init();
	kprintf("IDT initialized\n");

	phy_mem_init(mbi_info.mmap_tag, mbi_info.elf_sec_tag);
	phy_memory_test();

	// Preserve multiboot2 info in virtual memory
	const size_t mbi_alloc_size = round_up_to_page(mbi_size);
	fatptr_t mbi_buffer = phy_mem_alloc(mbi_alloc_size);
	if (mbi_buffer.ptr == nullptr)
		panic("Failed to allocate space for multiboot info copy\n");

	struct vmm_entry mbi_virt = {
		.ptr = (void *)mbi_info.kernel_end_addr,
		.size = mbi_buffer.len,
		.flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT,
	};
	map_pages(&mbi_buffer, &mbi_virt);

	memcpy(mbi_virt.ptr, (void *)mbi_addr, mbi_size);
	memset((uint8_t *)mbi_virt.ptr + mbi_size, 0, mbi_virt.size - mbi_size);

	const unsigned long relocated_addr = (unsigned long)mbi_virt.ptr;
	if (mbi_info.mmap_tag != nullptr) {
		const size_t offset = (size_t)((unsigned long)mbi_info.mmap_tag - mbi_addr);
		mbi_info.mmap_tag = (struct multiboot_tag_mmap *)(relocated_addr + offset);
	}
	if (mbi_info.elf_sec_tag != nullptr) {
		const size_t offset = (size_t)((unsigned long)mbi_info.elf_sec_tag - mbi_addr);
		mbi_info.elf_sec_tag = (struct multiboot_tag_elf_sections *)(relocated_addr + offset);
	}
	if (mbi_info.acpi_tag != nullptr) {
		const size_t offset = (size_t)((unsigned long)mbi_info.acpi_tag - mbi_addr);
		mbi_info.acpi_tag = (struct multiboot_tag *)(relocated_addr + offset);
	}

	if (preserved_entry_count < sizeof(preserved_entries) / sizeof(preserved_entries[0]))
		preserved_entries[preserved_entry_count++] = mbi_virt;

	section_divisor("Virtual memory init:\n");
	init_vir_mem(mbi_info.elf_sec_tag, preserved_entries, preserved_entry_count);

	section_divisor("Init kernel memory allocator:\n");

	init_kmalloc();
	init_slab_allocator();

	allocator_t gpa_alloc = get_gpa_allocator();
	gpa_test(gpa_alloc);

	section_divisor("SMP init:\n");
	smp_init(mbi_info.acpi_tag);

	struct madt_ioapic_info ioapic_desc = { 0 };
	struct madt_irq_override overrides[16] = { 0 };
	size_t override_count = smp_get_irq_overrides(overrides, 16);
	if (apic_capable && smp_get_ioapic_info(&ioapic_desc)) {
		struct ioapic_override ioapic_overrides[16] = { 0 };
		for (size_t i = 0; i < override_count && i < 16; i++) {
			ioapic_overrides[i].source = overrides[i].source;
			ioapic_overrides[i].gsi = overrides[i].gsi;
			ioapic_overrides[i].flags = overrides[i].flags;
		}
		ioapic_register_overrides(ioapic_overrides, override_count);
		ioapic_init(ioapic_desc.phys_addr, ioapic_desc.gsi_base, lapic_get_id());
		ioapic_configure_legacy_irqs();
		/* ioapic_unmask_irq(0); */
		ioapic_unmask_irq(1);
		pic_disable();
	}

	section_divisor("ATA PIO Test drives:\n");
	kprintf("    - hda: %s type \n", ata_pio_debug_devtype(ata_pio_detect_devtype(0)));
	kprintf("    - hdb: %s type\n", ata_pio_debug_devtype(ata_pio_detect_devtype(0)));

	kprintf("Content of hdb\n\n");
	fatptr_t hdb_t = gpa_alloc.alloc(513);
	char *hdb_v = hdb_t.ptr;
	memset(hdb_v, 0, 513);
	for (size_t i = 0; i < (430 * 1024) / 512; i++) {
		ata_pio_28_read(i, 1, hdb_v);
		kprintf("%s", hdb_v);
	}
	gpa_alloc.free(hdb_t);
	__asm__ volatile("sti");       // set the interrupt flag
}
