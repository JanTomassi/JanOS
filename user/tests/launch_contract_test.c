#include "syscall.h"

#include <assert.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(USER_SYS_EXIT == 1, "exit syscall number changed");
_Static_assert(USER_SYS_READ == 3, "read syscall number changed");
_Static_assert(USER_SYS_WRITE == 4, "write syscall number changed");

static unsigned char *read_file(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	long end;
	unsigned char *data;

	assert(file != 0);
	assert(fseek(file, 0, SEEK_END) == 0);
	end = ftell(file);
	assert(end > 0);
	assert(fseek(file, 0, SEEK_SET) == 0);
	*size = (size_t)end;
	data = malloc(*size);
	assert(data != 0);
	assert(fread(data, 1, *size, file) == *size);
	assert(fclose(file) == 0);
	return data;
}

static void test_elf_launch_contract(const char *path)
{
	size_t size;
	unsigned char *image = read_file(path, &size);
	const Elf32_Ehdr *header = (const Elf32_Ehdr *)image;
	const Elf32_Phdr *program_headers;
	const Elf32_Shdr *sections;
	int found_entry = 0;
	int found_interpreter = 0;

	assert(size >= sizeof(*header));
	assert(memcmp(header->e_ident, ELFMAG, SELFMAG) == 0);
	assert(header->e_ident[EI_CLASS] == ELFCLASS32);
	assert(header->e_ident[EI_DATA] == ELFDATA2LSB);
	assert(header->e_type == ET_EXEC);
	assert(header->e_machine == EM_386);
	assert(header->e_entry == 0x00400000);
	assert(header->e_phentsize == sizeof(Elf32_Phdr));
	assert(header->e_shentsize == sizeof(Elf32_Shdr));
	assert((size_t)header->e_phoff + header->e_phnum * sizeof(Elf32_Phdr) <= size);
	assert((size_t)header->e_shoff + header->e_shnum * sizeof(Elf32_Shdr) <= size);

	program_headers = (const Elf32_Phdr *)(image + header->e_phoff);
	for (unsigned i = 0; i < header->e_phnum; ++i) {
		const Elf32_Phdr *program = &program_headers[i];
		if (program->p_type == PT_INTERP)
			found_interpreter = 1;
		if (program->p_type == PT_LOAD && (program->p_flags & PF_X) != 0)
			assert(header->e_entry >= program->p_vaddr &&
				header->e_entry < program->p_vaddr + program->p_memsz);
	}
	assert(!found_interpreter);

	sections = (const Elf32_Shdr *)(image + header->e_shoff);
	assert(header->e_shstrndx < header->e_shnum);
	for (unsigned i = 0; i < header->e_shnum; ++i) {
		const Elf32_Shdr *section = &sections[i];
		if (section->sh_type != SHT_SYMTAB && section->sh_type != SHT_DYNSYM)
			continue;
		assert(section->sh_link < header->e_shnum);
		const Elf32_Sym *symbols = (const Elf32_Sym *)(image + section->sh_offset);
		const char *names = (const char *)(image + sections[section->sh_link].sh_offset);
		assert(section->sh_entsize == sizeof(Elf32_Sym));
		for (size_t j = 0; j < section->sh_size / sizeof(Elf32_Sym); ++j) {
			const Elf32_Sym *symbol = &symbols[j];
			if (symbol->st_name < sections[section->sh_link].sh_size &&
				strcmp(names + symbol->st_name, "_start") == 0) {
				assert(ELF32_ST_BIND(symbol->st_info) == STB_GLOBAL);
				assert(symbol->st_value == header->e_entry);
				found_entry = 1;
			}
		}
	}
	assert(found_entry);
	free(image);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	test_elf_launch_contract(argv[1]);
	return 0;
}
