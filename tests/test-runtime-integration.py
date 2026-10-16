#!/usr/bin/env python3
"""Opt-in live tests for hostnamed's D-Bus, polkit and machine-info paths.

Run only in a disposable VM with the installed hostnamed service. The stress
mode temporarily changes /etc/machine-info and restores it on exit.
"""
import argparse
import concurrent.futures
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid

DEST = "org.freedesktop.hostname1"
OBJ = "/org/freedesktop/hostname1"
IFACE = "org.freedesktop.hostname1"


def run(*args, check=True, timeout=30):
    p = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=timeout)
    if check and p.returncode:
        raise RuntimeError(f"command failed ({p.returncode}): {' '.join(args)}\n{p.stdout}")
    return p


def call(method, *args, check=True):
    return run("gdbus", "call", "--system", "--dest", DEST, "--object-path",
               OBJ, "--method", IFACE + "." + method, *args, check=check)


def get_field(key):
    p = call("GetMachineInfo", key)
    # GVariant string result: ('value',)
    text = p.stdout.strip()
    if not (text.startswith("('") and text.endswith("',)")):
        raise RuntimeError(f"unexpected GetMachineInfo({key}) reply: {text}")
    return text[2:-3].replace("\\'", "'").replace("\\\\", "\\")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def auth_test(expected, interactive):
    marker = "hostnamed-auth-test-" + uuid.uuid4().hex[:10]
    old = get_field("PRETTY_HOSTNAME")
    p = call("SetPrettyHostname", marker, "true" if interactive else "false",
             check=False)
    ok = p.returncode == 0
    try:
        if expected == "success":
            require(ok, f"expected authorization success, got: {p.stdout}")
            require(get_field("PRETTY_HOSTNAME") == marker,
                    "authorized call returned success but value did not persist")
        elif expected == "denied":
            require(not ok and "AccessDenied" in p.stdout,
                    f"expected an explicit polkit denial, got: {p.stdout}")
        elif expected == "challenge":
            require(not ok and "InteractiveAuthorizationRequired" in p.stdout,
                    f"expected interactive-authorization challenge, got: {p.stdout}")
        else:
            raise AssertionError("expected result must be success, denied, or challenge")
        print(f"authorization {expected}, interactive={interactive}: PASS")
    finally:
        # Do not leave a test hostname behind when authorization succeeded.
        if ok and get_field("PRETTY_HOSTNAME") == marker:
            call("SetPrettyHostname", old, "false", check=False)


def restore_file(path, data, mode):
    lock_path = Path(str(path) + ".lock")
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with open(lock_path, "a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        fd, tmp = tempfile.mkstemp(prefix=".hostnamed-test-restore-", dir=str(path.parent))
        try:
            with os.fdopen(fd, "wb") as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
                os.fchmod(f.fileno(), mode)
            os.replace(tmp, path)
            dfd = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
            try:
                os.fsync(dfd)
            finally:
                os.close(dfd)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)


