calc_inc = include_directories('.')
calc_logic_sources = files('calc.c')
user_linker_script = meson.project_source_root() / 'libc/linker/i386/user.ld'

calc_app = executable(
  'calc',
  calc_logic_sources,
  files('main.c'),
  crt0,
  include_directories: [calc_inc, libc_inc, sdk_inc],
  link_with: user_libc,
  dependencies: libgcc,
  c_args: [
    '-m32',
    '-ffreestanding',
    '-fno-stack-protector',
    '-fno-pie',
    '-fno-asynchronous-unwind-tables',
  ],
  link_args: ['-nostdlib', '-no-pie', '-Wl,-T,' + user_linker_script],
  link_depends: files('../../libc/linker/i386/user.ld'),
  install: true,
  install_dir: get_option('bindir'),
)
