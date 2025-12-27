#include <kernel/display.h>
#include <kernel/interrupt.h>
#include <kernel/timer.h>
#include <stdint.h>
#include <string.h>

#include "ahci.h"
#include "ata_pio.h"
#include "dma.h"
#include "pci.h"
#include "pic.h"
#include "port.h"

#define AHCI_PORT_MAX 32
#define AHCI_MAX_CMD_SLOTS 32
#define AHCI_MAX_PRDT 8
#define AHCI_SECTOR_SIZE 512
#define AHCI_IRQ_TIMEOUT_TICKS 200
#define AHCI_MAX_PRDT_BYTES (0x400000u * AHCI_MAX_PRDT)

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

#define HBA_PxCMD_ST (1 << 0)
#define HBA_PxCMD_FRE (1 << 4)
#define HBA_PxCMD_FR (1 << 14)
#define HBA_PxCMD_CR (1 << 15)

#define HBA_PxIS_TFES (1 << 30)

#define HBA_PORT_DET_PRESENT 0x3
#define HBA_PORT_IPM_ACTIVE 0x1

enum fis_type { FIS_TYPE_REG_H2D = 0x27 };

/* Per-port register block defined by AHCI 1.3.1, section 3.3.10. Each field is
 * a 32-bit MMIO register. We treat it as volatile to avoid reordering when
 * touching hardware state.
 *   clb/clbu : 1 KiB command list base (low/high).
 *   fb/fbu   : 256-byte received FIS base (low/high).
 *   is       : interrupt status for this port.
 *   ie       : interrupt enable for this port.
 *   cmd      : command and status (start, FIS enable, power, etc.).
 *   tfd      : task file data (device status/error).
 *   sig      : device signature.
 *   ssts     : SATA status (device detection/interface power management).
 *   sctl     : SATA control (COMRESET, speed, etc.).
 *   serr     : SATA error (PHY/link errors).
 *   sact     : SATA active (NCQ outstanding commands).
 *   ci       : command issue bitmask (non-NCQ).
 *   sntf     : SATA notification.
 *   fbs      : FIS-based switching. */
typedef volatile struct {
	uint32_t clb;
	uint32_t clbu;
	uint32_t fb;
	uint32_t fbu;
	uint32_t is;
	uint32_t ie;
	uint32_t cmd;
	uint32_t rsv0;
	uint32_t tfd;
	uint32_t sig;
	uint32_t ssts;
	uint32_t sctl;
	uint32_t serr;
	uint32_t sact;
	uint32_t ci;
	uint32_t sntf;
	uint32_t fbs;
	uint32_t rsv1[11];
	uint32_t vendor[4];
} hba_port_t;

/* Global HBA register space (HBA memory mapped registers). ports[] is sized to
 * at least 1 but indexed using the port map, so the backing BAR must cover the
 * whole register set.
 *   cap     : host capabilities (ports, command slots, 64-bit, etc.).
 *   ghc     : global host control (AE, interrupts).
 *   is      : global interrupt status (one bit per port).
 *   pi      : port implemented bitmask.
 *   vs      : AHCI version.
 *   ccc_*   : command completion coalescing.
 *   em_*    : enclosure management.
 *   cap2    : extended capabilities.
 *   bohc    : BIOS/OS handoff.
 *   ports   : per-port register blocks. */
typedef volatile struct {
	uint32_t cap;
	uint32_t ghc;
	uint32_t is;
	uint32_t pi;
	uint32_t vs;
	uint32_t ccc_ctl;
	uint32_t ccc_pts;
	uint32_t em_loc;
	uint32_t em_ctl;
	uint32_t cap2;
	uint32_t bohc;
	uint8_t rsv[0xA0 - 0x2C];
	uint8_t vendor[0x100 - 0xA0];
	hba_port_t ports[1];
} hba_mem_t;

