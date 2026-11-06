#!/usr/bin/env python3
"""Regression checks for hardware identity fallback and Slackware hostname semantics."""
from pathlib import Path
import sys

source_path = Path(sys.argv[1])
source = source_path.read_text(encoding="utf-8")
hardware_source = (source_path.parent / "rcl-hardware.c").read_text(encoding="utf-8")

def require(condition, message):
    if not condition:
        raise AssertionError(message)

placeholder_start = hardware_source.index("rcl_dmi_is_placeholder( const gchar *v )")
placeholder_end = hardware_source.index("/* First usable (non-empty, non-placeholder)", placeholder_start)
placeholder = hardware_source[placeholder_start:placeholder_end]
for pattern in (
    '"fill"', '"default"', '"system manufacturer"',
    '"system product name"', '"not specified"', '"not applicable"',
    '"not set"', '"not defined"', '"unspecified"', '"placeholder"',
    '"unknown"', '"undefined"', '"invalid"', '"productconfigid"', '"serial number"',
):
    require(pattern in placeholder, f"placeholder pattern missing: {pattern}")

snapshot_start = hardware_source.index("rcl_hardware_snapshot_new (void)")
snapshot_end = hardware_source.index("rcl_hardware_snapshot_free (", snapshot_start)
hardware = hardware_source[snapshot_start:snapshot_end]
require('rcl_hardware_first_usable_dup (a, b, NULL)' in hardware,
        "HardwareVendor must not expose a DT compatible prefix as a marketing name")
require('rcl_hardware_first_usable_dup (a, b, dt)' in hardware,
        "HardwareModel should retain the device-tree model fallback")
require('if (refresh_hardware)' in source and 'rcl_hardware_snapshot_new' in source,
        "hardware discovery must be restricted to full refreshes")

static_start = source.index("do_set_static_hostname (RclHostnameDaemon")
static_end = source.index("static void\ndo_set_machine_info", static_start)
static_setter = source[static_start:static_end]
require("sethostname (hostname, strlen (hostname))" in static_setter,
        "static hostname setter must pass the configured value unchanged")
valid_start = source.rindex("rcl_hostname_is_valid( const gchar *name )")
valid_end = source.index("/* --------------------------------------------------------------------------\n   Pretty-hostname sanitiser", valid_start)
validator = source[valid_start:valid_end]
require("c == '.'" in validator and "name[i - 1] == '-'" in validator,
        "hostname validator must permit dotted FQDNs while validating labels")

print("hardware fallback and Slackware FQDN invariants: PASS")
