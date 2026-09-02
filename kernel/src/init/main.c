#include <kernel/multiboot.h>

#include <kernel/tty.h>
#include <kernel/serial.h>
#include <kernel/display.h>
#include <kernel/elf32.h>

#include <kernel/phy_mem.h>
#include <kernel/vir_mem.h>
#include <kernel/allocator.h>
#include <kernel/mem_allocs.h>

#include <kernel/interrupt.h>
#include <kernel/storage.h>
#include <kernel/block_device.h>
#include <kernel/fat16.h>
#include <kernel/memblock.h>

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
#include <string.h>

extern void idt_init(void);
extern void init_kmalloc(void);
extern void *HIGHER_HALF;

size_t GLOBAL_TICK = 0;
static void pit_tick_handler(uint8_t irq_line, void *context)
{
	(void)irq_line;
	(void)context;
	++GLOBAL_TICK;
}

static void imcr_route_to_apic(void)
{
	outb(0x22, 0x70);
	outb(0x23, 0x01);
}

void section_divisor(char *section_name)
{
	const char* div = "---------------------------------------"
			  "-------------------------------------\n";
	const size_t div_len = strlen(div);
	const size_t section_len = strlen(section_name);

	if (section_name != NULL)
		kprintf("\n%s", section_name);

	if ((long)div_len - (long)section_len < 1)
		panic("section divisor to long");

	kprintf(div + section_len);
}

void phy_memory_test()
{
	section_divisor("Testing physical memory allocator");
	const size_t alloc_count = 4;

	fatptr_t ptr[alloc_count];

	for (size_t k = 0; k <= 10; k++){
		kprintf("Level is: %x\n", k);
		for(size_t i = 0; i < alloc_count; i++){
			fatptr_t mem = phy_mem_alloc(PAGE_SIZE << k, PHY_MEM_ALLOC_HIGH);
			if (mem.ptr == nullptr)
				panic("physical allocator test allocation failed at level %x\n", k);
			kprintf("mem allocated: ptr %x, len %x\n", mem.ptr, mem.len);
			ptr[i] = mem;
		}
		for(size_t i = 0; i < alloc_count; i++){
			phy_mem_free(ptr[i]);
		}
		kprintf("\n");
	}
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

	fatptr_t ptr[512] = { 0 };

	for(size_t i = 0; i < 512; i++){
		fatptr_t mem = gpa_alloc.alloc(8);
		ptr[i] = mem;
	}
	for(size_t i = 0; i < 512; i++){
		gpa_alloc.free(ptr[i]);
	}

	for(size_t i = 0; i < 512; i++){
		fatptr_t mem = gpa_alloc.alloc(16);
		ptr[i] = mem;
	}
	for(size_t i = 0; i < 512; i++){
		gpa_alloc.free(ptr[i]);
	}

	for(size_t i = 0; i < 512; i++){
		fatptr_t mem = gpa_alloc.alloc(32);
		ptr[i] = mem;
	}
	for(size_t i = 0; i < 512; i++){
		gpa_alloc.free(ptr[i]);
	}

	for(size_t i = 0; i < 512; i++){
		fatptr_t mem = gpa_alloc.alloc(64);
		ptr[i] = mem;
	}
	for(size_t i = 0; i < 512; i++){
		gpa_alloc.free(ptr[i]);
	}

	for(size_t i = 0; i < 512; i++){
		fatptr_t mem = gpa_alloc.alloc(128);
		ptr[i] = mem;
	}
	for(size_t i = 0; i < 512; i++){
		gpa_alloc.free(ptr[i]);
	}

	for(size_t i = 0; i < 512; i++){
		fatptr_t mem = gpa_alloc.alloc(256);
		ptr[i] = mem;
	}
	for(size_t i = 0; i < 512; i++){
		gpa_alloc.free(ptr[i]);
	}

	for(size_t i = 0; i < 512; i++){
		fatptr_t mem = gpa_alloc.alloc(512);
		ptr[i] = mem;
	}
	for(size_t i = 0; i < 512; i++){
		gpa_alloc.free(ptr[i]);
	}

	for(size_t i = 0; i < 512; i++){
		fatptr_t mem = gpa_alloc.alloc(1024);
		ptr[i] = mem;
	}
	for(size_t i = 0; i < 512; i++){
		gpa_alloc.free(ptr[i]);
	}

	for(size_t i = 0; i < 512; i++){
		fatptr_t mem = gpa_alloc.alloc(2048);
		ptr[i] = mem;
	}
	for(size_t i = 0; i < 512; i++){
		gpa_alloc.free(ptr[i]);
	}

	for(size_t i = 0; i < 512; i++){
		fatptr_t mem = gpa_alloc.alloc(4096);
		ptr[i] = mem;
	}
	for(size_t i = 0; i < 512; i++){
		gpa_alloc.free(ptr[i]);
	}
}