/* Command header described in AHCI 1.3.1 section 4.2.2. Packed to match the
 * hardware layout; used by the HBA to interpret command tables.
 *   cfl    : length of CFIS in DWORDs.
 *   w      : write flag (1=host-to-device).
 *   p      : prefetchable.
 *   r/b/c  : reset/bist/clear busy upon R_OK.
 *   pmp    : port multiplier port.
 *   prdtl  : number of PRDT entries.
 *   prdbc  : byte count transferred by HBA.
 *   ctba/u : physical base of the command table. */
typedef struct {
	uint8_t cfl : 5;
	uint8_t a : 1;
	uint8_t w : 1;
	uint8_t p : 1;
	uint8_t r : 1;
	uint8_t b : 1;
	uint8_t c : 1;
	uint8_t rsv0 : 1;
	uint8_t pmp : 4;
	uint16_t prdtl;
	uint32_t prdbc;
	uint32_t ctba;
	uint32_t ctbau;
	uint32_t rsv1[4];
} __attribute__((packed)) hba_cmd_header_t;

/* Physical region descriptor table entry (PRDT) mapping a contiguous buffer for
 * DMA.
 *   dba/dbau : 64-bit physical address of buffer.
 *   dbc      : byte count minus 1 (max 4 MiB).
 *   i        : interrupt on completion of this entry. */
typedef struct {
	uint32_t dba;
	uint32_t dbau;
	uint32_t rsv0;
	uint32_t dbc : 22;
	uint32_t rsv1 : 9;
	uint32_t i : 1;
} __attribute__((packed)) hba_prdt_entry_t;

/* Register host-to-device FIS (section 7.2.2). Packed so the CFIS byte array in
 * the command table maps 1:1 to the hardware layout. The packed bitfields carry
 * flags for command/control and port multiplier port, and the LBA/count bytes
 * hold the 48-bit LBA and sector count. */
typedef struct {
	uint8_t fis_type;
	uint8_t pmport : 4;
	uint8_t rsv0 : 3;
	uint8_t c : 1;
	uint8_t command;
	uint8_t featurel;
	uint8_t lba0;
	uint8_t lba1;
	uint8_t lba2;
	uint8_t device;
	uint8_t lba3;
	uint8_t lba4;
	uint8_t lba5;
	uint8_t featureh;
	uint8_t countl;
	uint8_t counth;
	uint8_t icc;
	uint8_t control;
	uint8_t rsv1[4];
} __attribute__((packed)) fis_reg_h2d_t;

/* Command table (section 4.2.3). Holds the command FIS, ATAPI command (unused
 * here), reserved bytes, and the PRDT list that references DMA buffers. */
typedef struct {
	uint8_t cfis[64];
	uint8_t acmd[16];
	uint8_t rsv[48];
	hba_prdt_entry_t prdt_entry[AHCI_MAX_PRDT];
} __attribute__((packed)) hba_cmd_tbl_t;

/* Runtime state for each probed AHCI port. Tracks hardware registers, IRQ
 * flags, the active command slot, and the DMA-backed buffers for command list,
 * received FIS area, and per-slot command tables.
 *   present      : port is implemented and device present.
 *   port_no      : port index.
 *   port         : pointer to MMIO port registers.
 *   irq_pending  : port signaled an interrupt for the current command.
 *   irq_error    : transport error seen (TFES).
 *   active_slot  : slot currently in use.
 *   cmd_list     : DMA buffer for 1 KiB command list.
 *   fis          : DMA buffer for 256-byte received FIS area.
 *   cmd_tables   : DMA buffers for per-slot command tables. */
struct ahci_port_state {
	bool present;
	uint8_t port_no;
	volatile hba_port_t *port;
	volatile bool irq_pending;
	volatile bool irq_error;
	uint8_t active_slot;
	struct dma_buffer cmd_list;
	struct dma_buffer fis;
	struct dma_buffer cmd_tables[AHCI_MAX_CMD_SLOTS];
};

