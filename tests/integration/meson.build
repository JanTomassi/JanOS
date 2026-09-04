qemu_test_harness = files('qemu_test.py')
python = find_program('python3', 'python', native: true, required: false)

if python.found()
  if get_option('kernel_tests')
    foreach cpu_count : [1, 2, 4]
      selftest_args = [
        qemu_test_harness,
        '--qemu', qemu,
        '--iso', janos_kernel_test_iso,
        '--smp', cpu_count.to_string(),
        '--timeout', '30',
        '--label', 'kernel-selftest-' + cpu_count.to_string() + 'cpu',
        '--expect', 'JANOS:TEST:MEMORY:PASS',
        '--expect', 'JANOS:TEST:SYSCALL:PASS',
        '--expect', 'JANOS:TEST:PROCESS:PASS',
        '--expect', 'JANOS:TEST:BOOT:PASS',
        '--expect', 'JANOS:TEST:EXIT:0',
        '--expect-exit', '0',
      ]
      test(
        'kernel-selftest-' + cpu_count.to_string() + 'cpu',
        python,
        args: selftest_args,
        depends: [janos_kernel_test_iso],
        timeout: 40,
        is_parallel: false,
        suite: 'qemu',
      )
    endforeach
  endif
else
  warning('Python is unavailable; deterministic QEMU tests are disabled')
endif

if get_option('kernel_tests')
  pingpong_guest_test = files('../guest/pingpong_qemu.sh')
  test(
    'pingpong-guest',
    sh,
    args: [pingpong_guest_test, qemu, janos_pingpong_iso, calc_disk.full_path()],
    depends: [janos_pingpong_iso, calc_disk],
    timeout: 30,
    is_parallel: false,
    suite: 'qemu',
  )
  test(
    'pingpong-guest-4cpu',
    sh,
    args: [pingpong_guest_test, qemu, janos_pingpong_iso, calc_disk.full_path()],
    depends: [janos_pingpong_iso, calc_disk],
    env: ['JANOS_QEMU_SMP=4'],
    timeout: 30,
    is_parallel: false,
    suite: 'qemu',
  )
  framebuffer_guest_test = files('../guest/framebuffer_qemu.sh')
  test(
    'framebuffer-guest',
    sh,
    args: [framebuffer_guest_test, qemu, janos_framebuffer_iso, calc_disk.full_path()],
    depends: [janos_framebuffer_iso, calc_disk],
    timeout: 40,
    is_parallel: false,
    suite: 'qemu',
  )
endif

shell_guest_test = files('../guest/shell_qemu.sh')
test(
  'shell-guest',
  sh,
  args: [shell_guest_test, qemu, janos_iso, calc_disk.full_path()],
  depends: [janos_iso, calc_disk],
  timeout: 60,
  is_parallel: false,
  suite: 'qemu',
)
