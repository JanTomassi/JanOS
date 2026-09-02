# JanOS Test Plan

JanOS testing should use three layers rather than one large end-to-end test:

```text
Host unit tests
    |
Kernel self-tests in QEMU
    |
Full QEMU integration tests
```

Each lower layer is slower and covers more integration. Logic should be tested
on the host where possible, while privileged CPU and hardware behavior must be
tested inside QEMU.

## Current Tests

The current Meson suites contain:

- `host:calc`: calculator parsing, arithmetic, overflow, and invalid input.
- `host:launch-contract`: validates the generated user ELF format and entry.
- `contract:kernel-multiboot2-contract`: verifies that GRUB recognizes the
  kernel as Multiboot2.

Run them with:

```sh
meson test -C build --print-errorlogs
```

## Test Layout

The intended layout is:

```text
tests/
├── host/
│   ├── calc_test.c
│   ├── elf_loader_test.c
│   ├── fat16_test.c
│   └── fakes/
├── kernel/
│   ├── test.h
│   ├── test_runner.c
│   ├── memory_test.c
│   ├── interrupt_test.c
│   ├── process_test.c
│   └── syscall_test.c
├── integration/
│   ├── qemu_test.py
│   └── expected/
└── images/
    ├── basic-fat16.img
    ├── corrupt-fat16.img
    └── elf-fat16.img
```

## Host Unit Tests

Host tests execute with the native compiler and must not use privileged CPU
instructions or real kernel page tables.

### ELF Loader

Test `elf32_load()` using a fake implementation of `struct elf_load_ops`. The
fake should record mappings, copied bytes, zeroed ranges, permissions, and
cleanup calls.

Required cases:

- Valid single- and multi-segment ELF files.
- Invalid magic, class, byte order, type, machine, and version.
- Truncated headers and program-header tables.
- Invalid, overflowing, or overlapping segment ranges.
- BSS zeroing when `memsz` is greater than `filesz`.
- Entry point outside an executable segment.
- Rejection of dynamic, interpreter, and TLS segments.
- Mapping, copying, zeroing, and protection failures.
- Unmapping of all successful mappings after a later failure.

The pure ELF loader should eventually be separated from the FAT16 adapter so
the host test does not need kernel allocator or storage stubs.

### FAT16

Initially test pure FAT16 operations:

- BPB and filesystem layout calculation.
- End-of-chain detection.
- 8.3 filename decoding.
- Deleted and unused directory entries.
- Invalid sector and cluster sizes.

Full file tests should use an injected block-device interface:

```c
struct block_device {
    bool (*read)(void *context, uint32_t lba,
                 uint16_t count, void *destination);
    void *context;
};
```

A memory-backed fake can then test boot-sector reads, directory lookup,
multi-cluster files, offsets, EOF behavior, failed reads, corrupt chains, and
cyclic chains without AHCI or ATA hardware.

## Kernel Self-Tests

Add a Meson option that enables tests before entering normal userspace:

```meson
option(
  'kernel_tests',
  type: 'boolean',
  value: false,
  description: 'Run kernel self-tests before launching userspace',
)
```

When enabled, compile `tests/kernel` into a dedicated test kernel. The test
runner should execute after memory, interrupts, and process infrastructure are
initialized, but before `i386_context_enter_user()`.

Each assertion should report a stable serial marker:

```text
JANOS:TEST:MEMORY:PASS
JANOS:TEST:PROCESS:PASS
JANOS:TEST:SYSCALL:PASS
```

Normal debug messages must not be used as test contracts.

### Memory Management

Memblock tests should cover overlapping and reserved ranges, alignment,
bottom-up and top-down allocation, exhaustion, freeing, reuse, invalid ranges,
and integer overflow.

Physical allocator tests should cover single and multiple pages, low and high
memory restrictions, alignment, reuse, exhaustion, and invalid frees. The
existing allocator test helpers in `kernel/src/init/main.c` should be moved to
`tests/kernel/memory_test.c` and converted to assertions.

Virtual-memory tests should cover:

- Page-directory creation and destruction.
- Mapping and unmapping.
- User, writable, and executable permissions.
- Protection changes and address translation.
- Virtual range splitting, reuse, and coalescing.
- Cross-page user copies.
- Invalid and higher-half user addresses.

Slab and general allocator tests should verify object alignment, cache reuse,
large allocation fallback, zero-size requests, and invalid or cross-cache
frees.

### Interrupts

Host tests can cover IRQ handler registration, duplicate rejection, handler
ordering, unregistration, and pool exhaustion when architecture operations are
stubbed.

QEMU kernel tests should exercise the real entry path for:

- Timer interrupts.
- A software-triggered IRQ vector.
- The `int 0x80` syscall vector.
- User divide-by-zero and invalid-opcode faults.
- User faults terminating only the current process.

### Process Creation

Test process-system initialization, PID allocation, address-space and stack
creation, parent-child relationships, argument layout, state transitions, exit
status, destruction, and reaping.

The current single-active-process restriction should have an explicit test
until scheduling supports multiple runnable processes.

