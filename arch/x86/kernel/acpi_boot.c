/*
 * Copyright (c) 2016 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <asm/asm.h>
#include <asm/apic.h>
#include <asm/processor.h>
#include <asm/irq_vectors.h>

#include <lego/acpi.h>
#include <lego/string.h>
#include <lego/kernel.h>
#include <lego/early_ioremap.h>

int acpi_lapic;
int acpi_ioapic;
u64 acpi_lapic_addr = APIC_DEFAULT_PHYS_BASE;

/**
 * acpi_register_lapic - register a local apic and generates a logic cpu number
 * @id: local apic id to register
 * @acpiid: ACPI id to register
 * @enabled: this cpu is enabled or not
 *
 * Returns the logic cpu number which maps to the local apic
 */
static int acpi_register_lapic(int id, u32 acpiid, u8 enabled)
{
	return 0;
}

static int __init acpi_parse_madt_handler(struct acpi_table_header *table)
{
	struct acpi_table_madt *madt;

	if (!cpu_has(X86_FEATURE_APIC))
		return -EINVAL;

	madt = (struct acpi_table_madt *)table;
	if (WARN_ON(!madt))
		return -ENODEV;

	if (madt->address) {
		acpi_lapic_addr = (u64) madt->address;
		pr_info("Local APIC address %#x\n",
			madt->address);
	}

	return 0;
}

static int __init
acpi_parse_sapic(struct acpi_subtable_header *header, const unsigned long end)
{
	struct acpi_madt_local_sapic *processor = NULL;

	processor = (struct acpi_madt_local_sapic *)header;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	acpi_register_lapic((processor->id << 8) | processor->eid,/* APIC ID */
			    processor->processor_id, /* ACPI ID */
			    processor->lapic_flags & ACPI_MADT_ENABLED);

	return 0;
}

static int __init
acpi_parse_lapic(struct acpi_subtable_header * header, const unsigned long end)
{
	struct acpi_madt_local_apic *processor = NULL;

	processor = (struct acpi_madt_local_apic *)header;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* Ignore invalid ID */
	if (processor->id == 0xff)
		return 0;

	/*
	 * We need to register disabled CPU as well to permit
	 * counting disabled CPUs. This allows us to size
	 * cpus_possible_map more accurately, to permit
	 * to not preallocating memory for all NR_CPUS
	 * when we use CPU hotplug.
	 */
	acpi_register_lapic(processor->id,	/* APIC ID */
			    processor->processor_id, /* ACPI ID */
			    processor->lapic_flags & ACPI_MADT_ENABLED);

	return 0;
}

static int __init
acpi_parse_x2apic(struct acpi_subtable_header *header, const unsigned long end)
{
	struct acpi_madt_local_x2apic *processor = NULL;
	int apic_id;
	u8 enabled;

	processor = (struct acpi_madt_local_x2apic *)header;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	apic_id = processor->local_apic_id;
	enabled = processor->lapic_flags & ACPI_MADT_ENABLED;
#ifdef CONFIG_X86_X2APIC
	/*
	 * We need to register disabled CPU as well to permit
	 * counting disabled CPUs. This allows us to size
	 * cpus_possible_map more accurately, to permit
	 * to not preallocating memory for all NR_CPUS
	 * when we use CPU hotplug.
	 */
	if (!apic->apic_id_valid(apic_id) && enabled)
		pr_warn("x2apic entry ignored\n");
	else
		acpi_register_lapic(apic_id, processor->uid, enabled);
#else
	pr_warn("x2apic entry ignored\n");
#endif

	return 0;
}

