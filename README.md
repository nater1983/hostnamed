# hostnamed

A lightweight implementation of the `org.freedesktop.hostname1` D-Bus service
(`hostnamed`), following the same daemon pattern as `timedated`.

## Dependencies

| Library | pkg-config name | Minimum version |
|---|---|---|
| GLib / GIO | `glib-2.0`, `gio-2.0`, `gio-unix-2.0` | 2.56 |
| polkit | `polkit-gobject-1` | 0.113 |
| D-Bus | `dbus-1` | any recent |

The daemon is activated by dbus-daemon directly via D-Bus service activation
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

# Disable polkit (for testing only – not for production)
meson setup build -Dpolkit=disabled

# Override the D-Bus service directory explicitly
meson setup build -Ddbus_system_service_dir=/usr/share/dbus-1/system-services
```

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
├── src/
│   ├── meson.build       # compiles the hostnamed binary
│   ├── main.c            # GMainLoop, D-Bus name ownership, inotify watches
│   ├── rcl-hostname.h    # public interface: types, macros, path constants
│   └── rcl-hostname.c    # D-Bus skeleton, property sync, polkit, file I/O
├── data/
│   ├── meson.build                                    # installs D-Bus and polkit data files
│   ├── org.freedesktop.hostname1.service.in           # D-Bus activation
│   ├── org.freedesktop.hostname1.conf                 # D-Bus security policy
│   └── org.freedesktop.hostname1.policy               # polkit actions
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