static struct ahci_controller g_ctrl = { 0 };
static volatile hba_mem_t *g_hba = 0;
static struct ahci_port_state g_ports[AHCI_PORT_MAX] = { 0 };
static uint8_t g_irq_line = 0xFF;
static bool g_irq_registered = false;

static bool is_ahci_class(uint8_t bus, uint8_t slot, uint8_t func)
{
	uint32_t class_reg = pci_config_read_dword(bus, slot, func, 0x08);
	uint8_t class_code = (uint8_t)(class_reg >> 24);
	uint8_t subclass = (uint8_t)(class_reg >> 16);
	return class_code == 0x01 && subclass == 0x06;
}

static bool is_multifunction(uint8_t bus, uint8_t slot)
{
	uint32_t header = pci_config_read_dword(bus, slot, 0, 0x0C);
	return (header >> 16) & 0x80;
}

static bool port_device_present(volatile hba_port_t *port)
{
	uint32_t ssts = port->ssts;
	uint8_t det = ssts & 0x0F;
	uint8_t ipm = (ssts >> 8) & 0x0F;
	return det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE;
}

static int find_cmdslot(volatile hba_port_t *port)
{
	uint32_t slots = port->sact | port->ci;
	for (int i = 0; i < AHCI_MAX_CMD_SLOTS; i++) {
		if ((slots & (1u << i)) == 0)
			return i;
	}
	return -1;
}

static void stop_cmd(volatile hba_port_t *port)
{
	port->cmd &= ~HBA_PxCMD_ST;
	port->cmd &= ~HBA_PxCMD_FRE;
	while (port->cmd & HBA_PxCMD_FR)
		;
	while (port->cmd & HBA_PxCMD_CR)
		;
}

static void start_cmd(volatile hba_port_t *port)
{
	while (port->cmd & HBA_PxCMD_CR)
		;
	port->cmd |= HBA_PxCMD_FRE;
	port->cmd |= HBA_PxCMD_ST;
}

static bool wait_ready(volatile hba_port_t *port)
{
	size_t guard = 100000;
	while ((port->tfd & (ATA_SR_BSY | ATA_SR_DRQ)) && --guard)
		;
	return guard != 0;
}

static void ahci_free_port_buffers(struct ahci_port_state *state)
{
	dma_free(&state->cmd_list);
	dma_free(&state->fis);
	for (int i = 0; i < AHCI_MAX_CMD_SLOTS; i++)
		dma_free(&state->cmd_tables[i]);
}

static void ahci_irq_handler(void)
{
	if (!g_hba)
		return;

	uint32_t pending = g_hba->is;
	for (uint8_t p = 0; p < AHCI_PORT_MAX; p++) {
		if (!(pending & (1u << p)) || !g_ports[p].present)
			continue;

		volatile hba_port_t *port = g_ports[p].port;
		uint32_t status = port->is;
		port->is = status;
		g_hba->is = (1u << p);

		if (status & HBA_PxIS_TFES)
			g_ports[p].irq_error = true;

		g_ports[p].irq_pending = true;
	}
}

static size_t fill_prdts(hba_prdt_entry_t *prdts, uint32_t phys_base, uint32_t byte_count)
{
	uint32_t remaining = byte_count;
	size_t idx = 0;

	while (remaining && idx < AHCI_MAX_PRDT) {
		uint32_t chunk = remaining > 0x400000 ? 0x400000 : remaining;
		prdts[idx].dba = phys_base + (byte_count - remaining);
		prdts[idx].dbau = 0;
		prdts[idx].dbc = chunk - 1;
		prdts[idx].i = (remaining <= chunk);
		remaining -= chunk;
		idx++;
	}

	return idx;
}

