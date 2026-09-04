#include <kernel/multiboot.h>

#include <kernel/serial.h>
#include <kernel/display.h>
#include <kernel/elf32.h>

#include <kernel/phy_mem.h>
#include <kernel/vir_mem.h>
#include <kernel/allocator.h>

#include <kernel/interrupt.h>
#include <kernel/storage.h>
#include <kernel/block_device.h>
#include <kernel/fat16.h>
#include <kernel/framebuffer_boot.h>
#include <kernel/memblock.h>
#include <kernel/stage5.h>

#include <arch/i386/ata_pio.h>
#include <arch/i386/control_register.h>
#include <arch/i386/cpuid.h>
#include <arch/i386/ioapic.h>
#include <arch/i386/lapic.h>
#include <arch/i386/pic.h>
#include <arch/i386/ps2.h>
#include <arch/i386/irq.h>
#include <arch/i386/smp.h>
#include <arch/i386/port.h>
#include "../exec/multiboot_exec.h"
#include <kernel/process/process.h>
#include <arch/i386/context.h>
#include <kernel/syscall.h>
#include <kernel/scheduler.h>
#include <string.h>
#ifdef JANOS_KERNEL_TESTS
#include <kernel/test.h>
#endif

extern void idt_init(void);
extern void init_kmalloc(void);

size_t GLOBAL_TICK = 0;
static void pit_tick_handler(uint8_t irq_line, void *context)
{
	(void)irq_line;
	(void)context;
	++GLOBAL_TICK;
}

static void pit_init(void)
{
	/* 100 Hz keeps the scheduler independent of the storage interrupt rate. */
	outb(0x43, 0x36);
	const uint16_t divisor = 1193182u / 100u;
	outb(0x40, (uint8_t)divisor);
	outb(0x40, (uint8_t)(divisor >> 8));
	if (!irq_register_handler(0, pit_tick_handler, nullptr))
		panic("Failed to register PIT handler\n");
}

static bool find_application_disk(struct block_device *out_device)
{
	if (out_device == nullptr)
		return false;
	if (block_device_find("sata1", out_device) &&
	    fat16_find_entry_by_name(out_device, "calc", &(fat_dir_entry_t){ 0 }))
		return true;
	for (size_t i = 0; i < block_device_count(); ++i) {
		struct block_device candidate;
		fat_dir_entry_t entry;
		if (!block_device_get(i, &candidate) ||
		    !fat16_find_entry_by_name(&candidate, "calc", &entry))
			continue;
		*out_device = candidate;
		return true;
	}
	return false;
}

static void imcr_route_to_apic(void)
{
	outb(0x22, 0x70);
	outb(0x23, 0x01);
}

static void section_divisor(const char *section_name)
{
	const char* div = "---------------------------------------"
			  "-------------------------------------\n";
	const size_t div_len = strlen(div);
	if (section_name == nullptr)
		return;
	const size_t section_len = strlen(section_name);
	kprintf("\n%s", section_name);

	if ((long)div_len - (long)section_len < 1)
		panic("section divisor too long");

	kprintf(div + section_len);
}

struct mbi_info {
	struct multiboot_tag_mmap *mmap_tag;
	struct multiboot_tag_elf_sections *elf_sec_tag;
	const struct multiboot_tag *acpi_tag;
};
static struct mbi_info find_mbi_info(uintptr_t mbi_addr, size_t mbi_size)
{
	struct mbi_info res = { 0 };
	const uint8_t *end = (const uint8_t *)(mbi_addr + mbi_size);
	const struct multiboot_tag *tag =
		(const struct multiboot_tag *)(mbi_addr + MULTIBOOT_INFO_HEADER_SIZE);
	while ((uintptr_t)tag < (uintptr_t)end) {
		if ((uintptr_t)end - (uintptr_t)tag < MULTIBOOT_TAG_HEADER_SIZE ||
		    tag->size < MULTIBOOT_TAG_HEADER_SIZE ||
		    (uintptr_t)end - (uintptr_t)tag < tag->size)
			panic("Invalid Multiboot tag\n");
		if (tag->type == MULTIBOOT_TAG_TYPE_END) {
			if (tag->size != MULTIBOOT_TAG_HEADER_SIZE)
				panic("Invalid Multiboot end tag\n");
			break;
		}
		switch (tag->type) {
		case MULTIBOOT_TAG_TYPE_MMAP:
			res.mmap_tag = (struct multiboot_tag_mmap *)tag;
			break;
		case MULTIBOOT_TAG_TYPE_ELF_SECTIONS:
			res.elf_sec_tag = (struct multiboot_tag_elf_sections *)tag;
			break;
		case MULTIBOOT_TAG_TYPE_ACPI_OLD:
		case MULTIBOOT_TAG_TYPE_ACPI_NEW:
			res.acpi_tag = tag;
			break;
		default:
			break;
		}
		uint32_t aligned_size = (tag->size + MULTIBOOT_TAG_ALIGN - 1) &
			~(MULTIBOOT_TAG_ALIGN - 1);
		if (aligned_size < tag->size || (uintptr_t)end - (uintptr_t)tag < aligned_size)
			panic("Invalid Multiboot tag alignment\n");
		tag = (const struct multiboot_tag *)((const uint8_t *)tag + aligned_size);
	}
	return res;
}

