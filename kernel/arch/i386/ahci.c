#include "ahci.h"

#include <kernel/display.h>
#include <kernel/interrupt.h>
#include <string.h>

#include "dma.h"
#include "irq.h"
#include "mmio.h"
#include "pci.h"

MODULE("AHCI");

#define AHCI_GHC_HR (1u << 0)
#define AHCI_GHC_IE (1u << 1)
#define AHCI_GHC_AE (1u << 31)

#define AHCI_PORT_CMD_ST (1u << 0)
#define AHCI_PORT_CMD_FRE (1u << 4)
#define AHCI_PORT_CMD_FR (1u << 14)
#define AHCI_PORT_CMD_CR (1u << 15)

#define AHCI_PORT_TFD_ERR (1u << 0)
#define AHCI_PORT_TFD_DRQ (1u << 3)
#define AHCI_PORT_TFD_BSY (1u << 7)

#define AHCI_PORT_SSTS_DET_MASK 0x0F
#define AHCI_PORT_SSTS_DET_PRESENT 0x03
#define AHCI_PORT_SSTS_IPM_MASK 0x0F00
#define AHCI_PORT_SSTS_IPM_ACTIVE 0x0100

#define AHCI_PORT_IRQ_TFES (1u << 30)

#define AHCI_PRDT_MAX_BYTES (4u * 1024u * 1024u)
#define AHCI_MMIO_WINDOW (0x2000u)

struct ahci_port_state {
	bool active;
	uint8_t port_index;
	volatile struct hba_port *port;
	struct dma_buffer cmd_list;
	struct dma_buffer fis;
	struct dma_buffer cmd_tables[AHCI_CMD_SLOT_COUNT];
	volatile bool irq_fired;
};

static struct {
	bool present;
	struct pci_device pci;
	struct mmio_region mmio;
	volatile struct hba_mem *hba;
	struct ahci_port_state ports[AHCI_MAX_PORTS];
	uint8_t irq_line;
} ahci_state;

static void ahci_port_stop(volatile struct hba_port *port)
{
	port->cmd &= ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);
	while (port->cmd & (AHCI_PORT_CMD_FR | AHCI_PORT_CMD_CR))
		;
}

static void ahci_port_start(volatile struct hba_port *port)
{
	while (port->cmd & AHCI_PORT_CMD_CR)
		;
	port->cmd |= AHCI_PORT_CMD_FRE;
	port->cmd |= AHCI_PORT_CMD_ST;
}

static bool ahci_port_present(volatile struct hba_port *port)
{
	uint32_t ssts = port->ssts;
	uint32_t det = ssts & AHCI_PORT_SSTS_DET_MASK;
	uint32_t ipm = ssts & AHCI_PORT_SSTS_IPM_MASK;
	return det == AHCI_PORT_SSTS_DET_PRESENT && ipm == AHCI_PORT_SSTS_IPM_ACTIVE;
}

static int ahci_find_free_slot(volatile struct hba_port *port)
{
	uint32_t slots = port->sact | port->ci;
	for (int i = 0; i < AHCI_CMD_SLOT_COUNT; i++) {
		if ((slots & (1u << i)) == 0)
			return i;
	}
	return -1;
}

static bool ahci_port_rebase(struct ahci_port_state *state)
{
	volatile struct hba_port *port = state->port;

	ahci_port_stop(port);

	state->cmd_list = dma_alloc(1024);
	state->fis = dma_alloc(256);
	if (state->cmd_list.virt == nullptr || state->fis.virt == nullptr)
		return false;

	memset(state->cmd_list.virt, 0, 1024);
	memset(state->fis.virt, 0, 256);

	port->clb = (uint32_t)(uintptr_t)state->cmd_list.phys.ptr;
	port->clbu = 0;
	port->fb = (uint32_t)(uintptr_t)state->fis.phys.ptr;
	port->fbu = 0;

	struct hba_cmd_header *cmd_header = (struct hba_cmd_header *)state->cmd_list.virt;
	for (int i = 0; i < AHCI_CMD_SLOT_COUNT; i++) {
		state->cmd_tables[i] = dma_alloc(4096);
		if (state->cmd_tables[i].virt == nullptr)
			return false;

		memset(state->cmd_tables[i].virt, 0, 4096);
		cmd_header[i].cfl = sizeof(struct fis_reg_h2d) / 4;
		cmd_header[i].prdtl = 0;
		cmd_header[i].ctba = (uint32_t)(uintptr_t)state->cmd_tables[i].phys.ptr;
		cmd_header[i].ctbau = 0;
	}

	port->is = 0xFFFFFFFFu;
	port->ie = 0xFFFFFFFFu;

	ahci_port_start(port);
	return true;
}

static void ahci_irq_handler(uint8_t irq_line, void *context)
{
	(void)irq_line;
	struct hba_mem *hba = context;
	if (hba == nullptr)
		return;

	uint32_t is = hba->is;
	if (is == 0)
		return;

	for (uint8_t i = 0; i < AHCI_MAX_PORTS; i++) {
		if ((is & (1u << i)) == 0)
			continue;

		struct ahci_port_state *state = &ahci_state.ports[i];
		if (!state->active)
			continue;

		volatile struct hba_port *port = state->port;
		port->is = port->is;
		state->irq_fired = true;
	}

	hba->is = is;
}

bool ahci_probe(void)
{
	struct pci_device dev = { 0 };
	if (!pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA, PCI_PROG_IF_AHCI, &dev))
		return false;

	ahci_state.present = true;
	ahci_state.pci = dev;
	ahci_state.irq_line = dev.irq_line;
	return true;
}

