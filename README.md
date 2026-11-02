# Mk61

Linux support software for the **Gamakay MK61** mechanical keyboard.

![Gamakay MK61 keyboard](assets/gamakay-mk61.png)

## Overview

`mk61` is a clean-room reimplementation of the keyboard configuration
protocol, developed by **cleanrrom** from observations of the original
Windows executable. It provides a small native Linux stack for inspecting the
keyboard, communicating with its HID interfaces, and controlling supported
LED features.

The project is intentionally split into layers:

- **`libkbcore`** — C library for report validation, packet encoding and
  decoding, profiles, macros, and LED data. It has no GUI or global I/O.
- **`kbdd`** — per-user daemon that owns the `hidraw` devices and exposes a
  Unix-domain socket API.
- **`kbctl`** — command-line client for querying the daemon and using the
  available keyboard controls.

The implementation targets Linux and is currently experimental. Hardware
writes are deliberately restricted until they have been validated on the
target keyboard.

## Features

- C11 implementation with a public, opaque-handle core API.
- HID feature-report parsing and encoding.
- Keyboard discovery through `hidraw`.
- Profile inspection, active-profile queries, and active-profile switching
  (`kbctl profile <1-4>`, confirmed by a read-back).
- LED color, position, sweep, brightness, speed, and built-in effect commands.
  The `led brightness` value also scales host-driven solid colors, which are
  sent as direct RGB frames instead of using the keyboard's built-in effect.
- Per-key LED colors by name (`led key w,a,s,d FF0000`, groups like `wasd`/`numbers`)
  using the SK61L/MK61L matrix map confirmed on hardware.
- Host-computed LED animations (`breath`, `cycle`, `rainbow`) streamed at 25 fps.
- Optional virtual keyboard input through `uinput`.
- Unix-socket IPC between `kbctl` and `kbdd`.
- Automatic keyboard reconnect after USB unplug/replug while `kbdd` stays running.
- Unit tests and optional AddressSanitizer/UndefinedBehaviorSanitizer tests.
- systemd user-service and udev integration.

## Requirements

- Linux
- A C compiler (GCC or another C11-compatible compiler)
- GNU Make
- `hidraw` access to the keyboard
- `systemd --user` and `udev` for the supplied installation flow
- Optional: `/dev/uinput` access for virtual keyboard input

## Build

Build the library, daemon, and CLI from the repository root:

```sh
make
```

Run the normal test suite:

```sh
make test
```

Run the sanitizer test suite when the compiler runtime is available:

```sh
make asan
```

GitHub Actions runs the build, unit tests, and sanitizer tests on pushes and
pull requests. Publishing a GitHub release also builds Fedora RPM and source
RPM packages and attaches them to that release for download.

Build outputs are placed under `build/`; the libraries are produced as
`libkbcore.a` and `libkbcore.so`.

## Installation

The project includes a Fedora-oriented convenience script that builds,
tests, installs, and starts the user service:

```sh
./install.sh
```

LED and profile writes are opt-in (disabled by default):

```sh
ENABLE_LED=1 ./install.sh
```

For a manual installation, use the Makefile targets:

```sh
sudo make install PREFIX=/usr LIBDIR=/usr/lib64
```

The install target places `kbdd` and `kbctl` in `bin`, the library in the
selected library directory, `kbcore.h` in the include directory, and any
available udev/systemd integration files under the corresponding prefix.

To remove an installation made by the supplied script:

```sh
./uninstall.sh
```

The Fedora RPM also installs the user-service configuration, enables LED and
profile writes, and enables `kbdd` globally for user systemd managers. It
reloads and starts the service for active user sessions; a new login session
will start it automatically as well. Verify it with:

```sh
systemctl --user status kbdd
kbctl status
```

After updating an existing installation, rerun `./install.sh` so the user
service is reloaded with the current unit definition.

## Usage

After `kbdd` is running, use `kbctl help` to list all commands. Detailed
help is available for every command and LED subcommand:

```sh
kbctl help
kbctl help status
kbctl help led
kbctl help led pos
kbctl status --help
kbctl led pos --help
```

The daemon watches for HID interfaces that are added or removed. If the
keyboard is unplugged, `kbdd` keeps running and automatically discovers it
again when it is plugged back in.

### Device and daemon commands

```sh
kbctl list
kbctl status
kbctl version
kbctl read-config
kbctl enable-notifications
kbctl watch
kbctl active-profile
```

- `list` lists the devices discovered by `kbdd`.
- `status` shows daemon state, connected devices, and enabled capabilities.
- `version` prints the `kbctl` and `libkbcore` versions.
- `read-config` reads the daemon configuration summary.
- `enable-notifications` enables keyboard input notifications.
- `watch` subscribes to profile, layer, and LED effect events and prints them
  until `Ctrl+C`.