void kernel_main(unsigned int magic, unsigned long mbi_addr)
{
	display_t serial_dpy = init_serial();
	uint8_t serial_dpy_reg = DISPLAY_MAX_DISPS;
	if (serial_dpy.putc != nullptr || serial_dpy.puts != nullptr) {
		serial_dpy_reg = display_register(serial_dpy);
		display_set_debug(serial_dpy_reg);
		display_setcurrent(serial_dpy_reg);
	}

	section_divisor("Control registers");
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

	memblock_init(mbi_addr, false);
	memblock_dump();

	phy_mem_init();

	struct mbi_info mbi_info = find_mbi_info(mbi_addr, mbi_size);
	const size_t mbi_copy_size = round_up_to_page(mbi_size);
	fatptr_t mbi_copy_phys = phy_mem_alloc(mbi_copy_size, PHY_MEM_ALLOC_HIGH);
	if (mbi_copy_phys.ptr == nullptr)
		panic("Failed to allocate Multiboot copy\n");
	memcpy(mbi_copy_phys.ptr, (const void *)mbi_addr, mbi_size);
	memset((uint8_t *)mbi_copy_phys.ptr + mbi_size, 0, mbi_copy_size - mbi_size);

	section_divisor("Virtual memory init");
	vmm_init(mbi_info.elf_sec_tag, nullptr, 0);
	init_kmalloc();
	init_slab_allocator();
	vmm_finish_init(mbi_info.elf_sec_tag, nullptr, 0);
	struct vmm_entry *mbi_copy_virt = vmm_alloc(mbi_copy_size,
		VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);
	if (mbi_copy_virt == nullptr)
		panic("Failed to allocate virtual Multiboot copy\n");
	map_pages(&mbi_copy_phys, mbi_copy_virt);
	mbi_info = find_mbi_info((uintptr_t)mbi_copy_virt->ptr, mbi_size);

	idt_init();
	smp_init((struct multiboot_tag *)mbi_info.acpi_tag);

	struct madt_ioapic_info ioapic_info;
	if (smp_get_ioapic_info(&ioapic_info)) {
		struct madt_irq_override overrides[16];
		size_t override_count = smp_get_irq_overrides(overrides, 16);
		struct ioapic_override ioapic_overrides[16];
		for (size_t i = 0; i < override_count; i++)
			ioapic_overrides[i] = (struct ioapic_override){
				.source = overrides[i].source,
				.gsi = overrides[i].gsi,
				.flags = overrides[i].flags,
			};
		ioapic_register_overrides(ioapic_overrides, override_count);
		ioapic_init(ioapic_info.phys_addr, ioapic_info.gsi_base, lapic_get_id());
		ioapic_configure_legacy_irqs();
	}

	/* The legacy PIC starts with IRQ0 mapped to exception vector 8. */
	if (cpuid_has_apic()) {
		imcr_route_to_apic();
		pic_disable();
	}

	ps2_init();
	block_device_init();

	process_system_init();
	scheduler_init();
	syscall_init();
	if (!syscall_register_console_handlers())
		panic("Failed to register console syscalls\n");
	pit_init();
#ifdef JANOS_KERNEL_TESTS
	kernel_test_boot(mbi_copy_virt->ptr, mbi_size);
#endif

	__asm__ volatile("sti");

	struct block_device application_disk;
	if (!find_application_disk(&application_disk))
		panic("No FAT16 application disk found\n");

	struct i386_context initial_context;
	bool framebuffer_started = framebuffer_boot_services(mbi_copy_virt->ptr, mbi_size,
		&application_disk,
		&initial_context);
	if (!framebuffer_started)
		kprintf("Framebuffer services unavailable\n");
	else
		framebuffer_console_enable();

	if (framebuffer_started && !framebuffer_boot_clear())
		panic("Failed to queue framebuffer clear request\n");

	bool initial_context_ready = framebuffer_started;
	if (!stage5_boot_services(&application_disk, &initial_context,
		&initial_context_ready))
		panic("Failed to start Stage 5 userspace services\n");

	i386_context_enter_user(&initial_context);

	kprintf("Finish init\n");
}