def stress_test(path):
    if os.geteuid() != 0:
        raise RuntimeError("--stress must run as root in a disposable VM")
    path = Path(path)
    if not path.is_absolute():
        raise RuntimeError("--machine-info must be an absolute path")
    if path.is_symlink():
        raise RuntimeError(f"refusing symlink machine-info path: {path}")
    original = path.read_bytes() if path.exists() else None
    original_mode = (path.stat().st_mode & 0o777) if path.exists() else 0o644
    marker = uuid.uuid4().hex[:8]
    methods = {
        "PRETTY_HOSTNAME": ("SetPrettyHostname", [f"stress-pretty-{marker}"]),
        "ICON_NAME": ("SetIconName", ["computer-laptop"]),
        "CHASSIS": ("SetChassis", ["laptop"]),
        "DEPLOYMENT": ("SetDeployment", [f"stress-deployment-{marker}"]),
        "LOCATION": ("SetLocation", [f"stress-location-{marker}"]),
    }
    try:
        # Concurrent updates to distinct keys must all survive.
        def set_key(item):
            key, (method, values) = item
            value = values[0]
            call(method, value, "false")
            return key, value

        with concurrent.futures.ThreadPoolExecutor(max_workers=len(methods)) as pool:
            expected = dict(pool.map(set_key, methods.items()))
        for key, value in expected.items():
            require(get_field(key) == value,
                    f"concurrent update to {key} was lost (expected {value!r}, got {get_field(key)!r})")
        print("concurrent updates to different keys: PASS")

        # Repeated writes to the same key must not return false success.
        same_key = "DEPLOYMENT"
        method = "SetDeployment"
        values = [f"stress-same-{marker}-{i}" for i in range(24)]
        errors = []
        def repeat(value):
            p = call(method, value, "false", check=False)
            if p.returncode:
                errors.append((value, p.stdout))
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            list(pool.map(repeat, values))
        require(not errors, "same-key updates unexpectedly failed: " + repr(errors[:3]))
        final = get_field(same_key)
        require(final in values,
                f"final same-key value {final!r} was not one of the successful requests")
        print("repeated concurrent updates to same key: PASS")

        # External edits that preserve other fields should not be overwritten by
        # the next daemon update. This deliberately uses the documented lock.
        lock_path = Path(str(path) + ".lock")
        with open(lock_path, "a+") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            current = path.read_text(encoding="utf-8") if path.exists() else ""
            external = f"# external-test-{marker}=preserved\n"
            fd, tmp = tempfile.mkstemp(prefix=".hostnamed-external-", dir=str(path.parent))
            try:
                with os.fdopen(fd, "w", encoding="utf-8") as f:
                    f.write(current)
                    if current and not current.endswith("\n"):
                        f.write("\n")
                    f.write(external)
                    f.flush()
                    os.fsync(f.fileno())
                os.replace(tmp, path)
            finally:
                if os.path.exists(tmp):
                    os.unlink(tmp)
            fcntl.flock(lock, fcntl.LOCK_UN)
        call("SetLocation", f"stress-after-external-{marker}", "false")
        require(external.strip() in path.read_text(encoding="utf-8"),
                "cooperative external modification was silently lost")
        print("lock-cooperating external modification: PASS")
    finally:
        if original is None:
            try:
                path.unlink()
            except FileNotFoundError:
                pass
        else:
            restore_file(path, original, original_mode)
        # Give directory-monitor callbacks time to refresh cached properties.
        time.sleep(0.3)



def daemon_restart_test():
    if os.geteuid() != 0:
        raise RuntimeError("--restart must run as root in a disposable VM")
    old = get_field("PRETTY_HOSTNAME")
    marker = "hostnamed-restart-" + uuid.uuid4().hex[:8]
    call("SetPrettyHostname", marker, "false")
    status = run("busctl", "--system", "status", DEST).stdout
    import re
    match = re.search(r"(?m)^\s*PID=([0-9]+)\s*$", status)
    require(match is not None, "could not find hostnamed PID in busctl status")
    pid = int(match.group(1))
    try:
        os.kill(pid, 15)
        deadline = time.monotonic() + 20
        last_error = None
        while time.monotonic() < deadline:
            try:
                if get_field("PRETTY_HOSTNAME") == marker:
                    print("daemon process restart and persisted state: PASS")
                    return
            except Exception as exc:
                last_error = exc
            time.sleep(0.25)
        raise RuntimeError(f"service did not reactivate with persisted state; last error: {last_error}")
    finally:
        # Re-activate the service and restore the original value.
        try:
            call("SetPrettyHostname", old, "false", check=False)
        except Exception:
            pass


def persistence_failure_test(path):
    if os.geteuid() != 0:
        raise RuntimeError("--persistence-failure must run as root in a disposable VM")
    path = Path(path)
    lock_path = Path(str(path) + ".lock")
    if lock_path.is_symlink():
        raise RuntimeError(f"refusing to replace existing lock symlink: {lock_path}")
    backup = lock_path.with_name(lock_path.name + ".hostnamed-test-backup-" + uuid.uuid4().hex[:8])
    had_lock = lock_path.exists()
    if had_lock:
        os.replace(lock_path, backup)
    try:
        os.symlink("/etc/passwd", lock_path)
        p = call("SetLocation", "hostnamed-persistence-failure-test", "false", check=False)
        require(p.returncode != 0,
                "setter unexpectedly succeeded when the machine-info lock was a symlink")
        require("error" in p.stdout.lower() or "failed" in p.stdout.lower()
                or "denied" in p.stdout.lower(),
                f"setter failed without a useful D-Bus error: {p.stdout}")
        print("persistence/lock failure is returned to D-Bus client: PASS")
    finally:
        try:
            lock_path.unlink()
        except FileNotFoundError:
            pass
        if had_lock and backup.exists():
            os.replace(backup, lock_path)