struct mbi_info{
	struct multiboot_tag_mmap *mmap_tag;
	struct multiboot_tag_elf_sections *elf_sec_tag;
	struct multiboot_tag *acpi_tag;
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
	const uintptr_t mbi_offset = mbi_addr & (PAGE_SIZE - 1);
	const uintptr_t mbi_high_addr = (uintptr_t)&HIGHER_HALF + (mbi_addr - mbi_offset);
	const size_t mbi_alloc_size = round_up_to_page(mbi_offset + mbi_size);
	const struct vmm_entry preserved_entries[] = {
		{
			.ptr = (void *)mbi_high_addr,
			.size = mbi_alloc_size,
			.flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT,
		},
	};

	/* struct vmm_entry mbi_virt = { */
	/* 	.ptr = (void *)mbi_info.kernel_end_addr, */
	/* 	.size = mbi_buffer.len, */
	/* 	.flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT, */
	/* }; */
	/* map_pages(&mbi_buffer, &mbi_virt); */

	/* memcpy(mbi_virt.ptr, (void *)mbi_addr, mbi_size); */
	/* memset((uint8_t *)mbi_virt.ptr + mbi_size, 0, mbi_virt.size - mbi_size); */

	/* const unsigned long relocated_addr = (unsigned long)mbi_virt.ptr; */
	/* if (mbi_info.mmap_tag != nullptr) { */
	/* 	const size_t offset = (size_t)((unsigned long)mbi_info.mmap_tag - mbi_addr); */
	/* 	mbi_info.mmap_tag = (struct multiboot_tag_mmap *)(relocated_addr + offset); */
	/* } */
	/* if (mbi_info.elf_sec_tag != nullptr) { */
	/* 	const size_t offset = (size_t)((unsigned long)mbi_info.elf_sec_tag - mbi_addr); */
	/* 	mbi_info.elf_sec_tag = (struct multiboot_tag_elf_sections *)(relocated_addr + offset); */
	/* } */
	/* if (mbi_info.acpi_tag != nullptr) { */
	/* 	const size_t offset = (size_t)((unsigned long)mbi_info.acpi_tag - mbi_addr); */
	/* 	mbi_info.acpi_tag = (struct multiboot_tag *)(relocated_addr + offset); */
	/* } */

	/* if (preserved_entry_count < sizeof(preserved_entries) / sizeof(preserved_entries[0])) */
	/* 	preserved_entries[preserved_entry_count++] = mbi_virt; */

	section_divisor("Virtual memory init");
	vmm_init(mbi_info.elf_sec_tag, nullptr, 0);
	init_kmalloc();
	init_slab_allocator();
	struct vmm_entry *mbi_copy_virt = vmm_alloc(mbi_copy_size,
		VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);
	if (mbi_copy_virt == nullptr)
		panic("Failed to allocate virtual Multiboot copy\n");
	map_pages(&mbi_copy_phys, mbi_copy_virt);
	mbi_info = find_mbi_info((uintptr_t)mbi_copy_virt->ptr, mbi_size);

	/* allocator_t gpa_alloc = get_gpa_allocator(); */
	/* gpa_test(gpa_alloc); */

	/* section_divisor("Initializing programable interrupt controller:\n"); */

