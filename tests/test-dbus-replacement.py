#!/usr/bin/env python3
"""Check D-Bus name takeover by a second hostnamed on a private bus."""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time


def wait_until(check, label, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = check()
            if value:
                return value
        except (OSError, subprocess.SubprocessError):
            pass
        time.sleep(0.1)
    raise AssertionError("timeout waiting for " + label)


def bus_call(env, method, *args):
    result = subprocess.run(
        ["gdbus", "call", "--system", "--dest", "org.freedesktop.DBus",
         "--object-path", "/org/freedesktop/DBus", "--method",
         "org.freedesktop.DBus." + method, *args],
        env=env, capture_output=True, text=True, timeout=3, check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else ""


def owner_pid(env):
    owner = bus_call(env, "GetNameOwner", "org.freedesktop.hostname1")
    match = re.search(r"'(:[0-9]+\\.[0-9]+)'", owner)
    if not match:
        return None
    value = bus_call(env, "GetConnectionUnixProcessID", match.group(1))
    match = re.search(r"(?:uint32\\s+)?([0-9]+)", value)
    return int(match.group(1)) if match else None


def stop(proc):
    if proc is not None and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


def main():
    if len(sys.argv) != 2:
        print("usage: test-dbus-replacement.py /path/to/hostnamed", file=sys.stderr)
        return 2
    if any(shutil.which(tool) is None for tool in ("dbus-daemon", "gdbus")):
        print("SKIP: missing dbus-daemon or gdbus")
        return 77
    executable = os.path.abspath(sys.argv[1])
    if not os.path.isfile(executable):
        print("missing executable: " + executable, file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="hostnamed-replace-") as tmp:
        directory = pathlib.Path(tmp)
        socket = directory / "bus"
        config = directory / "system.conf"
        log_path = directory / "hostnamed.log"
        config.write_text(
            '<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" '
            '"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">\\n'
            '<busconfig>\\n'
            '  <type>system</type>\\n'
            '  <listen>unix:path=' + str(socket) + '</listen>\\n'
            '  <policy context="default">\\n'
            '    <allow send_destination="*"/>\\n'
            '    <allow receive_sender="*"/>\\n'
            '    <allow own="*"/>\\n'
            '  </policy>\\n'
            '</busconfig>\\n',
            encoding="utf-8",
        )
        env = os.environ.copy()
        env["DBUS_SYSTEM_BUS_ADDRESS"] = "unix:path=" + str(socket)
        env.pop("G_DEBUG", None)
        bus = first = replacement = None
        with log_path.open("w+", encoding="utf-8") as log:
            try:
                bus = subprocess.Popen(
                    ["dbus-daemon", "--config-file=" + str(config),
                     "--nofork", "--nopidfile"],
                    stdout=subprocess.DEVNULL, stderr=log,
                )
                wait_until(lambda: socket.exists() and bus.poll() is None,
                           "private bus startup")
                first = subprocess.Popen(
                    [executable, "--verbose"], env=env,
                    stdout=log, stderr=subprocess.STDOUT,
                )
                wait_until(lambda: owner_pid(env) == first.pid,
                           "first daemon ownership")
                print("PASS: original owns private D-Bus name", flush=True)

                replacement = subprocess.Popen(
                    [executable, "--replace", "--verbose"], env=env,
                    stdout=log, stderr=subprocess.STDOUT,
                )
                wait_until(lambda: owner_pid(env) == replacement.pid,
                           "replacement ownership")
                print("PASS: replacement owns name, PID " +
                      str(replacement.pid), flush=True)

                wait_until(lambda: first.poll() is not None,
                           "original daemon shutdown")
                if first.returncode != 0:
                    raise AssertionError("original exited with status " +
                                         str(first.returncode))
                print("PASS: original exited cleanly", flush=True)

                # Longer than the 2.5-second reconnect delay: a competing
                # reconnect attempt must not steal the name back.
                time.sleep(3.2)
                if owner_pid(env) != replacement.pid:
                    raise AssertionError("replacement lost name after retry window")
                if replacement.poll() is not None:
                    raise AssertionError("replacement daemon exited unexpectedly")
                print("PASS: replacement retains ownership after reconnect window",
                      flush=True)

                replacement.terminate()
                if replacement.wait(timeout=5) != 0:
                    raise AssertionError("replacement did not shut down cleanly")
                print("PASS: replacement exited cleanly", flush=True)
                return 0
            except Exception as exc:
                print("FAIL: " + str(exc), file=sys.stderr)
                log.flush()
                print(log_path.read_text(encoding="utf-8")[-8000:],
                      file=sys.stderr)
                return 1
            finally:
                stop(replacement)
                stop(first)
                stop(bus)


if __name__ == "__main__":
    sys.exit(main())
