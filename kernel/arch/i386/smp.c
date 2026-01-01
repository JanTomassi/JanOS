#include "smp.h"

#include "../i386/cpuid.h"
#include "../i386/lapic.h"
#include <kernel/allocator.h>
#include <kernel/display.h>
#include <kernel/phy_mem.h>
#include <kernel/spinlock.h>
#include <kernel/vir_mem.h>
#include <kernel/interrupt.h>
#include "../i386/control_register.h"
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

MODULE("SMP");

#define MAX_CPUS 16
#define AP_STACK_SIZE (16 * 1024)

#define IPI_DELIVERY_MODE_INIT 0x5
#define IPI_DELIVERY_MODE_STARTUP 0x6

struct idtr_desc {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed));

static struct cpu_info cpus[MAX_CPUS];
static size_t cpu_count = 0;
static spinlock_t cpu_lock = { 0 };
static struct idtr_desc bsp_idtr = { 0 };

struct rsdp_descriptor {
	char signature[8];
	uint8_t checksum;
	char oem_id[6];
	uint8_t revision;
	uint32_t rsdt_address;
	uint32_t length;
	uint64_t xsdt_address;
	uint8_t extended_checksum;
	uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char oem_id[6];
	char oem_table_id[8];
	uint32_t oem_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
} __attribute__((packed));

struct madt_header {
	struct acpi_sdt_header header;
	uint32_t lapic_addr;
	uint32_t flags;
	uint8_t entries[];
} __attribute__((packed));

extern uint8_t ap_trampoline_start;
extern uint8_t ap_trampoline_end;
extern uint32_t ap_trampoline_cr3;
extern uint32_t ap_trampoline_entry;
extern uint32_t ap_trampoline_stack;
extern uint16_t ap_trampoline_idt_ptr;
extern uint8_t ap_trampoline_stack_buf_top;

extern void ap_start(void);
void ap_main(void);
void *smp_get_stack_top(uint8_t apic_id);

static uintptr_t ap_trampoline_phys_base = 0;
static struct vmm_entry *ap_trampoline_virt = nullptr;

static void record_idtr(void)
{
	__asm__ volatile("sidt %0" : "=m"(bsp_idtr) : : "memory");
}

static void register_cpu(uint8_t apic_id)
{
	spin_lock(&cpu_lock);
	for (size_t i = 0; i < cpu_count; i++) {
		if (cpus[i].apic_id == apic_id) {
			spin_unlock(&cpu_lock);
			return;
		}
	}

	if (cpu_count < MAX_CPUS) {
		cpus[cpu_count].apic_id = apic_id;
		cpus[cpu_count].online = false;
		cpus[cpu_count].stack_top = nullptr;
		cpu_count++;
		mprint("Registered CPU with APIC ID %u\n", apic_id);
	}
	spin_unlock(&cpu_lock);
}

static void map_physical_range(void *phys, size_t len, uint16_t flags)
{
	void *base = (void *)round_down_to_page((uintptr_t)phys);
	const size_t offset = (uintptr_t)phys - (uintptr_t)base;
	const size_t mapped_len = round_up_to_page(offset + len);
	fatptr_t phys_range = {
		.ptr = base,
		.len = mapped_len,
	};
	struct vmm_entry virt = {
		.ptr = base,
		.size = mapped_len,
		.flags = flags,
	};
	map_pages(&phys_range, &virt);
}

static bool parse_madt(void *phys_addr)
{
	map_physical_range(phys_addr, PAGE_SIZE, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT);
	struct madt_header *madt = (struct madt_header *)phys_addr;

	map_physical_range(phys_addr, madt->header.length, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT);

	bool registered = false;
	size_t offset = sizeof(struct madt_header);
	while (offset + 2 <= madt->header.length) {
		uint8_t *entry = ((uint8_t *)madt) + offset;
		uint8_t type = entry[0];
		uint8_t length = entry[1];
		if (length < 2)
			break;

		if (type == 0 && length >= 8) {
			uint8_t apic_id = entry[3];
			uint32_t flags = *(uint32_t *)(entry + 4);
			if (flags & 0x1) {
				register_cpu(apic_id);
				registered = true;
			}
		}

		offset += length;
	}

	return registered;
}

