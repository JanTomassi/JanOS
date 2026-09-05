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

Every test is registered with Meson. The current suites contain:

- `host:calc`: calculator parsing, arithmetic, overflow, and invalid input.
- `host:framebuffer-protocol`: framebuffer message sizes, IDs, and stable error
  values.
- `host:boot-log` and `host:display-session`: ring ordering, overwrite behavior,
  session ownership, token validation, and release rules.
- `host:elf-loader`: real ELF segment validation and loading through injected
  map, copy, zero, unmap, and protection callbacks.
- `host:fat16`: real FAT16 layout, directory, file, partial-read, allocation
  failure, injected block-device, and cyclic-chain coverage.
- `host:memblock`: real memblock range, reservation, allocation, splitting,
  alignment, and overflow coverage.
- `host:syscall-registry`: real syscall registration, dispatch, errors, and
  blocked-result behavior.
- `host:pci`: memory-backed PCI configuration-space reads and writes, register
  lane extraction, device decoding, BARs, class matching, multifunction
  enumeration, full bus traversal, and bus-master enablement.
- `host:libc`: native freestanding string-library behavior, including overlap
  and boundary cases.
- `host:qemu-harness`: host-only success, missing-marker, wrong-exit, and
  timeout checks for the QEMU harness.
- `host:scheduler-alternation`, `host:ipc-state`, and `host:pingpong`: pure
  protocol/model tests for behavior not yet separable from privileged kernel
  state.
- `contract:launch-contract`: validates the generated user ELF format and
  entry point.
- `contract:kernel-multiboot2-contract`: verifies that GRUB recognizes the
  kernel as Multiboot2.
- `qemu:kernel-selftest-1cpu`, `qemu:kernel-selftest-2cpu`, and
  `qemu:kernel-selftest-4cpu`: boot the real test kernel and exercise physical
  and virtual allocation, process creation, page-directory isolation, user
  mapping permissions, cross-page-safe copies, syscall dispatch, and process
  cleanup.
- `qemu:pingpong-guest`, `qemu:pingpong-guest-4cpu`,
  `qemu:framebuffer-guest`, and `qemu:shell-guest`: userspace integration
  scenarios using the real disk and services. The shell scenario also covers
  authorized framebuffer handoff, foreground calculator input, explicit
  calculator exit, one-shot execution, and absence of blocked or zombie
  calculators after reaping.

Run them with:

```sh
meson test -C build --print-errorlogs
meson test -C build --suite host --print-errorlogs
meson test -C build --suite contract --print-errorlogs
meson test -C build --suite qemu --print-errorlogs
```

The host and contract suites do not require QEMU. QEMU tests are omitted when
the `qemu` feature is disabled or its native dependencies are unavailable.

## Test Layout

The implemented test code is organized as:

```text
tests/
├── host/
│   ├── test.h
│   ├── calc_test.c
│   ├── boot_log_test.c
│   ├── display_session_test.c
│   ├── elf_loader_test.c
│   ├── fat16_test.c
│   ├── memblock_test.c
│   ├── pci_test.c
│   ├── syscall_registry_test.c
│   ├── libc_test.c
│   └── meson.build
├── kernel/
│   ├── test_runner.c
│   ├── test_entry.c
│   └── test_exit.c
├── guest/
│   ├── pingpong_qemu.sh
│   ├── framebuffer_qemu.sh
│   └── shell_qemu.sh
├── integration/
│   ├── qemu_test.py
│   ├── qemu_harness_test.py
│   └── meson.build
└── images/                 # deterministic fixtures are added here as needed
```

## Adding A Test

Use the lowest layer that exercises the behavior under test:

1. Put deterministic logic with no CPU or device dependency in a native test.
2. Give storage, filesystem, and loader code an injected callback or context
   and use an in-memory fake in the native test.
3. Use a kernel self-test when the production behavior requires physical
   addresses, page tables, interrupt state, privilege transitions, or real
   scheduler state.
4. Use a full QEMU integration test only when several initialized subsystems or
   real userspace programs must interact.

For a new native test, add one executable and one `test()` entry to
`tests/host/meson.build`. Link the smallest production source set possible and
inject external operations through callbacks. A fake should record calls,
arguments, ownership, and failure injection rather than reimplementing the
production algorithm.

For a new deterministic kernel test, add a named case to
`tests/kernel/test_runner.c`, emit `JANOS:TEST:<NAME>:PASS` or
`JANOS:TEST:<NAME>:FAIL`, and finish with `kernel_test_finish(0)` or
`kernel_test_finish(1)`. Register the case in
`tests/integration/meson.build` with an explicit CPU count, timeout, expected
markers, and expected exit status.

