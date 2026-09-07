# 04d9:a22a Holtek / E-Signal mouse configuration

A small Linux userspace configuration driver in C for the inspected
`04d9:a22a`, `bcdDevice=0101`, E-Signal USB Gaming Mouse. It uses the existing
kernel hidraw interface; it is **not** a kernel module or a replacement input driver.

## Status

As of 2026-09-07, current-slot DPI writes **1200 -> 3200 -> 1600** succeeded on
the connected mouse. Full profile readback passed before and after slot activation,
and the owner confirmed that both changes worked. The last intentionally retained
setting is **1600 DPI, profile index 1, slot 1**. These are wire indices.

Changing only the X table worked while the Y table remained unchanged. This is
practical evidence for shared-X DPI in the tested configuration, not proof of an
identified firmware XY-mode flag. Sensor identity, accurate physical CPI, full DPI
range, other revisions and power-cycle persistence have not been established.

The saved C files are byte-identical to the tested `/tmp/opencode/` implementation.
This remains an experimental single-device tool, not a universal Holtek driver.

## Project goals

1. Provide a reproducible C implementation of this mouse's configuration protocol
   without the unavailable vendor driver or retail model information.
2. Support safe, minimal current-slot DPI changes, preserving every unrelated byte.
3. Keep discovery, protocol assumptions, test results and information sources auditable.
4. Extend capabilities only after targeted experiments establish the relevant fields.
5. Avoid firmware updates, unexplained writes, system-wide installation and services.

Implemented: device/revision/descriptor checks, current profile/slot reads, profile
download, DPI inspection and dry-run diff, guarded DPI write, backup and restricted
restore, active-slot selection and resolution-count control (`slot`), report rate
(`rate`) control, active-profile switching (`profile`, 0..5) and wheel direction
toggle (`wheel`).

Not implemented: independent XY control, RGB, remapping, macros, profile copy/rename,
firmware access, GUI, daemon, udev rule installation, or automatic support for other
Holtek VID/PID combinations.

## Build and offline tests

Requirements: Linux, a C11 compiler (GCC tested), libc development headers, Linux
UAPI headers (`linux/hidraw.h`, `linux/input.h`) and GNU Make. No libusb, HIDAPI,
Python, network access or root permission is needed to build/test.

Run in this directory:

```sh
make
make test
make sanitize
make analyze
```

`make sanitize` needs compiler ASan/UBSan support. `make analyze` uses GCC's
`-fanalyzer`. Neither accesses USB. See [development](docs/DEVELOPMENT.md) for
manual build commands, test coverage and failure handling.

## Use

Close other mouse configuration applications. Only one matching mouse may be
connected. Root is needed on the inspected machine because hidraw is mode 0600.
The program finds the device dynamically; do not hardcode `/dev/hidraw6`.

```sh
sudo ./build/main show          # 打印当前状态
sudo ./build/main               # 无参数等价于 show
sudo ./build/main help          # 或 -h/--help/usage/--usage
sudo ./build/main plan 3200
```

Both commands send query requests via SET_FEATURE, but **do not write configuration**,
activate a slot or create backups. `plan` prints the proposed changed offsets.

All other subcommands write immediately (no confirmation gate) and may persist:

```sh
sudo ./build/main dpi 3200
```

The CLI accepts multiples of 100 from 100 to 6300. The sensor is inferred to be a
PixArt PAW33xx part with a 6-bit resolution register: raw values 0..63 map to
0..6300 CPI, and raw >= 64 wraps modulo 64 (so 6400 acts as 0, 8000 as 1600). The
6300 limit reflects this discovered wrap, not a proven maximum sensor rating. Only
1200 (initial), 3200 and 1600 have direct user confirmation; the wrap is confirmed
by the slot 6 (raw 62, fast) versus slot 7 (raw 80, slow) comparison.

The active resolution slot can be switched, and the number of enabled slots can be
changed. The firmware rejects selecting a slot above the configured count, so the
count may need raising first:

```sh
sudo ./build/main slot 7
sudo ./build/main slot --count 8 7
sudo ./build/main slot -c 8 7
```

`slot SLOT` only selects the active slot; it does not auto-modify the count and
reports failure if the firmware rejects it. `slot [-c|--count N] SLOT` first sets
the resolution count to N (when it differs), then selects SLOT.

The report rate can be changed with command 03, independent of the configuration
block. Options are 125, 250, 500 and 1000 Hz:

```sh
sudo ./build/main rate 500
```

The current rate is also shown by `show`.

The active profile can be switched with command 02. Six profiles exist (indices
0..5); the firmware silently rejects switching to index >= 6:

```sh
sudo ./build/main profile 0
```

The current profile is also shown by `show`.

A profile's config and button blocks can be duplicated into another profile
(indices 0..5; source and destination must differ). The global profile-0 block is
snapshotted and restored around the write, like other write commands:

```sh
sudo ./build/main profile -d 1 2
sudo ./build/main profile --duplicate 1 2
```

The wheel scroll direction can be flipped with a single toggle (button block command
0d). This also recovers from the vendor driver's inverted-scroll bug:

```sh
sudo ./build/main wheel
```

**Firmware-bug warning:** writing the button block can corrupt the global
profile-0 sensor configuration on this firmware family. `wheel` prints a warning,
snapshots profile 0, and automatically restores it if corruption is detected.
If movement stops after `wheel`, run `restore` with a previously saved snapshot.

The current wheel direction is also shown by `show`.

### Snapshot and restore

`snapshot` saves the full device state (all six profiles' config and button blocks)
to a file; `restore` writes that state back. No automatic backup files are created
by other subcommands:

```sh
sudo ./build/main snapshot /path/to/a22a.snap
sudo ./build/main restore /path/to/a22a.snap
```

Snapshots carry a checksum and are validated before restore. They do not encode a
serial number; they hold configuration only. Keep a snapshot from a known-good state
as a recovery point before experimenting.

## Safety boundaries

- Writes may be persistent. There is no verified volatile-only setting command.
- The protocol transfers a full 128-byte block even though only one byte is changed.
- Interrupted/unsynchronized writes can leave an unknown state and may soft-brick
  related devices. Do not unplug, stop the process, or press DPI/profile buttons.
- Advisory locking coordinates this tool, not unrelated applications or device buttons.
- No updater, bootloader, firmware-memory, unknown-command or password scanning exists.
- No automatic backup is created; use `snapshot`/`restore` explicitly.
- No serial number exists; the program cannot distinguish physically identical units.

## Documentation map

| Path | Content |
| --- | --- |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | Identity, descriptors, transport, commands, fields, backup layout |
| [docs/STATUS.md](docs/STATUS.md) | Implemented/not-implemented features and field understanding |
| [docs/EXPERIMENTS.md](docs/EXPERIMENTS.md) | Evidence, raw baseline blocks, successful writes, validation limits |
| [docs/SOURCES.md](docs/SOURCES.md) | Pinned code sources, official documents, comparison and provenance |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Build/test workflow, code map, safeguards, next work |
| [references/README.md](references/README.md) | Four complete official PDFs, retrieval URLs and SHA-256 hashes |
| `main.c` | Current working driver and encoding self-tests |
| `main-test.c` | Offline mocked transport tests |
| `.clang-format` | GNU-based clang-format style (SortIncludes: Never) |
| `Makefile` | Build, test, sanitize, analyze and clean targets |

Build output stays in `build/`. Nothing is installed automatically. No Git repository
or commit was created for this handoff. No project license is assigned here; upstream
code references and Holtek PDFs retain their respective rights and license terms.
