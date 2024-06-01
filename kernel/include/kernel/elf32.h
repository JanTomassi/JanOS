#pragma once

#include <stdint.h>

#define EI_NIDENT 16
typedef uint32_t Elf32_Addr; // 4 4 Unsigned program address
typedef uint16_t Elf32_Half; // 2 2 Unsigned medium integer
typedef uint32_t Elf32_Off; // 4 4 Unsigned file offset
typedef uint32_t Elf32_Sword; // 4 4 Signed large integer
typedef uint32_t Elf32_Word; // 4 4 Unsigned large integer

typedef struct {
	Elf32_Word sh_name;
	Elf32_Word sh_type;
	Elf32_Word sh_flags;
	Elf32_Addr sh_addr;
	Elf32_Off sh_offset;
	Elf32_Word sh_size;
	Elf32_Word sh_link;
	Elf32_Word sh_info;
	Elf32_Word sh_addralign;
	Elf32_Word sh_entsize;
} Elf32_Shdr;
