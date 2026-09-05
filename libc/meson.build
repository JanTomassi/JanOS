libc_inc = include_directories('include')

freestanding_sources = files(
  'src/stdlib/abort.c',
  'src/string/memcmp.c',
  'src/string/memcpy.c',
  'src/string/memmove.c',
  'src/string/memset.c',
  'src/string/strlen.c',
)

libk = static_library(
  'k',
  freestanding_sources,
  include_directories: [libc_inc, sdk_inc],
  c_args: ['-ffreestanding', '-D__is_libc', '-D__is_libk'],
  pic: false,
  install: true,
  override_options: ['c_std=gnu11'],
)

user_libc = static_library(
  'c',
  freestanding_sources + files('src/unistd/syscall.c', 'src/stdio/puts.c'),
  include_directories: [libc_inc, sdk_inc],
  c_args: ['-ffreestanding', '-D__is_libc'],
  pic: false,
  install: true,
  override_options: ['c_std=gnu11'],
)

crt0 = custom_target(
  'crt0',
  input: 'crt/i386/crt0.s',
  output: 'crt0.o',
  command: [nasm, '-f', 'elf32'] + nasm_debug_args +
    ['@INPUT@', '-o', '@OUTPUT@'],
  install: true,
  install_dir: get_option('libdir') / 'janos',
)

install_subdir('include', install_dir: get_option('includedir'), strip_directory: true)
install_data('linker/i386/user.ld', install_dir: get_option('libdir') / 'janos')
