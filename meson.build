project(
  'JanOS',
  'c',
  version: '0.1.0',
  meson_version: '>=1.3.0',
  default_options: [
    'buildtype=debug',
    'c_std=c2x',
    'default_library=static',
    'warning_level=2',
    'b_pie=false',
    'b_staticpic=false',
  ],
)

if not meson.is_cross_build()
  error('Configure JanOS with --cross-file config/i686-elf.ini')
endif

if host_machine.cpu_family() != 'x86'
  error('JanOS currently supports only 32-bit x86')
endif

target_cc = meson.get_compiler('c')
cross_cc = find_program(meson.get_external_property('compiler'), native: true)
libgcc_result = run_command(cross_cc, '-print-libgcc-file-name', check: true)
libgcc = declare_dependency(link_args: [libgcc_result.stdout().strip()])
nasm = find_program('nasm', native: true)
sh = find_program('sh', native: true)
sdk_inc = include_directories('sdk/include')

nasm_objects = generator(
  nasm,
  output: '@BASENAME@.o',
  arguments: ['-f', 'elf32', '-g', '@INPUT@', '-o', '@OUTPUT@'],
)

subdir('libc')
subdir('kernel')
subdir('apps')

mkdisk = find_program('tools/mkdisk.sh', native: true)
calc_disk = custom_target(
  'calc-disk',
  input: [calc_app, files('data/janos.psf2')],
  output: 'calc-disk.img',
  command: [sh, mkdisk, '@OUTPUT@', '@INPUT0@', '@INPUT1@'],
  build_by_default: false,
)

if get_option('host_tests')
  subdir('tests/host')
endif

install_subdir('sdk/include', install_dir: get_option('includedir'), strip_directory: true)
install_data(
  'tools/janos-cc',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

iso_option = get_option('iso')
grub_mkrescue = find_program('grub-mkrescue', native: true, required: iso_option)

if grub_mkrescue.found()
  iso_script = files('tools/mkiso.sh')
  grub_config = files('data/grub.cfg')
  janos_iso = custom_target(
    'iso',
    input: [janos_kernel, calc_app, grub_config],
    output: 'JanOS.iso',
    command: [sh, iso_script, grub_mkrescue, '@OUTPUT@', '@INPUT0@', '@INPUT1@', '@INPUT2@'],
    build_by_default: false,
  )

  qemu_option = get_option('qemu')
  qemu = find_program('qemu-system-i386', native: true, required: qemu_option)
  if qemu.found()
    qemu_script = files('tools/run-qemu.sh')
    run_target('run', command: [sh, qemu_script, qemu, janos_iso, 'run', get_option('qemu_disk')])
    run_target('debug', command: [sh, qemu_script, qemu, janos_iso, 'debug', get_option('qemu_disk')])
    run_target(
      'run-sata',
      command: [sh, qemu_script, qemu, janos_iso, 'sata',
        get_option('qemu_sata_disk1'), get_option('qemu_sata_disk2')],
    )
    run_target(
      'debug-sata',
      command: [sh, qemu_script, qemu, janos_iso, 'debug-sata',
        get_option('qemu_sata_disk1'), get_option('qemu_sata_disk2')],
    )
    run_target(
      'run-disk',
      command: [sh, qemu_script, qemu, janos_iso, 'sata', calc_disk.full_path(), ''],
      depends: [calc_disk],
    )
    run_target(
      'debug-disk',
      command: [sh, qemu_script, qemu, janos_iso, 'debug-sata', calc_disk.full_path(), ''],
      depends: [calc_disk],
    )
  endif
endif

grub_file = find_program('grub-file', native: true, required: false)
if grub_file.found()
  test(
    'kernel-multiboot2-contract',
    grub_file,
    args: ['--is-x86-multiboot2', janos_kernel],
    suite: 'contract',
  )
endif