Every test must have:

- At least one success assertion and one meaningful failure-path assertion.
- A bounded timeout for external processes and hardware polling.
- Stable machine-readable results separate from diagnostic output.
- Cleanup checks for resources allocated before a later failure.
- A direct `meson test` command documented beside the implementation.

Useful commands during development are:

```sh
meson compile -C build
meson test -C build --suite host --print-errorlogs
meson test -C build --suite contract --print-errorlogs
meson test -C build --suite qemu --print-errorlogs
meson test -C build --print-errorlogs
meson test -C build kernel-selftest-2cpu --print-errorlogs
meson test -C build --list
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

JanOS implements this seam with `block_device.read_fn` and
`block_device.context`; hardware devices leave the callbacks unset and use
their normal backend. The native FAT16 test supplies a memory-backed callback
and a small allocator fake, so no AHCI, ATA, or physical-memory setup is needed.

## Kernel Self-Tests

The `kernel_tests` Meson option controls a dedicated test kernel and defaults to
enabled:

```meson
option(
  'kernel_tests',
  type: 'boolean',
  value: true,
  description: 'Build the dedicated kernel test image',
)
```

When enabled, `tests/kernel` is linked into `JanOS.test.kernel`. The `selftest`
Multiboot command runs after memory, interrupts, and process infrastructure are
initialized, but before `i386_context_enter_user()`.

The self-test emits stable markers and exits through QEMU's
`isa-debug-exit` device. A missing marker, panic, abnormal QEMU exit, or timeout
is a failure; a host-side process kill is never interpreted as success.

The current kernel self-test covers:

- Physical-page allocation and release.
- Page-aligned virtual allocation, access, and release.
- Independent process page directories and address-space IDs.
- Parent-child relationships and process snapshots.
- User mapping permission validation and protection changes.
- Safe copies into and out of an inactive process address space.
- Borrowed physical mappings and teardown.
- Process and address-space cleanup.

It runs with one, two, and four virtual CPUs through the
`kernel-selftest-1cpu`, `kernel-selftest-2cpu`, and `kernel-selftest-4cpu`
Meson tests.

The pingpong integration test follows the same separation: `JanOS.iso` is the
normal boot image, while `JanOS-pingpong.iso` uses the test kernel and loads the
`PINGPONG` application from the accompanying FAT16 disk.

The framebuffer integration test is separate again: `JanOS-framebuffer.iso`
uses the test kernel and loads `FBSERVER`, `FBCLIENT`, and `JANOS.PSF` from the
accompanying FAT16 disk. Normal `JanOS.iso` does not contain application or
font modules. The guest requires `FBSERVER_READY`, `FBCLIENT_PASS`,
`FBCLIENT_UNAVAILABLE_PASS`, and a distinct-page-directory `FBTEST_SPACE`
marker.

Each assertion reports a stable serial marker:

```text
JANOS:TEST:MEMORY:PASS
JANOS:TEST:SYSCALL:PASS
JANOS:TEST:PROCESS:PASS
JANOS:TEST:BOOT:PASS
JANOS:TEST:EXIT:0
```

Normal debug messages must not be used as test contracts.

### Memory Management

The native `memblock` test covers overlapping and reserved ranges, alignment,
bottom-up and top-down allocation, freeing, reuse, invalid ranges, splitting,
and integer overflow. Extend it with a synthetic Multiboot image rather than
booting QEMU when the behavior does not require page tables.

The QEMU memory test covers physical and virtual allocation because those
implementations use real physical addresses, recursive page tables, and CPU
instructions. Add allocator cases there when a native fake would no longer test
the production implementation.

The QEMU process test currently covers these virtual-memory cases:

- Page-directory creation and destruction.
- Mapping and unmapping.
- User, writable, and executable permissions.
- Protection changes and address translation.
- Virtual range splitting, reuse, and coalescing.
- Cross-page user copies.
- User mapping teardown and borrowed-page mappings.

Remaining virtual-memory cases to add are:

- Invalid and higher-half user addresses.
- Exhaustion and invalid frees.

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

## PCI Bus Driver

PCI devices are identified by a bus, device, and function address, commonly
written as `bus:device.function`. Each function exposes a 256-byte
configuration space. JanOS accesses it through the x86 `0xCF8` address port and
`0xCFC` data port: the address selects a dword, and word or byte helpers select
a lane within that dword.

The PCI host test replaces those privileged port operations with a
little-endian, memory-backed configuration space. Its fake devices demonstrate:

- Vendor ID `0xffff` means that no function exists at an address.
- Class, subclass, and programming-interface bytes identify a driver-compatible
  device. A programming interface of `0xff` means "any interface".
- Header-type bit 7 means that function numbers 1 through 7 must also be
  enumerated; otherwise only function zero belongs to the device.
- BARs at offsets `0x10` through `0x24` tell a driver where its device registers
  are mapped. The test verifies that all six values are copied into the
  descriptor.
- Command-register bit 1 enables memory-space access and bit 2 enables bus
  mastering. The driver preserves the status half of the command/status dword
  while setting those bits.

Read the comments in `tests/host/pci_test.c` alongside the fake configuration
space to follow each step without needing real PCI hardware.

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

Deterministic tests use `tests/integration/qemu_test.py`. It starts QEMU in a
new process group, captures the complete serial stream, applies a monotonic
timeout, terminates the complete process group on timeout, and prints the full
serial log on failure. It rejects normal exits, signals, timeouts, missing
markers, and known panic/bug markers.

The QEMU command includes:

```text
-display none
-serial stdio
-monitor none
-no-reboot
-device isa-debug-exit,iobase=0xf4,iosize=0x04
```

The test kernel writes a status to port `0xf4` and then halts. QEMU returns the
guest status encoded as `(status << 1) | 1`; the harness decodes it and requires
the expected value. A timeout is always a failure; it must not be interpreted as
a successful test.

The QEMU harness should:

1. Start QEMU with an explicit timeout and CPU count.
2. Capture serial output.
3. Wait for the guest-controlled QEMU exit.
4. Decode and validate the exit status.
5. Require expected pass markers.
6. Print the complete serial log when a test fails.

Register a test in the `qemu` suite by passing the harness and its expectations:

```meson
test(
  'kernel-selftest-2cpu',
  python,
  args: [qemu_test_harness,
    '--qemu', qemu,
    '--iso', janos_kernel_test_iso,
    '--smp', '2',
    '--expect', 'JANOS:TEST:MEMORY:PASS',
    '--expect', 'JANOS:TEST:PROCESS:PASS',
    '--expect-exit', '0'],
  depends: janos_kernel_test_iso,
  timeout: 40,
  is_parallel: false,
  suite: 'qemu',
)
```

## Full Boot Test

The current self-test completion markers are:

```text
JANOS:TEST:MEMORY:PASS
JANOS:TEST:PROCESS:PASS
JANOS:TEST:BOOT:PASS
JANOS:TEST:EXIT:0
```

The detailed boot log remains diagnostic output. Do not use `Finish init` or a
serial substring alone as the completion condition; completion requires the
guest-controlled exit status and every expected marker.

## SMP Matrix

The implemented kernel self-test runs with one, two, and four virtual CPUs:

```text
-smp 1
-smp 2
-smp 4
```

For each run, the harness checks the self-test markers and exit status. Future
SMP and userspace tests should additionally verify discovered and online CPU
counts, APIC IDs, startup attempts, interrupt routing, and userspace syscalls.
Search for stable markers rather than comparing the complete log because output
ordering between CPUs can vary.

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

The current suite organization is:

```text
host:
  calc
  elf-loader
  fat16
  framebuffer-protocol
  ipc-state
  libc
  memblock
  pci
  pingpong
  qemu-harness
  scheduler-alternation
  syscall-registry