void ahci_init(void)
{
	if (!ahci_state.present)
		return;

	uint32_t bar5 = ahci_state.pci.bar[5] & ~0xFu;
	if (bar5 == 0)
		return;

	pci_enable_bus_mastering(&ahci_state.pci);

	ahci_state.mmio = mmio_map(bar5, AHCI_MMIO_WINDOW);
	if (ahci_state.mmio.virt == nullptr)
		return;

	ahci_state.hba = (volatile struct hba_mem *)ahci_state.mmio.virt;

	ahci_state.hba->ghc |= AHCI_GHC_HR;
	while (ahci_state.hba->ghc & AHCI_GHC_HR)
		;

	ahci_state.hba->ghc |= AHCI_GHC_AE;
	ahci_state.hba->ghc |= AHCI_GHC_IE;
	ahci_state.hba->is = 0xFFFFFFFFu;

	uint32_t implemented = ahci_state.hba->pi;
	for (uint8_t i = 0; i < AHCI_MAX_PORTS; i++) {
		if ((implemented & (1u << i)) == 0)
			continue;

		volatile struct hba_port *port = &ahci_state.hba->ports[i];
		if (!ahci_port_present(port))
			continue;

		struct ahci_port_state *state = &ahci_state.ports[i];
		*state = (struct ahci_port_state){
			.active = true,
			.port_index = i,
			.port = port,
			.irq_fired = false,
		};

		if (!ahci_port_rebase(state)) {
			state->active = false;
			continue;
		}

		mprint("Port %u active\n", i);
	}

	if (ahci_state.irq_line < 16)
		irq_register_handler(ahci_state.irq_line, ahci_irq_handler, (void *)ahci_state.hba);
}

static struct ahci_port_state *ahci_first_port(void)
{
	for (uint8_t i = 0; i < AHCI_MAX_PORTS; i++) {
		if (ahci_state.ports[i].active)
			return &ahci_state.ports[i];
	}
	return nullptr;
}

static bool ahci_exec_dma(uint32_t lba_addr, uint16_t sector_count, void *buffer, bool write)
{
	struct ahci_port_state *state = ahci_first_port();
	if (state == nullptr || sector_count == 0)
		return false;

	volatile struct hba_port *port = state->port;
	while (port->tfd & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ))
		;

	int slot = ahci_find_free_slot(port);
	if (slot < 0)
		return false;

	size_t byte_count = (size_t)sector_count * 512u;
	struct dma_buffer data = dma_alloc(byte_count);
	if (data.virt == nullptr)
		return false;

	if (write)
		memcpy(data.virt, buffer, byte_count);

	uint32_t prdt_count = (uint32_t)((byte_count + AHCI_PRDT_MAX_BYTES - 1) / AHCI_PRDT_MAX_BYTES);
	struct hba_cmd_header *cmd_header = (struct hba_cmd_header *)state->cmd_list.virt;
	struct hba_cmd_header *header = &cmd_header[slot];
	header->cfl = sizeof(struct fis_reg_h2d) / 4;
	header->w = write ? 1 : 0;
	header->prdtl = prdt_count;
	header->prdbc = 0;

	struct hba_cmd_table *cmd_table = (struct hba_cmd_table *)state->cmd_tables[slot].virt;
	memset(cmd_table, 0, 4096);

	uintptr_t data_phys = (uintptr_t)data.phys.ptr;
	size_t remaining = byte_count;
	for (uint32_t i = 0; i < prdt_count; i++) {
		uint32_t chunk = remaining > AHCI_PRDT_MAX_BYTES ? AHCI_PRDT_MAX_BYTES : (uint32_t)remaining;
		cmd_table->prdt_entry[i].dba = (uint32_t)data_phys;
		cmd_table->prdt_entry[i].dbau = 0;
		cmd_table->prdt_entry[i].dbc = chunk - 1;
		cmd_table->prdt_entry[i].i = (i == prdt_count - 1) ? 1 : 0;

		data_phys += chunk;
		remaining -= chunk;
	}

	struct fis_reg_h2d *fis = (struct fis_reg_h2d *)cmd_table->cfis;
	*fis = (struct fis_reg_h2d){
		.fis_type = AHCI_FIS_TYPE_REG_H2D,
		.c = 1,
		.command = write ? AHCI_CMD_WRITE_DMA_EXT : AHCI_CMD_READ_DMA_EXT,
		.lba0 = (uint8_t)(lba_addr & 0xFF),
		.lba1 = (uint8_t)((lba_addr >> 8) & 0xFF),
		.lba2 = (uint8_t)((lba_addr >> 16) & 0xFF),
		.lba3 = (uint8_t)((lba_addr >> 24) & 0xFF),
		.device = 1 << 6,
		.countl = (uint8_t)(sector_count & 0xFF),
		.counth = (uint8_t)((sector_count >> 8) & 0xFF),
	};

	port->is = 0xFFFFFFFFu;
	state->irq_fired = false;
	port->ci |= 1u << slot;

	for (size_t spin = 0; spin < 1000000; spin++) {
		if ((port->ci & (1u << slot)) == 0)
			break;
		if (state->irq_fired)
			break;
		if (port->is & AHCI_PORT_IRQ_TFES)
			break;
	}

	bool ok = (port->ci & (1u << slot)) == 0 && (port->is & AHCI_PORT_IRQ_TFES) == 0;

	if (ok && !write)
		memcpy(buffer, data.virt, byte_count);

	dma_free(&data);
	return ok;
}

bool ahci_read28(uint32_t lba_addr, uint16_t sector_count, void *dest)
{
	return ahci_exec_dma(lba_addr, sector_count, dest, false);
}

bool ahci_write28(uint32_t lba_addr, uint16_t sector_count, const void *src)
{
	return ahci_exec_dma(lba_addr, sector_count, (void *)src, true);
}
