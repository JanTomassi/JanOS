fbserver_app = executable(
  'fbserver',
  files('main.c'),
  crt0,
  include_directories: [libc_inc, sdk_inc],
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
