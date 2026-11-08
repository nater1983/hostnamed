#!/usr/bin/env python3
"""Exercise repeated hostnamed startup and SIGTERM shutdown on a private bus."""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

CYCLES = 25
SERVICE = "org.freedesktop.hostname1"


def wait_until(predicate, description, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise AssertionError("timed out waiting for " + description)


def owner_pid(env):
    def call(method, arg):
        result = subprocess.run(
            ["gdbus", "call", "--system", "--dest", "org.freedesktop.DBus",
             "--object-path", "/org/freedesktop/DBus",
             "--method", "org.freedesktop.DBus." + method, arg],
            env=env, capture_output=True, text=True, timeout=2,
        )
        return result.stdout if result.returncode == 0 else ""
    name = call("GetNameOwner", SERVICE)
    match = re.search(r"'(:[0-9]+[.][0-9]+)'", name)
    if match is None:
        return None
    pid = call("GetConnectionUnixProcessID", match.group(1))
    match = re.search(r"([0-9]+)", pid)
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
        print("usage: test-lifecycle-stress.py /path/to/hostnamed", file=sys.stderr)
        return 2
    if not shutil.which("dbus-daemon") or not shutil.which("gdbus"):
        print("SKIP: private bus tools unavailable")
        return 77
    executable = os.path.abspath(sys.argv[1])
    if not os.path.isfile(executable):
        print("missing hostnamed executable", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="hostnamed-stress-") as tmp:
        directory = pathlib.Path(tmp)
        socket = directory / "bus"
        config = directory / "system.conf"
        # Join XML lines with chr(10) to keep escaping unambiguous.
        config.write_text(chr(10).join([
            '<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">',
            '<busconfig>',
            '  <type>system</type>',
            '  <listen>unix:path=' + str(socket) + '</listen>',
            '  <policy context="default">',
            '    <allow send_destination="*"/>',
            '    <allow receive_sender="*"/>',
            '    <allow own="*"/>',
            '  </policy>',
            '</busconfig>',
            '',
        ]), encoding="utf-8")
        env = os.environ.copy()
        env["DBUS_SYSTEM_BUS_ADDRESS"] = "unix:path=" + str(socket)
        env.pop("G_DEBUG", None)
        bus = None
        daemon = None
        log_path = directory / "stress.log"
        with log_path.open("w+", encoding="utf-8") as log:
            try:
                bus = subprocess.Popen(
                    ["dbus-daemon", "--config-file=" + str(config),
                     "--nofork", "--nopidfile"],
                    stdout=subprocess.DEVNULL, stderr=log,
                )
                wait_until(lambda: socket.exists() and bus.poll() is None,
                           "private bus startup")
                for cycle in range(1, CYCLES + 1):
                    daemon = subprocess.Popen(
                        [executable], env=env, stdout=log,
                        stderr=subprocess.STDOUT,
                    )
                    pid = daemon.pid
                    wait_until(lambda: daemon.poll() is None and
                               owner_pid(env) == pid,
                               "ownership in cycle " + str(cycle))
                    daemon.terminate()
                    try:
                        code = daemon.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        raise AssertionError("SIGTERM hang in cycle " + str(cycle))
                    if code != 0:
                        raise AssertionError("exit status " + str(code) +
                                             " in cycle " + str(cycle))
                    daemon = None
                    # Ensure name release before next process starts.
                    wait_until(lambda: owner_pid(env) is None,
                               "name release in cycle " + str(cycle))
                    if cycle % 5 == 0:
                        print("PASS: " + str(cycle) + "/" + str(CYCLES) +
                              " startup/shutdown cycles", flush=True)
                return 0
            except Exception as exc:
                print("FAIL: " + str(exc), file=sys.stderr)
                log.flush()
                print(log_path.read_text(encoding="utf-8")[-12000:],
                      file=sys.stderr)
                return 1
            finally:
                stop(daemon)
                stop(bus)


if __name__ == "__main__":
    sys.exit(main())
