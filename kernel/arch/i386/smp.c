#include <kernel/display.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <kernel/vir_mem.h>

#include <string.h>

#include "./cpuid.h"
#include "./lapic.h"
#include "./mmio.h"
#include "./smp_trampoline.h"

extern uint8_t smp_trampoline_start[];
extern uint8_t smp_trampoline_end[];

static struct smp_cpu cpus[SMP_MAX_CPUS];
static volatile bool ap_online[SMP_MAX_CPUS];
static size_t cpu_count = 1;

static uint8_t ap_stacks[SMP_MAX_CPUS][4096] __attribute__((aligned(16)));

struct smp_trampoline_args {
	uint32_t stack_ptr;
	uint32_t entry_point;
	uint32_t page_directory;
};

static fatptr_t trampoline_region;
static fatptr_t trampoline_data_region;

static uint32_t read_cr3(void)
{
	uint32_t value;
	__asm__ volatile("mov %%cr3, %0" : "=r"(value));
	return value;
}

static void smp_delay(void)
{
	for (volatile uint32_t i = 0; i < 100000; i++)
		;
}

static bool smp_map_trampoline(void)
{
	if (trampoline_region.ptr && trampoline_data_region.ptr)
		return true;

	uint16_t flags = VMM_PAGE_FLAG_CACHE_DISABLE_BIT |
			 VMM_PAGE_FLAG_READ_WRITE_BIT |
			 VMM_PAGE_FLAG_PRESENT_BIT;

	trampoline_region = mmio_map_region(
		(fatptr_t){ .ptr = (void *)SMP_TRAMPOLINE_BASE, .len = 4096 },
		flags);
	trampoline_data_region = mmio_map_region(
		(fatptr_t){ .ptr = (void *)SMP_TRAMPOLINE_DATA_BASE,
			    .len = 4096 },
		flags);

	if (!trampoline_region.ptr || !trampoline_data_region.ptr) {
		kprintf("Failed to map SMP trampoline regions.\n");
		return false;
	}

	return true;
}

static void smp_prepare_trampoline(const struct smp_trampoline_args *args)
{
	if (!smp_map_trampoline())
		return;

	size_t tramp_len =
		(size_t)(smp_trampoline_end - smp_trampoline_start);
	memcpy(trampoline_region.ptr, smp_trampoline_start, tramp_len);
	memcpy(trampoline_data_region.ptr, args, sizeof(*args));
}

static void smp_start_cpu(uint32_t apic_id,
			  const struct smp_trampoline_args *args)
{
	smp_prepare_trampoline(args);

	lapic_send_ipi(apic_id, LAPIC_DM_INIT, 0);
	smp_delay();

	uint8_t vector = (uint8_t)(SMP_TRAMPOLINE_BASE >> 12);
	lapic_send_ipi(apic_id, LAPIC_DM_STARTUP, vector);
	smp_delay();
}

void smp_init(void)
{
	struct feature_info info = cpuid_get_feature_info();
	uint32_t logical = cpuid_get_logical_processor_count();

	if (!info.APIC)
		kprintf("APIC not available, falling back to single processor.\n");

	if (logical == 0)
		logical = 1;

	if (logical > SMP_MAX_CPUS)
		logical = SMP_MAX_CPUS;

	for (size_t i = 0; i < logical; i++) {
		cpus[i].id = (uint8_t)i;
		cpus[i].bootstrap = i == 0;
		cpus[i].online = i == 0;
		ap_online[i] = i == 0;
	}

	cpu_count = logical;

	kprintf("SMP init: %u logical processor(s) detected (capped at %u).\n",
		(uint32_t)cpu_count, SMP_MAX_CPUS);
}

void smp_bootstrap_aps(void)
{
	if (cpu_count <= 1) {
		kprintf("SMP: single CPU available, skipping AP startup.\n");
		return;
	}

	if (!lapic_init()) {
		kprintf("SMP: LAPIC init failed, running BSP only.\n");
		return;
	}

	uint32_t cr3 = read_cr3();
	kprintf("SMP: boot page directory at %x\n", cr3);

	for (size_t cpu = 1; cpu < cpu_count; cpu++) {
		struct smp_trampoline_args args = {
			.stack_ptr = (uint32_t)((size_t)ap_stacks[cpu] +
						sizeof(ap_stacks[cpu])),
			.entry_point = (uint32_t)(size_t)smp_ap_entry,
			.page_directory = cr3,
		};

		kprintf("SMP: starting CPU %u via SIPI...\n", (uint32_t)cpu);
		smp_start_cpu(cpu, &args);

		size_t wait_ticks = 0;
		while (!ap_online[cpu] && wait_ticks < 500000) {
			wait_ticks++;
		}

		if (ap_online[cpu]) {
			kprintf("SMP: CPU %u online.\n", (uint32_t)cpu);
		} else {
			kprintf("SMP: CPU %u failed to report online.\n",
				(uint32_t)cpu);
		}
	}
}

size_t smp_get_cpu_count(void)
{
	return cpu_count;
}

const struct smp_cpu *smp_get_cpus(void)
{
	return cpus;
}

void smp_ap_entry(void)
{
	uint32_t apic_id = lapic_get_id();
	size_t idx = apic_id < SMP_MAX_CPUS ? apic_id : 0;

	cpus[idx].id = (uint8_t)apic_id;
	cpus[idx].bootstrap = false;
	cpus[idx].online = true;
	ap_online[idx] = true;

	kprintf("SMP: AP %u reached C entry, parking idle.\n", apic_id);

	while (1)
		__asm__ volatile("hlt");
}
