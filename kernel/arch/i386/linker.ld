/* The bootloader will look at this image and start execution at the symbol
   designated as the entry point. */
ENTRY(_start)

HIGHER_HALF = 0xC0000000;

MEMORY
{
	rom (rx) : ORIGIN = 2M,				LENGTH = 1M
	ram (rw) : ORIGIN = ORIGIN(rom)+LENGTH(rom), 	LENGTH = 1M

    	hk_rom (rx) : ORIGIN = HIGHER_HALF+ORIGIN(rom),	       LENGTH = 1M
    	hk_ram (rw) : ORIGIN = ORIGIN(hk_rom)+LENGTH(hk_rom),  LENGTH = 1M
}

SECTIONS
{
	/DISCARD/ :
	{
		/* *(.got*) */
		*(.eh_frame)
        	*(.note .note.*)
	}

	. = ORIGIN(rom);

	.multiboot . :
	{
		. = ALIGN(8);
		*(.multiboot)
		. = ALIGN(8);
	} AT> rom

	.boot . :
	{
		*(.boot)
	} AT> rom

	. += HIGHER_HALF;

	/* Code. */
	.text . :
	{
		*(.text)
	} AT> rom

	/* Read-only data. */
	.rodata . :
	{
		*(.rodata)
	} AT> rom

	/* Read-only data. */
	.eh_frame . :
	{
		*(.eh_frame)
	} AT> rom

	. = ORIGIN(hk_ram);

	/* Read-write data (initialized) */
	.data . :
	{
		*(.data)
	}  AT> ram

	/* Global offset table */
	.got . :
	{
		*(.got*)
	}  AT> ram

	/* Read-write data (uninitialized) and stack */
	.bss . :
	{
		*(COMMON)
		*(.bss)
	}  AT> ram
}