- `active-profile` queries the active profile from the keyboard.

### Profile commands

Profile numbers use the hardware numbering `1` through `4`:

```sh
kbctl read-profile [profile]
kbctl restore-profile <profile> <file>
kbctl dump-block <1-4> [file]
kbctl diff-block <a.bin> <b.bin>
kbctl profile <1-4>
```

- `kbctl` waits up to 3 s for `kbdd` to create its socket (useful right after
  `systemctl --user restart kbdd`). Set `KBCTL_CONNECT_TIMEOUT_MS` to change the
  wait (`0` = single attempt, maximum 60000). Offline commands (`led keys`,
  `led effects`, `diff-block`, `help`) never connect.
- `led direction` changes the animation direction (`0` through `3`) in profile
  byte 5, which is report byte 13.
- `led report-rate` changes the report-rate index (`0` through `254`); the
  stored value is index plus one in profile byte 0, which is report byte 8.
- `read-profile` reads a profile; without an argument it reads profile `1`.
  Profile numbers are consistently `1..4`, matching the keyboard protocol.
- `restore-profile` restores a 264-byte file created by `dump-block`, verifies
  the complete block by reading it back, and rolls back to the previous block
  if verification fails. It changes RAM only and requires
  `KBDD_ENABLE_PROFILE_WRITE=1`.
- `dump-block` reads and displays a raw 264-byte profile parameter block,
  optionally saving it to a file.
- `diff-block` compares two saved 264-byte blocks and reports changed offsets.
- `profile <1-4>` switches the keyboard's active profile. It requires
  `KBDD_ENABLE_PROFILE_WRITE=1` and works in three steps:
  read the active profile with `0x81`, send `0x01` with the requested profile,
  then read `0x81` again. If the keyboard does not confirm the change, `kbdd`
  asks it to return to the previous profile and the command fails. Requesting
  the profile that is already active sends nothing. Host-driven LED streaming
  is stopped on a switch, because those frames belong to the previous
  profile's lighting. The factory-reset value `0xFF` of opcode `0x01` is
  rejected by the library and can never be sent.

### LED commands

LED commands are available when LED support has been explicitly enabled:

```sh
kbctl led effects
kbctl led solid FF0000
kbctl led clear
kbctl led pos 12 00FF00
kbctl led pos 24 FF0000
kbctl led key w,a,s,d FF0000
kbctl led key esc,numbers 00AAFF
kbctl led keys
kbctl led off
kbctl led sweep --delay 100 --color 0000FF
kbctl led anim breath --color FF0000 --period 3000
kbctl led anim cycle
kbctl led anim rainbow --period 4000
kbctl led release
kbctl led effect lighting
kbctl led brightness 8
kbctl led speed 50
kbctl led direction 2
kbctl led report-rate 0
kbctl led effect breath --save
kbctl led brightness 5 --save
kbctl led save
```

- `led solid <RRGGBB>` fills every matrix position with one RGB color.
- `led off` turns every matrix position off.
- `led clear` clears the host-controlled frame.
- `led pos <n> <RRGGBB>` changes one zero-based position while preserving
  other colors, allowing a multicolor custom layout.
- `led key <keys> <RRGGBB>` colors keys by name instead of matrix position. `<keys>`
  is a comma-separated list of key names and/or groups (`wasd`, `numbers`,
  `letters`, `mods`, `all`); other keys keep their colors. Names are
  case-insensitive and have aliases (`escape`, `-`, `win`...); ambiguous names
  (`shift`, `ctrl`, `alt`) are rejected, use `lshift`/`rshift` etc. It is a
  convenience over `led pos`, so the same persistence rules apply.
- `led keys` lists the 61 key names with their matrix positions (offline).
  The matrix index is `column * 5 + row`; gaps (8, 19, 24, 34, 39, 44, 49, 62,
  63) have no key, because wide keys occupy more than one column.
- `led sweep` lights positions sequentially to map physical keys (it now prints
  the key name next to each position). Use
  `--delay MS`, `--color RRGGBB`, and `--from N`.
- `led anim <breath|cycle|rainbow>` runs an animation computed by `kbdd` and
  streamed as frames at 25 fps (every 40 ms), independent of the key map:
  - `breath` pulses one color smoothly (`--color RRGGBB`, default white).
  - `cycle` rotates the hue of the whole keyboard.
  - `rainbow` moves a rainbow across the matrix columns (matrix index =
    column * 5 + row, so a column is one color at a given instant).
  `--period MS` sets one full loop (200-60000 ms; defaults: breath 4000,
  cycle 8000, rainbow 6000). `led brightness` scales animations like other host
  colors. The animation type, period, color, stride, and brightness are saved
  and restored after a reconnect or daemon restart. Any other `led` frame
  command or effect/profile change replaces the saved animation; use
  `led clear` to remove the saved visual configuration.
  Requires `KBDD_ENABLE_LED=1`.
