#include <kernel/acpi.h>
#include <kernel/display.h>
#include <kernel/mmio.h>
#include <string.h>

MODULE("ACPI");

bool acpi_checksum_valid(const void *table, size_t length)
{
	const uint8_t *bytes = table;
	uint8_t sum = 0;
	for (size_t i = 0; i < length; i++)
		sum += bytes[i];
	return sum == 0;
}

static const struct acpi_rsdp_descriptor *acpi_validate_rsdp(const struct acpi_rsdp_descriptor *rsdp_phys)
{
	if (!rsdp_phys)
		return nullptr;

	const struct acpi_rsdp_descriptor *rsdp_header = mmio_map((uintptr_t)rsdp_phys, sizeof(struct acpi_rsdp_descriptor));
	const size_t rsdp_len = rsdp_header->revision >= 2 ? rsdp_header->length : 20;
	const struct acpi_rsdp_descriptor *rsdp = mmio_map((uintptr_t)rsdp_phys, rsdp_len);

	if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0) {
		mprint("ACPI: invalid RSDP signature\n");
		return nullptr;
	}

	if (!acpi_checksum_valid(rsdp, rsdp_len)) {
		mprint("ACPI: RSDP checksum invalid\n");
		return nullptr;
	}

	return rsdp;
}

const struct acpi_rsdp_descriptor *acpi_find_rsdp(const struct multiboot_tag_old_acpi *old_rsdp,
						  const struct multiboot_tag_new_acpi *new_rsdp)
{
	const struct acpi_rsdp_descriptor *candidate_new = new_rsdp ? (const struct acpi_rsdp_descriptor *)new_rsdp->rsdp : nullptr;
	const struct acpi_rsdp_descriptor *candidate_old = old_rsdp ? (const struct acpi_rsdp_descriptor *)old_rsdp->rsdp : nullptr;
	const struct acpi_rsdp_descriptor *validated_new = acpi_validate_rsdp(candidate_new);
	const struct acpi_rsdp_descriptor *validated_old = acpi_validate_rsdp(candidate_old);

	return validated_new ? validated_new : validated_old;
}

static const struct acpi_sdt_header *acpi_find_table_rsdt(const struct acpi_sdt_header *rsdt, const char signature[4])
{
	if (!rsdt)
		return nullptr;

	rsdt = mmio_map((uintptr_t)get_phy_addr(rsdt), rsdt->length);
	if (!acpi_checksum_valid(rsdt, rsdt->length))
		return nullptr;

	const uint32_t entry_count = (rsdt->length - sizeof(struct acpi_sdt_header)) / sizeof(uint32_t);
	const uint32_t *entries = (const uint32_t *)(rsdt + 1);

	for (uint32_t i = 0; i < entry_count; i++) {
		const struct acpi_sdt_header *hdr = mmio_map((uintptr_t)entries[i], sizeof(struct acpi_sdt_header));
		hdr = mmio_map((uintptr_t)entries[i], hdr->length);
		if (memcmp(hdr->signature, signature, 4) == 0 && acpi_checksum_valid(hdr, hdr->length))
			return hdr;
	}

	return nullptr;
}

static const struct acpi_sdt_header *acpi_find_table_xsdt(const struct acpi_sdt_header *xsdt, const char signature[4])
{
	if (!xsdt)
		return nullptr;

	xsdt = mmio_map((uintptr_t)xsdt, xsdt->length);
	if (!acpi_checksum_valid(xsdt, xsdt->length))
		return nullptr;

	const uint32_t entry_count = (xsdt->length - sizeof(struct acpi_sdt_header)) / sizeof(uint64_t);
	const uint64_t *entries = (const uint64_t *)(xsdt + 1);

	for (uint32_t i = 0; i < entry_count; i++) {
		const struct acpi_sdt_header *hdr = mmio_map((uintptr_t)entries[i], sizeof(struct acpi_sdt_header));
		hdr = mmio_map((uintptr_t)entries[i], hdr->length);
		if (memcmp(hdr->signature, signature, 4) == 0 && acpi_checksum_valid(hdr, hdr->length))
			return hdr;
	}

	return nullptr;
}

const struct acpi_madt *acpi_find_madt(const struct acpi_rsdp_descriptor *rsdp)
{
	if (!rsdp)
		return nullptr;

	const struct acpi_sdt_header *xsdt = nullptr;
	const struct acpi_sdt_header *rsdt = mmio_map((uintptr_t)rsdp->rsdt_address, sizeof(struct acpi_sdt_header));

	if (rsdp->revision >= 2 && rsdp->xsdt_address != 0)
		xsdt = mmio_map((uintptr_t)rsdp->xsdt_address, sizeof(struct acpi_sdt_header));

	const struct acpi_sdt_header *madt_hdr = nullptr;

	if (xsdt)
		madt_hdr = acpi_find_table_xsdt(xsdt, "APIC");

	if (!madt_hdr)
		madt_hdr = acpi_find_table_rsdt(rsdt, "APIC");

	return (const struct acpi_madt *)madt_hdr;
}
