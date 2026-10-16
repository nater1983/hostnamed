#!/usr/bin/env python3
"""Verify the DESTDIR tree produced by meson install and packaged hostnamectl."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

stage = Path(sys.argv[1]).resolve()
files = [p for p in stage.rglob("*") if p.is_file()]
by_name = {}
for path in files:
    by_name.setdefault(path.name, []).append(path)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


for name in (
    "org.freedesktop.hostname1.service",
    "org.freedesktop.hostname1.conf",
    "org.freedesktop.hostname1.xml",
    "org.freedesktop.hostname1.policy",
    "org.freedesktop.hostname1.rules",
    "hostnamed",
    "hostnamectl",
    "hostnamed.mo",
):
    require(name in by_name, f"staged install is missing {name}")

service = by_name["org.freedesktop.hostname1.service"][0].read_text()
require("Name=org.freedesktop.hostname1" in service, "D-Bus service name is wrong")
require("User=root" in service, "D-Bus activation must run the daemon as root")
require("Exec=/usr/libexec/hostnamed" in service or
        "Exec=/usr/lib/hostnamed" in service,
        "D-Bus activation Exec path does not match the packaged daemon")

for name in ("org.freedesktop.hostname1.conf", "org.freedesktop.hostname1.policy",
             "org.freedesktop.hostname1.xml"):
    path = by_name[name][0]
    require(path.stat().st_size > 0, f"{name} is empty")

client = by_name["hostnamectl"][0]
require(os.access(client, os.X_OK), "installed hostnamectl is not executable")
help_result = subprocess.run([str(client), "--help"], text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
require(help_result.returncode == 0 and "set-location" in help_result.stdout,
        "installed hostnamectl help/command list failed")

with tempfile.TemporaryDirectory(prefix="hostnamed-client-test-") as tmp:
    stub_dir = Path(tmp)
    log = stub_dir / "gdbus-args"
    stub = stub_dir / "gdbus"
    stub.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$HOSTNAMED_GDBUS_LOG\"\nexit 0\n")
    stub.chmod(0o755)
    env = dict(os.environ, PATH=str(stub_dir) + os.pathsep + os.environ.get("PATH", ""),
               HOSTNAMED_GDBUS_LOG=str(log))
    result = subprocess.run([str(client), "set-location", "packaging-test-location"],
                            env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    require(result.returncode == 0, f"hostnamectl setter failed with stub D-Bus: {result.stdout}")
    args = log.read_text()
    require("SetLocation" in args and "packaging-test-location" in args and "true" in args,
            "hostnamectl did not call the expected hostname1 D-Bus method")

print("staged install layout and packaged hostnamectl behavior: PASS")
