#!/usr/bin/env python3
"""Exercise hostnamed recovery across two isolated D-Bus restarts.

No connection to the real system bus is made. Only read-only D-Bus calls
are issued. Requires dbus-daemon and gdbus.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

SERVICE = "org.freedesktop.hostname1"
OBJECT = "/org/freedesktop/hostname1"
TIMEOUT = 15.0


def require_tools():
    for tool in ("dbus-daemon", "gdbus"):
        if shutil.which(tool) is None:
            print("SKIP: missing " + tool)
            sys.exit(77)


def wait_until(check, label, timeout=TIMEOUT):
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        try:
            result = check()
            if result:
                return result
        except (OSError, subprocess.SubprocessError) as exc:
            last = str(exc)
        time.sleep(0.15)
    raise AssertionError("timeout waiting for " + label + (": " + last if last else ""))


def property_read(env):
    result = subprocess.run(
        ["gdbus", "call", "--system", "--dest", SERVICE,
         "--object-path", OBJECT, "--method",
         "org.freedesktop.DBus.Properties.Get", SERVICE, "StaticHostname"],
        env=env, capture_output=True, text=True, timeout=3,
        check=False,
    )
    if result.returncode != 0:
        return None
    if not result.stdout.strip().startswith("(<"):
        return None
    return result.stdout.strip()



def dbus_owner_pid(env):
    """Ask the private bus which process currently owns the service."""
    owner = subprocess.run(
        ["gdbus", "call", "--system", "--dest", "org.freedesktop.DBus",
         "--object-path", "/org/freedesktop/DBus", "--method",
         "org.freedesktop.DBus.GetNameOwner", SERVICE],
        env=env, capture_output=True, text=True, timeout=3,
        check=False,
    )
    if owner.returncode != 0:
        return None
    match = re.search(r"'(:[0-9]+\\.[0-9]+)'", owner.stdout)
    if not match:
        return None
    result = subprocess.run(
        ["gdbus", "call", "--system", "--dest", "org.freedesktop.DBus",
         "--object-path", "/org/freedesktop/DBus", "--method",
         "org.freedesktop.DBus.GetConnectionUnixProcessID", match.group(1)],
        env=env, capture_output=True, text=True, timeout=3,
        check=False,
    )
    if result.returncode != 0:
        return None
    pid_match = re.search(r"(?:uint32\\s+)?([0-9]+)", result.stdout)
    return int(pid_match.group(1)) if pid_match else None


def main():
    require_tools()
    if len(sys.argv) != 2:
        print("usage: test-dbus-reconnect.py /path/to/hostnamed", file=sys.stderr)
        return 2
    executable = os.path.abspath(sys.argv[1])
    if not os.path.isfile(executable):
        print("missing hostnamed executable: " + executable, file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="hostnamed-dbus-") as tmp:
        directory = pathlib.Path(tmp)
        socket = directory / "bus"
        config = directory / "system.conf"
        log_path = directory / "hostnamed.log"
        config.write_text(
            '<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" '
            '"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">\n'
            '<busconfig>\n'
            '  <type>system</type>\n'
            '  <listen>unix:path=' + str(socket) + '</listen>\n'
            '  <policy context="default">\n'
            '    <allow send_destination="*"/>\n'
            '    <allow receive_sender="*"/>\n'
            '    <allow own="*"/>\n'
            '  </policy>\n'
            '</busconfig>\n',
            encoding="utf-8",
        )
        env = os.environ.copy()
        env["DBUS_SYSTEM_BUS_ADDRESS"] = "unix:path=" + str(socket)
        # Avoid inherited fatal warnings interfering with the test.
        env.pop("G_DEBUG", None)
        bus = None
        daemon = None
        with log_path.open("w+", encoding="utf-8") as log:
            def start_bus():
                nonlocal bus
                bus = subprocess.Popen(
                    ["dbus-daemon", "--config-file=" + str(config),
                     "--nofork", "--nopidfile"],
                    stdout=subprocess.DEVNULL, stderr=log,
                )
                wait_until(lambda: socket.exists() and bus.poll() is None,
                           "private D-Bus socket")

            def stop_bus():
                nonlocal bus
                if bus is not None:
                    if bus.poll() is None:
                        bus.terminate()
                        try:
                            bus.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            bus.kill()
                            bus.wait(timeout=5)
                    bus = None
                # dbus-daemon usually removes its socket on exit.
                if socket.exists():
                    socket.unlink()

            try:
                start_bus()
                daemon = subprocess.Popen(
                    [executable, "--verbose"], env=env,
                    stdout=log, stderr=subprocess.STDOUT,
                )
                pid = daemon.pid
                initial = wait_until(
                    lambda: property_read(env) if daemon.poll() is None else None,
                    "initial hostname property")
                print("PASS: initial D-Bus property " + initial, flush=True)
                for cycle in (1, 2):
                    stop_bus()
                    # Allow GLib to dispatch the closed signal before restarting.
                    time.sleep(0.6)
                    if daemon.poll() is not None:
                        raise AssertionError("hostnamed exited on disconnect")
                    start_bus()
                    value = wait_until(
                        lambda: property_read(env) if daemon.poll() is None else None,
                        "reconnection cycle " + str(cycle),
                        timeout=TIMEOUT,
                    )
                    if daemon.pid != pid or daemon.poll() is not None:
                        raise AssertionError("hostnamed process did not survive")
                    if value != initial:
                        raise AssertionError("property changed across restart")
                    print("PASS: reconnect cycle " + str(cycle) +
                          ", PID " + str(pid) + ", property " + value, flush=True)
                # Verify the private bus reports the same surviving owner PID.
                time.sleep(0.5)
                if daemon.poll() is not None:
                    raise AssertionError("hostnamed exited after reconnection")
                if dbus_owner_pid(env) != pid:
                    raise AssertionError("D-Bus name is not owned by original PID")
                print("PASS: D-Bus owner PID matches surviving daemon", flush=True)
                return 0
            except Exception as exc:
                print("FAIL: " + str(exc), file=sys.stderr)
                log.flush()
                print(log_path.read_text(encoding="utf-8")[-8000:], file=sys.stderr)
                return 1
            finally:
                if daemon is not None and daemon.poll() is None:
                    daemon.terminate()
                    try:
                        daemon.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        daemon.kill()
                        daemon.wait(timeout=5)
                stop_bus()


if __name__ == "__main__":
    sys.exit(main())
