include ../make.config

PROGRAM := calc/calc
USER_CFLAGS := -m32 -ffreestanding -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -Wall -Wextra -Icalc
USER_LDFLAGS := -m elf_i386 -nostdlib
USER_LIBS := -lgcc
LIBGCC := $(shell $(CC) -print-libgcc-file-name)

.PHONY: all calc test clean

all: calc

calc: $(PROGRAM)

$(PROGRAM): calc/start.o calc/calc.o calc/repl.o calc/syscall.o calc/linker.ld
	$(LD) $(USER_LDFLAGS) -T calc/linker.ld -o $@ calc/start.o calc/calc.o calc/repl.o calc/syscall.o $(LIBGCC)

calc/%.o: calc/%.c calc/calc.h calc/syscall.h
	$(CC) $(USER_CFLAGS) -std=c2x -c $< -o $@

calc/start.o: calc/start.s
	$(AS) $(ASFLAGS) $< -o $@

test:
	$(MAKE) -C tests

clean:
	rm -f calc/*.o $(PROGRAM)
	$(MAKE) -C tests clean