static bool ahci_wait_for_completion(uint8_t portno, int slot)
{
	struct ahci_port_state *state = &g_ports[portno];
	size_t deadline = GLOBAL_TICK + AHCI_IRQ_TIMEOUT_TICKS;

	while (state->port->ci & (1u << slot)) {
		if (state->port->is & HBA_PxIS_TFES) {
			state->irq_error = true;
			break;
		}

		if (state->irq_pending)
			break;

		if (GLOBAL_TICK > deadline)
			break;
	}

	bool error = state->irq_error || (state->port->is & HBA_PxIS_TFES);
	state->port->is = state->port->is;
	g_hba->is = g_hba->is;
	state->irq_pending = false;
	state->irq_error = false;

	return !error && !(state->port->ci & (1u << slot));
}

static bool ahci_rebase_port(uint8_t portno)
{
	struct ahci_port_state *state = &g_ports[portno];
	volatile hba_port_t *port = state->port;

	ahci_free_port_buffers(state);
	stop_cmd(port);

	port->is = (uint32_t)-1;
	port->ie = 0xFFFFFFFF;
	port->serr = (uint32_t)-1;

	if (!dma_alloc(1024, &state->cmd_list))
		goto fail;
	if (!dma_alloc(256, &state->fis))
		goto fail;

	for (int i = 0; i < AHCI_MAX_CMD_SLOTS; i++) {
		if (!dma_alloc(sizeof(hba_cmd_tbl_t), &state->cmd_tables[i]))
			goto fail;
	}

	port->clb = state->cmd_list.phys;
	port->clbu = 0;
	port->fb = state->fis.phys;
	port->fbu = 0;

	hba_cmd_header_t *cmd_header = (hba_cmd_header_t *)(uintptr_t)state->cmd_list.virt;
	for (int i = 0; i < AHCI_MAX_CMD_SLOTS; i++) {
		cmd_header[i].prdtl = 0;
		cmd_header[i].ctba = state->cmd_tables[i].phys;
		cmd_header[i].ctbau = 0;
	}

	start_cmd(port);
	return true;
fail:
	ahci_free_port_buffers(state);
	return false;
}

bool ahci_probe(struct ahci_controller *out)
{
	if (!out)
		return false;

	memset(out, 0, sizeof(*out));

	for (uint16_t bus = 0; bus < 256; bus++) {
		uint8_t bus_id = (uint8_t)bus;
		for (uint8_t slot = 0; slot < 32; slot++) {
			uint32_t vendor_device = pci_config_read_dword(bus_id, slot, 0, 0x00);
			if (vendor_device == 0xFFFFFFFF)
				continue;

			uint8_t max_func = is_multifunction(bus_id, slot) ? 8 : 1;
			for (uint8_t func = 0; func < max_func; func++) {
				vendor_device = pci_config_read_dword(bus_id, slot, func, 0x00);
				if (vendor_device == 0xFFFFFFFF)
					continue;

				if (!is_ahci_class(bus_id, slot, func))
					continue;

				uint32_t bar5 = pci_config_read_dword(bus_id, slot, func, PCI_BAR5_OFFSET);
				if (!bar5 || (bar5 & 0x1))
					continue;

				volatile hba_mem_t *hba = (volatile hba_mem_t *)(uintptr_t)(bar5 & 0xFFFFFFF0);
				uint32_t pi = hba->pi;
				uint32_t port_map = 0;
				uint8_t chosen_port = 0xFF;

				for (uint8_t p = 0; p < AHCI_PORT_MAX; p++) {
					if (!(pi & (1u << p)))
						continue;

					volatile hba_port_t *port = &hba->ports[p];
					if (!port_device_present(port))
						continue;

					port_map |= (1u << p);
					if (chosen_port == 0xFF)
						chosen_port = p;
				}

				if (!port_map)
					continue;

				uint32_t line_reg = pci_config_read_dword(bus_id, slot, func, PCI_INTERRUPT_LINE_OFFSET);
				out->bus = bus_id;
				out->device = slot;
				out->function = func;
				out->bar5 = bar5 & 0xFFFFFFF0;
				out->port = chosen_port;
				out->port_map = port_map;
				out->irq_line = (uint8_t)(line_reg & 0xFF);
				return true;
			}
		}
	}

	return false;
}

