# Framebuffer Service

`fbserver` is a userspace framebuffer service loaded from the FAT16 application
disk. It receives the framebuffer user address and metadata, plus a read-only
PSF2 mapping, as startup arguments.
It validates the PSF2 header, renders glyphs, tracks the cursor, and implements
clear, scroll, `putc`, and `puts` through the IPC protocol in
`sdk/include/janos/framebuffer.h`.

The kernel does not render glyphs or own the display memory. A framebuffer
capability stores the physical address, size, pitch, dimensions, depth, and
Multiboot type. Granting the capability maps borrowed physical pages only in
the server page directory. Destroying that mapping unmaps the page-table
entries but never returns the display range to the physical allocator.

The normal `JanOS.iso` contains only the kernel. The accompanying FAT16
application disk contains `calc`, `fbserver`, `fbclient`, and `JANOS.PSF`.
During normal boot the kernel creates the framebuffer capability and loads only
the server from that disk. `fbclient` remains a dedicated test client, and
`JanOS-framebuffer.iso` is the isolated guest image for the framebuffer protocol
test.

The client uses `janos_ipc_call()` and reports success only after validating a
reply for every operation. Calling an unavailable or stale endpoint returns
`-JANOS_EBADF`; the dedicated guest test records this as
`FBCLIENT_UNAVAILABLE_PASS` and the client does not fault.

The kernel starts in a BOOT display session. Kernel diagnostics and process
console writes are captured in the bounded boot-log ring and drained by the
server. Stage 5 atomically queues a kernel-only session update and publishes the
shell as the foreground owner before making the shell runnable. The update
contains an opaque per-handoff token; stale or user-sent session updates are
rejected by `fbserver`.

After handoff, serial remains the primary diagnostic channel. Only the shell and
its descendants are mirrored to the framebuffer, while kernel and unrelated
process output remains serial-only.