static bool parse_acpi_tag(struct multiboot_tag *acpi_tag)
{
	if (acpi_tag == nullptr)
		return false;

	switch (acpi_tag->type) {
	case MULTIBOOT_TAG_TYPE_ACPI_OLD:
	case MULTIBOOT_TAG_TYPE_ACPI_NEW: {
		struct rsdp_descriptor *rsdp = (struct rsdp_descriptor *)(((struct multiboot_tag_new_acpi *)acpi_tag)->rsdp);
		void *rsdt_phys = (void *)(uintptr_t)rsdp->rsdt_address;
		map_physical_range(rsdt_phys, PAGE_SIZE,
				   VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT);

		struct acpi_sdt_header *rsdt = (struct acpi_sdt_header *)rsdt_phys;
		map_physical_range(rsdt_phys, rsdt->length,
				   VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT);

		size_t entry_count = (rsdt->length - sizeof(struct acpi_sdt_header)) / sizeof(uint32_t);
		uint32_t *entries = (uint32_t *)((uint8_t *)rsdt + sizeof(struct acpi_sdt_header));
		for (size_t i = 0; i < entry_count; i++) {
			struct acpi_sdt_header *hdr = (struct acpi_sdt_header *)(uintptr_t)entries[i];
			map_physical_range(hdr, PAGE_SIZE,
					   VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT);
			if (memcmp(hdr->signature, "APIC", 4) == 0) {
				map_physical_range(hdr, hdr->length,
						   VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT);
				if (parse_madt(hdr))
					return true;
			}
		}
		break;
	}
	default:
		break;
	}

	return false;
}

static void parse_apic_ids(struct multiboot_tag *acpi_tag)
{
	bool found_cpus = false;

	found_cpus |= parse_acpi_tag(acpi_tag);

	if (!found_cpus) {
		kprintf("SMP: falling back to BSP CPUID enumeration only\n");
		register_cpu(cpuid_apic_id());
	}
}

static void setup_ap_trampoline(void)
{
	const size_t trampoline_size = (&ap_trampoline_end) - (&ap_trampoline_start);
	const size_t trampoline_pages = round_up_to_page(trampoline_size);

	fatptr_t phys = phy_mem_alloc_below(trampoline_pages, MIBI(1));
	if (phys.ptr == nullptr)
		panic("Failed to allocate physical memory for AP trampoline\n");

	ap_trampoline_virt = vir_mem_alloc(trampoline_pages, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);
	if (ap_trampoline_virt == nullptr)
		panic("Failed to allocate virtual memory for AP trampoline\n");

	map_pages(&phys, ap_trampoline_virt);
	memset(ap_trampoline_virt->ptr, 0, ap_trampoline_virt->size);
	memcpy(ap_trampoline_virt->ptr, &ap_trampoline_start, trampoline_size);

	ap_trampoline_phys_base = (uintptr_t)phys.ptr;

	const size_t cr3_off = (size_t)((uintptr_t)&ap_trampoline_cr3 - (uintptr_t)&ap_trampoline_start);
	const size_t entry_off = (size_t)((uintptr_t)&ap_trampoline_entry - (uintptr_t)&ap_trampoline_start);
	const size_t stack_ptr_off = (size_t)((uintptr_t)&ap_trampoline_stack - (uintptr_t)&ap_trampoline_start);
	const size_t idt_ptr_off = (size_t)((uintptr_t)&ap_trampoline_idt_ptr - (uintptr_t)&ap_trampoline_start);
	const size_t stack_top_off = (size_t)((uintptr_t)&ap_trampoline_stack_buf_top - (uintptr_t)&ap_trampoline_start);

	uint32_t *trampoline_cr3 = (uint32_t *)((uint8_t *)ap_trampoline_virt->ptr + cr3_off);
	uint32_t *trampoline_entry = (uint32_t *)((uint8_t *)ap_trampoline_virt->ptr + entry_off);
	uint32_t *trampoline_stack = (uint32_t *)((uint8_t *)ap_trampoline_virt->ptr + stack_ptr_off);
	struct idtr_desc *trampoline_idtr = (struct idtr_desc *)((uint8_t *)ap_trampoline_virt->ptr + idt_ptr_off);

	*trampoline_cr3 = (uint32_t)get_CR3_reg();
	*trampoline_entry = (uint32_t)&ap_start;
	*trampoline_stack = ap_trampoline_phys_base + stack_top_off - 4;

	*trampoline_idtr = bsp_idtr;

	mprint("AP trampoline copied to %x (size %u)\n", (unsigned)ap_trampoline_phys_base, (unsigned)trampoline_pages);
}

