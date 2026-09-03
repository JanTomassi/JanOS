# Repository Structure

JanOS separates target code by responsibility and keeps generated files in the
Meson build directory.

| Path | Responsibility |
|---|---|
| `kernel/src/init` | Kernel composition root and initialization order |
| `kernel/src/core` | Architecture-neutral kernel services |
| `kernel/src/mm` | Generic memory allocators and early memory management |
| `kernel/src/process` | Processes, address spaces, stacks, and wait queues |
| `kernel/src/exec` | FAT16 ELF loading and retained Multiboot reference loader |
| `kernel/src/syscall` | Kernel syscall dispatch and handlers |
| `kernel/src/fs` | Filesystem implementations |
| `kernel/src/drivers` | Bus, input, storage, and console drivers |
| `kernel/src/arch/i386` | Boot, CPU, interrupt, paging, and context mechanisms |
| `kernel/include/kernel` | Kernel subsystem interfaces |
| `kernel/include/arch/i386` | Architecture-private interfaces |
| `libc/src` | User C library and sources shared with `libk` |
| `libc/crt` | User process startup code |
| `libc/linker` | User executable linker scripts |
| `sdk/include/janos` | Stable kernel/user ABI definitions |
| `apps` | JanOS user applications |
| `tests/host` | Tests that execute on the development machine |
| `config` | Meson cross-machine definitions |
| `data` | Static image configuration |

## Build Graph

```text
shared C sources -> libk.a -> JanOS.kernel
                  -> libc.a + crt0.o -> calc
kernel + grub.cfg -> JanOS.iso
apps + font + image generator -> build/calc-disk.img
JanOS.iso + calc-disk.img -> QEMU
```

Meson uses the native compiler for host tests and `i686-elf-gcc` for all JanOS
artifacts. NASM inputs are generated as ELF32 objects. Kernel and application
linker scripts then produce static ELF32 executables.

The `build/` directory contains all intermediate and final build artifacts.
`dist/sysroot/` is only an installation staging area and is never a compiler
input.
