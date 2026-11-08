#!/usr/bin/env python3
"""Read-only integration smoke test against a running system-bus hostnamed.

Run manually: python3 tests/test-runtime-dbus.py
Never changes hostname, machine-info, or system bus configuration.
"""
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

DEST = "org.freedesktop.hostname1"
PATH = "/org/freedesktop/hostname1"
IFACE = DEST


def call(method, *args):
    proc = subprocess.run(
        ["gdbus", "call", "--system", "--dest", DEST,
         "--object-path", PATH, "--method", method, *args],
        text=True, capture_output=True, timeout=15, check=False,
    )
    if proc.returncode:
        raise RuntimeError(f"{method} failed: {proc.stderr.strip()}")
    return proc.stdout.strip()


def main():
    introspection = call("org.freedesktop.DBus.Introspectable.Introspect")
    # gdbus prints an escaped string representation; verify key API names
    # without assuming the escaping format.
    for name in ("org.freedesktop.hostname1", "SetHostname",
                 "GetProductUUID", "MachineID", "BootID", "Describe"):
        if name not in introspection:
            raise AssertionError(f"introspection missing {name}")

    all_props = call("org.freedesktop.DBus.Properties.GetAll", IFACE)
    for name, signature in (
        ("Hostname", "s"), ("StaticHostname", "s"),
        ("MachineID", "ay"), ("BootID", "ay"),
        ("FirmwareDate", "t"), ("VSockCID", "u"),
    ):
        value = call("org.freedesktop.DBus.Properties.Get", IFACE, name)
        if not value:
            raise AssertionError(f"empty response for {name}")
        if f"'{name}':" not in all_props:
            raise AssertionError(f"GetAll omitted {name}")
        if signature == "ay" and len(re.findall(r"0x[0-9a-fA-F]{2}", value)) != 16:
            raise AssertionError(f"{name} is not a 16-byte array: {value}")
        if signature == "t" and "uint64 " not in value:
            raise AssertionError(f"{name} is not uint64: {value}")
        if signature == "u" and "uint32 " not in value:
            raise AssertionError(f"{name} is not uint32: {value}")
        print(f"PASS: {name} ({signature})")

    description = call(f"{IFACE}.Describe")
    if not description:
        raise AssertionError("Describe returned an empty response")
    print("PASS: Describe")
    print("PASS: read-only runtime D-Bus smoke test")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