static void build_stacks(void)
{
	allocator_t gpa = get_gpa_allocator();
	for (size_t i = 0; i < cpu_count; i++) {
		fatptr_t stack = gpa.alloc(AP_STACK_SIZE);
		if (stack.ptr == nullptr)
			panic("Failed to allocate stack for CPU %u\n", cpus[i].apic_id);

		cpus[i].stack_top = (uint8_t *)stack.ptr + stack.len;
	}
}

static void start_aps(void)
{
	const uint8_t trampoline_vector = ap_trampoline_phys_base >> 12;

	for (size_t i = 0; i < cpu_count; i++) {
		if (cpus[i].apic_id == lapic_get_id())
			continue;

		mprint("Sending INIT to APIC %u\n", cpus[i].apic_id);
		lapic_send_ipi(cpus[i].apic_id, 0, IPI_DELIVERY_MODE_INIT);

		for (volatile int wait = 0; wait < 100000; wait++)
			;

		mprint("Sending SIPI to APIC %u\n", cpus[i].apic_id);
		lapic_send_ipi(cpus[i].apic_id, trampoline_vector, IPI_DELIVERY_MODE_STARTUP);

		for (volatile int wait = 0; wait < 100000; wait++)
			;

		lapic_send_ipi(cpus[i].apic_id, trampoline_vector, IPI_DELIVERY_MODE_STARTUP);
	}
}

void smp_init(struct multiboot_tag *acpi_tag)
{
	if (!cpuid_has_apic()) {
		kprintf("APIC not available, skipping SMP init\n");
		return;
	}

	record_idtr();
	parse_apic_ids(acpi_tag);
	lapic_enable();
	setup_ap_trampoline();
	build_stacks();

	for (size_t i = 0; i < cpu_count; i++) {
		if (cpus[i].apic_id == lapic_get_id()) {
			cpus[i].online = true;
			break;
		}
	}

	start_aps();

	for (volatile int wait = 0; wait < 1000000; wait++) {
		bool all_online = true;
		for (size_t i = 0; i < cpu_count; i++) {
			if (cpus[i].apic_id == lapic_get_id())
				continue;
			if (!cpus[i].online) {
				all_online = false;
				break;
			}
		}
		if (all_online)
			break;
	}

	for (size_t i = 0; i < cpu_count; i++)
		mprint("CPU APIC %u status: %s\n", cpus[i].apic_id, cpus[i].online ? "online" : "offline");
}

struct cpu_info *smp_get_cpus(size_t *count)
{
	if (count != nullptr)
		*count = cpu_count;
	return cpus;
}

void *smp_get_stack_top(uint8_t apic_id)
{
	for (size_t i = 0; i < cpu_count; i++) {
		if (cpus[i].apic_id == apic_id)
			return cpus[i].stack_top;
	}
	return nullptr;
}

void ap_main(void)
{
	uint8_t apic_id = lapic_get_id();

	__asm__ volatile("mov $0x10, %ax\n"
			 "mov %ax, %ds\n"
			 "mov %ax, %es\n"
			 "mov %ax, %fs\n"
			 "mov %ax, %gs\n"
			 "mov %ax, %ss\n");

	size_t idx = MAX_CPUS;
	for (size_t i = 0; i < cpu_count; i++) {
		if (cpus[i].apic_id == apic_id) {
			idx = i;
			break;
		}
	}

	lapic_enable();
	__asm__ volatile("lidt %0" : : "m"(bsp_idtr));

	if (idx < MAX_CPUS) {
		spin_lock(&cpu_lock);
		cpus[idx].online = true;
		spin_unlock(&cpu_lock);
		mprint("AP (APIC %u) online\n", apic_id);
	}

	__asm__ volatile("sti");
	for (;;)
		__asm__ volatile("hlt");
}
