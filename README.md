# hostnamed

A lightweight implementation of the `org.freedesktop.hostname1` D-Bus service
(`hostnamed`), following the same daemon pattern as `timedated`.

## Dependencies

| Library | pkg-config name | Minimum version |
|---|---|---|
| GLib / GIO | `glib-2.0`, `gio-2.0`, `gio-unix-2.0` | 2.56 |
| polkit | `polkit-gobject-1` | 0.113 |
| D-Bus | `dbus-1` | any recent |

polkit is required; there is no build option to disable it. The daemon is
activated by dbus-daemon directly via D-Bus service activation
(`[D-BUS Service]`). There is no systemd dependency.

On Debian/Ubuntu:
```sh
apt install libglib2.0-dev libpolkit-gobject-1-dev libdbus-1-dev \
            meson ninja-build pkg-config
```

On Fedora/RHEL:
```sh
dnf install glib2-devel polkit-devel dbus-devel \
            meson ninja-build pkg-config
```

## Building

```sh
meson setup build
ninja -C build
```

Common configure options:

```sh
# Install under /usr instead of /usr/local
meson setup build --prefix=/usr

# Override the D-Bus service directory explicitly
meson setup build -Ddbus_system_service_dir=/usr/share/dbus-1/system-services

# Use a different privileged group for passwordless polkit access
meson setup build -Dprivileged-group=sudo
```

All options (see `meson_options.txt`):

| Option | Default | Purpose |
|---|---|---|
| `dbus_system_service_dir` | `<datadir>/dbus-1/system-services` | D-Bus service activation file |
| `dbus_system_conf_dir` | `<datadir>/dbus-1/system.d` | D-Bus security policy |
| `polkit_policy_dir` | polkit `actiondir` | polkit `.policy` files |
| `hostname_conf` | `/etc/HOSTNAME` | hostname file |
| `os_conf` | `/etc/os-release` | os-release file |
| `machine_conf` | `/etc/machine-info` | machine-info file |
| `privileged-group` | `wheel` | group allowed to change hostname without authentication |
| `tests` | `true` | build and register the test suite (`meson test`) |

The daemon binary is installed to `libexecdir`, which is where the D-Bus
service file points.

## Testing

```sh
meson test -C build --print-errorlogs
```

This runs the security-source and filesystem/packaging invariant checks. The
staged-install check is run by hand. `tests/test-runtime-integration.py` is an
opt-in live test: run it only in a disposable VM with hostnamed installed.

```sh
meson install -C build --destdir "$PWD/stage"
python3 tests/test-install-layout.py "$PWD/stage"
```

CI (`.github/workflows/build.yml`) runs the build, `meson test`, syntax checks
for the harnesses and `slack/hostnamectl`, XML validation, and the staged
install check on every push and pull request.

## Installing

```sh
ninja -C build install          # installs to prefix (default /usr/local)
sudo ninja -C build install     # for system-wide install
```

## Project layout

```
.
├── meson.build           # top-level build definition
├── meson_options.txt     # user-settable options (-D flags)
├── AUTHORS, COPYING      # authors, GPLv2 license
├── ChangeLog.md          # commit-level history
├── BUGS-NEWS             # user-visible release notes and known issues
├── hostnamed.SlackBuild  # builds a Slackware package
├── slack/                # slack-desc and the hostnamectl helper script
├── man/                  # hostnamed(8), hostnamectl(1)
├── tests/                # security, packaging, install-layout and live runtime checks
├── .github/workflows/    # CI: build, meson test, XML validation, staged install
├── src/
│   ├── meson.build       # compiles the hostnamed binary
│   ├── main.c            # GMainLoop, D-Bus name ownership, inotify watches
│   ├── rcl-hostname.h    # public interface: types, macros, path constants
│   └── rcl-hostname.c    # D-Bus skeleton, property sync, polkit, file I/O
├── data/
│   ├── meson.build                                    # installs D-Bus and polkit data files
│   ├── org.freedesktop.hostname1.service.in           # D-Bus activation
│   ├── org.freedesktop.hostname1.conf                 # D-Bus security policy
│   ├── org.freedesktop.hostname1.xml                  # interface description (installed)
│   ├── org.freedesktop.hostname1.policy               # polkit actions
│   └── org.freedesktop.hostname1.rules.in             # polkit rules (privileged group)
└── po/
    ├── meson.build       # gettext / i18n integration
    ├── POTFILES          # source files with translatable strings
    └── LINGUAS           # enabled translation languages
```

## D-Bus interface quick reference

```sh
# Read all properties
busctl get-property org.freedesktop.hostname1 \
       /org/freedesktop/hostname1 org.freedesktop.hostname1 Hostname

# Set static hostname (triggers polkit prompt)
busctl call org.freedesktop.hostname1 \
       /org/freedesktop/hostname1 org.freedesktop.hostname1 \
       SetStaticHostname sb myhost true

# Dump everything as JSON
busctl call org.freedesktop.hostname1 \
       /org/freedesktop/hostname1 org.freedesktop.hostname1 Describe
```

## Adding a translation

1. Add the language code to `po/LINGUAS` (one code per line).
2. Generate an initial `.po` file:
   ```sh
   ninja -C build hostnamed-pot
   msginit -l de -o po/de.po -i po/hostnamed.pot
   ```
3. Translate the strings in `po/de.po`.
4. Rebuild – Meson compiles and installs `.mo` files automatically.
