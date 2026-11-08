python = import('python').find_installation('python3', required : true)

test(
  'security-source-invariants',
  python,
  args : [
    files('test-security-invariants.py'),
    join_paths(meson.project_source_root(), 'src', 'rcl-hostname.c'),
    join_paths(meson.project_source_root(), 'slack', 'hostnamectl'),
    join_paths(meson.project_source_root(), 'data', 'org.freedesktop.hostname1.xml'),
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

test(
  'hardware-hostname-fallbacks',
  python,
  args : [
    files('test-hardware-hostname-fallbacks.py'),
    join_paths(meson.project_source_root(), 'src', 'rcl-hostname.c'),
  ],
)

test(
  'daemon-lifecycle-invariants',
  python,
  args : [
    files('test-daemon-lifecycle.py'),
    join_paths(meson.project_source_root(), 'src', 'main.c'),
  ],
)

test(
  'configuration-parsing-invariants',
  python,
  args : [
    files('test-config-parsing.py'),
    join_paths(meson.project_source_root(), 'src', 'rcl-hostname.c'),
  ],
)

test(
  'configuration-monitoring',
  python,
  args : [
    files('test-config-monitoring.py'),
    join_paths(meson.project_source_root(), 'src', 'main.c'),
    join_paths(meson.project_source_root(), 'src', 'rcl-hostname.c'),
  ],
)

# Runs only against a temporary private bus, never the real system bus.
dbus_daemon = find_program('dbus-daemon', required : false)
gdbus = find_program('gdbus', required : false)
if dbus_daemon.found() and gdbus.found()
  test(
    'dbus-reconnect-private-bus',
    python,
    args : [
      files('test-dbus-reconnect.py'),
      hostnamed_exe,
    ],
    timeout : 75,
    suite : 'integration',
  )
  test(
    'dbus-shutdown-private-bus',
    python,
    args : [
      files('test-dbus-shutdown.py'),
      hostnamed_exe,
    ],
    timeout : 60,
    suite : 'integration',
  )
endif
