# Console

JanOS requests an 800x400x32 Multiboot framebuffer and registers it when GRUB
returns a supported RGB 24- or 32-bit mode. The console uses the existing
`display_t` interface and MMIO mapping; serial remains available as a display
but is no longer selected when the framebuffer is valid. PS/2 line editing
feeds the active console through `console_read()`.

The first user process is the existing `calc` application. It provides
`help`, `info`, and `calc <expression>` commands, while preserving direct
expression input.

This is intentionally an MVP shell. The current process API permits only one
active process and `process_start()` can be called only before entering that
process. There is no scheduler or shell-side process creation yet, so commands
are handled inside `calc` rather than launching separate programs. The
framebuffer font is a small built-in ASCII subset; unsupported punctuation is
rendered as blank.
