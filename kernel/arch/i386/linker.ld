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
		*(.multiboot)
	} AT> rom

	.boot . :
	{
		*(.boot)
	} AT> rom

	. += HIGHER_HALF;
	
	/* Code. */
	.text . : ALIGN(4096)
	{
		*(.text)
		FILL(0xaa)
	} AT> rom
 
	/* Read-only data. */
	.rodata . : ALIGN(4096)
	{
		*(.rodata)
		FILL(0xaa)
	} AT> rom

	/* Read-only data. */
	.eh_frame . : ALIGN(4096)
	{
		*(.eh_frame)
	} AT> rom
		
	. = ORIGIN(hk_ram);

	/* Read-write data (initialized) */
	.data . : ALIGN(4096)
	{
		*(.data)
	}  AT> ram

	/* Global offset table */
	.got . : ALIGN(4096)
	{
		*(.got*)
	}  AT> ram
 
	/* Read-write data (uninitialized) and stack */
	.bss . : ALIGN(4096)
	{
		*(COMMON)
		*(.bss)
	}  AT> ram
}
