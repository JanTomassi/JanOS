#include "ioapic.h"

#include <kernel/display.h>
#include <kernel/vir_mem.h>
#include "lapic.h"

MODULE("IOAPIC");

#define IOAPIC_REG_IOAPICID 0x00
#define IOAPIC_REG_IOAPICVER 0x01
#define IOAPIC_REG_IOREDTBL 0x10

#define IOAPIC_REDIR_DEST_SHIFT 56

#define IOAPIC_REDIR_FLAG_MASK (1u << 16)
#define IOAPIC_REDIR_FLAG_TRIGGER_LEVEL (1u << 15)
#define IOAPIC_REDIR_FLAG_POLARITY_LOW (1u << 13)

struct ioapic_state {
	volatile uint32_t *base;
	uint32_t gsi_base;
	uint8_t dest_apic_id;
	struct ioapic_override overrides[16];
	size_t override_count;
};

static struct ioapic_state ioapic = { 0 };

static void ioapic_map(uintptr_t phys_addr)
{
	if (ioapic.base != nullptr)
		return;

	fatptr_t phys = {
		.ptr = (void *)phys_addr,
		.len = PAGE_SIZE,
	};
	struct vmm_entry virt = {
		.ptr = (void *)phys_addr,
		.size = PAGE_SIZE,
		.flags = VMM_ENTRY_PRESENT_BIT | VMM_ENTRY_READ_WRITE_BIT | VMM_ENTRY_CACHE_DISABLE_BIT,
	};

	map_pages(&phys, &virt);
	ioapic.base = virt.ptr;
}

static inline void ioapic_write_reg(uint8_t reg, uint32_t val)
{
	ioapic.base[0] = reg;
	ioapic.base[4] = val;
}

static inline uint32_t ioapic_read_reg(uint8_t reg)
{
	ioapic.base[0] = reg;
	return ioapic.base[4];
}

static void ioapic_write_redir(uint8_t index, uint64_t value)
{
	ioapic_write_reg(IOAPIC_REG_IOREDTBL + index * 2, (uint32_t)value);
	ioapic_write_reg(IOAPIC_REG_IOREDTBL + index * 2 + 1, (uint32_t)(value >> 32));
}

static uint64_t ioapic_read_redir(uint8_t index)
{
	uint32_t low = ioapic_read_reg(IOAPIC_REG_IOREDTBL + index * 2);
	uint32_t high = ioapic_read_reg(IOAPIC_REG_IOREDTBL + index * 2 + 1);
	return ((uint64_t)high << 32) | low;
}

static const struct ioapic_override *find_override(uint8_t source)
{
	for (size_t i = 0; i < ioapic.override_count; i++) {
		if (ioapic.overrides[i].source == source)
			return &ioapic.overrides[i];
	}
	return nullptr;
}

static uint64_t make_redir_entry(uint8_t vector, const struct ioapic_override *override)
{
	uint64_t entry = vector;
	uint16_t flags = override ? override->flags : 0;

	// polarity: bits 0-1 (conform/active high/active low)
	if ((flags & 0x3) == 0x3)
		entry |= IOAPIC_REDIR_FLAG_POLARITY_LOW;
	// trigger mode: bits 2-3 (conform/edge/level)
	if ((flags & 0xC) == 0xC)
		entry |= IOAPIC_REDIR_FLAG_TRIGGER_LEVEL;

	entry |= ((uint64_t)ioapic.dest_apic_id) << IOAPIC_REDIR_DEST_SHIFT;

	return entry;
}

void ioapic_init(uintptr_t phys_addr, uint32_t gsi_base, uint8_t apic_id)
{
	ioapic_map(phys_addr);

	ioapic.gsi_base = gsi_base;
	ioapic.dest_apic_id = apic_id;

	uint32_t ver = ioapic_read_reg(IOAPIC_REG_IOAPICVER);
	uint8_t max_redirs = (ver >> 16) & 0xFF;

	mprint("IOAPIC id=%u gsi_base=%u max_redirs=%u\n", ioapic_read_reg(IOAPIC_REG_IOAPICID) >> 24, gsi_base, max_redirs);

	for (uint8_t i = 0; i <= max_redirs; i++) {
		uint64_t entry = ioapic_read_redir(i);
		entry |= IOAPIC_REDIR_FLAG_MASK;
		ioapic_write_redir(i, entry);
	}
}

void ioapic_register_overrides(const struct ioapic_override *overrides, size_t count)
{
	if (count > sizeof(ioapic.overrides) / sizeof(ioapic.overrides[0]))
		count = sizeof(ioapic.overrides) / sizeof(ioapic.overrides[0]);

	for (size_t i = 0; i < count; i++)
		ioapic.overrides[i] = overrides[i];
	ioapic.override_count = count;
}

void ioapic_configure_legacy_irqs(void)
{
	for (uint8_t irq = 0; irq < 16; irq++) {
		const struct ioapic_override *override = find_override(irq);
		uint32_t gsi = override ? override->gsi : (ioapic.gsi_base + irq);
		uint8_t index = (uint8_t)(gsi - ioapic.gsi_base);

		uint64_t entry = make_redir_entry((uint8_t)(32 + irq), override);
		entry |= IOAPIC_REDIR_FLAG_MASK;
		ioapic_write_redir(index, entry);
	}
}

static void set_mask(uint8_t irq, bool masked)
{
	const struct ioapic_override *override = find_override(irq);
	uint32_t gsi = override ? override->gsi : (ioapic.gsi_base + irq);
	uint8_t index = (uint8_t)(gsi - ioapic.gsi_base);

	uint64_t entry = ioapic_read_redir(index);
	if (masked)
		entry |= IOAPIC_REDIR_FLAG_MASK;
	else
		entry &= ~IOAPIC_REDIR_FLAG_MASK;

	ioapic_write_redir(index, entry);
}

void ioapic_mask_irq(uint8_t irq)
{
	set_mask(irq, true);
}

void ioapic_unmask_irq(uint8_t irq)
{
	set_mask(irq, false);
}
