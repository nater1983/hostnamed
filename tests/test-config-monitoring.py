#!/usr/bin/env python3
"""Regression checks for event-driven configuration monitoring."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
hardware_source = Path(sys.argv[2]).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


require("g_file_monitor_directory" in source,
        "configuration monitoring must watch containing directories")
require("RCL_HOSTNAME_FILE" in source and "RCL_MACHINE_INFO_FILE" in source,
        "hostname and machine-info must be monitored")
require("RCL_OS_RELEASE_FILE" in source,
        "os-release changes must refresh exported operating-system properties")
require("g_timeout_add(100" in source,
        "monitor bursts must be coalesced with a short debounce")
require("rcl_main_config_sync_cb" in source,
        "monitor events must schedule a single coalesced refresh")
require("g_timeout_add_seconds(2" not in source,
        "the daemon must not poll all hardware properties every two seconds")
require("rcl_daemon_sync_config_properties" in source,
        "configuration events must use the configuration-only refresh path")
require("refresh_hardware" in hardware_source,
        "hardware discovery must be separated from configuration refreshes")
require("g_timeout_add_seconds(30" in source,
        "a low-frequency configuration-only reconciliation must remain enabled")
require("config_reconcile_id" in source,
        "the configuration reconciliation timer must be cancelled on shutdown")
require("reconcile_id" not in source,
        "the obsolete periodic full-refresh source must be removed")

print("event-driven configuration monitoring invariants: PASS")
