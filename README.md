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
- Profile inspection and active-profile queries.
- LED color, position, sweep, brightness, speed, and built-in effect commands.
  The `led brightness` value also scales host-driven solid colors, which are
  sent as direct RGB frames instead of using the keyboard's built-in effect.
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

Build outputs are placed under `build/`; the libraries are produced as
`libkbcore.a` and `libkbcore.so`.

## Installation

The project includes a Fedora-oriented convenience script that builds,
tests, installs, and starts the user service:

```sh
./install.sh
```

LED and profile writes are opt-in:

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

After updating an existing installation, rerun `./install.sh` so the user
service receives the `ReadWritePaths` permission required to persist LED
settings.

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
kbctl active-profile
```

- `list` lists the devices discovered by `kbdd`.
- `status` shows daemon state, connected devices, and enabled capabilities.
- `version` prints the `kbctl` and `libkbcore` versions.
- `read-config` reads the daemon configuration summary.
- `enable-notifications` enables keyboard input notifications.
- `active-profile` queries the active profile from the keyboard.

### Profile commands

Profile indexes use `0` through `3` for the four profiles:

```sh
kbctl read-profile [index]
kbctl write-profile <index> <file>
kbctl dump-block <1-4> [file]
kbctl diff-block <a.bin> <b.bin>
```

- `read-profile` reads a profile; without an index it reads profile `0`.
- `write-profile` writes a profile file and requires
  `KBDD_ENABLE_PROFILE_WRITE=1`. The CLI backs up the active profile first.
- `dump-block` reads and displays a raw 264-byte profile parameter block,
  optionally saving it to a file.
- `diff-block` compares two saved 264-byte blocks and reports changed offsets.

### LED commands

LED commands are available when LED support has been explicitly enabled:

```sh
kbctl led effects
kbctl led solid FF0000
kbctl led clear
kbctl led pos 12 00FF00
kbctl led pos 24 FF0000
kbctl led off
kbctl led sweep --delay 100 --color 0000FF
kbctl led release
kbctl led effect lighting
kbctl led brightness 8
kbctl led speed 50
```

- `led solid <RRGGBB>` fills every matrix position with one RGB color.
- `led off` turns every matrix position off.
- `led clear` clears the host-controlled frame.
- `led pos <n> <RRGGBB>` changes one zero-based position while preserving
  other colors, allowing a multicolor custom layout.
- `led sweep` lights positions sequentially to map physical keys. Use
  `--delay MS`, `--color RRGGBB`, and `--from N`.
- `led release` stops periodic LED frame streaming.
- `led effects` lists the built-in effects supported by the keyboard.
- `led effect <name|index>` changes the active profile effect.
- `led brightness <0-10>` changes brightness; `0` is off and `10` is maximum.
- `led speed <2-102>` changes raw effect speed; lower values are faster.

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
