/* The bootloader will look at this image and start execution at the symbol
   designated as the entry point. */
ENTRY(_start)

INIT_ADDR = 1M;
HIGHER_HALF = 0xC0000000;

PHDRS
{
  multiboot	PT_LOAD FLAGS(4);            /* R-- */

  boot   	PT_LOAD FLAGS(5);            /* R-X */

  text   	PT_LOAD FLAGS(5);            /* R-X */
  rodata 	PT_LOAD FLAGS(4);            /* R-- */
  data   	PT_LOAD FLAGS(6);            /* RW- */
}

SECTIONS
{
	/DISCARD/ :
	{
        	*(.note .note.*)
	}

	. = INIT_ADDR;

	.multiboot : AT (ADDR(.multiboot))
	{
		. = ALIGN(8);
		*(.multiboot)
		. = ALIGN(8);
	} :multiboot

	. = ALIGN(4K);

	.boot : AT (ADDR(.boot))
	{
		KEEP(*(.boot .boot.*))
	} :boot

	. += HIGHER_HALF;
	. = ALIGN(4K);

	/* Code. */
	.text : AT(ADDR(.text) - HIGHER_HALF)
	{
		*(.text .text.*)
	} :text

	. = ALIGN(4K);

	/* Read-only data. */
	.rodata : AT(ADDR(.rodata) - HIGHER_HALF)
	{
		*(.rodata .rodata.*)
	} :rodata

	/* Read-only data. */
	.eh_frame : AT(ADDR(.eh_frame) - HIGHER_HALF)
	{
		*(.eh_frame .eh_frame.*)
	} :rodata

	/* Global offset table */
	.got : AT(ADDR(.got) - HIGHER_HALF)
	{
		*(.got .got.*)
	} :rodata

	. = ALIGN(4K);

	/* Read-write data (initialized) */
	.data : AT(ADDR(.data) - HIGHER_HALF)
	{
		*(.data .data.*)
	} :data

	/* Read-write data (uninitialized) and stack */
	.bss : AT(ADDR(.bss) - HIGHER_HALF)
	{
		*(COMMON)
		*(.bss .bss.*)
	} :data

	. = ALIGN(4K);
	.init.text : AT(ADDR(.init.text) - HIGHER_HALF)
	{
		*(.init.text .init.text.*)
	}

	.init.data : AT(ADDR(.init.data) - HIGHER_HALF)
	{
		*(.init.data .init.data.*)
	}
}
