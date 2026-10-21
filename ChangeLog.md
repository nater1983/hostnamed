# Changelog

All notable changes to `hostnamed` are documented here, newest release first.

## [0.3.1] - 2026-10-21

### Release and documentation
- Updated project, SlackBuild, and man-page version metadata to 0.3.1.
- Consolidated release history in `ChangeLog.md`; replaced the obsolete
  `BUGS-NEWS` release/issue document with `BUGS.md` for known issues.
- Updated the SlackBuild documentation list to package `ChangeLog.md` and
  `BUGS.md`.

### Chassis and machine-info handling
- Kept `vm` and `container` out of chassis classification; virtualization
  detection is separate from chassis type.
- Resolve chassis fallback in order: machine-info `CHASSIS`, SMBIOS chassis
  type, then the device-tree chassis-type property.
- Parse double-quoted machine-info values and resolve duplicate keys using
  the last matching entry, so later edits take precedence.

### D-Bus recovery and configuration monitoring
- Refresh the Polkit authority after the system D-Bus connection/object is
  restarted, and release the cached authority during daemon shutdown.
- Reconcile hostname and machine-info properties periodically as well as
  watching configuration files, so coalesced or missed inotify events do not
  leave exported D-Bus properties stale indefinitely.

### Authorization and Polkit
- Improved the interactive-authorization diagnostic to explain that a
  Polkit authentication agent must be available in the caller's session.
- Added a target-system integration script to test authorization for a
  `wheel` user and denial for an unauthorized unprivileged user against the
  installed system D-Bus service and Polkit policy.
- Registered a Meson check for the integration script's shell syntax.

### Localization
- Initialize gettext after setting the process locale, bind the daemon's
  translation domain to Meson's configured locale directory, select UTF-8
  catalog encoding, and activate the domain.
- Added a regression test for gettext initialization order and the configured
  locale/catalog installation paths.

### Hardware identification
- Broadened DMI placeholder detection beyond a finite list of exact strings
  to catch common firmware-template values such as default, fill, unknown,
  undefined, invalid, and placeholder markers.
- Stopped treating a device-tree `compatible` vendor prefix as a marketing
  vendor name. `HardwareVendor` now prefers usable DMI system/board vendor
  fields and remains empty if neither is usable.
- Kept `HardwareModel` fallback through DMI product name, board name, and
  device-tree model; hardware serial fallback continues to use device-tree
  serial data when DMI serial fields are unavailable.
- Kept chassis classification independent of virtualization and avoid guessing
  chassis type when machine-info, SMBIOS, and device-tree data are unavailable.

### Hostname behavior
- Verified in code and regression checks that fully qualified static hostname
  values are passed unchanged to `sethostname()` and accepted by the hostname
  validator when their labels satisfy RFC 1123 rules.
- This documents the daemon-side behavior; target-system Slackware `rc.S`
  boot behavior still needs to be verified on an installed system.

### Regression tests
- Added `hardware-hostname-fallbacks` checks for DMI placeholder patterns,
  vendor/model fallback behavior, and fully qualified static hostnames.
- Updated `filesystem-and-packaging-invariants` to remove its obsolete
  expectation that `HardwareVendor` falls back to a device-tree compatible
  prefix, and instead assert DMI-only vendor fallback plus device-tree model
  fallback.
- Added gettext initialization/catalog-path checks and retained security,
  filesystem/packaging, and Polkit integration test coverage.

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
