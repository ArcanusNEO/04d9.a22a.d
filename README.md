# 04d9:a22a Holtek / E-Signal mouse configuration

A small Linux userspace configuration driver in C for the inspected
`04d9:a22a`, `bcdDevice=0101`, E-Signal USB Gaming Mouse. It uses the kernel
hidraw interface; it is **not** a kernel module or a replacement input driver.

## Status

As of 2026-09-07, current-slot DPI writes **1200 -> 3200 -> 1600** succeeded on
the connected mouse, with full profile readback before and after slot activation.
That was the last state recorded at the time. A read-only observation on
2026-09-29 reports profile 1 / slot 1, X raw `0x18` (Sensor=3327 estimate 2400
CPI), Y raw `0x14`, and 250 Hz. This estimate is not a physical calibration.

The fitted sensor identity, accurate physical CPI, the effective maximum, other
revisions and power-cycle persistence remain unestablished. The CLI uses the
OEM Sensor=3327 write encoding, but only the low 1200/1600/3200 settings have
hardware write/readback evidence. This is an experimental single-device tool,
not a universal Holtek driver.

## Project goals

1. Reproduce this mouse's configuration protocol without the vendor driver or
   retail model information.
2. Support safe, minimal current-slot DPI changes, preserving unrelated bytes.
3. Keep discovery, protocol assumptions, test results and sources auditable.
4. Extend capabilities only after targeted experiments establish the fields.
5. Avoid firmware updates, unexplained writes, system-wide installation and services.

Implemented: device/revision/descriptor checks, state reads, profile download,
DPI inspection, guarded DPI write, slot selection and count (`slot`), report
rate (`rate`), profile switching and duplication (`profile`), wheel direction
(`wheel`), full snapshot/restore, and LED mode/parameter writes using the
statically recovered Windows-writer encoding.

Not implemented: LED color-mode/indicator-enable switches, X/Y scale controls,
button remapping, macros, profile rename, firmware access, GUI, daemon, udev
rules, or support for other Holtek VID/PIDs.

LED mode writes use the recovered Windows mode enum and per-effect transforms.
The visible device effects and the Windows slider-to-A/B conversion have not
been hardware-verified. Do not interpret A/B as generic brightness/speed values;
see the mode table below and [docs/STATUS.md](docs/STATUS.md).

## Build and offline tests

Requirements: Linux, a C11 compiler (GCC tested), libc development headers, Linux
UAPI headers (`linux/hidraw.h`, `linux/input.h`) and GNU Make. No libusb, HIDAPI,
Python, network access or root is needed to build/test.

```sh
make
make test
make sanitize
```

`make sanitize` needs ASan/UBSan support. Neither accesses USB. See
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for manual builds and test coverage.

## Use

Close other mouse configuration applications. Only one matching mouse may be
connected. Root is needed because hidraw is mode 0600. The program finds the
device dynamically; do not hardcode `/dev/hidraw6`.

```sh
sudo ./src/main show          # print current state
sudo ./src/main               # no args equals show
sudo ./src/main help          # or -h/--help/usage/--usage
```

`show` sends query requests and **does not write configuration**. All other
subcommands write immediately and may persist:

```sh
sudo ./src/main dpi 3200     # 200..12400, step 100; Sensor=3327 encoding
sudo ./src/main slot 7       # select active slot (1..8)
sudo ./src/main slot -c 8 7  # set count then select slot
sudo ./src/main rate 500     # 125/250/500/1000 Hz
sudo ./src/main profile 3    # switch active profile (0..5)
sudo ./src/main profile -d 1 2  # copy profile 1 -> 2
sudo ./src/main wheel        # flip scroll direction
sudo ./src/main led mode breathing 6 # OEM mode 3; B=6 -> bytes 71..73: 3,0,5
sudo ./src/main led mode wave 80 3   # OEM mode 5; A/B -> bytes 5,80,3
sudo ./src/main led color fixed ff8040 # fill global color table
sudo ./src/main led color dpi 4 00ff00 # set stage 4 in active profile
```

`slot SLOT` selects only; it does not auto-raise the count and reports failure
if the firmware rejects it. `slot -c N SLOT` sets the count first when it differs.

### LED modes

`led mode MODE [A [B]]` edits mode byte 71 and both mode-parameter bytes 72/73
in one guarded profile write. Pass exactly the number of parameters required by
the mode:

| Windows mode | CLI name | Byte 71 | Arguments | Bytes 72/73 |
| --- | --- | ---: | --- | --- |
| Off | `off` | 0 | none | `0, 0` |
| Standard | `standard` | 1 | `A` (0..255) | `A, 0` |
| Twinkle | `twinkle` | 2 | `A` (0..255) | `A, 0` |
| Breathing | `breathing` | 3 | `B` (0..11) | `0, 11-B` |
| Neon | `neon` | 4 | `B` (0..11) | `0, 11-B` |
| Wave | `wave` | 5 | `A B` (both 0..255) | `A, B` |
| Slide | `slide` | 6 | none | `0, 0` |
| Finger movement | `finger` | 7 | `A` (0..255) | `A, 0` |
| Flip up and down | `flip` | 8 | `A` (0..255), `B` (0..20) | `A, 20-B` |
| YO-YO | `yoyo` | 9 | `A` (0..255), `B` (0..15) | `A, 15-B` |
| Cross flash | `cross` | 10 | `A` (0..255), `B` (0..30) | `A, 30-B` |
| Flying star | `flying` | 11 | none | `0, 0` |

