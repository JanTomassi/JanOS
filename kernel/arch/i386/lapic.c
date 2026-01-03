#include "lapic.h"
#include <kernel/display.h>
#include <kernel/vir_mem.h>
#include <kernel/interrupt.h>
#include <stddef.h>
#include <stdbool.h>

extern void PIC_remap(int offset1, int offset2);
extern void pic_disable(void);

#define LAPIC_MMIO_BASE 0xFEE00000
#define LAPIC_REG_ID 0x20
#define LAPIC_REG_EOI 0xB0
#define LAPIC_REG_SVR 0xF0
#define LAPIC_REG_ERR_STAT 0x280
#define LAPIC_REG_ICR_LOW 0x300
#define LAPIC_REG_ICR_HIGH 0x310

#define LAPIC_SVR_ENABLE (1 << 8)

MODULE("LAPIC");

static volatile uint32_t *lapic_base = nullptr;

static inline uint32_t lapic_read(uint32_t reg)
{
	return *((volatile uint32_t*)((size_t)lapic_base + reg)) = (*((volatile uint32_t*)((size_t)lapic_base + reg)));
}

static inline void lapic_write(uint32_t reg, uint32_t val, uint32_t mask)
{
	*((volatile uint32_t*)((size_t)lapic_base + reg)) = (*((volatile uint32_t*)((size_t)lapic_base + reg)) & ~mask) | val;
}

static void lapic_map_base(void)
{
	if (lapic_base != nullptr)
		return;

	fatptr_t phys = {
		.ptr = (void *)LAPIC_MMIO_BASE,
		.len = PAGE_SIZE,
	};
	struct vmm_entry virt = {
		.ptr = (void *)LAPIC_MMIO_BASE,
		.size = PAGE_SIZE,
		.flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT,
	};

	map_pages(&phys, &virt);
	lapic_base = (uint32_t *)virt.ptr;
}

void lapic_enable(void)
{
	lapic_map_base();

	PIC_remap(0x20, 0x28);
	pic_disable();

	uint32_t svr = 0xFF | LAPIC_SVR_ENABLE;
	lapic_write(LAPIC_REG_SVR, svr, (~0));
	mprint("LAPIC enabled with SVR=0x%x\n", svr);
}

void lapic_eoi(void)
{
	if (lapic_base == nullptr)
		return;

	lapic_write(LAPIC_REG_EOI, 0, (~0));
}

void lapic_send_ipi(uint8_t apic_id, uint8_t vector, uint8_t delivery_mode)
{
	lapic_map_base();

	const uint32_t dest = ((uint32_t)apic_id) << 24;
	lapic_write(LAPIC_REG_ICR_HIGH, dest, 0x0f000000);

	uint32_t icr_low = (delivery_mode << 8) | vector;
	lapic_write(LAPIC_REG_ICR_LOW, icr_low, (~0));

	lapic_wait_delivery();
}

uint8_t lapic_get_id(void)
{
	lapic_map_base();
	return (uint8_t)(lapic_read(LAPIC_REG_ID) >> 24);
}

void lapic_start_ap(char cpu_idx){
	lapic_clear_error();

	lapic_set_icr_cpu(cpu_idx);
	lapic_trigger_init();

	lapic_wait_delivery();

	lapic_set_icr_cpu(cpu_idx);
	lapic_trigger_deassert();

	lapic_wait_delivery();
	for (volatile int wait = 0; wait < 100000; wait++) ; // wait 10 msec
	for(size_t j = 0; j < 2; j++) {
		lapic_clear_error();
		lapic_set_icr_cpu(cpu_idx);
		lapic_trigger_startup(); // trigger STARTUP IPI for 0100:0000
		for (volatile int wait = 0; wait < 100000; wait++) ; // wait 200 usec
		lapic_wait_delivery();
	}
}

void lapic_clear_error(void){
	lapic_map_base();
	lapic_write(LAPIC_REG_ERR_STAT, 0, (~0));
}

void lapic_set_icr_cpu(char cpu_idx){
	lapic_map_base();
	lapic_write(LAPIC_REG_ICR_HIGH, cpu_idx << 24, ~0);
}

void lapic_trigger_init(){
	lapic_map_base();
	lapic_write(LAPIC_REG_ICR_LOW, 0x00C500, ~0xfff00000);
}
void lapic_trigger_deassert(){
	lapic_map_base();
	lapic_write(LAPIC_REG_ICR_LOW, 0x008500, ~0xfff00000);
}
void lapic_trigger_startup(){
	lapic_map_base();
	lapic_write(LAPIC_REG_ICR_LOW, 0x000601, ~0xfff0f800);
}

inline void lapic_wait_delivery(){
	lapic_map_base();
	do {
		__asm__ __volatile__ ("pause" : : : "memory");
	}while(lapic_read(LAPIC_REG_ICR_LOW) & (1 << 12));
}
