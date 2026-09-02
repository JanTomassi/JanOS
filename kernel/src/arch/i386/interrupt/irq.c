#include <arch/i386/irq.h>

#include <kernel/interrupt.h>
#include <kernel/spinlock.h>
#include <stddef.h>

#define IRQ_LINE_COUNT 16
#define IRQ_HANDLER_SLOTS 32

struct irq_handler_entry {
	irq_handler_t handler;
	void *context;
	struct irq_handler_entry *next;
	bool in_use;
};

static struct irq_handler_entry irq_handler_pool[IRQ_HANDLER_SLOTS] = { 0 };
static struct irq_handler_entry *irq_handlers[IRQ_LINE_COUNT] = { 0 };
static spinlock_t irq_lock = { 0 };

static struct irq_handler_entry *irq_handler_alloc(void)
{
	for (size_t i = 0; i < IRQ_HANDLER_SLOTS; i++) {
		if (!irq_handler_pool[i].in_use) {
			irq_handler_pool[i].in_use = true;
			irq_handler_pool[i].next = nullptr;
			return &irq_handler_pool[i];
		}
	}

	return nullptr;
}

static void irq_handler_free(struct irq_handler_entry *entry)
{
	if (entry == nullptr)
		return;
	entry->in_use = false;
	entry->handler = nullptr;
	entry->context = nullptr;
	entry->next = nullptr;
}

static void irq_update_shared_state(uint8_t irq_line)
{
	struct irq_handler_entry *head = irq_handlers[irq_line];
	bool shared = head != nullptr && head->next != nullptr;
	irq_set_shared((uint8_t)(IRQ_1 + irq_line), shared);
}

bool irq_register_handler(uint8_t irq_line, irq_handler_t handler, void *context)
{
	if (irq_line >= IRQ_LINE_COUNT || handler == nullptr)
		return false;

	spin_lock(&irq_lock);
	struct irq_handler_entry *head = irq_handlers[irq_line];
	for (struct irq_handler_entry *it = head; it != nullptr; it = it->next) {
		if (it->handler == handler && it->context == context) {
			spin_unlock(&irq_lock);
			return true;
		}
	}

	struct irq_handler_entry *entry = irq_handler_alloc();
	if (entry == nullptr) {
		spin_unlock(&irq_lock);
		return false;
	}

	entry->handler = handler;
	entry->context = context;
	entry->next = nullptr;

	if (head == nullptr) {
		irq_handlers[irq_line] = entry;
	} else {
		while (head->next != nullptr)
			head = head->next;
		head->next = entry;
	}

	irq_update_shared_state(irq_line);
	irq_unmask((uint8_t)(IRQ_1 + irq_line));
	spin_unlock(&irq_lock);
	return true;
}

bool irq_unregister_handler(uint8_t irq_line, irq_handler_t handler, void *context)
{
	if (irq_line >= IRQ_LINE_COUNT || handler == nullptr)
		return false;

	spin_lock(&irq_lock);
	struct irq_handler_entry *prev = nullptr;
	struct irq_handler_entry *cur = irq_handlers[irq_line];

	while (cur != nullptr) {
		if (cur->handler == handler && cur->context == context) {
			if (prev == nullptr)
				irq_handlers[irq_line] = cur->next;
			else
				prev->next = cur->next;

			irq_handler_free(cur);
			irq_update_shared_state(irq_line);
			if (irq_handlers[irq_line] == nullptr)
				irq_mask((uint8_t)(IRQ_1 + irq_line));
			spin_unlock(&irq_lock);
			return true;
		}
		prev = cur;
		cur = cur->next;
	}

	spin_unlock(&irq_lock);
	return false;
}

static void irq_dispatch(uint8_t vector)
{
	if (vector < IRQ_1)
		return;

	uint8_t irq_line = (uint8_t)(vector - IRQ_1);
	if (irq_line >= IRQ_LINE_COUNT)
		return;

	irq_handler_t handlers[IRQ_HANDLER_SLOTS];
	void *contexts[IRQ_HANDLER_SLOTS];
	size_t count = 0;

	spin_lock(&irq_lock);
	for (struct irq_handler_entry *it = irq_handlers[irq_line];
	     it != nullptr && count < IRQ_HANDLER_SLOTS; it = it->next) {
		handlers[count] = it->handler;
		contexts[count] = it->context;
		count++;
	}
	spin_unlock(&irq_lock);

	for (size_t i = 0; i < count; i++)
		handlers[i](irq_line, contexts[i]);
}

#define DEFINE_IRQ_DISPATCH(vector)       \
	void isr_##vector##_handler(void) \
	{                                 \
		irq_dispatch(vector);      \
	}

DEFINE_IRQ_DISPATCH(32)
DEFINE_IRQ_DISPATCH(33)
DEFINE_IRQ_DISPATCH(34)
DEFINE_IRQ_DISPATCH(35)
DEFINE_IRQ_DISPATCH(36)
DEFINE_IRQ_DISPATCH(37)
DEFINE_IRQ_DISPATCH(38)
DEFINE_IRQ_DISPATCH(39)
DEFINE_IRQ_DISPATCH(40)
DEFINE_IRQ_DISPATCH(41)
DEFINE_IRQ_DISPATCH(42)
DEFINE_IRQ_DISPATCH(43)
DEFINE_IRQ_DISPATCH(44)
DEFINE_IRQ_DISPATCH(45)
DEFINE_IRQ_DISPATCH(46)
DEFINE_IRQ_DISPATCH(47)
