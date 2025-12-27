#include <kernel/display.h>
#include <kernel/interrupt.h>

#define IRQ_LINES 16

static irq_handler_t irq_handlers[IRQ_LINES] = { 0 };

static void irq_dispatch(uint8_t irq)
{
	if (irq < IRQ_LINES && irq_handlers[irq]) {
		irq_handlers[irq]();
		return;
	}

	kprintf("Unhandled IRQ: %u\n", irq);
}

bool irq_register_handler(uint8_t irq, irq_handler_t handler)
{
	if (irq >= IRQ_LINES)
		return false;

	irq_handlers[irq] = handler;
	return true;
}

void irq_unregister_handler(uint8_t irq)
{
	if (irq < IRQ_LINES)
		irq_handlers[irq] = 0;
}

#define DEFINE_IRQ_STUB(num)              \
	void isr_##num##_handler(void)    \
	{                                 \
		irq_dispatch((num) - 32); \
	}

DEFINE_IRQ_STUB(32)
DEFINE_IRQ_STUB(33)
DEFINE_IRQ_STUB(34)
DEFINE_IRQ_STUB(35)
DEFINE_IRQ_STUB(36)
DEFINE_IRQ_STUB(37)
DEFINE_IRQ_STUB(38)
DEFINE_IRQ_STUB(39)
DEFINE_IRQ_STUB(40)
DEFINE_IRQ_STUB(41)
DEFINE_IRQ_STUB(42)
DEFINE_IRQ_STUB(43)
DEFINE_IRQ_STUB(44)
DEFINE_IRQ_STUB(45)
DEFINE_IRQ_STUB(46)
DEFINE_IRQ_STUB(47)
