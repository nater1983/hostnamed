#!/usr/bin/env python3
"""Source-level security regression guards for hostnamed's privileged paths."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
hostnamectl = Path(sys.argv[2]).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


# Atomic privileged writes: root ownership, no symlink traversal, safe mode,
# explicit 0644 for machine-info, complete writes and durability reporting.
atomic_start = source.index("atomic_replace_file_mode (const gchar *path")
atomic_end = source.index("/* Preserve the historical call shape", atomic_start)
atomic = source[atomic_start:atomic_end]
for fragment, message in [
    ("g_lstat (path, &st)", "replacement must lstat the destination"),
    ("!S_ISREG (st.st_mode)", "replacement must reject non-regular destinations"),
    ("fchown (fd, 0, 0)", "replacement must normalize ownership to root"),
    ("required_mode != 0", "replacement must support an explicit required mode"),
    ("safe_mode &= ~0022;", "replacement must strip group/world write permissions"),
    ("fchmod (fd, safe_mode)", "replacement must apply the safe mode"),
    ("fsync (fd)", "temporary file contents must be synced before rename"),
    ("g_rename (tmp_path, path)", "replacement must use atomic rename"),
    ("sync_parent_directory (path, error)", "directory sync errors must propagate"),
]:
    require(fragment in atomic, message)

# Machine-info writes are serialized and always enforce 0644. Lock creation
# rejects symlinks and untrusted owners; unrelated keys survive updates.
lock_start = source.index("lock_machine_info_file (GError **error)")
lock_end = source.index("static gboolean\nwrite_machine_info_key", lock_start)
lock_fn = source[lock_start:lock_end]
for fragment, message in [
    ("O_NOFOLLOW", "machine-info lock must not follow symlinks"),
    ("flock (fd, LOCK_EX)", "machine-info writes must be serialized"),
    ("st.st_uid != 0", "machine-info lock must be root-owned"),
    ("S_ISREG (st.st_mode)", "machine-info lock must be a regular file"),
]:
    require(fragment in lock_fn, message)

writer_start = source.index("static gboolean\nwrite_machine_info_key")
writer_end = source.index("/* --------------------------------------------------------------------------\n   Input validation", writer_start)
writer = source[writer_start:writer_end]
require(writer.index("lock_machine_info_file (error)") <
        writer.index("g_file_get_contents (RCL_MACHINE_INFO_FILE"),
        "machine-info must be locked before reading")
require("atomic_replace_file_mode (RCL_MACHINE_INFO_FILE, out->str," in writer and
        "out->len, 0644, error)" in writer,
        "machine-info replacement must explicitly request mode 0644")
require("Preserve comments, unknown keys, and all unrelated entries" in writer,
        "machine-info updates must preserve unrelated entries")

# Static hostname changes must not write PRETTY_HOSTNAME. The CLI must use
# hostname1 D-Bus calls, leaving privileged file writes to the daemon.
static_start = source.index("do_set_static_hostname (")
static_end = source.index("static void\ndo_set_machine_info", static_start)
static_fn = source[static_start:static_end]
require("write_machine_info_key" not in static_fn and "PRETTY_HOSTNAME" not in static_fn,
        "static hostname changes must not overwrite PRETTY_HOSTNAME")
for method in ("SetStaticHostname", "SetHostname", "SetPrettyHostname",
               "SetIconName", "SetChassis", "SetDeployment", "SetLocation"):
    require("dbus_set " + method in hostnamectl,
            f"hostnamectl must route {method} through D-Bus")
require("/etc/machine-info" not in hostnamectl and
        "MACHINE_INFO_FILE" not in hostnamectl and "mv " not in hostnamectl,
        "hostnamectl must not write privileged configuration files directly")

# Machine-info access is restricted to valid field names; unknown/missing
# fields are rejected instead of exposing arbitrary files.
get_start = source.index('g_strcmp0(method_name, "GetMachineInfo")')
get_end = source.index('if( g_strcmp0(method_name, "Describe")', get_start)
get_handler = source[get_start:get_end]
require('g_regex_match_simple("^[A-Z][A-Z0-9_]*$"' in get_handler,
        "machine-info field names must be validated")
require("Machine-info field" in get_handler and "not set" in get_handler,
        "missing machine-info fields must be reported")

print("security source invariants: PASS")