def client_disconnect_test():
    old = get_field("PRETTY_HOSTNAME")
    marker = "hostnamed-disconnect-" + uuid.uuid4().hex[:8]
    argv = ["gdbus", "call", "--system", "--dest", DEST, "--object-path", OBJ,
            "--method", IFACE + ".SetPrettyHostname", marker, "true"]
    proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)
    try:
        # With an active polkit agent, an unauthorized non-admin caller should
        # remain pending while the agent is shown. Terminating gdbus simulates
        # the caller disconnecting while authorization is outstanding.
        time.sleep(0.25)
        require(proc.poll() is None,
                "setter did not remain pending; run as a non-admin user with an active polkit agent")
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)
        # The daemon must continue serving requests after the caller disappears.
        value = get_field("PRETTY_HOSTNAME")
        require(value in (old, marker),
                f"unexpected hostname after disconnected client: {value!r}")
        call("GetMachineInfo", "ICON_NAME")
        print("client disconnect does not wedge daemon: PASS")
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=3)
        try:
            if get_field("PRETTY_HOSTNAME") == marker:
                call("SetPrettyHostname", old, "false", check=False)
        except Exception:
            pass




def get_property(name):
    output = run("busctl", "--system", "get-property", DEST, OBJ, IFACE, name).stdout.strip()
    parts = output.split(" ", 1)
    if len(parts) != 2 or parts[0] != "s":
        raise RuntimeError(f"unexpected {name} property reply: {output}")
    return json.loads(parts[1])


def replace_bytes(path, data, mode=0o644):
    fd, tmp = tempfile.mkstemp(prefix=".hostnamed-integration-", dir=str(path.parent))
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
            os.fchmod(stream.fileno(), mode)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def wait_until(predicate, description, timeout=8):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except Exception as exc:
            last = exc
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {description}; last error: {last}")


