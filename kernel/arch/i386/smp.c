#include "smp.h"

#include "../i386/cpuid.h"
#include "../i386/lapic.h"
#include <kernel/cleanup.h>
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

#define LAPIC_MMIO_BASE 0xFEE00000

#define MAX_CPUS 16
#define AP_STACK_SIZE (16 * 1024)

#define IPI_DELIVERY_MODE_INIT 0x5
#define IPI_DELIVERY_MODE_STARTUP 0x6

#define NO_CACHE_RW_DEV_VMM_FLAGS (VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT)

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

typedef struct tramp_gdtr {
	uint16_t size;
	uint32_t ptr
} __attribute__((packed)) tramp_gdtr_t;

extern uint8_t ap_trampoline_start;
extern uint8_t ap_trampoline_end;
extern uint32_t ap_trampoline_cr3;
extern uint32_t ap_trampoline_entry;
extern uint32_t ap_trampoline_stack;
extern uint16_t ap_trampoline_idt_ptr;
extern uint8_t ap_trampoline_stack_buf_top;
extern tramp_gdtr_t ap_trampoline_gdtr;
extern uint64_t ap_trampoline_gdt;

extern void ap_start(void);
void ap_main(void);
void *smp_get_stack_top(uint8_t apic_id);

static uintptr_t ap_trampoline_phys_base = 0;
static size_t ap_trampoline_page_count = 0;
static struct vmm_entry *ap_trampoline_virt = nullptr;
static struct madt_ioapic_info ioapic_info = { 0 };
static struct madt_irq_override irq_overrides[16] = { 0 };
static size_t irq_override_count = 0;
static bool ioapic_found = false;

static struct madt_header *madt_header = nullptr;
static struct acpi_sdt_header *rsdp_header = nullptr;

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

static void unmap_physical_range(void *virt_addr, size_t len){
	void *base = (void *)round_down_to_page((uintptr_t)virt_addr);
	const size_t offset = (uintptr_t)virt_addr - (uintptr_t)base;
	const size_t mapped_len = round_up_to_page(offset + len);
	struct vmm_entry virt = {
		.ptr = base,
		.size = mapped_len,
		.flags = 0,
	};
	unmap_pages(nullptr, &virt);
}

