# Console

Kernel diagnostics always use the serial display. Kernel init does not render
glyphs itself; it starts the userspace framebuffer service when the framebuffer
and its FAT16 application files are available. PS/2 line editing still feeds
`console_read()` for the calculator process.

The foreground user process is the existing `calc` application. It provides
`help`, `info`, `cpu`, and `calc <expression>` commands, while preserving
direct expression input.

This is intentionally an MVP shell. `fbserver` owns the framebuffer and keeps
display rendering outside the kernel, while serial remains the independent
diagnostic channel.
