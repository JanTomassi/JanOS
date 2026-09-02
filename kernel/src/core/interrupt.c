#include <kernel/interrupt.h>

#include <kernel/display.h>
#include <kernel/process/process.h>
#include <kernel/vir_mem.h>

#include <arch/i386/ioapic.h>
#include <arch/i386/pic.h>
#include <arch/i386/cpuid.h>
#include <arch/i386/lapic.h>

#define IRQ_LINE_COUNT 16

static void halt_after_user_fault(struct i386_trap_frame *frame)
{
	struct process *process = process_current();
	if (process != nullptr && (frame->cs & 3u) == 3u) {
		uint32_t cr2 = 0;
		if (frame->vector == INTN_PAGE_FAULT)
			__asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
		kprintf("User fault: vector=%u EIP=%x CS=%x error=%x CR2=%x\n",
			frame->vector, frame->eip, frame->cs, frame->error_code, cr2);
		process_exit_current(128 + (int)frame->vector);
		__asm__ volatile("cli");
		for (;;)
			__asm__ volatile("hlt");
	}
	panic("Unhandled exception %u: EIP=%x CS=%x error=%x\n",
		frame->vector, frame->eip, frame->cs, frame->error_code);
}

void exception_dispatch(struct i386_trap_frame *frame)
{
	if (frame == nullptr)
		panic("Exception entry supplied a null frame\n");

	if (frame->vector == INTN_PAGE_FAULT && (frame->cs & 3u) == 0u) {
		uint32_t cr2;
		__asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
		panic("Kernel page fault: CR2=%x error=%x EIP=%x CS=%x\n",
			cr2, frame->error_code, frame->eip, frame->cs);
	}
	halt_after_user_fault(frame);
}

static bool irq_shared[IRQ_LINE_COUNT] = { false };

static uint8_t irq_vector_to_line(uint8_t vector)
{
	return vector >= IRQ_1 ? (uint8_t)(vector - IRQ_1) : vector;
}

static bool irq_line_is_level_triggered(uint8_t line)
{
	if (!cpuid_has_apic())
		return false;

	return ioapic_irq_is_level(line);
}

static bool irq_line_is_shared(uint8_t line)
{
	if (line >= IRQ_LINE_COUNT)
		return false;

	return irq_shared[line];
}

void irq_set_shared(uint8_t irq, bool shared)
{
	uint8_t line = irq_vector_to_line(irq);
	if (line >= IRQ_LINE_COUNT)
		return;

	irq_shared[line] = shared;
}

void irq_prepare(uint8_t irq)
{
	uint8_t line = irq_vector_to_line(irq);
	if (!cpuid_has_apic() || line >= IRQ_LINE_COUNT)
		return;

	if (irq_line_is_level_triggered(line) && !irq_line_is_shared(line))
		ioapic_mask_irq(line);
}

void irq_mask(uint8_t irq)
{
	uint8_t line = irq_vector_to_line(irq);
	if (cpuid_has_apic())
		ioapic_mask_irq(line);
	else
		pic_mask_irq(line);
}

void irq_unmask(uint8_t irq)
{
	uint8_t line = irq_vector_to_line(irq);
	if (cpuid_has_apic())
		ioapic_unmask_irq(line);
	else
		pic_unmask_irq(line);
}

void irq_ack(uint8_t irq)
{
	uint8_t line = irq_vector_to_line(irq);
	bool apic = cpuid_has_apic();
	bool level = irq_line_is_level_triggered(line);
	bool shared = irq_line_is_shared(line);

	if (apic) {
		lapic_eoi();

		if (line < IRQ_LINE_COUNT && level && !shared)
			ioapic_unmask_irq(line);
	} else {
		PIC_sendEOI(line);
	}
}