static bool parse_madt(void *phys_addr)
{
	size_t madt_length = 0;
	bool registered = false;
	WITH(map_physical_range(phys_addr, PAGE_SIZE, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT),
	     unmap_physical_range(phys_addr, PAGE_SIZE))
	{
		struct madt_header *madt = (struct madt_header *)phys_addr;
		madt_length = madt->header.length;
	}

	WITH(map_physical_range(phys_addr, madt_length, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT),
	     unmap_physical_range(phys_addr, madt_length))
	{
		struct madt_header *madt = (struct madt_header *)phys_addr;
		allocator_t gpa = get_gpa_allocator();
		fatptr_t copy_madt = gpa.alloc(madt_length);
		memcpy(copy_madt.ptr, (uint8_t*)madt, madt_length);
		madt_header = copy_madt.ptr;
	}

	lapic_set_base(madt_header->lapic_addr);
	size_t offset = sizeof(struct madt_header);
	while (offset + 2 <= madt_header->header.length) {
		uint8_t *entry = ((uint8_t *)madt_header) + offset;
		uint8_t type = entry[0];
		uint8_t length = entry[1];
		if (length < 2)
			break;

		switch (type) {
		case 0:
			if (length >= 8) {
				uint8_t apic_id = entry[3];
				uint32_t flags = *(uint32_t *)(entry + 4);
				if (flags & 0x1) {
					register_cpu(apic_id);
					registered = true;
				}
			}
			break;
		case 1:
			if (length >= 12 && !ioapic_found) {
				ioapic_info.ioapic_id = entry[2];
				ioapic_info.phys_addr = *(uint32_t *)(entry + 4);
				ioapic_info.gsi_base = *(uint32_t *)(entry + 8);
				ioapic_found = true;
				mprint("MADT IOAPIC: id=%u phys=%x gsi_base=%u\n", ioapic_info.ioapic_id,
				       (unsigned)ioapic_info.phys_addr, ioapic_info.gsi_base);
			}
			break;
		case 2:
			if (length >= 10 && irq_override_count < sizeof(irq_overrides) / sizeof(irq_overrides[0])) {
				struct madt_irq_override *ovr = &irq_overrides[irq_override_count++];
				ovr->source = entry[3];
				ovr->gsi = *(uint32_t *)(entry + 4);
				ovr->flags = *(uint16_t *)(entry + 8);
				mprint("MADT IRQ override: src=%u gsi=%u flags=%x\n", ovr->source, ovr->gsi, ovr->flags);
			}
			break;
		default:
			break;
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
		void *rsdp_phys = (void *)(uintptr_t)rsdp->rsdt_address;
		size_t rsdp_length = 0;

		WITH(map_physical_range(rsdp_phys, PAGE_SIZE, NO_CACHE_RW_DEV_VMM_FLAGS),
		     unmap_physical_range(rsdp_phys, PAGE_SIZE))
		{
			struct acpi_sdt_header *rsdp = (struct acpi_sdt_header *)rsdp_phys;
			rsdp_length = rsdp->length;
		}

		WITH(map_physical_range(rsdp_phys, rsdp_length, NO_CACHE_RW_DEV_VMM_FLAGS),
		     unmap_physical_range(rsdp_phys, rsdp_length))
		{
			struct acpi_sdt_header *rsdp = (struct acpi_sdt_header *)rsdp_phys;
			allocator_t gpa = get_gpa_allocator();
			fatptr_t copy_rsdp = gpa.alloc(rsdp_length);
			memcpy(copy_rsdp.ptr, (uint8_t*)rsdp, rsdp_length);
			rsdp_header = copy_rsdp.ptr;
		}

		size_t entry_count = (rsdp_header->length - sizeof(struct acpi_sdt_header)) / sizeof(uint32_t);
		uint32_t *entries = (uint32_t *)((uint8_t *)rsdp_header + sizeof(struct acpi_sdt_header));
		for (size_t i = 0; i < entry_count; i++) {
			size_t hdr_length = 0;
			bool is_apic = false;
			WITH(map_physical_range((void*)entries[i], PAGE_SIZE, NO_CACHE_RW_DEV_VMM_FLAGS),
			     unmap_physical_range((void*)entries[i], PAGE_SIZE))
			{
				struct acpi_sdt_header *hdr = (struct acpi_sdt_header *)entries[i];
				hdr_length = hdr->length;
				is_apic = memcmp(hdr->signature, "APIC", 4) == 0;
			}

			if (is_apic) {
				WITH(map_physical_range((void*)entries[i], hdr_length, NO_CACHE_RW_DEV_VMM_FLAGS),
				     unmap_physical_range((void*)entries[i], PAGE_SIZE))
				{
					struct acpi_sdt_header *hdr = (struct acpi_sdt_header *)entries[i];
					if (parse_madt(hdr))
						return true;
				}
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
	const size_t trampoline_page_count = trampoline_pages / PAGE_SIZE;

	fatptr_t phys = phy_mem_alloc_below(trampoline_pages, MIBI(1));
	if (phys.ptr == nullptr)
		panic("Failed to allocate physical memory for AP trampoline\n");

	ap_trampoline_virt = vmm_alloc(trampoline_pages, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);
	if (ap_trampoline_virt == nullptr)
		panic("Failed to allocate virtual memory for AP trampoline\n");

	map_pages(&phys, ap_trampoline_virt);
	memset(ap_trampoline_virt->ptr, 0, ap_trampoline_virt->size);
	memcpy(ap_trampoline_virt->ptr, &ap_trampoline_start, trampoline_size);

	ap_trampoline_phys_base = (uintptr_t)phys.ptr;
	ap_trampoline_page_count = trampoline_page_count;

	struct vmm_entry identity_entry = {
		.ptr = (void *)ap_trampoline_phys_base,
		.size = trampoline_pages,
		.flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT,
	};
	map_pages(&phys, &identity_entry);

	const size_t cr3_off = (size_t)((uintptr_t)&ap_trampoline_cr3 - (uintptr_t)&ap_trampoline_start);
	const size_t entry_off = (size_t)((uintptr_t)&ap_trampoline_entry - (uintptr_t)&ap_trampoline_start);
	const size_t stack_ptr_off = (size_t)((uintptr_t)&ap_trampoline_stack - (uintptr_t)&ap_trampoline_start);
	const size_t idt_ptr_off = (size_t)((uintptr_t)&ap_trampoline_idt_ptr - (uintptr_t)&ap_trampoline_start);
	const size_t stack_top_off = (size_t)((uintptr_t)&ap_trampoline_stack_buf_top - (uintptr_t)&ap_trampoline_start);
	const size_t gdtr_off = (size_t)((uintptr_t)&ap_trampoline_gdtr - (uintptr_t)&ap_trampoline_start);
	const size_t gdt_off = (size_t)((uintptr_t)&ap_trampoline_gdt - (uintptr_t)&ap_trampoline_start);

	uint32_t *trampoline_cr3 = (uint32_t *)((uint8_t *)ap_trampoline_virt->ptr + cr3_off);
	uint32_t *trampoline_entry = (uint32_t *)((uint8_t *)ap_trampoline_virt->ptr + entry_off);
	uint32_t *trampoline_stack = (uint32_t *)((uint8_t *)ap_trampoline_virt->ptr + stack_ptr_off);
	struct idtr_desc *trampoline_idtr = (struct idtr_desc *)((uint8_t *)ap_trampoline_virt->ptr + idt_ptr_off);
	tramp_gdtr_t *tramp_gdtr = (tramp_gdtr_t*)(ap_trampoline_virt->ptr + gdtr_off);

	*trampoline_cr3 = (uint32_t)get_CR3_reg();
	*trampoline_entry = (uint32_t)&ap_start;
	*trampoline_stack = ap_trampoline_phys_base + stack_top_off - 4;
	*trampoline_idtr = bsp_idtr;
	tramp_gdtr->ptr = (uintptr_t)(phys.ptr + gdt_off);

	mprint("AP trampoline copied to %x (size %u)\n", (unsigned)ap_trampoline_phys_base, (unsigned)trampoline_pages);
}

static void cleanup_ap_trampoline(void)
{
	if (ap_trampoline_virt == nullptr)
		return;
	fatptr_t phys = (fatptr_t){
		.ptr = (void*)ap_trampoline_phys_base,
		.len = ap_trampoline_page_count,
	};

	const size_t trampoline_size = (&ap_trampoline_end) - (&ap_trampoline_start);
	const size_t trampoline_pages = round_up_to_page(trampoline_size);

	struct vmm_entry identity_entry = {
		.ptr = (void *)ap_trampoline_phys_base,
		.size = trampoline_pages,
		.flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT,
	};

	unmap_pages(&phys, &identity_entry);

	ap_trampoline_phys_base = 0;
	ap_trampoline_virt = nullptr;
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
	// for each Local APIC ID we do...
	for(size_t i = 0; i < cpu_count; i++) {
		// do not start BSP, that's already running this code
		if (cpus[i].online)
			continue;
		lapic_start_ap(i);
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

	cleanup_ap_trampoline();

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

bool smp_get_ioapic_info(struct madt_ioapic_info *info)
{
	if (!ioapic_found || info == nullptr)
		return false;

	*info = ioapic_info;
	return true;
}

size_t smp_get_irq_overrides(struct madt_irq_override *out, size_t max)
{
	if (out == nullptr || max == 0)
		return 0;

	size_t count = irq_override_count < max ? irq_override_count : max;
	for (size_t i = 0; i < count; i++)
		out[i] = irq_overrides[i];
	return count;
}