def filesystem_edge_cases(path):
    if os.geteuid() != 0:
        raise RuntimeError("--filesystem-edge-cases must run as root in a disposable VM")
    path = Path(path)
    if not path.is_absolute() or path.is_symlink():
        raise RuntimeError("machine-info path must be absolute and not initially a symlink")
    original = path.read_bytes() if path.exists() else None
    original_stat = path.stat() if path.exists() else None
    lock_path = Path(str(path) + ".lock")
    lock_backup = lock_path.with_name(lock_path.name + ".edge-backup-" + uuid.uuid4().hex[:8])
    lock_existed = lock_path.exists() or lock_path.is_symlink()
    if lock_path.is_symlink():
        raise RuntimeError(f"refusing to test over existing lock symlink: {lock_path}")
    if lock_existed:
        os.replace(lock_path, lock_backup)
    try:
        # Missing file: a setter should create a valid file instead of reporting
        # success while leaving the path absent.
        missing_backup = path.with_name(path.name + ".edge-missing-" + uuid.uuid4().hex[:8])
        if path.exists():
            os.replace(path, missing_backup)
        try:
            call("SetLocation", "edge-missing-" + uuid.uuid4().hex[:8], "false")
            require(path.is_file(), "setter did not create missing machine-info")
            print("missing machine-info: PASS")
        finally:
            if path.exists():
                path.unlink()
            if missing_backup.exists():
                os.replace(missing_backup, path)

        # Malformed unrelated lines survive; duplicate instances of the updated
        # key are collapsed into exactly one entry.
        malformed = b"this is malformed and should survive\nLOCATION=\"old\"\nLOCATION=\"duplicate\"\n"
        replace_bytes(path, malformed)
        new_location = "edge-duplicate-" + uuid.uuid4().hex[:8]
        call("SetLocation", new_location, "false")
        updated = path.read_text(encoding="utf-8")
        require("this is malformed and should survive" in updated,
                "malformed unrelated content was unexpectedly discarded")
        require(sum(line.startswith("LOCATION=") for line in updated.splitlines()) == 1,
                "duplicate LOCATION keys were not collapsed")
        require(get_field("LOCATION") == new_location,
                "updated LOCATION value did not parse after duplicate cleanup")
        print("malformed contents and duplicate keys: PASS")

        # The writer must refuse a symlink and must not modify its target.
        target_fd, target_name = tempfile.mkstemp(prefix="hostnamed-symlink-target-")
        os.close(target_fd)
        target = Path(target_name)
        target_data = b"LOCATION=\"outside-target\"\n"
        target.write_bytes(target_data)
        path.unlink()
        os.symlink(target, path)
        p = call("SetLocation", "edge-symlink", "false", check=False)
        require(p.returncode != 0, "setter accepted a symlink machine-info path")
        require(path.is_symlink(), "setter replaced the symlink despite rejecting it")
        require(target.read_bytes() == target_data, "symlink target was modified")
        path.unlink()
        target.unlink()
        print("machine-info symlink rejection: PASS")

        # An unexpected owner is not inherited by the replacement.
        replace_bytes(path, b"LOCATION=\"ownership-test\"\n")
        os.chown(path, 65534, 65534)
        call("SetLocation", "edge-owner-" + uuid.uuid4().hex[:8], "false")
        st = path.stat()
        require(st.st_uid == 0 and st.st_gid == 0,
                f"replacement retained unexpected ownership {st.st_uid}:{st.st_gid}")
        require((st.st_mode & 0o777) == 0o644,
                f"replacement has unexpected mode {st.st_mode & 0o777:o}")
        print("unexpected ownership and permissions: PASS")

        # A directory at the lock pathname reliably exercises a persistence
        # open failure even on root, unlike chmod-based tests that root bypasses.
        if lock_path.exists():
            lock_path.unlink()
        lock_path.mkdir()
        p = call("SetLocation", "edge-lock-failure", "false", check=False)
        require(p.returncode != 0, "setter succeeded with a directory as lock path")
        require("error" in p.stdout.lower() or "failed" in p.stdout.lower()
                or "denied" in p.stdout.lower(),
                f"lock-path failure did not return a useful error: {p.stdout}")
        lock_path.rmdir()
        print("lock/persistence failure reporting: PASS")
    finally:
        if path.is_symlink():
            path.unlink()
        if path.exists():
            path.unlink()
        if original is not None:
            fd, tmp = tempfile.mkstemp(prefix=".hostnamed-edge-restore-", dir=str(path.parent))
            try:
                with os.fdopen(fd, "wb") as stream:
                    stream.write(original)
                    stream.flush()
                    os.fsync(stream.fileno())
                    os.fchown(stream.fileno(), original_stat.st_uid, original_stat.st_gid)
                    os.fchmod(stream.fileno(), original_stat.st_mode & 0o777)
                os.replace(tmp, path)
            finally:
                if os.path.exists(tmp):
                    os.unlink(tmp)
        if lock_path.exists():
            if lock_path.is_dir():
                lock_path.rmdir()
            else:
                lock_path.unlink()
        if lock_existed and lock_backup.exists():
            os.replace(lock_backup, lock_path)


