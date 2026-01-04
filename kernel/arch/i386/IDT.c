#include <stdint.h>
#include <kernel/display.h>

#define GATE_TYPE_TASK (0x5)
#define GATE_TYPE_INTERRUPT (0xE)
#define GATE_TYPE_TRAP (0xF)
#define DPL_USER_LEVEL (0b11 << 5)
#define DPL_KERNEL_LEVEL (0b00 << 5)
#define PRESENT (1 << 7)

#define IDT_MAX_DESCRIPTORS 256

MODULE("IDT");

typedef struct {
	uint16_t isr_low;   // The lower 16 bits of the ISR's address
	uint16_t kernel_cs; // The GDT segment selector that the CPU will load into CS before calling the ISR
	uint8_t reserved;   // Set to zero
	uint8_t attributes; // Type and attributes; see the IDT page
	uint16_t isr_high;  // The higher 16 bits of the ISR's address
} __attribute__((packed)) idt_entry_t;

__attribute__((aligned(0x10))) static idt_entry_t idt[256]; // Create an array of IDT entries; aligned for performance

typedef struct {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed)) idtr_t;

static idtr_t idtr;

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags)
{
	idt_entry_t *descriptor = &idt[vector];

	descriptor->isr_low = (uint32_t)isr & 0xFFFF;
	descriptor->kernel_cs = 0x08; // this value can be whatever offset your kernel code selector is in your GDT
	descriptor->attributes = flags;
	descriptor->isr_high = (uint32_t)isr >> 16;
	descriptor->reserved = 0;
}

extern void *isr_stub_table[];

void idt_init(void)
{
	idtr.base = (uint32_t)&idt[0];
	idtr.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_MAX_DESCRIPTORS - 1);

	for (uint16_t vector = 0; vector < (32 + 16); vector++) {
		idt_set_descriptor(vector, (void *)isr_stub_table[vector], PRESENT | DPL_KERNEL_LEVEL | (vector < 20 ? GATE_TYPE_TRAP : GATE_TYPE_INTERRUPT));
	}

	__asm__ volatile("lidt %0;\n"
			 "jmp longjmp_after_gdt;\n"
			 "longjmp_after_gdt:"
			 :
			 : "m"(idtr)); // load the new IDT
	/* __asm__ volatile("sti");       // set the interrupt flag */
}
