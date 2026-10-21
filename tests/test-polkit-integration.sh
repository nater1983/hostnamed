#!/bin/sh
# Live Polkit authorization test for the installed target system.
# Run as root with system D-Bus, polkitd, and hostnamed active.
# Usage: tests/test-polkit-integration.sh WHEEL_USER UNPRIVILEGED_USER
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "Run as root to test both accounts and restore /etc/machine-info." >&2
  exit 2
fi
if [ "$#" -ne 2 ]; then
  echo "Usage: $0 WHEEL_USER UNPRIVILEGED_USER" >&2
  exit 2
fi

wheel_user=$1
unprivileged_user=$2
command -v runuser >/dev/null || { echo "runuser is required" >&2; exit 2; }
id "$wheel_user" >/dev/null
id "$unprivileged_user" >/dev/null
id -nG "$wheel_user" | tr ' ' '\n' | grep -qx wheel || {
  echo "$wheel_user is not a member of wheel" >&2
  exit 2
}
id -nG "$unprivileged_user" | tr ' ' '\n' | grep -qx wheel && {
  echo "$unprivileged_user must not be a member of wheel" >&2
  exit 2
}

bus_name=org.freedesktop.hostname1
object=/org/freedesktop/hostname1
iface=org.freedesktop.hostname1
method=SetLocation
tmp=$(mktemp)
had_machine_info=0
if [ -e /etc/machine-info ]; then
  cp -p /etc/machine-info "$tmp"
  had_machine_info=1
fi
restore() {
  if [ "$had_machine_info" -eq 1 ]; then
    cp -p "$tmp" /etc/machine-info
  else
    rm -f /etc/machine-info
  fi
  rm -f "$tmp"
}
trap restore EXIT HUP INT TERM

call_method() {
  user=$1
  value=$2
  runuser -u "$user" -- gdbus call --system \
    --dest "$bus_name" --object-path "$object" \
    --method "$iface.$method" "$value" false
}

echo "Checking authorized wheel user: $wheel_user"
if ! call_method "$wheel_user" "hostnamed-polkit-integration-test" >/dev/null; then
  echo "FAIL: wheel user was not authorized. Check the installed polkit rule and group setup." >&2
  exit 1
fi

echo "Checking unauthorized unprivileged user: $unprivileged_user"
if call_method "$unprivileged_user" "hostnamed-polkit-integration-test" >/dev/null 2>&1; then
  echo "FAIL: unprivileged user unexpectedly changed machine information." >&2
  exit 1
fi

echo "PASS: wheel user authorized; unprivileged user denied."
echo "This test uses the live system bus and polkit daemon; it does not test interactive prompts."
