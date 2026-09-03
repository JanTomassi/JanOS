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

test('calc', calc_test, suite: 'host')
test(
  'launch-contract',
  launch_contract_test,
  args: [calc_app.full_path()],
  depends: calc_app,
  suite: 'host',
)
test('scheduler-alternation', scheduler_alternation_test, suite: 'host')
test('ipc-state', ipc_state_test, suite: 'host')
test('pingpong', pingpong_test, suite: 'host')
test('framebuffer-protocol', framebuffer_protocol_test, suite: 'host')