contract:
  launch-contract
  kernel-multiboot2-contract

qemu:
  kernel-selftest-1cpu
  kernel-selftest-2cpu
  kernel-selftest-4cpu
  pingpong-guest
  pingpong-guest-4cpu
  framebuffer-guest
  shell-guest
```

Commands:

```sh
meson test -C build --suite host --print-errorlogs
meson test -C build --suite contract --print-errorlogs
meson test -C build --suite qemu --print-errorlogs
meson test -C build --print-errorlogs
```

## Implementation Order

Implemented milestones:

1. Stable kernel test markers and guest-controlled QEMU exit status.
2. Reusable Python QEMU harness with process-group cleanup and failure logs.
3. Native ELF-loader tests using injected load operations.
4. Native FAT16 tests using an injected block device and allocator fake.
5. Native memblock, syscall-registry, and libc tests.
6. Kernel memory, syscall, process, and address-space self-tests.
7. One-, two-, and four-CPU kernel self-test matrix.
8. Existing pingpong, framebuffer, and shell integration tests registered in the
   `qemu` suite.

Remaining milestones:

1. Add dedicated interrupt, fault, syscall-user, and process-lifecycle guests.
2. Add deterministic FAT16 fixture generation and storage failure tests.
3. Add bounded AHCI, ATA, and LAPIC polling timeouts before testing failures.
4. Add ELF loading and execution tests from deterministic FAT16 images.
5. Add CI jobs for host, contract, and QEMU suites.
