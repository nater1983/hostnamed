#!/usr/bin/env python3
"""Regression guards for filesystem safety, D-Bus authorization and hardware IDs."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
source = (root / "src/rcl-hostname.c").read_text(encoding="utf-8")
main_source = (root / "src/main.c").read_text(encoding="utf-8")
meson = (root / "data/meson.build").read_text(encoding="utf-8")
top_meson = (root / "meson.build").read_text(encoding="utf-8")
slackbuild = (root / "hostnamed.SlackBuild").read_text(encoding="utf-8")
hostnamectl = (root / "slack/hostnamectl").read_text(encoding="utf-8")
rules = (root / "data/org.freedesktop.hostname1.rules.in").read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


# D-Bus setters authorize the actual system-bus caller before privileged work.
for fragment, message in [
    ("polkit_system_bus_name_new", "authorization must bind to the D-Bus caller"),
    ("polkit_authority_check_authorization(", "mutating calls must go through PolicyKit"),
    ("POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION", "interactive calls must permit a polkit agent"),
    ('"org.freedesktop.DBus.Error.AccessDenied"', "denials must return AccessDenied"),
    ('"org.freedesktop.DBus.Error.InteractiveAuthorizationRequired"', "challenges must request interactive authorization"),
    ('"org.freedesktop.DBus.Error.Failed"', "authorization and persistence failures must be reported"),
]:
    require(fragment in source, message)
require("@PRIVILEGED_GROUP@" in rules and "subject.isInGroup" in rules,
        "polkit rules must grant configured privileged-group users access")
require("org.freedesktop.hostname1.set-static-hostname" in rules and
        "org.freedesktop.hostname1.set-machine-info" in rules,
        "polkit rules must cover hostname and machine-info mutations")

# UUID validation enforces the canonical 8-4-4-4-12 form and rejects
# all-zero/all-FF placeholders. Unsupported hardware reads return a stable
# D-Bus NotSupported error.
uuid_start = source.index("parse_uuid_bytes(")
uuid_end = source.index("static void\ndo_get_product_uuid", uuid_start)
uuid_fn = source[uuid_start:uuid_end]
for fragment, message in [
    ("strlen (str) != 36", "UUID must have canonical length"),
    ("i == 8 || i == 13 || i == 18 || i == 23", "UUID hyphens must be in canonical positions"),
    ("or_all != 0 && and_all != 0xff", "empty UUID placeholders must be rejected"),
]:
    require(fragment in uuid_fn, message)
require('"org.freedesktop.DBus.Error.NotSupported"' in source,
        "unavailable product UUID must return NotSupported")

# Hardware identification should ignore DMI placeholders and fall back to
# board/device-tree data where available.
for fragment, message in [
    ("rcl_dmi_is_placeholder", "firmware placeholders must be filtered"),
    ("RCL_DMI_BOARD_VENDOR", "board vendor must be considered"),
    ("RCL_DMI_BOARD_NAME", "board model must be considered"),
    ("read_dt_string( RCL_DT_COMPATIBLE )", "hardware vendor must fall back to device-tree compatible"),
    ("read_dt_string( RCL_DT_MODEL )", "hardware model must fall back to device-tree model"),
    ("RCL_DT_SERIAL", "hardware serial must fall back to device-tree serial"),
    ('"System Product Name"', "common DMI placeholders must be recognized"),
]:
    require(fragment in source, message)

# hostnamectl is installed by both supported packaging paths and uses D-Bus
# instead of writing /etc/machine-info itself.
for fragment, message in [
    ("org.freedesktop.hostname1.service", "D-Bus activation service must be installed"),
    ("org.freedesktop.hostname1.conf", "system bus policy must be installed"),
    ("org.freedesktop.hostname1.xml", "D-Bus interface metadata must be installed"),
    ("org.freedesktop.hostname1.policy", "polkit policy must be installed"),
    ("org.freedesktop.hostname1.rules", "polkit rules must be installed"),
]:
    require(fragment in meson, message)
require("install_data(\n  'slack/hostnamectl'" in top_meson,
        "Meson installs must include hostnamectl")
require('cat slack/hostnamectl > $PKG/usr/bin/hostnamectl' in slackbuild,
        "SlackBuild must package hostnamectl")
require("gdbus call --system" in hostnamectl and "dbus_set SetStaticHostname" in hostnamectl,
        "hostnamectl must use the system D-Bus API")
require("/etc/machine-info" not in hostnamectl and "MACHINE_INFO_FILE" not in hostnamectl,
        "hostnamectl must not bypass the daemon for machine-info writes")

print("filesystem, authorization and hardware invariants: PASS")
