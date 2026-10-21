python = import('python').find_installation('python3', required : true)

test(
  'security-source-invariants',
  python,
  args : [
    files('test-security-invariants.py'),
    join_paths(meson.project_source_root(), 'src', 'rcl-hostname.c'),
    join_paths(meson.project_source_root(), 'slack', 'hostnamectl'),
  ],
)

test(
  'filesystem-and-packaging-invariants',
  python,
  args : [
    files('test-filesystem-packaging-invariants.py'),
    meson.project_source_root(),
  ],
)

test(
  'polkit-integration-script-syntax',
  find_program('sh'),
  args : ['-n', files('test-polkit-integration.sh')],
)

test(
  'gettext-installation-wiring',
  python,
  args : [
    files('test-gettext-config.py'),
    join_paths(meson.project_source_root(), 'src', 'main.c'),
    join_paths(meson.project_source_root(), 'meson.build'),
    join_paths(meson.project_source_root(), 'po', 'meson.build'),
  ],
)
