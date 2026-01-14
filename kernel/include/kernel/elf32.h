#pragma once

#include <stdint.h>

#define ELF_SHF_WRITE            (0x1)
#define ELF_SHF_ALLOC            (0x2)
#define ELF_SHF_EXECINSTR        (0x4)
#define ELF_SHF_MERGE            (0x10)
#define ELF_SHF_STRINGS          (0x20)
#define ELF_SHF_INFO_LINK        (0x40)
#define ELF_SHF_LINK_ORDER       (0x80)
#define ELF_SHF_OS_NONCONFORMING (0x100)
#define ELF_SHF_GROUP            (0x200)
#define ELF_SHF_TLS              (0x400)
#define ELF_SHF_MASKOS           (0x0ff00000)
#define ELF_SHF_AMD64_LARGE      (0x10000000)
#define ELF_SHF_ORDERED          (0x40000000)
#define ELF_SHF_EXCLUDE          (0x80000000)
#define ELF_SHF_MASKPROC         (0xf0000000)

/* Section header types */
#define ELF_SHT_NULL     (0x0)
#define ELF_SHT_PROGBITS (0x1)
#define ELF_SHT_SYMTAB   (0x2)
#define ELF_SHT_STRTAB   (0x3)
#define ELF_SHT_RELA     (0x4)
#define ELF_SHT_HASH     (0x5)
#define ELF_SHT_DYNAMIC  (0x6)
#define ELF_SHT_NOTE     (0x7)
#define ELF_SHT_NOBITS   (0x8)
#define ELF_SHT_REL      (0x9)
#define ELF_SHT_SHLIB    (0xA)
#define ELF_SHT_DYNSYM   (0xB)
#define ELF_SHT_NUM      (0xC)
#define ELF_SHT_LOPROC   (0x70000000)
#define ELF_SHT_HIPROC   (0x7fffffff)
#define ELF_SHT_LOUSER   (0x80000000)
#define ELF_SHT_HIUSER   (0xffffffff)

#define EI_NIDENT 16
typedef uint32_t Elf32_Addr;  // 4 4 Unsigned program address
typedef uint16_t Elf32_Half;  // 2 2 Unsigned medium integer
typedef uint32_t Elf32_Off;   // 4 4 Unsigned file offset
typedef uint32_t Elf32_Sword; // 4 4 Signed large integer
typedef uint32_t Elf32_Word;  // 4 4 Unsigned large integer

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
