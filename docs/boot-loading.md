# Boot Loading

## Normal Boot

The normal GRUB entry in `data/grub.cfg` loads only `JanOS.kernel` with
Multiboot2. It does not provide application or font modules.

The `calc-disk` Meson target creates the FAT16 application disk and stores:

```text
CALC
FBSERVER
FBCLIENT
PINGPONG
JANOS.PSF
```

`kernel/src/init/main.c` initializes the block-device driver, finds the FAT16
device containing `CALC`, and uses the same device for all application startup.
`process_exec_block_device_app()` loads an ELF from the FAT16 root directory,
maps its segments into a new address space, and starts the process. The
framebuffer bootstrap uses the same path for `FBSERVER` and `FBCLIENT`, and
maps `JANOS.PSF` into the framebuffer server as read-only user memory.

Run the normal disk-backed image with:

```sh
meson compile -C build calc-disk iso
./tools/run-qemu.sh qemu-system-i386 build/JanOS.iso sata build/calc-disk.img
```

## GDB Debugging

The `gdb` Meson target starts the disk-backed kernel paused at reset, loads the
kernel symbols, and connects GDB to QEMU's remote stub on port `1234`:

```sh
meson compile -C build gdb
```

The target prefers `i686-elf-gdb` and falls back to `gdb`. Set breakpoints such
as `kernel_initialize` or `init_virtual_memory`, then use `continue`.

The ISO and the application disk are separate intentionally. The ISO contains
the kernel boot artifact; the FAT16 disk contains software loaded after the
kernel starts.

## Retained Multiboot Path

The Multiboot ELF loader remains in `kernel/src/exec/multiboot_exec.c` for
future bootstrapping work and dedicated legacy tests. It is not used by the
normal boot path.

To use that path in a future test image:

1. Add a GRUB module entry whose command line is the exact module name:

   ```text
   multiboot2 /boot/JanOS.test.kernel
   module2 /boot/example example
   ```

2. Find the module with `process_find_multiboot_module()`.
3. Call `process_load_multiboot_app()` to copy the module and load its ELF
   segments into a new process address space.
4. Call `process_start()` with the desired arguments.
5. Call `process_initial_context()` and enter the context, or use
   `process_exec_multiboot_app()` for the combined load/start operation.

The framebuffer test path also demonstrates mapping a Multiboot file into a
process with `address_space_map_borrowed()`. That mapping borrows the physical
pages supplied by GRUB and must not be treated as a disk-backed application.
The normal framebuffer bootstrap instead reads `JANOS.PSF` from FAT16.

Multiboot still supplies boot metadata such as the memory map and framebuffer
mode. “No software from Multiboot” means that application binaries and font
data are not loaded as Multiboot modules.
