# Changelog

## [Unreleased]

### Hardware identity and Slackware hostname behavior
- Expanded DMI placeholder filtering to catch common firmware-template patterns,
  not only a finite list of exact strings.
- Stopped presenting a device-tree compatible vendor prefix as a marketing
  HardwareVendor name. When DMI vendor fields are unusable, HardwareVendor is
  empty; HardwareModel can still fall back to the device-tree model.
- Added regression checks that dotted static hostnames are passed unchanged to
  sethostname() and remain valid as RFC 1123 fully qualified names.

### Localization
- Initialize gettext in the daemon using the Meson-configured locale directory
  and UTF-8 catalog encoding.
- Add a regression test for daemon gettext initialization and compiled catalog
  installation paths.

### Authorization diagnostics and integration testing
- Clarify the interactive-authorization error so clients know that a polkit
  authentication agent must be available in the caller's session.
- Add a target-system integration script that checks wheel authorization and
  non-wheel denial against the live system D-Bus and polkit setup.

### D-Bus recovery and configuration monitoring
- Refresh the PolKit authority when the D-Bus object is restarted, and release
  the cached authority during daemon shutdown.
- Reconcile hostname and machine-info properties every two seconds in addition
  to directory monitoring, so missed or coalesced filesystem events do not
  leave D-Bus properties stale indefinitely.

## [Unreleased]

### Chassis and machine-info handling
- Removed `vm` and `container` as inferred/accepted chassis types. Virtualization
  detection remains separate from chassis classification.
- Chassis fallback continues to use `CHASSIS` from machine-info, then SMBIOS
  chassis type, then the device-tree chassis-type property.
- Kept machine-info parsing deterministic: double-quoted values are unquoted,
  and the first matching key is used when duplicate keys are present.

All notable changes to `hostnamed` are documented here, newest release first.

## [0.3.1] - 2026-10-21

### Release and documentation
- Updated the project, SlackBuild, and man-page version metadata to 0.3.1.
- Consolidated release history in `ChangeLog.md`; `BUGS-NEWS` is obsolete and is replaced by `BUGS.md` for known issues.
- Updated the SlackBuild documentation list to package `ChangeLog.md` and `BUGS.md`.

## [0.3.0] - 2026-10-16

### Security and persistence
- Routed hostname and machine-info changes from `hostnamectl` through the
  system D-Bus `org.freedesktop.hostname1` API. The daemon performs the
  privileged file updates and polkit authorization; the shell client no longer
  bypasses that authorization path with direct configuration-file writes.
- Fixed `set-hostname` so changing the static/transient hostname does not
  overwrite `PRETTY_HOSTNAME` unless a pretty-hostname change is explicitly
  requested.
- Hardened atomic replacement so the resulting file mode is selected
  deliberately rather than inheriting `mktemp`'s restrictive mode. Machine
  metadata writes install `/etc/machine-info` with mode `0644`.
- Added advisory locking around machine-info read/modify/write updates to
  serialize cooperating hostnamed writers and reduce lost updates.
- Kept unrelated machine-info entries when updating a single key and retained
  validation in the writer as defence in depth.
- Tightened product UUID parsing to require canonical 36-character
  `8-4-4-4-12` UUID syntax and reject all-zero and all-`FF` placeholder
  values. Unavailable or invalid product UUIDs return a D-Bus error instead
  of fabricated data.

### D-Bus and polkit
- Kept hostname-setting operations behind the daemon's D-Bus methods so
  authorization and persistence follow one path.
- Scoped the root polkit authorization bypass to hostname1 actions rather than
  granting unconditional success for unrelated actions.
- Standardized hostname-operation errors and kept exported property state
  synchronized on persistence error paths, including errors after an on-disk
  replacement has taken effect.
- Marked immutable hostname1 properties with `EmitsChangedSignal=const` in
  both the shipped interface XML and embedded introspection data.
- Interactive polkit challenges use
  `org.freedesktop.DBus.Error.InteractiveAuthorizationRequired` when the
  caller requests non-interactive authorization but an authentication prompt
  could satisfy the action.
- polkit is now a required build dependency. The obsolete `-Dpolkit` build
  option and related conditional configuration were removed.

### Hostname behavior and hardware identification
- Static and transient hostname validation follows strict RFC 1123 label
  rules: labels must be 1–63 characters, cannot be empty, and cannot begin or
  end with a hyphen.
- `SetPrettyHostname` preserves free-form dotted names instead of truncating
  every value at the first dot.
- Fixed duplicate or spurious `Chassis` and `IconName` property-change
  notifications by calculating effective values before updating the cache.
- `IconName` follows the effective chassis, including chassis data from
  machine-info when no explicit icon is configured.
- Populated `DefaultHostname` from os-release where available, with
  `localhost` as the fallback; `HostnameSource` can report the default
  hostname state.
- Improved hardware identification fallback order and skip common DMI
  placeholder strings. Hardware model and vendor can fall back to device-tree
  data on platforms without usable DMI values.
- Hardware serial lookup tries DMI product, board, and chassis serial values
  before falling back to the device-tree serial-number property.
- Expanded chassis mappings for server, embedded, and desktop form factors;
  kept the Slackware `hostnamectl` implementation aligned with the daemon.

### Tests and packaging
- Expanded security-invariant and filesystem/packaging regression checks for
  machine-info permissions and updates, hostnamectl's D-Bus usage, polkit
  authorization wiring, UUID validation, hardware fallbacks, and install
  layout.
