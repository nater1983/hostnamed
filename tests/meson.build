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
