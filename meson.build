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
  input: [calc_app, pingpong_app, fbserver_app, fbclient_app, procserv_app,
    input_app, shell_app,
    files('data/janos.psf2')],
  output: 'calc-disk.img',
  command: [sh, mkdisk, '@OUTPUT@', '@INPUT0@', '@INPUT1@', '@INPUT2@',
    '@INPUT3@', '@INPUT4@', '@INPUT5@', '@INPUT6@', '@INPUT7@'],
  build_by_default: false,
)

mktags = find_program('tools/mktags.sh', native: true)
run_target('tags', command: [sh, mktags])

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
  iso_script = files('tools/mkiso-disk.sh')
  grub_config = files('data/grub.cfg')
  janos_iso = custom_target(
    'iso',
      input: [janos_kernel, grub_config],
      output: 'JanOS.iso',
      command: [sh, iso_script, grub_mkrescue, '@OUTPUT@', '@INPUT0@', '@INPUT1@'],
      build_by_default: false,
  )
  pingpong_grub_config = files('data/grub-pingpong.cfg')
  janos_pingpong_iso = custom_target(
    'pingpong-iso',
    input: [janos_test_kernel, pingpong_grub_config],
    output: 'JanOS-pingpong.iso',
    command: [sh, iso_script, grub_mkrescue, '@OUTPUT@', '@INPUT0@', '@INPUT1@'],
    build_by_default: false,
  )
  framebuffer_grub_config = files('data/grub-framebuffer.cfg')
  janos_framebuffer_iso = custom_target(
    'framebuffer-iso',
    input: [janos_test_kernel, framebuffer_grub_config],
    output: 'JanOS-framebuffer.iso',
    command: [sh, iso_script, grub_mkrescue, '@OUTPUT@', '@INPUT0@', '@INPUT1@'],
    build_by_default: false,
  )

  qemu_option = get_option('qemu')
  qemu = find_program('qemu-system-i386', native: true, required: qemu_option)
  if qemu.found()
    qemu_script = files('tools/run-qemu.sh')
    run_disk = get_option('qemu_disk')
    if run_disk == ''
      run_disk = calc_disk.full_path()
    endif
    sata_disk1 = get_option('qemu_sata_disk1')
    if sata_disk1 == ''
      sata_disk1 = calc_disk.full_path()
    endif
    sata_disk2 = get_option('qemu_sata_disk2')
    run_target('run', command: [sh, qemu_script, qemu, janos_iso, 'run', run_disk],
      depends: [calc_disk])
    run_target('debug', command: [sh, qemu_script, qemu, janos_iso, 'debug', run_disk],
      depends: [calc_disk])
    run_target(
      'run-sata',
      command: [sh, qemu_script, qemu, janos_iso, 'sata',
        sata_disk1, sata_disk2],
      depends: [calc_disk],
    )
    run_target(
      'debug-sata',
      command: [sh, qemu_script, qemu, janos_iso, 'debug-sata',
        sata_disk1, sata_disk2],
      depends: [calc_disk],
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
    pingpong_guest_test = find_program('tests/guest/pingpong_qemu.sh', native: true)
    test(
      'pingpong-guest',
      pingpong_guest_test,
      args: [qemu, janos_pingpong_iso, calc_disk.full_path()],
      depends: [janos_pingpong_iso, calc_disk],
      timeout: 30,
      is_parallel: false,
      suite: 'guest',
    )
    test(
      'pingpong-disk-guest',
      pingpong_guest_test,
      args: [qemu, janos_pingpong_iso, calc_disk.full_path()],
      depends: [janos_pingpong_iso, calc_disk],
      timeout: 30,
      is_parallel: false,
      suite: 'guest',
    )
    test(
      'pingpong-guest-4cpu',
      pingpong_guest_test,
      args: [qemu, janos_pingpong_iso, calc_disk.full_path()],
      depends: [janos_pingpong_iso, calc_disk],
      env: ['JANOS_QEMU_SMP=4'],
      timeout: 30,
      is_parallel: false,
      suite: 'guest',
    )
    framebuffer_guest_test = files('tests/guest/framebuffer_qemu.sh')
    test(
      'framebuffer-guest',
      sh,
      args: [framebuffer_guest_test, qemu, janos_framebuffer_iso, calc_disk.full_path()],
      depends: [janos_framebuffer_iso, calc_disk],
      timeout: 40,
      is_parallel: false,
      suite: 'guest',
    )
    shell_guest_test = files('tests/guest/shell_qemu.sh')
    test(
      'shell-guest',
      sh,
      args: [shell_guest_test, qemu, janos_iso, calc_disk.full_path()],
      depends: [janos_iso, calc_disk],
      timeout: 60,
      is_parallel: false,
      suite: 'guest',
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