- CI configures and compiles the project, runs registered Meson tests,
  syntax-checks the test harnesses and `hostnamectl`, validates shipped XML,
  and checks staged installation and packaging.

## [0.2.7.1] - 2026-06-27 to 2026-09-01

- Added validation for hostname metadata values and corrected the machine-info
  path constant.
- Added man pages for `hostnamed(8)` and `hostnamectl(1)`; compressed them
  in the SlackBuild.
- Added and updated translations through Weblate, including Arabic Latin
  script and Indonesian and Estonian updates.
- Removed obsolete `sbindir` paths from the build.

## [0.2.6] - 2026-06-20

### Security and authorization
- Fixed a Polkit authorization-rule logic error that could bypass the intended
  privileged-group check, and corrected JSON escaping in the D-Bus interface
  (contribution merged from PR #1).
- Reworked Polkit authorization to use asynchronous callbacks, preventing the
  authentication-agent dialog from blocking the daemon's main loop.
- Added dedicated Polkit actions for `GetProductUUID` and
  `GetHardwareSerial`, instead of reusing hostname-setting authorization.
  Custom Polkit rules that explicitly authorize only hostname-setting actions
  may need corresponding rules for these hardware-information actions.
- Added defence-in-depth validation inside the machine-info writer, in
  addition to caller-side validation.
- Refactored the Polkit rule template and privileged-group handling.

### Hostname and machine information
- Added configurable paths for the static hostname file, os-release, and
  machine-info, avoiding source edits for alternate filesystem layouts.
- Updated the daemon's file-monitor and configuration references to use the
  configured paths. The history also records intermediate path-configuration
  corrections during development.
- Added direct machine-info property-cache updates and immediate
  `PropertiesChanged` signals after successful writes, rather than relying
  solely on a later inotify-triggered full resynchronization.
- Added pretty-hostname sanitization to derive a short display label when a
  desktop client sends a fully qualified hostname as the pretty hostname.

### Build, documentation, and packaging
- Added the GPL-2.0 license file and an AUTHORS file.
- Added user-facing NEWS/release notes and known-issues documentation.
- Removed the unrelated `localectl` script and its leftover SlackBuild
  references; locale management belongs to the separate `localed` service.
- Added manual pages for `hostnamectl(1)` and `hostnamed(8)`, wired them into
  the Meson build, and configured the SlackBuild to install them under
  `/usr/man`.
- Updated the SlackBuild to compress installed manual pages and adjust
  handling of manual-page symlinks.
- Added the release documentation to the package's documentation directory.

## Historical development: 0.2.1–0.2.5

The available Git history does not contain separate version bumps, release
tags, or release records for 0.2.1 through 0.2.5. The recorded version
transition is from 0.2.0 to 0.2.6 on 2026-06-20. Development commits whose
changes can be verified are summarized in the 0.2.6 entry above; the current
history does not support assigning those changes to individual intermediate
versions.

## [0.2.0] - 2026-06-19

- Bumped the project version from 0.1.0 to 0.2.0.
- Added configurable os-release and machine-info paths.
- Improved pretty-hostname derivation from FQDN input.
- Exposed `FirmwareDate` as a D-Bus uint64 in microseconds since the Unix
  epoch.
- Updated machine-info property-cache values and emitted
  `PropertiesChanged` directly.

## Historical development: 0.1.1–0.1.7

The inspected Git history does not show separate version-bump commits or
release tags for 0.1.1 through 0.1.7. The next recorded version bump is from
0.1.0 to 0.2.0 on 2026-06-19. The changes below summarize development commits
between the initial 0.1.0 implementation and that 0.2.0 version bump; they
are not attributed to individual 0.1.x releases because the available history
does not establish those boundaries.

### Hostname handling and validation
- Added configurable hostname-file and default-hostname settings, plus a
  configurable privileged group.
- Refactored the `hostnamectl` script for clearer argument parsing and
  behavior, and continued updating its implementation.
- Added input validation for hostnames and machine-info values.
- Refined hostname state handling and logging through changes to the internal
  state structure.
- Improved the handling of hostname-setting authorization and simplified the
  Polkit rule logic.

### D-Bus and security policy
- Refined the `org.freedesktop.hostname1` D-Bus security policy with explicit
  interface access rules.
- Updated Polkit rules for hostname management and adjusted authorization
  handling in the daemon.
- Continued refining the D-Bus interface and service implementation during
  the initial development period.

### Hardware and firmware information
- Changed `FirmwareDate` to the D-Bus unsigned 64-bit type and added parsing
  of firmware dates into microseconds since the Unix epoch.

### Build and project configuration
- Added and refined Meson build configuration and options.
- Added the initial Slackware packaging script and updated packaging/config
  defaults during development.
- Added translation infrastructure and initial project documentation as part
  of the first implementation.

## [0.1.0] - 2026-05-31 to 2026-06-17

### Added
- Initial implementation of `org.freedesktop.hostname1` over plain D-Bus,
  without a systemd or logind dependency.
- Hostname and machine-info setters protected by polkit.
- Hostname and machine-info value validation.
- `hostnamectl` and `localectl` companion scripts and SlackBuild packaging.
- D-Bus interface XML, activation service, polkit policy and rules, Meson
  build files, translations, and initial documentation.

### Changed
- Refined authorization handling, hostname state management, D-Bus interface
  metadata, and build configuration during initial development.
- Changed `FirmwareDate` to the D-Bus uint64 representation.
- Tightened the D-Bus security policy and simplified the polkit rules.
