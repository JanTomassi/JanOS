# Building JanOS Applications

Applications provide an ordinary C `main` function. The shared `crt0.o` reads
`argc` and `argv` from the initial process stack, calls `main`, and terminates
the process with `_Exit`.

Available interfaces currently include:

- `<string.h>` memory and string primitives
- `<stdlib.h>` and `abort`
- `<unistd.h>` with `read`, `write`, and `_Exit`
- `<stdio.h>` with `putchar` and `puts`
- `<janos/syscall.h>` for the shared kernel/user syscall numbers

In-tree applications should follow `apps/calc/meson.build`: compile against
`libc_inc` and `sdk_inc`, link with `user_libc`, add `crt0`, and use
`libc/linker/i386/user.ld`.

Install a reusable SDK with:

```sh
DESTDIR="$PWD/dist/sysroot" meson install -C build
```

For a small client, the installed compiler wrapper is the shortest interface:

```sh
dist/sysroot/usr/bin/janos-cc -c main.c -o main.o
dist/sysroot/usr/bin/janos-cc main.o -o app
```

Set `JANOS_CC` if `i686-elf-gcc` is not on `PATH`, or `JANOS_SYSROOT` when the
SDK is installed separately from the wrapper.

The resulting client-facing files are under:

```text
dist/sysroot/usr/include
dist/sysroot/usr/lib32/libc.a
dist/sysroot/usr/lib32/janos/crt0.o
dist/sysroot/usr/lib32/janos/user.ld
```

A standalone application link has this shape:

```sh
i686-elf-gcc --sysroot="$JANOS_SYSROOT" -isystem=/usr/include -ffreestanding \
  -fno-stack-protector -fno-pie -c main.c -o main.o
i686-elf-gcc -nostdlib -no-pie \
  -Wl,-T,"$JANOS_SYSROOT/usr/lib32/janos/user.ld" \
  "$JANOS_SYSROOT/usr/lib32/janos/crt0.o" main.o \
  -L"$JANOS_SYSROOT/usr/lib32" -lc -lgcc -o app
```
