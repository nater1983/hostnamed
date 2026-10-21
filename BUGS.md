# Known Issues

This file lists known limitations and areas that still need target-system
verification for hostnamed 0.3.1. These items are not necessarily release
blockers. Please report issues at
https://github.com/nater1983/hostnamed/issues

- Run `tests/test-polkit-integration.sh WHEEL_USER UNPRIVILEGED_USER` as
  root on the installed target system to verify live system-bus authorization
  for a wheel user and denial for a non-wheel user. Interactive prompts require
  a polkit authentication agent in the caller's desktop session; headless
  sessions should use non-interactive authorization or provide an agent.
- The DMI placeholder list is finite; unknown firmware template strings may
  still be reported. Device-tree HardwareVendor uses the vendor prefix from
  the first compatible entry rather than a marketing name, and some boards do
  not provide a chassis-type property.
- Slackware startup behavior for fully qualified values in /etc/HOSTNAME should
  be verified against the target system's rc.S; hostnamed passes the configured
  static hostname to sethostname().
- Translation loading by the daemon has not been verified on installed systems;
  confirm that translations under the configured locale directory are found.
