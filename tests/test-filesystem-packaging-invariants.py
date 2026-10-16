#!/usr/bin/env python3
"""Source and packaging invariants for filesystem, monitor and install behavior."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
source = (root / "src/rcl-hostname.c").read_text(encoding="utf-8")
main_source = (root / "src/main.c").read_text(encoding="utf-8")
meson = (root / "data/meson.build").read_text(encoding="utf-8")
top_meson = (root / "meson.build").read_text(encoding="utf-8")
po_meson = (root / "po/meson.build").read_text(encoding="utf-8")
slackbuild = (root / "hostnamed.SlackBuild").read_text(encoding="utf-8")
hostnamectl = (root / "slack/hostnamectl").read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


# Filesystem safety and failure reporting.
for fragment, message in [
    ("G_FILE_ERROR_NOENT", "missing machine-info must be handled explicitly"),
    ("g_str_has_prefix (lines[i], prefix)", "writer must identify matching keys"),
    ("if (!found && value", "writer must append absent keys"),
    ("g_lstat (path, &st)", "replacement must lstat instead of following symlinks"),
    ("!S_ISREG (st.st_mode)", "replacement must reject non-regular paths"),
    ("fchown (fd, 0, 0)", "replacement must normalize ownership"),
    ("fchmod (fd, safe_mode)", "replacement must enforce safe mode"),
    ("create temporary file for %s", "temporary-file creation failures need useful errors"),
    ("sync temporary file for %s", "fsync failures need useful errors"),
    ("rename temporary file to %s", "rename failures need useful errors"),
    ("sync_parent_directory (path, error)", "directory synchronization errors must propagate"),
    ("restore_static_hostname_file (old_contents, old_length", "static hostname changes must roll back file state after failures"),
    ("if (!persisted)", "static hostname file errors must trigger rollback before returning"),
    ("sethostname (hostname, strlen (hostname))", "kernel hostname must be updated only after persistence succeeds"),
    ("file rollback failed:", "rollback failures must be reported alongside the original error"),
    ("g_dbus_method_invocation_return_dbus_error (invocation", "persistence failures must be returned to D-Bus callers"),
]:
    require(fragment in source, message)

# The process-level watcher monitors parent directories (not stale inodes),
# rate-limits event storms, and coalesces duplicate notifications into one
# idle refresh. Pending refresh sources are removed during shutdown.
for fragment, message in [
    ("g_file_monitor_directory", "configuration directories must be monitored for atomic rename"),
    ("g_file_monitor_set_rate_limit(monitor, 50)", "monitor events should be rate-limited"),
    ("g_idle_add(rcl_main_reload_properties", "duplicate monitor events must be coalesced"),
    ("watch->state->reload_id == 0", "only one reload should be queued at a time"),
    ("g_source_remove(state->reload_id)", "pending reload must be canceled during shutdown"),
    ("if (state->bus_ready && state->daemon)", "reloads must not touch a disconnected D-Bus connection"),
    ("state->bus_ready = FALSE", "bus disconnect must disable property refreshes"),
    ("bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR)", "translation domain must use the configured locale directory"),
    ("bind_textdomain_codeset", "translations must use UTF-8"),
    ("textdomain(GETTEXT_PACKAGE)", "gettext domain must be activated"),
]:
    require(fragment in main_source, message)

# D-Bus recovery: a transport failure must be retried rather than mistaken
# for a competing daemon; stale authorization work must not outlive its bus.
for fragment, message in [
    ("g_timeout_add(RECONNECT_DELAY_MS", "bus loss must schedule reconnection"),
    ("g_bus_unown_name(state->watch_id)", "reconnect must discard the previous name watch"),
    ("g_dbus_connection_is_closed(connection)", "closed bus connections must be distinguished from live name takeovers"),
    ("state->bus_ready = FALSE", "bus loss must stop property refreshes"),
    ("g_cancellable_cancel(priv->bus_cancellable)", "shutdown must cancel pending authorization work"),
    ("ctx->cancellable, on_check_done, ctx", "polkit requests must use the bus-generation cancellable"),
    ("g_cancellable_is_cancelled(ctx->cancellable)", "stale authorization callbacks must not execute operations"),
    ("auth_ctx_caller_is_connected( ctx )", "authorized operations must be discarded when their caller disconnects"),
    ('"GetNameOwner"', "caller liveness must be checked against the current system bus"),
    ("ctx->sender = g_strdup( sender )", "authorization context must retain the caller's unique bus name"),
    ("g_dbus_connection_is_closed( priv->connection )", "authorization must reject a closed system-bus connection"),
    ('"org.freedesktop.DBus.Error.InteractiveAuthorizationRequired"', "non-interactive challenges must request interactive authorization"),
    ('"org.freedesktop.DBus.Error.AccessDenied"', "explicit policy denials must return AccessDenied"),
    ('"org.freedesktop.DBus.Error.Cancelled"', "cancelled interactive authorization must return Cancelled"),
    ("G_IO_ERROR_CANCELLED", "interactive authorization cancellation must be distinguished from service failure"),
    ('"Authorization service unavailable"', "unavailable PolicyKit must return a D-Bus error"),
    ("g_clear_object(&priv->authority)", "a lost bus must discard the stale polkit authority"),
    ('"org.freedesktop.DBus.Error.Disconnected"', "set requests must fail if the bus is unavailable"),
]:
    require(fragment in main_source + source, message)

# Hardware metadata: reject malformed product UUIDs, report unsupported UUID
# reads explicitly, filter firmware placeholders, and classify virtual chassis.
for fragment, message in [
    ("strlen (str) != 36", "product UUID must use canonical 36-character formatting"),
    ("i == hyphens[h]", "product UUID hyphens must occur in canonical positions"),
    ('"org.freedesktop.DBus.Error.NotSupported"', "unavailable product UUID must return NotSupported"),
    ("rcl_dmi_is_placeholder", "firmware placeholder strings must be ignored"),
    ("read_dt_string( RCL_DT_COMPATIBLE )", "hardware vendor must fall back to device-tree compatible"),
    ("read_dt_string( RCL_DT_MODEL )", "hardware model must fall back to device-tree model"),
    ("detect_virtual_chassis ()", "chassis detection must classify containers and virtual machines"),
    ('g_strdup ("container")', "container chassis must be identified"),
    ('g_strdup ("vm")', "virtual-machine chassis must be identified"),
    ("parse_id128_text (id, bytes)", "machine and boot IDs must be validated before publication"),
    ("length != 32 && length != 36", "ID parser must reject malformed lengths"),
    ("g_variant_unref (nv)", "unchanged metadata refresh variants must be released"),
    ("strlen (date_str) != 10", "firmware dates must use exact MM/DD/YYYY formatting"),
    ("strlen (support_end) == 10", "OS support dates must use exact YYYY-MM-DD formatting"),
    ("gmtime_r (&t, &normalized)", "calendar dates must reject impossible day/month combinations"),
]:
    require(fragment in source, message)

# Installation contract: service activation, bus access policy, polkit policy,
# interface metadata, translations and the actual hostnamectl executable.
for fragment, message in [
    ("org.freedesktop.hostname1.service", "D-Bus activation service must be installed"),
    ("org.freedesktop.hostname1.conf", "system bus policy must be installed"),
    ("org.freedesktop.hostname1.xml", "D-Bus interface XML must be installed"),
    ("org.freedesktop.hostname1.policy", "polkit action policy must be installed"),
    ("org.freedesktop.hostname1.rules", "polkit rules must be installed"),
]:
    require(fragment in meson, message)
require("install_data(\n  'slack/hostnamectl'" in top_meson,
        "Meson installs must include the Slackware hostnamectl client")
require("install_dir   : localedir" in po_meson and "install       : true" in po_meson,
        "compiled translations must be installed")
require('cat slack/hostnamectl > $PKG/usr/bin/hostnamectl' in slackbuild,
        "SlackBuild must package hostnamectl into /usr/bin")
require('gdbus call --system' in hostnamectl and 'dbus_set SetStaticHostname' in hostnamectl,
        "hostnamectl must use the system D-Bus API for hostname changes")
require('/etc/machine-info' not in hostnamectl and 'MACHINE_INFO_FILE' not in hostnamectl,
        "hostnamectl must not bypass the daemon for machine-info writes")

# Hardware metadata cache owns all dynamically allocated strings, string
# vectors and GVariants until object finalization.
for fragment, message in [
    ("g_free( priv->os_fancy_name )", "OS fancy name cache must be freed"),
    ("g_free( priv->os_image_id )", "OS image ID cache must be freed"),
    ("g_free( priv->os_image_version )", "OS image version cache must be freed"),
    ("g_strfreev( priv->tags )", "machine tags cache must be freed"),
    ("g_free( priv->hw_sku )", "hardware SKU cache must be freed"),
    ("g_free( priv->hw_version )", "hardware version cache must be freed"),
    ("g_free( priv->firmware_vendor )", "firmware vendor cache must be freed"),
    ("g_free( priv->chassis_asset_tag )", "chassis asset tag cache must be freed"),
    ("g_clear_pointer( &priv->machine_id, g_variant_unref )", "machine ID variant must be unreferenced"),
    ("g_clear_pointer( &priv->boot_id, g_variant_unref )", "boot ID variant must be unreferenced"),
]:
    require(fragment in source, message)

print("filesystem, monitor and packaging invariants: PASS")