def inotify_burst_test(hostname_path, machine_info_path):
    if os.geteuid() != 0:
        raise RuntimeError("--inotify-burst must run as root in a disposable VM")
    hostname_path = Path(hostname_path)
    machine_info_path = Path(machine_info_path)
    if hostname_path.is_symlink() or machine_info_path.is_symlink():
        raise RuntimeError("refusing to run against symlink configuration paths")
    originals = {}
    for path in (hostname_path, machine_info_path):
        originals[path] = (path.read_bytes() if path.exists() else None,
                           path.stat() if path.exists() else None)
    marker = uuid.uuid4().hex[:8]
    final_hostname = "hn-" + marker
    final_pretty = "Pretty-" + marker
    try:
        # Repeated atomic replacements generate create/move/change bursts,
        # including duplicate notifications for the same final contents.
        for i in range(18):
            replace_bytes(hostname_path, f"burst-{marker}-{i}\n".encode())
            pretty = f"Pretty-{marker}-{i}"
            replace_bytes(machine_info_path,
                          f'PRETTY_HOSTNAME="{pretty}"\nICON_NAME="computer"\n'.encode())
        replace_bytes(hostname_path, (final_hostname + "\n").encode())
        replace_bytes(machine_info_path,
                      f'PRETTY_HOSTNAME="{final_pretty}"\nICON_NAME="computer"\n'.encode())
        # Add duplicate no-op notifications by atomically replacing each file
        # with identical bytes again, as real editors and package tools do.
        replace_bytes(hostname_path, (final_hostname + "\n").encode())
        replace_bytes(machine_info_path,
                      f'PRETTY_HOSTNAME="{final_pretty}"\nICON_NAME="computer"\n'.encode())
        wait_until(lambda: get_property("StaticHostname") == final_hostname,
                   "final static hostname after monitor burst")
        wait_until(lambda: get_field("PRETTY_HOSTNAME") == final_pretty,
                   "final pretty hostname after monitor burst")
        print("inotify bursts, duplicate notifications, and reload coalescing: PASS")
    finally:
        for path, (data, st) in originals.items():
            if path.exists() or path.is_symlink():
                path.unlink()
            if data is not None:
                fd, tmp = tempfile.mkstemp(prefix=".hostnamed-monitor-restore-", dir=str(path.parent))
                try:
                    with os.fdopen(fd, "wb") as stream:
                        stream.write(data)
                        stream.flush()
                        os.fsync(stream.fileno())
                        if st is not None:
                            os.fchown(stream.fileno(), st.st_uid, st.st_gid)
                            os.fchmod(stream.fileno(), st.st_mode & 0o777)
                    os.replace(tmp, path)
                finally:
                    if os.path.exists(tmp):
                        os.unlink(tmp)
        time.sleep(0.3)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--authorization", nargs=2, metavar=("EXPECTED", "INTERACTIVE"),
                        help="test one live SetPrettyHostname authorization case; INTERACTIVE is true or false")
    parser.add_argument("--filesystem-edge-cases", action="store_true",
                        help="exercise missing, malformed, duplicate, symlink and ownership cases")
    parser.add_argument("--inotify-burst", action="store_true",
                        help="exercise bursts of atomic hostname and machine-info replacements")
    parser.add_argument("--hostname-file", default="/etc/HOSTNAME",
                        help="static hostname path used by --inotify-burst")
    parser.add_argument("--stress", action="store_true",
                        help="run destructive concurrent-write tests and restore machine-info afterwards")
    parser.add_argument("--client-disconnect", action="store_true",
                        help="terminate a client during an interactive SetPrettyHostname call")
    parser.add_argument("--restart", action="store_true",
                        help="terminate hostnamed and verify D-Bus activation restores persisted state")
    parser.add_argument("--persistence-failure", action="store_true",
                        help="temporarily replace the lock with a symlink and verify the setter reports failure")
    parser.add_argument("--machine-info", default="/etc/machine-info",
                        help="machine-info path used by the installed daemon (default: /etc/machine-info)")
    args = parser.parse_args()
    if os.environ.get("HOSTNAMED_INTEGRATION_TESTS") != "1":
        parser.error("set HOSTNAMED_INTEGRATION_TESTS=1 only on a disposable test system")
    try:
        if args.authorization:
            expected, interactive = args.authorization
            if interactive not in ("true", "false"):
                parser.error("INTERACTIVE must be true or false")
            auth_test(expected, interactive == "true")
        if args.stress:
            stress_test(args.machine_info)
        if args.filesystem_edge_cases:
            filesystem_edge_cases(args.machine_info)
        if args.inotify_burst:
            inotify_burst_test(args.hostname_file, args.machine_info)
        if args.client_disconnect:
            client_disconnect_test()
        if args.restart:
            daemon_restart_test()
        if args.persistence_failure:
            persistence_failure_test(args.machine_info)
        if not (args.authorization or args.stress or args.filesystem_edge_cases or args.inotify_burst or args.client_disconnect or args.restart or args.persistence_failure):
            parser.error("choose an integration-test mode")
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