- `led release` stops periodic LED frame streaming.
- `led effects` lists the built-in effects supported by the keyboard.
- `led effect <name|index>` changes the active profile effect.
- `led brightness <0-10>` changes brightness; `0` is off and `10` is maximum.
- `led speed <2-102>` changes the raw effect speed; lower values are faster. It is
  a single profile byte (block offset 19) shared by every effect, so it behaves the
  same whichever effect is active.
- `led color <effect> <RRGGBB> [RRGGBB ...]` sets the colors stored in the keyboard for
  a built-in effect of the active profile. Accepted color counts are: `wave` 1-14,
  `breath`/`encounter`/`pulse`/`cross`/`trigger`/`ripple`/`blossom` 1-2, and
  `starlight`/`rain`/`lighting` exactly 1. With fewer colors than slots they repeat.
  It does not switch the effect (use `led effect`); `cycle` and `game_mode` have no
  colors. The nine per-effect
  "factory rainbow" flags (block offsets 32-40) are cleared, as the official driver
  does on every write, otherwise the keyboard may show the factory rainbow instead of
  your color. The change is RAM only until `--save` or `led save`. Needs
  `KBDD_ENABLE_PROFILE_WRITE=1`. Example for a persistent solid red:
  `kbctl led effect lighting; kbctl led color lighting FF0000 --save`.
- `led save` makes the active profile's current block (effect, brightness, speed)
  permanent. `effect`, `brightness` and `speed` only change the keyboard's RAM; the
  keyboard writes its flash when the "custom LED count" byte (block offset 134)
  changes to `1` or `255`. `led save` writes the block with that byte set to `1`
  (the keyboard must read it back as `1`) and then to `255` (the clean value),
  and checks that the rest of the block is unchanged. Appending `--save` to
  `led effect|brightness|speed` does both steps. To limit flash wear, set
  several values and run `led save` once. A profile is never saved when that byte
  is not `255` (custom LED data in use) or when the effect is 14 or 15; the
  factory-reset value `0xFF` of opcode `0x01` is never sent. Needs
  `KBDD_ENABLE_PROFILE_WRITE=1`; a backup of the block is stored in
  `~/.local/state/kbctl/` first. Saved values survive unplugging the keyboard.

Frame commands accept `--once` to send one frame without periodic refresh and
`--stride N` to override the frame layout (default: the detected matrix size).
The daemon persists the selected lighting mode, custom colors, per-effect
speed, and global brightness in `$XDG_CONFIG_HOME/kbdd/config` (or
`~/.config/kbdd/config`). Selecting `solid`, `pos`, or `effect` replaces the
other visual modes, so only one is restored after a keyboard reconnect or
daemon restart. `led clear` removes all saved colors and disables the saved
visual mode; brightness remains available as the global setting for the next
mode.
The `effect`, `brightness`, and `speed` commands require
`KBDD_ENABLE_PROFILE_WRITE=1`; frame commands require `KBDD_ENABLE_LED=1`.

For complete command syntax:

```sh
kbctl help
```

Profile writes require the additional `KBDD_ENABLE_PROFILE_WRITE=1`
environment setting. Virtual keyboard input requires
`KBDD_ENABLE_UINPUT=1`. These safeguards are intentional while hardware
support is being validated.

Enabling uinput grants the daemon the ability to inject keystrokes into the
host, so it is disabled by default and should only be turned on on trusted,
single-user machines. To use it, grant `/dev/uinput` access (see
`packaging/udev/60-kbdd.rules` for a dedicated-group or uaccess rule), set
`KBDD_ENABLE_UINPUT=1` in the daemon environment, and restart `kbdd`. Never
enable it on shared or multi-user systems.

## Repository layout

```text
include/              Public C headers
src/core/             Protocol, profile, macro, and LED library code
src/daemon/           HID, uinput, daemon, and IPC implementation
src/cli/              kbctl command-line client
tests/                Unit and device-layer tests
packaging/            udev, systemd, and RPM packaging files
assets/               Project images used by the documentation
```

## Project status and limitations

This is not an official Gamakay project. Device behavior was reconstructed
from the original Windows executable and validated incrementally. The
supported device model, report layout, and write paths may evolve as more
hardware testing is completed. Review the source and test results before
enabling write operations on a production keyboard.

## License

This project is licensed under the **GNU General Public License, version 2
only**, matching the license used by the Linux kernel. See
[LICENSE](LICENSE) for the complete license text.

The Gamakay name and MK61 product designation belong to their respective
owners. They are used here only to identify the supported hardware.
