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

test('calc', calc_test, suite: 'host')
test(
  'launch-contract',
  launch_contract_test,
  args: [calc_app.full_path()],
  depends: calc_app,
  suite: 'host',
)