## Userspace Integration Tests

Create small purpose-built applications instead of making the calculator test
every kernel feature:

```text
apps/tests/
├── syscall-test/
├── fault-test/
├── argv-test/
└── exit-test/
```

The syscall program should test valid stdout/stderr writes, invalid file
descriptors, null and unmapped pointers, buffers crossing page boundaries,
unknown syscall numbers, and exit status propagation.

An end-to-end user execution test proves that the kernel can find the boot
module, validate and map its ELF segments, build `argc` and `argv`, enter ring
3, service a syscall, and process the application's exit.

## Deterministic QEMU Results

Automated QEMU tests need both serial output and a guest-controlled exit code.
Configure QEMU with:

```text
-display none
-serial stdio
-monitor none
-no-reboot
-device isa-debug-exit,iobase=0xf4,iosize=0x04
```

Add a test-only kernel helper that writes the final status to port `0xf4` and
then halts. The host harness must decode QEMU's `isa-debug-exit` status. A
timeout is always a failure; it must not be interpreted as a successful test.

The QEMU harness should:

1. Start QEMU with an explicit timeout and CPU count.
2. Capture serial output.
3. Wait for the guest-controlled QEMU exit.
4. Decode and validate the exit status.
5. Require expected pass markers.
6. Print the complete serial log when a test fails.

Register the harness in the `qemu` suite:

```meson
test(
  'kernel-boot',
  qemu_test_harness,
  args: [janos_test_iso],
  depends: janos_test_iso,
  suite: 'qemu',
  timeout: 30,
)
```

## Full Boot Test

Add machine-readable milestones to the boot path:

```text
JANOS:BOOT:SERIAL:PASS
JANOS:BOOT:MEMBLOCK:PASS
JANOS:BOOT:PHYSICAL_MEMORY:PASS
JANOS:BOOT:VIRTUAL_MEMORY:PASS
JANOS:BOOT:IDT:PASS
JANOS:BOOT:SMP:PASS
JANOS:BOOT:PROCESS:PASS
JANOS:BOOT:SYSCALL:PASS
JANOS:BOOT:ELF:PASS
JANOS:BOOT:USER_MODE:PASS
```

Do not use the current `Finish init` message as the completion condition. It is
after the non-returning transition to user mode and therefore cannot represent
a successful boot.

## SMP Matrix

Run the boot test with one, two, and four virtual CPUs:

```text
-smp 1
-smp 2
-smp 4
```

For each run, verify the discovered CPU count, APIC IDs, startup attempts,
online CPU count, absence of startup timeout, interrupt routing, and userspace
syscalls after SMP initialization. The harness should search for markers rather
than compare the complete log because output ordering between CPUs can vary.

## Storage and Filesystem Integration

Generate deterministic FAT16 images during testing. Do not rely on untracked
workspace images. The generator should control image size, volume ID, label,
sector and cluster geometry, timestamps, input contents, and expected hashes.

Suggested images include an empty filesystem, a normal multi-cluster file, a
corrupt FAT chain, and a static JanOS ELF executable.

Storage tests should boot QEMU with AHCI, initialize storage in the test boot
path, and verify:

- PCI and AHCI device discovery.
- Sector reads and writes.
- FAT16 boot-sector parsing.
- Directory lookup and file reads.
- Missing-file and corrupt-chain failures.
- Loading an ELF file from FAT16.
- Starting that ELF as a process and receiving its exit status.

ATA PIO and AHCI polling loops must have bounded timeouts before they can be
tested reliably. An absent or broken device must fail the test instead of
hanging QEMU indefinitely.

## Meson Suites

The eventual suite organization should be:

```text
host:
  calc
  elf-loader
  fat16
  libc

contract:
  launch-contract
  kernel-multiboot2-contract

qemu:
  kernel-boot
  kernel-memory
  interrupt
  process
  syscall
  user-execution
  smp-1
  smp-2
  smp-4
  storage
  fat16-elf
```

Commands:

```sh
meson test -C build --suite host --print-errorlogs
meson test -C build --suite contract --print-errorlogs
meson test -C build --suite qemu --print-errorlogs
meson test -C build --print-errorlogs
```

## Implementation Order

1. Add stable serial markers and guest-controlled QEMU exit status.
2. Add the QEMU host harness and automated full-boot test.
3. Add host ELF-loader tests using fake load operations.
4. Add pure FAT16 host tests and an injectable block device.
5. Add kernel memory self-tests.
6. Add a dedicated syscall userspace application.
7. Add process and real ring-3 execution tests.
8. Generate deterministic FAT16 test images.
9. Add AHCI and ATA storage integration tests with bounded timeouts.
10. Add the one-, two-, and four-CPU SMP matrix.
11. Add ELF loading and execution from FAT16.

The first implementation milestone should be a QEMU test that boots a dedicated
test kernel, emits `JANOS:BOOT:PASS`, exits QEMU through `isa-debug-exit`, and is
reported by `meson test --suite qemu` as a normal passing or failing test.
