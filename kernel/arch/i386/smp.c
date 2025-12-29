#include <string.h>
#include <kernel/acpi.h>
#include <kernel/allocator.h>
#include <kernel/display.h>
#include <kernel/elf32.h>
#include <kernel/mmio.h>
#include <kernel/phy_mem.h>
#include <kernel/smp.h>
#include <kernel/vir_mem.h>

#include <stddef.h>
#include "./control_register.h"
#include "./lapic.h"
#include "./port.h"

#include <kernel/interrupt.h>

extern void pic_disable(void);

extern void idt_init(void);
extern uint32_t initial_page_dir[1024];
extern size_t HIGHER_HALF;

extern uint8_t ap_trampoline[];
extern uint8_t ap_trampoline_end[];
extern uint8_t ap_trampoline_data[];
extern uint32_t ap_trampoline_size;
extern uint32_t ap_trampoline_data_offset;

MODULE("SMP");

struct smp_trampoline_data {
	uint32_t page_directory;
	uint32_t stack_top;
	uint32_t c_entry;
};

static struct smp_enumeration_result smp_state;
static volatile uint32_t ap_ready[SMP_MAX_CPUS] = { 0 };
static uintptr_t smp_trampoline_base = 0;

static uint32_t virt_to_phys(uint32_t virt)
{
	return virt - (uint32_t)(size_t)&HIGHER_HALF;
}

void smp_ap_entry(void);

struct smp_elf_section {
	const void *addr;
	size_t size;
	size_t name_offset;
};

static bool smp_find_elf_section(const struct multiboot_tag_elf_sections *elf_tag, const char *wanted,
				 struct smp_elf_section *out)
{
	if (!elf_tag || !wanted || !out)
		return false;

	const Elf32_Shdr *sections = (const Elf32_Shdr *)elf_tag->sections;
	const char *section_names = (const char *)(sections[elf_tag->shndx].sh_addr);

	for (size_t i = 0; i < elf_tag->num; i++) {
		const char *name = &section_names[sections[i].sh_name];
		if (strcmp(name, wanted) == 0) {
			*out = (struct smp_elf_section){
				.addr = (const void *)(size_t)sections[i].sh_addr,
				.size = sections[i].sh_size,
				.name_offset = sections[i].sh_name,
			};
			return true;
		}
	}

	return false;
}

static size_t smp_find_cpu_index(uint8_t lapic_id)
{
	for (size_t i = 0; i < smp_state.cpu_count; i++)
		if (smp_state.cpus[i].lapic_id == lapic_id)
			return (size_t)i;
	return -1;
}

static void smp_add_cpu(uint8_t lapic_id, bool bsp, bool present)
{
	if (smp_find_cpu_index(lapic_id) >= 0)
		return;

	if (smp_state.cpu_count >= SMP_MAX_CPUS) {
		mprint("SMP: dropping CPU %u, max supported reached\n", lapic_id);
		return;
	}
	smp_state.cpus[smp_state.cpu_count++] = (struct smp_cpu){
		.lapic_id = lapic_id,
		.bsp = bsp,
		.present = present,
		.started = bsp,
	};
}

static void smp_parse_madt(const struct acpi_madt *madt, uint8_t bsp_id)
{
	if (!madt)
		return;

	const uint8_t *cur = madt->entries;
	const uint8_t *end = (const uint8_t *)madt + madt->header.length;

	smp_state.lapic_address = madt->lapic_address;
	smp_state.lapic_overridden = false;

	while (cur < end) {
		const uint8_t entry_type = cur[0];
		const uint8_t entry_len = cur[1];

		if (!entry_len)
			break;

		switch (entry_type) {
		case ACPI_MADT_LAPIC: {
			const struct acpi_madt_lapic_entry *lapic = (const struct acpi_madt_lapic_entry *)cur;
			const bool enabled = lapic->flags & 0x1;
			if (enabled)
				smp_add_cpu(lapic->apic_id, lapic->apic_id == bsp_id, true);
		} break;
		case ACPI_MADT_X2APIC: {
			const struct acpi_madt_x2apic_entry *x2 = (const struct acpi_madt_x2apic_entry *)cur;
			const bool enabled = x2->flags & 0x1;
			if (enabled)
				smp_add_cpu((uint8_t)x2->x2apic_id, x2->x2apic_id == bsp_id, true);
		} break;
		case ACPI_MADT_LAPIC_OVERRIDE: {
			const struct acpi_madt_lapic_override_entry *override = (const struct acpi_madt_lapic_override_entry *)cur;
			smp_state.lapic_address = (uint32_t)override->lapic_address;
			smp_state.lapic_overridden = true;
		} break;
		case ACPI_MADT_IOAPIC:
		case ACPI_MADT_INT_OVERRIDE:
		case ACPI_MADT_LAPIC_NMI:
		case ACPI_MADT_NMI_SOURCE:
		default:
			break;
		}

		cur += entry_len;
	}
}

static void smp_enumerate(const struct acpi_madt *madt)
{
	if (madt) {
		smp_state.lapic_address = madt->lapic_address;
		smp_state.lapic_overridden = false;
	}

	if (!smp_state.lapic_address)
		smp_state.lapic_address = 0xFEE00000;

	lapic_set_base(smp_state.lapic_address);
	lapic_enable();

	uint8_t bsp_id = lapic_get_id();
	smp_add_cpu(bsp_id, true, true);

	smp_parse_madt(madt, bsp_id);

	if (smp_state.lapic_overridden)
		lapic_set_base(smp_state.lapic_address);
}