bool ahci_init(const struct ahci_controller *ctrl)
{
	if (!ctrl)
		return false;

	g_ctrl = *ctrl;
	pci_enable_busmaster(ctrl->bus, ctrl->device, ctrl->function);

	g_hba = (volatile hba_mem_t *)(uintptr_t)ctrl->bar5;
	g_irq_line = ctrl->irq_line;

	g_hba->ghc |= (1u << 31);
	g_hba->is = (uint32_t)-1;
	g_hba->ghc |= (1u << 1);

	memset(g_ports, 0, sizeof(g_ports));
	bool found_port = false;

	for (uint8_t p = 0; p < AHCI_PORT_MAX; p++) {
		if (!(ctrl->port_map & (1u << p)))
			continue;

		volatile hba_port_t *port = &g_hba->ports[p];
		if (!port_device_present(port))
			continue;

		g_ports[p].present = true;
		g_ports[p].port_no = p;
		g_ports[p].port = port;
		g_ports[p].irq_pending = false;
		g_ports[p].irq_error = false;

		if (!ahci_rebase_port(p))
			return false;
		found_port = true;
	}

	if (!found_port)
		return false;

	if (g_irq_line < 16) {
		pic_clear_mask(g_irq_line);
		g_irq_registered = irq_register_handler(g_irq_line, ahci_irq_handler);
	}

	if (!g_irq_registered) {
		kprintf("AHCI controller %x:%x.%x found but IRQ %u could not be registered\n", ctrl->bus, ctrl->device, ctrl->function, g_irq_line);
		return false;
	}

	kprintf("AHCI controller %x:%x.%x (ports %x, BAR5=%x, IRQ %u) initialized\n", ctrl->bus, ctrl->device, ctrl->function, ctrl->port_map, ctrl->bar5,
		g_irq_line);

	return true;
}

static uint8_t pick_port(uint8_t requested)
{
	if (requested < AHCI_PORT_MAX && g_ports[requested].present)
		return requested;
	if (g_ports[g_ctrl.port].present)
		return g_ctrl.port;
	for (uint8_t i = 0; i < AHCI_PORT_MAX; i++) {
		if (g_ports[i].present)
			return i;
	}
	return 0xFF;
}

bool ahci_read28(uint8_t channel, uint8_t slavebit, uint32_t lba_addr, uint16_t sector_count, char *dest)
{
	(void)slavebit;

	if (!g_hba || !sector_count || !dest)
		return false;

	uint8_t portno = pick_port(channel);
	if (portno >= AHCI_PORT_MAX || !g_ports[portno].present)
		return false;

	struct ahci_port_state *state = &g_ports[portno];
	int slot = find_cmdslot(state->port);
	if (slot < 0)
		return false;

	uint32_t byte_count = (uint32_t)sector_count * AHCI_SECTOR_SIZE;
	if (byte_count > AHCI_MAX_PRDT_BYTES)
		return false;

	if (!wait_ready(state->port))
		return false;

	struct dma_buffer io_buf = { 0 };
	if (!dma_alloc(byte_count, &io_buf))
		return false;

	state->irq_pending = false;
	state->irq_error = false;
	state->active_slot = (uint8_t)slot;

	state->port->is = (uint32_t)-1;
	g_hba->is = (uint32_t)-1;

	hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)(uintptr_t)state->cmd_list.virt;
	hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t *)(uintptr_t)state->cmd_tables[slot].virt;

	memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));
	size_t prdt_count = fill_prdts(cmdtbl->prdt_entry, io_buf.phys, byte_count);

	cmdheader[slot].cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
	cmdheader[slot].w = 0;
	cmdheader[slot].prdtl = (uint16_t)prdt_count;
	cmdheader[slot].prdbc = 0;

	fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)(&cmdtbl->cfis);
	memset(cmdfis, 0, sizeof(fis_reg_h2d_t));
	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1;
	cmdfis->command = AHCI_CMD_READ_DMA_EXT;
	cmdfis->device = 1 << 6;

	cmdfis->lba0 = (uint8_t)(lba_addr);
	cmdfis->lba1 = (uint8_t)(lba_addr >> 8);
	cmdfis->lba2 = (uint8_t)(lba_addr >> 16);
	cmdfis->lba3 = (uint8_t)(lba_addr >> 24);
	cmdfis->lba4 = 0;
	cmdfis->lba5 = 0;

	cmdfis->countl = (uint8_t)(sector_count & 0xFF);
	cmdfis->counth = (uint8_t)((sector_count >> 8) & 0xFF);

	state->port->ci = 1u << slot;

	bool ok = ahci_wait_for_completion(portno, slot);
	if (ok)
		memcpy(dest, io_buf.virt, byte_count);

	dma_free(&io_buf);

	if (!ok)
		return false;

	return (state->port->tfd & ATA_SR_ERR) == 0;
}

