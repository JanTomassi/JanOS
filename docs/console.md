# Console

Kernel diagnostics always use the serial display. Kernel init does not render
glyphs itself; it starts the userspace framebuffer service when the framebuffer
and its FAT16 application files are available. The PS/2 driver only forwards raw
scan bytes to the userspace input server; the input server decodes keys and
sends logical key notifications to the shell.

The userspace shell owns line editing, command history, and command dispatch.
Its commands are `help`, `ps`, `info`, `font`, `calc`, `clear`, and `exit`.
`procserv` provides process snapshots and calculator spawning over IPC. A
calculator started without an expression remains blocked on the legacy stdin
path, which does not block the shell.

`fbserver` owns the framebuffer and keeps display rendering outside the kernel,
while serial remains the independent diagnostic channel.