static bool smp_copy_trampoline_from_elf(const struct multiboot_tag_elf_sections *elf_tag)
{
	struct smp_elf_section tramp_section;
	if (!smp_find_elf_section(elf_tag, ".ap_trampoline", &tramp_section)) {
		mprint(".ap_trampoline section not found in ELF headers\n");
		return false;
	}

	const uint32_t tramp_offset = (long)tramp_section.addr & (PAGE_SIZE-1);
	const uint32_t tramp_size = round_up_to_page(tramp_section.size);
	const char *tramp_sec_vm = mmio_map((uintptr_t)tramp_section.addr, tramp_section.size);

	fatptr_t tramp_mem = phy_mem_alloc_range(tramp_size, 0x100000);
	if (!tramp_mem.ptr) {
		mprint("failed to allocate low memory for trampoline\n");
		return false;
	}

	struct vmm_entry *tramp_vir_mem = vir_mem_alloc(tramp_mem.len, VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT);
	if (!tramp_vir_mem->ptr) {
		mprint("failed to allocate virtual memory for trampoline\n");
		return false;
	}

	map_page(tramp_mem.ptr, tramp_vir_mem->ptr, tramp_vir_mem->flags);
	memcpy(tramp_vir_mem->ptr + tramp_offset, tramp_sec_vm, tramp_section.size);
	smp_trampoline_base = (uintptr_t)(tramp_vir_mem->ptr + tramp_offset);

	return true;
}

static void smp_write_trampoline_data(uint32_t stack_top, uint32_t entry)
{
	const uint32_t data_off = ap_trampoline_data_offset;
	struct smp_trampoline_data *data = (struct smp_trampoline_data *)(smp_trampoline_base + data_off);

	data->page_directory = virt_to_phys((uint32_t)(size_t)initial_page_dir);
	data->stack_top = stack_top;
	data->c_entry = entry;
}

static void smp_delay(void)
{
	for (volatile size_t i = 0; i < 100000; i++)
		io_wait();
}

static bool smp_boot_cpu(struct smp_cpu *cpu, uint32_t stack_top)
{
	if (!cpu->present || cpu->bsp)
		return false;

	smp_write_trampoline_data(stack_top, (uint32_t)(size_t)&smp_ap_entry);

	const uint8_t vector = (smp_trampoline_base >> 12) & 0xFF;

	lapic_send_init(cpu->lapic_id);
	smp_delay();
	lapic_send_sipi(cpu->lapic_id, vector);
	smp_delay();
	lapic_send_sipi(cpu->lapic_id, vector);

	const size_t idx = smp_find_cpu_index(cpu->lapic_id);
	if (idx < 0)
		return false;

	for (size_t i = 0; i < 1000000; i++) {
		if (ap_ready[idx])
			break;
		smp_delay();
	}

	cpu->started = ap_ready[idx];
	return cpu->started;
}

static void smp_boot_aps(void)
{
	for (size_t i = 0; i < smp_state.cpu_count; i++) {
		struct smp_cpu *cpu = &smp_state.cpus[i];
		if (cpu->bsp || !cpu->present)
			continue;

		fatptr_t stack = mem_gpa_alloc(4096 * 2);
		uint32_t stack_top = (uint32_t)(size_t)stack.ptr + stack.len - 16;

		if (!smp_boot_cpu(cpu, stack_top)) {
			mprint("SMP: failed to start CPU %u\n", cpu->lapic_id);
		}
		else{
			mprint("SMP: CPU %u online\n", cpu->lapic_id);
		}
	}
}

void smp_init(const struct multiboot_tag_old_acpi *old_rsdp, const struct multiboot_tag_new_acpi *new_rsdp,
	      const struct multiboot_tag_elf_sections *elf_tag)
{
	memset(&smp_state, 0, sizeof(smp_state));
	memset((void *)ap_ready, 0, sizeof(ap_ready));

	const struct acpi_rsdp_descriptor *rsdp = acpi_find_rsdp(old_rsdp, new_rsdp);
	if (!rsdp) {
		mprint("SMP: no ACPI RSDP found, SMP disabled\n");
		return;
	}

	const struct acpi_madt *madt = acpi_find_madt(rsdp);
	if (!madt) {
		mprint("SMP: MADT missing, legacy MP tables unsupported\n");
		return;
	}

	if (!smp_copy_trampoline_from_elf(elf_tag)) {
		mprint("SMP: failed to copy AP trampoline, SMP disabled\n");
		return;
	}
	smp_enumerate(madt);
	pic_disable();

	mprint("SMP: found %u CPUs (requested BSP id %u)\n", smp_state.cpu_count, smp_state.cpus[0].lapic_id);
	smp_boot_aps();
}

void smp_ap_entry(void)
{
	/* Match BSP state set in boot.s */
	set_CR0_reg(CR0_REG_EM, false);
	set_CR0_reg(CR0_REG_MP, true);
	set_CR4_reg(CR4_REG_OSFXSR, true);
	set_CR4_reg(CR4_REG_OSXMMEXCPT, true);
	set_CR4_reg(CR4_REG_PSE, true);

	lapic_enable();
	idt_init();

	const uint8_t id = lapic_get_id();
	const size_t idx = smp_find_cpu_index(id);
	if (idx >= 0)
		ap_ready[idx] = 1;

	for (;;)
		__asm__ volatile("hlt");
}
