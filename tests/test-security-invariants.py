#!/usr/bin/env python3
"""Regression guards for privileged file-update and D-Bus disclosure paths.

These are source-level invariants, not substitutes for runtime concurrency tests.
"""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
hostnamectl = Path(sys.argv[2]).read_text(encoding="utf-8") if len(sys.argv) > 2 else ""


def require(condition, message):
    if not condition:
        raise AssertionError(message)


# Atomic replacement must never preserve group/world write bits.
require("safe_mode &= ~0022;" in source,
        "atomic replacement must strip group/world write permissions")
require("required_mode != 0" in source,
        "atomic replacement must support an explicit required mode")
require("out->len, TRUE, original, existed,\n                                     0644, error);" in source,
        "machine-info replacement must explicitly enforce mode 0644")

# The machine-info writer must take a root-owned, no-follow lock before reading.
lock_start = source.index("lock_machine_info_file (GError **error)")
lock_end = source.index("machine_info_snapshot_unchanged", lock_start)
lock_fn = source[lock_start:lock_end]
require("O_NOFOLLOW" in lock_fn and "flock (fd, LOCK_EX)" in lock_fn,
        "machine-info lock must use O_NOFOLLOW and an exclusive flock")
require("st.st_uid != 0" in lock_fn and "S_ISREG (st.st_mode)" in lock_fn,
        "machine-info lock must be a root-owned regular file")

writer_start = source.index("static gboolean\nwrite_machine_info_key")
writer_end = source.index("Input validation", writer_start)
writer = source[writer_start:writer_end]
require(writer.index("lock_machine_info_file (error)") <
        writer.index("g_file_get_contents (RCL_MACHINE_INFO_FILE"),
        "machine-info writer must lock before reading")
require("atomic_replace_file_checked (RCL_MACHINE_INFO_FILE" in writer,
        "machine-info writer must use checked atomic replacement")

# The final snapshot check must happen after the temporary file is synced and
# immediately before rename, with no intervening file operation.
checked_start = source.index("atomic_replace_file_checked (const gchar *path")
checked_end = source.index("\nstatic gboolean\natomic_replace_file (", checked_start)
checked = source[checked_start:checked_end]
snapshot = checked.index("if (check_snapshot)")
rename = checked.index("if (g_rename (tmp_path, path)")
require(snapshot < rename, "snapshot check must precede rename")
require(checked.rfind("fsync (fd)", 0, snapshot) != -1,
        "temporary file must be synced before final snapshot check")

# The bundled hostnamectl must delegate machine-info mutations to the daemon;
# it must not open or rewrite the machine-info file itself.
require("dbus_set SetIconName" in hostnamectl and
        "dbus_set SetChassis" in hostnamectl and
        "dbus_set SetDeployment" in hostnamectl and
        "dbus_set SetLocation" in hostnamectl,
        "hostnamectl must route machine-info changes through hostname1 D-Bus")
require("/etc/machine-info" not in hostnamectl and
        "MACHINE_INFO_FILE" not in hostnamectl and
        "flock " not in hostnamectl and
        "mv " not in hostnamectl,
        "hostnamectl must not implement a second machine-info writer")

# Only explicit public machine-info keys may be read over D-Bus.
get_start = source.index('g_strcmp0(method_name, "GetMachineInfo")')
get_end = source.index('if( g_strcmp0(method_name, "Describe")', get_start)
get_handler = source[get_start:get_end]
for key in ("PRETTY_HOSTNAME", "ICON_NAME", "CHASSIS", "DEPLOYMENT",
            "LOCATION", "TAGS"):
    require(f'"{key}"' in get_handler,
            f"public machine-info allowlist is missing {key}")
require("not publicly readable" in get_handler,
        "unknown machine-info keys must be rejected")

print("security source invariants: PASS")
