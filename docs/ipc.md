# IPC

The initial IPC ABI is in `sdk/include/janos/syscall.h`. Messages have a fixed
20-byte header and a 64-byte payload. Endpoint queues hold at most eight
messages and endpoint handles contain a slot and generation, so stale handles
are rejected.

`endpoint_create` creates an endpoint owned by the calling process. Any process
with a granted endpoint capability may use it. Send, receive, reply, and notify
rights are checked independently; only the owner may grant or revoke rights.
Endpoint queues are bounded at eight messages and are reserved for IPC traffic;
kernel framebuffer output uses a separate bounded console buffer.
The owner may close the endpoint. The kernel overwrites the sender field on
requests and notifications; user input cannot spoof it. Replies validate the
endpoint owner and outstanding request ID, then retire the request token.
Message flags are mutually exclusive and malformed combinations are rejected.

`JANOS_SYS_IPC_GRANT` takes an endpoint handle in `ebx`, a target PID in `ecx`,
and a rights mask in `edx`. `JANOS_SYS_IPC_CANCEL` takes the endpoint and
request ID and may be used by the request sender or endpoint owner. Close uses
the endpoint handle in `ebx` and revokes the endpoint and its capabilities.

A zero timeout is nonblocking. A nonzero receive timeout installs a
kernel-owned pending syscall continuation, blocks the process, and completes
with the received message or `EAGAIN` when the scheduler deadline expires. A
nonzero send timeout makes a successfully queued request synchronous: the
client remains blocked until the owner replies or the deadline expires. A full
queue blocks the sender with a kernel-owned copy and retries when the receiver
frees a slot. Reply completion restores the client's saved syscall context.
Process and endpoint cleanup revoke pending operations and wake affected
waiters.

## Framebuffer Service

Stage 4 keeps framebuffer ownership out of the kernel display path. The kernel
creates a framebuffer memory capability from the Multiboot metadata and grants
it to the framebuffer server. Granting maps the physical display range as
borrowed pages in the server's address space; the capability never frees those
physical pages. The mapping is revoked when the owning process exits. The PSF2
file is loaded from the FAT16 application disk and mapped read-only only into
the server.

`sdk/include/janos/framebuffer.h` defines the stable request types for `putc`,
`puts`, cursor movement, clear, and scroll. Each request receives a fixed IPC
reply containing a status code. `janos_ipc_call()` is the synchronous IPC
operation used by clients when they need that reply; its request, reply, and
timeout fit the existing i386 register ABI while message payloads remain at 64
bytes.

An unavailable or stale endpoint returns `-JANOS_EBADF` without entering the
server or dereferencing a user pointer. The framebuffer client treats that as
the stable unavailable-server result and remains alive rather than crashing.