The A/B values are inputs to the recovered OEM writer transform, not a claim
that the vendor application's 0..255 sliders map directly to these values. A
successful command verifies the profile block and activation readback; visible
lighting behavior remains unverified on the attached mouse.

### LED colors

Colors are six hexadecimal digits (`RRGGBB`); a leading `#` is also accepted
(quote it in a shell command). The global color table is in profile 0 at bytes
24..47, while eight per-DPI colors are stored at bytes 104..127 of profiles
1..5:

```sh
sudo ./src/main led color fixed ff8040
sudo ./src/main led color palette ff0000 ff8000 ffff00 80ff00 00ffff \
  0080ff 8000ff ff00ff
sudo ./src/main led color dpi 4 00ff00
```

`fixed` repeats one RGB value across the eight profile-0 entries; `palette`
writes eight distinct entries. Both preserve profile-0 indicator-enable bytes
16..23 and restore the previously active profile after the write. `dpi SLOT`
updates one color entry in the active profile (1..5) and preserves the other
seven entries. The command writes the recovered table layout and checks the
configuration readback; it does not change the vendor application's color-mode
radio choice or indicator-enable switches. Those controls and the physical LED
response still need a vendor trace and hardware verification.

The CLI uses the OEM Sensor=3327 stage encoder for 200..12400. High-range odd
steps can map to the preceding 200-CPI code value; the reported number is an
encoding estimate, not calibrated CPI. Hardware writes are confirmed only for
1200, 1600 and 3200.

The tested 1200/1600/3200 settings follow 100-CPI steps. The high-range encoding
now follows the OEM writer, but physical CPI and the effective maximum remain
unverified. The OEM Windows package declares `Sensor=3327`, superseding the
earlier PAW3333/modulo-64 inference. See
[docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md).

**Firmware-bug warning:** writing the button block can corrupt the global
profile-0 sensor configuration. `wheel` prints a warning, snapshots profile 0,
and restores it automatically if corruption is detected. If movement stops after
`wheel`, run `restore` with a saved snapshot.

### Snapshot and restore

```sh
sudo ./src/main snapshot /path/to/a22a.snap
sudo ./src/main restore /path/to/a22a.snap
```

`snapshot` saves all six profiles' config and button blocks; it does not capture
report rates or the active profile/slot. Snapshots carry a checksum and are
validated before restore. The owner has verified a normal restore on the attached
mouse; partial-transfer/error recovery still needs validation. Keep a known-good
snapshot (`recovery/a22a-baseline.snap`) as a recovery point.

## Safety boundaries

- Writes may be persistent; there is no verified volatile-only command.
- Interrupted/unsynchronized writes can leave an unknown state and may soft-brick
  the device. Do not unplug, stop the process, or press DPI/profile buttons.
- The advisory lock coordinates this tool, not unrelated applications.
- No updater, bootloader, firmware or unknown-command scanning exists.
- No automatic backup is created; use `snapshot`/`restore` explicitly.

## Documentation map

| Path | Content |
| --- | --- |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | Identity, descriptors, transport, commands, fields, backup layout |
| [docs/STATUS.md](docs/STATUS.md) | Implemented/not-implemented features and field understanding |
| [docs/EXPERIMENTS.md](docs/EXPERIMENTS.md) | Evidence, raw baseline blocks, successful writes, validation limits |
| [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) | OEM Windows installer extraction, HID DLL findings and conflicting sensor metadata |
| [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) | Windows/Linux capability comparison, confirmed gaps and staged completion plan |
| [vendor/IE3.0+A3327_setup.exe](vendor/IE3.0+A3327_setup.exe) | Checked-in original Windows setup package used for static analysis |
| [docs/SOURCES.md](docs/SOURCES.md) | Pinned code sources, official documents, comparison and provenance |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Build/test workflow, code map, safeguards, next work |
| [docs/references/README.md](docs/references/README.md) | Four official Holtek PDFs, URLs and SHA-256 hashes |
| [recovery/a22a-baseline.snap](recovery/a22a-baseline.snap) | Baseline device snapshot for recovery |
| `src/main.c` | Driver and encoding self-tests |
| `test/main.c` | Offline mocked transport tests (includes `src/main.c`) |
| `Makefile` | Recursive build, test, sanitize and clean targets |

Build output stays in `src/` and `test/`. Nothing is installed automatically. No
project license is assigned here; upstream code references and Holtek PDFs retain
their respective rights and license terms.
