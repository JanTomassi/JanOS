#include <kernel/interrupt.h>

#include "../arch/i386/ioapic.h"
#include "../arch/i386/pic.h"
#include "../arch/i386/cpuid.h"
#include "../arch/i386/lapic.h"

#define IRQ_LINE_COUNT 16

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