bool ahci_write28(uint8_t channel, uint8_t slavebit, uint32_t lba_addr, uint16_t sector_count, const char *src)
{
	(void)slavebit;

	if (!g_hba || !sector_count || !src)
		return false;

	uint8_t portno = pick_port(channel);
	if (portno >= AHCI_PORT_MAX || !g_ports[portno].present)
		return false;

	struct ahci_port_state *state = &g_ports[portno];
	int slot = find_cmdslot(state->port);
	if (slot < 0)
		return false;

	uint32_t byte_count = (uint32_t)sector_count * AHCI_SECTOR_SIZE;
	if (byte_count > AHCI_MAX_PRDT_BYTES)
		return false;

	if (!wait_ready(state->port))
		return false;

	struct dma_buffer io_buf = { 0 };
	if (!dma_alloc(byte_count, &io_buf))
		return false;
	memcpy(io_buf.virt, src, byte_count);

	state->irq_pending = false;
	state->irq_error = false;
	state->active_slot = (uint8_t)slot;

	state->port->is = (uint32_t)-1;
	g_hba->is = (uint32_t)-1;

	hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)(uintptr_t)state->cmd_list.virt;
	hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t *)(uintptr_t)state->cmd_tables[slot].virt;

	memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));
	size_t prdt_count = fill_prdts(cmdtbl->prdt_entry, io_buf.phys, byte_count);

	cmdheader[slot].cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
	cmdheader[slot].w = 1;
	cmdheader[slot].prdtl = (uint16_t)prdt_count;
	cmdheader[slot].prdbc = 0;

	fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)(&cmdtbl->cfis);
	memset(cmdfis, 0, sizeof(fis_reg_h2d_t));
	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1;
	cmdfis->command = AHCI_CMD_WRITE_DMA_EXT;
	cmdfis->device = 1 << 6;

	cmdfis->lba0 = (uint8_t)(lba_addr);
	cmdfis->lba1 = (uint8_t)(lba_addr >> 8);
	cmdfis->lba2 = (uint8_t)(lba_addr >> 16);
	cmdfis->lba3 = (uint8_t)(lba_addr >> 24);
	cmdfis->lba4 = 0;
	cmdfis->lba5 = 0;

	cmdfis->countl = (uint8_t)(sector_count & 0xFF);
	cmdfis->counth = (uint8_t)((sector_count >> 8) & 0xFF);

	state->port->ci = 1u << slot;

	bool ok = ahci_wait_for_completion(portno, slot);
	dma_free(&io_buf);

	if (!ok)
		return false;

	return (state->port->tfd & ATA_SR_ERR) == 0;
}