static int __init
acpi_parse_x2apic_nmi(struct acpi_subtable_header *header,
		      const unsigned long end)
{
	struct acpi_madt_local_x2apic_nmi *x2apic_nmi = NULL;

	x2apic_nmi = (struct acpi_madt_local_x2apic_nmi *)header;

	if (BAD_MADT_ENTRY(x2apic_nmi, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (x2apic_nmi->lint != 1)
		pr_warn("NMI not connected to LINT 1!\n");

	return 0;
}

static int __init
acpi_parse_lapic_nmi(struct acpi_subtable_header * header, const unsigned long end)
{
	struct acpi_madt_local_apic_nmi *lapic_nmi = NULL;

	lapic_nmi = (struct acpi_madt_local_apic_nmi *)header;

	if (BAD_MADT_ENTRY(lapic_nmi, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (lapic_nmi->lint != 1)
		pr_warn("NMI not connected to LINT 1!\n");

	return 0;
}

static int __init
acpi_parse_ioapic(struct acpi_subtable_header * header, const unsigned long end)
{
	struct acpi_madt_io_apic *ioapic = NULL;

	ioapic = (struct acpi_madt_io_apic *)header;

	if (BAD_MADT_ENTRY(ioapic, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	//mp_register_ioapic(ioapic->id, ioapic->address, ioapic->global_irq_base, &cfg);

	return 0;
}

static int __init
acpi_parse_nmi_src(struct acpi_subtable_header * header, const unsigned long end)
{
	struct acpi_madt_nmi_source *nmi_src = NULL;

	nmi_src = (struct acpi_madt_nmi_source *)header;

	if (BAD_MADT_ENTRY(nmi_src, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	return 0;
}

static int __init acpi_parse_madt_ioapic_entries(void)
{
	int count;

	count = acpi_table_parse_madt(ACPI_MADT_TYPE_IO_APIC, acpi_parse_ioapic,
				      MAX_IO_APICS);
	if (!count) {
		pr_err("No IOAPIC entries present\n");
		return -ENODEV;
	} else if (count < 0) {
		pr_err("Error parsing IOAPIC entry\n");
		return count;
	}

	/* Fill in identity legacy mappings where no override */
	//mp_config_acpi_legacy_irqs();

	count = acpi_table_parse_madt(ACPI_MADT_TYPE_NMI_SOURCE,
				      acpi_parse_nmi_src, NR_IRQS);
	if (count < 0) {
		pr_err("Error parsing NMI SRC entry\n");
		return count;
	}
	return 0;
}

static int __init acpi_parse_madt_lapic_entries(void)
{
	int ret, count, x2count;
	struct acpi_subtable_proc madt_proc[2];

	count = acpi_table_parse_madt(ACPI_MADT_TYPE_LOCAL_SAPIC,
				      acpi_parse_sapic, MAX_LOCAL_APIC);

	if (!count) {
		memset(madt_proc, 0, sizeof(madt_proc));
		madt_proc[0].id = ACPI_MADT_TYPE_LOCAL_APIC;
		madt_proc[0].handler = acpi_parse_lapic;
		madt_proc[1].id = ACPI_MADT_TYPE_LOCAL_X2APIC;
		madt_proc[1].handler = acpi_parse_x2apic;
		ret = acpi_table_parse_entries_array(ACPI_SIG_MADT,
				sizeof(struct acpi_table_madt),
				madt_proc, ARRAY_SIZE(madt_proc), MAX_LOCAL_APIC);
		if (ret < 0) {
			pr_err("Error parsing LAPIC/X2APIC entries\n");
			return ret;
		}

		count = madt_proc[0].count;
		x2count = madt_proc[1].count;
	}

	if (!count && !x2count) {
		pr_err("No LAPIC entries present\n");
		return -ENODEV;
	} else if (count < 0 || x2count < 0) {
		pr_err("Error parsing LAPIC entry\n");
		return count;
	}

	x2count = acpi_table_parse_madt(ACPI_MADT_TYPE_LOCAL_X2APIC_NMI,
					acpi_parse_x2apic_nmi, 0);

	count = acpi_table_parse_madt(ACPI_MADT_TYPE_LOCAL_APIC_NMI,
				      acpi_parse_lapic_nmi, 0);

	if (count < 0 || x2count < 0) {
		pr_err("Error parsing LAPIC NMI entry\n");
		return count;
	}

	return 0;
}

/**
 * acpi_boot_parse_madt
 * Process the Multiple APIC Description Table,
 * find all possible APIC and IO-APIC settings.
 */
static void __init acpi_boot_parse_madt(void)
{
	int ret;

	ret = acpi_parse_table(ACPI_SIG_MADT, acpi_parse_madt_handler);
	if (ret)
		return;

	ret = acpi_parse_madt_lapic_entries();
	if (!ret)
		acpi_lapic = 1;

	ret = acpi_parse_madt_ioapic_entries();
	if (!ret)
		acpi_ioapic = 1;

	/*
	 * ACPI supports both logical (e.g. Hyper-Threading)
	 * and physical processors, where MPS only supports physical.
	 */
	if (acpi_lapic && acpi_ioapic)
		pr_info("Using ACPI (MADT) for SMP configuration\n");
	else if (acpi_lapic)
		pr_info("Using ACPI for processor (LAPIC) configuration\n");
}

/*
 * Parse ACPI tables one-by-one
 * - MADT: Multiple APIC Description Table
 */
void __init acpi_boot_parse_tables(void)
{
	acpi_boot_parse_madt();
}