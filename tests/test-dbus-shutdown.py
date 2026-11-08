#!/usr/bin/env python3
"""Verify clean SIGTERM shutdown after private system-bus disconnection.

Runs only against a temporary private dbus-daemon. Does not modify the
machine's real system bus or send signals to an installed hostnamed.
"""
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time

SERVICE = "org.freedesktop.hostname1"
OBJECT = "/org/freedesktop/hostname1"


def wait_until(check, description, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.1)
    raise AssertionError("timed out waiting for " + description)


def name_responds(env):
    try:
        call = subprocess.run(
            ["gdbus", "call", "--system", "--dest", SERVICE,
             "--object-path", OBJECT, "--method",
             "org.freedesktop.DBus.Properties.Get", SERVICE, "StaticHostname"],
            env=env, capture_output=True, text=True, timeout=2,
            check=False,
        )
        return call.returncode == 0 and call.stdout.strip().startswith("(<")
    except (OSError, subprocess.TimeoutExpired):
        return False


def run_case(executable, delay):
    with tempfile.TemporaryDirectory(prefix="hostnamed-shutdown-") as tmp:
        directory = pathlib.Path(tmp)
        socket = directory / "bus"
        config = directory / "system.conf"
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
        env.pop("G_DEBUG", None)
        bus = None
        daemon = None
        log_path = directory / "hostnamed.log"
        with log_path.open("w+", encoding="utf-8") as log:
            try:
                bus = subprocess.Popen(
                    ["dbus-daemon", "--config-file=" + str(config),
                     "--nofork", "--nopidfile"],
                    stdout=subprocess.DEVNULL, stderr=log,
                )
                wait_until(lambda: socket.exists() and bus.poll() is None,
                           "private bus startup")
                daemon = subprocess.Popen(
                    [executable, "--verbose"], env=env,
                    stdout=log, stderr=subprocess.STDOUT,
                )
                wait_until(lambda: daemon.poll() is None and name_responds(env),
                           "hostnamed name acquisition")
                pid = daemon.pid
                bus.terminate()
                bus.wait(timeout=5)
                bus = None
                if socket.exists():
                    socket.unlink()

                # Two cases: immediate signal and signal after the closed
                # event has been dispatched but before the 2.5s retry.
                time.sleep(delay)
                if daemon.poll() is not None:
                    raise AssertionError("hostnamed exited before SIGTERM")
                daemon.send_signal(signal.SIGTERM)
                try:
                    code = daemon.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    raise AssertionError("hostnamed did not exit after SIGTERM")
                if code != 0:
                    raise AssertionError("hostnamed exited with status " + str(code))
                # The old PID is reaped; a late reconnect callback cannot run.
                if daemon.poll() != 0:
                    raise AssertionError("hostnamed process was not reaped")
                print("PASS: SIGTERM during bus outage (delay " +
                      str(delay) + "s), PID " + str(pid) +
                      ", clean exit status 0", flush=True)
            except Exception:
                log.flush()
                print(log_path.read_text(encoding="utf-8")[-8000:],
                      file=sys.stderr)
                raise
            finally:
                if daemon is not None and daemon.poll() is None:
                    daemon.terminate()
                    try:
                        daemon.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        daemon.kill()
                        daemon.wait(timeout=5)
                if bus is not None and bus.poll() is None:
                    bus.terminate()
                    try:
                        bus.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        bus.kill()
                        bus.wait(timeout=5)


def main():
    if len(sys.argv) != 2:
        print("usage: test-dbus-shutdown.py /path/to/hostnamed", file=sys.stderr)
        return 2
    if any(shutil.which(tool) is None for tool in ("dbus-daemon", "gdbus")):
        print("SKIP: dbus-daemon or gdbus missing")
        return 77
    executable = os.path.abspath(sys.argv[1])
    if not os.path.isfile(executable):
        print("missing executable: " + executable, file=sys.stderr)
        return 2
    try:
        for delay in (0.1, 1.0):
            run_case(executable, delay)
        return 0
    except Exception as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
