calc_test = executable(
  'calc-test',
  'calc_test.c',
  calc_logic_sources,
  native: true,
  include_directories: calc_inc,
  c_args: ['-Wall', '-Wextra', '-Werror'],
  override_options: ['c_std=c11'],
)

launch_contract_test = executable(
  'launch-contract-test',
  'launch_contract_test.c',
  native: true,
  include_directories: sdk_inc,
  c_args: ['-Wall', '-Wextra', '-Werror', '-Wno-pointer-to-int-cast'],
  override_options: ['c_std=c11'],
)

scheduler_alternation_test = executable(
  'scheduler-alternation-test',
  'scheduler_alternation_test.c',
  native: true,
  c_args: ['-Wall', '-Wextra', '-Werror'],
  override_options: ['c_std=c11'],
)

ipc_state_test = executable(
  'ipc-state-test',
  'ipc_state_test.c',
  native: true,
  include_directories: sdk_inc,
  c_args: ['-Wall', '-Wextra', '-Werror'],
  override_options: ['c_std=c11'],
)

pingpong_test = executable(
  'pingpong-test',
  'pingpong_test.c',
  native: true,
  include_directories: [sdk_inc, include_directories('../../apps/pingpong')],
  c_args: ['-Wall', '-Wextra', '-Werror'],
  override_options: ['c_std=c11'],
)

framebuffer_protocol_test = executable(
  'framebuffer-protocol-test',
  'framebuffer_protocol_test.c',
  native: true,
  include_directories: sdk_inc,
  c_args: ['-Wall', '-Wextra', '-Werror'],
  override_options: ['c_std=c11'],
)

boot_log_test = executable(
  'boot-log-test',
  'boot_log_test.c',
  '../../kernel/src/core/boot_log.c',
  native: true,
  include_directories: [kernel_inc, libc_inc, sdk_inc, include_directories('.')],
  c_args: ['-Wall', '-Wextra', '-Werror'],
  override_options: ['c_std=c11'],
)

elf_loader_test = executable(
  'elf-loader-test',
  'elf_loader_test.c',
  '../../kernel/src/exec/elf_loader.c',
  native: true,
  include_directories: [kernel_inc, libc_inc, sdk_inc, include_directories('.')],
  c_args: ['-Wall', '-Wextra', '-Werror', '-D__is_kernel', '-ffunction-sections'],
  link_args: ['-Wl,--gc-sections'],
  override_options: ['c_std=c2x'],
)

fat16_test = executable(
  'fat16-test',
  'fat16_test.c',
  '../../kernel/src/fs/fat16.c',
  '../../kernel/src/drivers/storage/storage.c',
  native: true,
  include_directories: [kernel_inc, libc_inc, sdk_inc, include_directories('.')],
  c_args: ['-Wall', '-Wextra', '-Werror', '-D__is_kernel', '-ffunction-sections'],
  link_args: ['-Wl,--gc-sections'],
  override_options: ['c_std=c2x'],
)

memblock_test = executable(
  'memblock-test',
  'memblock_test.c',
  '../../kernel/src/mm/memblock.c',
  native: true,
  include_directories: [kernel_inc, libc_inc, sdk_inc, include_directories('.')],
  c_args: ['-Wall', '-Wextra', '-Werror', '-D__is_kernel', '-ffunction-sections'],
  link_args: ['-Wl,--gc-sections'],
  override_options: ['c_std=c2x'],
)

syscall_registry_test = executable(
  'syscall-registry-test',
  'syscall_registry_test.c',
  '../../kernel/src/syscall/syscall.c',
  native: true,
  include_directories: [kernel_inc, libc_inc, sdk_inc, include_directories('.')],
  c_args: ['-Wall', '-Wextra', '-Werror', '-D__is_kernel', '-ffunction-sections'],
  link_args: ['-Wl,--gc-sections'],
  override_options: ['c_std=c2x'],
)

pci_test = executable(
  'pci-test',
  'pci_test.c',
  '../../kernel/src/drivers/bus/pci.c',
  native: true,
  include_directories: [kernel_inc, libc_inc, sdk_inc, include_directories('.')],
  c_args: ['-Wall', '-Wextra', '-Werror', '-D__is_kernel'],
  override_options: ['c_std=c2x'],
)

libc_test = executable(
  'libc-test',
  'libc_test.c',
  '../../libc/src/string/memcmp.c',
  '../../libc/src/string/memcpy.c',
  '../../libc/src/string/memmove.c',
  '../../libc/src/string/memset.c',
  '../../libc/src/string/strlen.c',
  native: true,
  include_directories: [libc_inc, include_directories('.')],
  c_args: ['-Wall', '-Wextra', '-Werror', '-fno-builtin'],
  override_options: ['c_std=c11'],
)

test('calc', calc_test, suite: 'host')
test(
  'launch-contract',
  launch_contract_test,
  args: [calc_app.full_path()],
  depends: calc_app,
  suite: 'contract',
)
test('scheduler-alternation', scheduler_alternation_test, suite: 'host')
test('ipc-state', ipc_state_test, suite: 'host')
test('pingpong', pingpong_test, suite: 'host')
test('framebuffer-protocol', framebuffer_protocol_test, suite: 'host')
test('boot-log', boot_log_test, suite: 'host')
test('elf-loader', elf_loader_test, suite: 'host')
test('fat16', fat16_test, suite: 'host')
test('memblock', memblock_test, suite: 'host')
test('syscall-registry', syscall_registry_test, suite: 'host')
test('pci', pci_test, suite: 'host')
test('libc', libc_test, suite: 'host')

python_host = find_program('python3', 'python', native: true, required: false)
if python_host.found()
  test(
    'qemu-harness',
    python_host,
    args: [files('../integration/qemu_harness_test.py')],
    suite: 'host',
  )
endif
