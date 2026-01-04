#include <kernel/interrupt.h>

#include "../arch/i386/ioapic.h"
#include "../arch/i386/pic.h"
#include "../arch/i386/cpuid.h"
#include "../arch/i386/lapic.h"

void irq_mask(uint8_t irq)
{
	if (cpuid_has_apic())
		ioapic_mask_irq(irq);
	else
		pic_mask_irq(irq);
}

void irq_unmask(uint8_t irq)
{
	if (cpuid_has_apic())
		ioapic_unmask_irq(irq);
	else
		pic_unmask_irq(irq);
}

void irq_ack(uint8_t irq)
{
	if (cpuid_has_apic())
		lapic_eoi();
	else
		PIC_sendEOI(irq);
}