	/* bool apic_capable = cpuid_has_apic(); */
	/* if (!apic_capable) { */
	/* 	PIC_remap(0x20, 0x28); */
	/* 	kprintf("- IRQ Master: start at dec: %u, hex: %x\n" */
	/* 		"                end at dec: %u, hex: %x\n", */
	/* 		0x20, 0x20, 0x20 + 7, 0x20 + 7); */
	/* 	kprintf("- IRQ Slave:  start at dec: %u, hex: %x\n" */
	/* 		"                end at dec: %u, hex: %x\n", */
	/* 		0x28, 0x28, 0x28 + 7, 0x28 + 7); */
	/* } else { */
	/* 	kprintf("APIC detected, routing interrupts via Local APIC/IOAPIC\n"); */
	/* 	imcr_route_to_apic(); */
	/* } */

	idt_init();
	smp_init(mbi_info.acpi_tag);

	/* struct madt_ioapic_info ioapic_desc = { 0 }; */
	/* struct madt_irq_override overrides[16] = { 0 }; */
	/* size_t override_count = smp_get_irq_overrides(overrides, 16); */
	/* if (apic_capable && smp_get_ioapic_info(&ioapic_desc)) { */
	/* 	struct ioapic_override ioapic_overrides[16] = { 0 }; */
	/* 	for (size_t i = 0; i < override_count && i < 16; i++) { */
	/* 		ioapic_overrides[i].source = overrides[i].source; */
	/* 		ioapic_overrides[i].gsi = overrides[i].gsi; */
	/* 		ioapic_overrides[i].flags = overrides[i].flags; */
	/* 	} */
	/* 	ioapic_register_overrides(ioapic_overrides, override_count); */
	/* 	ioapic_init(ioapic_desc.phys_addr, ioapic_desc.gsi_base, lapic_get_id()); */
	/* 	ioapic_configure_legacy_irqs(); */
	/* 	pic_disable(); */
	/* } */

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
	syscall_init();
	if (!syscall_register_console_handlers())
		panic("Failed to register console syscalls\n");

	struct process_exec_result calc;
	struct block_device application_disk;
	bool loaded_from_disk = false;
	if (block_device_find("sata1", &application_disk)) {
		loaded_from_disk = process_exec_block_device_calc(&application_disk, &calc);
	}
	for (size_t i = 0; i < block_device_count(); ++i) {
		if (!block_device_get(i, &application_disk))
			continue;
		if (loaded_from_disk)
			continue;
		if (process_exec_block_device_calc(&application_disk, &calc)) {
			loaded_from_disk = true;
			break;
		}
	}
	if (loaded_from_disk) {
		/* The disk image was already loaded while selecting the device. */
	} else if (!process_exec_multiboot_calc(mbi_copy_virt->ptr, mbi_size, &calc)) {
		panic("Failed to launch calc from Multiboot\n");
	}
	if (loaded_from_disk)
		kprintf("Loaded calc from FAT16 block device %s\n", application_disk.name);

	/* IRQ0 still targets the legacy exception vector until a scheduler is added. */
	pic_mask_irq(0);
	__asm__ volatile("sti");
	i386_context_enter_user(&calc.context);

	/* storage_init(); */

	/* struct storage_device device; */
	/* if (!storage_get_device(1, &device)) { */
	/* 	kprintf("No storage device available\n"); */
	/* 	return; */
	/* } */

	/* fat_BS_t *fat_bs = read_fat_boot_section(device); */

	/* fat16_layout_t layout; */
	/* fat16_compute_layout(fat_bs, &layout); */

	/* fat_dir_entry_t hp_entry; */
	/* if (fat16_find_entry_by_name(&device, "hp1.txt", &hp_entry)) { */
	/* 	size_t buf_size = hp_entry.file_size + 1; */
	/* 	fatptr_t buf = gpa_alloc.alloc(buf_size); */
	/* 	if (buf.ptr != nullptr) { */
	/* 		size_t read = 0; */
	/* 		if (fat16_read_file(&device, &hp_entry, buf.ptr, buf_size - 1, &read)) { */
	/* 			((char *)buf.ptr)[read] = '\0'; */
	/* 			kprintf("hp1.txt contents:\n%s\n", (char *)buf.ptr); */
	/* 		} */
	/* 		gpa_alloc.free(buf); */
	/* 	} */
	/* } else { */
	/* 	kprintf("hp1.txt not found\n"); */
	/* } */

	/* gpa_alloc.free((fatptr_t){.ptr=fat_bs, .len=sizeof(fat_BS_t)}); */

	kprintf("Finish init\n");
}
