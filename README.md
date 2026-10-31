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
- Optional virtual keyboard input through `uinput`.
- Unix-socket IPC between `kbctl` and `kbdd`.
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

## Usage

After `kbdd` is running, use `kbctl` to inspect the connection:

```sh
kbctl status
kbctl list
kbctl version
kbctl active-profile
```

LED commands are available when LED support has been explicitly enabled:

```sh
kbctl led effects
kbctl led solid FF0000
kbctl led off
kbctl led pos 12 00FF00 --once
kbctl led sweep --delay 100 --color 0000FF
kbctl led release
```

For the complete command list and option syntax:

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
