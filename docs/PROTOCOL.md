# Device and protocol reference

This records the investigation, not an official Holtek command specification.
Numbers in packet dumps are hexadecimal; profile offsets in tables are decimal.
"Observed" means the local A22A responded in the recorded experiment. "Candidate"
means inferred from public code or descriptors, without full behavioral validation.
Source IDs refer to [SOURCES.md](SOURCES.md); evidence is in [EXPERIMENTS.md](EXPERIMENTS.md).

## Identity and transport

| Attribute | Observed value |
| --- | --- |
| VID:PID | 04d9:a22a |
| bcdDevice | 0101, displayed by lsusb as 1.01; not necessarily firmware version |
| USB strings | E-Signal / USB Gaming Mouse |
| Serial number | None |
| Speed | Full Speed, 12 Mbit/s |
| EP0 maximum packet | 8 bytes |
| Declared power | 100 mA, bus powered, remote wakeup advertised |
| USB path during tests | 1-14, bus 001 device 005; not stable identifiers |
| Kernel HID binding | hid-generic |

| Interface | HID report descriptor | Interrupt endpoints | Role |
| --- | ---: | --- | --- |
| 0 | 71 bytes | 81 IN, max 8 bytes, interval 1 ms | Mouse |
| 1 | 140 bytes | 82 IN, max 16 bytes, interval 4 ms | Keyboard and auxiliary input |
| 2 | 32 bytes | 83 IN / 04 OUT, max 64 bytes, interval 4 ms | Vendor configuration |

Endpoint maximum size is not report length. Endpoint interval is not proof of the
selected polling rate or actual event frequency. Feature control transfers use EP0,
not the interrupt OUT endpoint. Linux hidraw indices were 4, 5, 6 during tests only.

### Interface 0 report descriptor

```text
05 01 09 02 a1 01 09 01 a1 00 05 09 15 00 25 01
19 01 29 05 75 01 95 05 81 02 95 03 81 01 05 01
16 01 80 26 ff 7f 09 30 09 31 75 10 95 02 81 06
15 81 25 7f 09 38 75 08 95 01 81 06 05 0c 0a 38
02 95 01 81 06 c0 c0
```

Unnumbered seven-byte input: byte 0 has five button bits and three padding bits;
1..2 signed little-endian relative X; 3..4 signed little-endian relative Y;
5 signed wheel; 6 signed AC Pan. X/Y logical range is -32767..32767.
Five reported buttons do not determine the number of physical controls.

### Interface 1 report descriptor

```text
05 01 09 06 a1 01 85 01 05 07 19 e0 29 e7 15 00
25 01 75 01 95 08 81 02 19 00 2a ff 00 15 00 26
ff 00 75 08 95 0e 81 00 c0 05 01 09 80 a1 01 85
02 19 81 29 83 15 00 25 01 75 01 95 03 81 02 95
05 81 01 c0 05 0c 09 01 a1 01 85 03 19 00 2a ff
02 15 00 26 ff 02 75 10 95 01 81 00 c0 05 01 09
02 a1 01 85 04 15 00 26 ff 7f 09 30 09 31 75 10
95 02 81 02 c0 06 01 ff 09 01 a1 01 85 06 15 00
26 ff 00 09 2f 75 08 95 03 81 00 c0
```

Input payload sizes excluding their one-byte Report ID:

| ID | Bytes | Descriptor meaning |
| --- | ---: | --- |
| 1 | 15 | Modifier byte plus 14 keyboard keycode slots |
| 2 | 1 | System control, three bits plus padding |
| 3 | 2 | Consumer control |
| 4 | 4 | Absolute X/Y, two 16-bit fields |
| 6 | 3 | Vendor page ff01, usage 2f; function unknown |

ID 6 must not be called DPI status without observed behavior.

### Interface 2 report descriptor

```text
06 00 ff 0a 00 ff a1 01 15 00 26 ff 00 09 20 75
08 95 40 81 02 09 21 91 02 09 22 95 08 b1 02 c0
```

Usage page and collection usage are ff00. Input usage 20 is 64 bytes; Output
usage 21 inherits the same count and is 64 bytes; Feature usage 22 is 8 bytes.
There is **no Report ID item**. Usage 22 is not a Report ID.

## API B framing

The unofficial API B from S1/S2 is supported by the observed command subset.
Do not generalize that conclusion to every command or field in the reference driver.

```text
Host feature buffer: 00 CMD ARG0 ARG1 ARG2 ARG3 ARG4 ARG5 CHECKSUM
USB feature payload:    CMD ARG0 ARG1 ARG2 ARG3 ARG4 ARG5 CHECKSUM
CHECKSUM = (0xff - CMD - sum(ARG0..ARG5)) & 0xff
```

The zero prefix is the HIDAPI/hidraw unnumbered-report placeholder, not part of
the eight-byte USB payload. Requests with this checksum worked; the investigation
did not deliberately submit incorrect checksums to prove firmware enforcement.
Observed responses have a zero final byte, not a valid request-style checksum.

Derived standard HID control setup for configuration interface 2:

| Request | bmRequestType | bRequest | wValue | wIndex | wLength |
| --- | --- | --- | --- | --- | --- |
| SET_FEATURE | 21 | 09 | 0300 | 0002 | 0008 |
| GET_FEATURE | a1 | 01 | 0300 | 0002 | 0008 |

These setup fields are the HID transport interpretation; no USBPcap/usbmon capture
was collected. The C implementation uses HIDIOCSFEATURE/HIDIOCGFEATURE instead
of constructing USB setup packets itself. Successful feature ioctls return 9 bytes.

### Observed commands

| CMD | Operation | Arguments and response | Current CLI |
| --- | --- | --- | --- |
| 00 | Echo | Four test bytes echoed from ARG0..3 | Historical probe only |
| 82 | Get active profile | ARG0 response is wire profile index | Used |
| 02 | Set active profile | ARG0 profile; verified by re-querying 82 | Used; 6 profiles |
| 83 | Active rate | ARG0 profile; ARG1 response is rate code | Used |
| 84 | Active resolution | ARG0 profile; ARG1 response is one-based slot | Used |
| 8c | Read profile | ARG0 profile; reply ARG1=80 hex; two 64-byte inputs | Used |
| 8d | Read buttons | Same block transport as 8c | Used (wheel/read) |
| 0c | Write profile | ARG0 profile, ARG1=80 hex, synchronized output chunks | Used; two writes passed |
| 04 | Select resolution | ARG0 profile, ARG1 slot | Used to reselect current slot |
| 03 | Set report rate | ARG0 profile, ARG1 rate code | Used; 1000 -> 500 confirmed |
| 0d | Write buttons | ARG0 profile, ARG1=80 hex, same chunks as 0c | Used (wheel) |

Rate encoding: 01=1000 Hz, 02=500 Hz, 04=250 Hz, 08=125 Hz. The active profile's
rate is a standalone command; it does not rewrite the 128-byte configuration block.
Observed: profile 1 returned code 01 (1000 Hz) initially, then code 02 (500 Hz)
after the owner-authorized `rate 500`. Profile 0 returned code 02 (500 Hz) on query.
Profile indices are zero-based in the reference; resolution slots are one-based.

### Profile count

Six profiles exist (indices 0..5), matching the reference driver's profile count.
The owner confirmed all six switch successfully via command 02. Index 6 and above
are silently rejected by the firmware: sending 02 {6} leaves the active profile
unchanged. Read-only queries at out-of-range indices return defaults or garbage
(profiles 6/7 return slot 02; profiles 8/9 return 0x55/0xaa), so query responses
alone do not establish the valid profile range. Profile 0's configuration block is
not a stable global template; its offset 2 changed from 0x3f to 0x00 after switching
profiles, so offset 2 is not a reliable enabled-profiles bitmask.

The configuration block's `enabled_rates` field (offset 64, observed 0x8f) is a
candidate bitmask of rates the firmware offers, and was not modified by the 03
command. The active rate applied via 03 was verified by re-querying 83.

### Read transaction

1. Query 82, then 84 to identify the active profile and slot.
2. Refuse unexplained queued input before initiating a block read.
3. Send 8c with the profile in ARG0.
4. GET_FEATURE must echo the command/profile and report length 80 hex.
5. Read exactly two 64-byte unnumbered Input reports, at most 1500 ms wait per chunk.

The tool intentionally does not strip a zero byte from interrupt-IN data. There is
no report-ID prefix there. It reads into 65 bytes to detect oversized reports.

### Write transaction

1. Read original profile, construct a copy with only the selected X low byte changed.
2. Recheck the active state and full original block.
3. Send 0c with ARG0=profile and ARG1=128 decimal.
4. GET_FEATURE readiness must echo command/profile and show remaining=128.
5. Write zero placeholder plus the first 64 payload bytes to hidraw (65-byte call).
6. GET_FEATURE must show remaining=64 before the second 65-byte call.
7. GET_FEATURE must show remaining=0 before continuing.
8. Read back all 128 bytes and require byte equality with the intended block.
9. Send 04 to reselect the same slot; requery state and compare the block again.

Readiness is polled at most 50 times with 1 ms sleeps. Ioctls retain kernel USB
timeouts; this is not a hard 50 ms end-to-end deadline. A timeout/short transfer or
unexpected response stops the operation. Further blind writes are not attempted.
The entire transaction is non-atomic and not guaranteed recoverable after failure.
Both successful experiments passed the stricter command/profile readiness checks.

### Commands not authorized by this implementation

S2 defines writes 02 (active profile), 0f (macro), and read 8f (macro). Command 02
and 0d/03/0c are now implemented; 0f/8f (macro) remain unimplemented. The reference
explicitly labels 0e and 0f with ARG0 > 50 dangerous. This is not a complete list of
unsafe combinations. No generic raw-command CLI is provided.

## Profile layout

Each of the six profiles exposes a 128-byte configuration block (read 8c, write 0c).
Profile 0 additionally carries the global device state (sensor init, enabled
profiles); profiles 1..5 are per-profile (DPI tables, local lighting, button map).
Field names below come from S2's packed struct and are **candidate** unless marked
"known". Only the fields explicitly marked as written have been changed on hardware.

Status legend: **known** = behavior observed on this device; **candidate** = inferred
from the reference driver / plausibly matches, not validated; **unknown** = no
reliable interpretation yet.

| Offset | Len | Name | Status | Notes |
| --- | ---: | --- | --- | --- |
| 0..1 | 2 | (global) | unknown | Profile 0 = `88 00`; profile 1..5 = `ff ff` |
| 2 | 1 | enabled profiles | candidate | Not stable; profile 0 read 0x3f then 0x00 after profile switch |
| 3..4 | 2 | (global) | unknown | Profile 0 = `42 03`/`00 00` across states |
| 5 | 1 | sensor SROM ID | candidate | Profile 0 = `03`; not a sensor model ID; NDA field |
| 6..7 | 2 | sensor fw size | candidate | Little endian; profile 0 = `fe 0f` (4094) |
| 8..15 | 8 | password | candidate | Not used for local API B transport |
| 16..23 | 8 | DPI indicator enable | candidate | Per-DPI indicator LED enable |
| 24..47 | 24 | illumination RGB | candidate | Eight RGB entries (3 bytes each) |
| 48..63 | 16 | sensor reg config | candidate | Eight register/value pairs; profile 1..5 = `2e 10 42 00 00...`; NDA |
| 64 | 1 | enabled rates | candidate | Bitmask; observed `8f` (high bits uninterpreted) |
| 65..69 | 5 | (padding) | unknown | |
| 70 | 1 | resolution count | **known** | Number of active slots; writable via `slot -c` |
| 71 | 1 | illumination mode | candidate | |
| 72 | 1 | illumination intensity | candidate | |
| 73 | 1 | illumination speed | candidate | |
| 74..75 | 2 | X/Y scale | candidate | Both 100 decimal; guard for DPI writes |
| 76..81 | 6 | (padding) | unknown | No confirmed XY-mode flag |
| 82..83 | 2 | X/Y high-bit masks | candidate | Ninth DPI bit per slot; observed `20 20` (slot 6 only) |
| 84..91 | 8 | X DPI table | **known** | Low byte = DPI/100, 6-bit; slot 1 at 84 writable |
| 92..99 | 8 | Y DPI table | candidate | Preserved untouched; shared-X assumed |
| 100 | 1 | enabled resolutions | candidate | Bitmask; observed `ff` |
| 101..102 | 2 | (padding) | unknown | |
| 103 | 1 | debounce | candidate | Milliseconds; observed 20 (0x14) |
| 104..127 | 24 | DPI RGB colors | candidate | Eight RGB entries (3 bytes each) |

### Field status by profile

Profile 0 is the global block; its offsets 0..63 hold sensor/global fields. Profiles
1..5 have `ff ff` at 0..1, zero global fields, and their own per-profile tables
(DPI at 84..99, count at 70, lighting at 71..73/104..127, button map in the separate
button block). The sensor init fields (5..7, 48) exist only in profile 0, which is
why the wheel-write firmware bug (which zeroes them) kills tracking across all
profiles; see [EXPERIMENTS.md](EXPERIMENTS.md).

### DPI encoding and limits

For the tested slot/configuration, `X low byte = DPI / 100`: 0c=1200, 20=3200,
10=1600. The writes and user feedback support using X as the shared setting.
No verified dynamic shared/independent XY switch was found. The public driver's
`independent_xy` comes from host-side sensor metadata, not a mode bit it reads.

The sensor is inferred to be a PixArt PAW33xx part (PAW3333 family, see
[SOURCES.md](SOURCES.md) S6). It exposes a **6-bit resolution register**, so only
raw 0..63 map monotonically to 0..6300 CPI. Raw values 64..255 wrap modulo 64:

| raw | raw mod 64 | effective CPI |
| ---: | ---: | ---: |
| 12 | 12 | 1200 |
| 16 | 16 | 1600 |
| 32 | 32 | 3200 |
| 62 | 62 | 6200 (fast) |
| 63 | 63 | 6300 |
| 64 | 0 | 0 (minimum; feels very slow) |
| 80 | 16 | 1600 |

This empirically explains every observed anomaly: 6400 (raw 64) became the slowest,
8000 (raw 80) behaved like 1600, and "6 is faster than 7" (raw 62 > raw 80). It is
consistent with the firmware passing the low six bits of the host value to the sensor.

The CLI therefore accepts `dpi` values from 100..6300 by 100, writes only
`84 + slot - 1`, and preserves both candidate high masks. If the selected X
high-mask bit is set, writes are refused. Displayed values apply the six-bit mask,
including untested/inactive slots, so a stored 224 (raw 224) is reported as
224 & 63 = 32, i.e. 3200 CPI, not 22400. Count and enable mask are displayed using
the reference interpretation but never normalized/written.

Restore may reinstate a backed-up low-byte value outside the `dpi` command's range
(it also re-applies the six-bit mask for display). It still requires the inspected
scales, compatible layout and clear selected X high-mask bit, and permits no
unrelated byte differences.

### Button records

The button block is read with 8d and written with 0d, using the same two-chunk
transport as the profile block. It holds 16 four-byte records followed by 64 zero
bytes. Each record is `type(1) data(3)`.

| Type | Name | Decode | Status |
| --- | --- | --- | --- |
| 0x00 | keyboard | modifiers(1) hid_key(1) hid_key2(1) | candidate |
| 0x01 | mouse | pad(1) button(1) pad(1); f0=left f1=right f2=middle f3=mb4 f4=mb5 | **known** |
| 0x02 | acpi | | candidate |
| 0x03 | media | pad(1) usage LE(2) | candidate |
| 0x04 | scroll | pad(1) event(1) pad(1); 01=up 02=down | **known** (records 14/15 written) |
| 0x05 | rate | | unknown (label only; no decode) |
| 0x06 | report | | candidate |
| 0x07 | dpi | pad(1) event(1) pad(1); 03=cycle | **known** |
| 0x08 | profile | | candidate |
| 0x09 | macro | mode(1) index(1) pad(1) | candidate |
| 0x0a | multiclick | hid_key(1) delay(1) count(1) | candidate |
| 0x0b | special | | unknown (label only; no decode) |
| 0x0c | (quirks) | maps to left-click | **known** (reference quirks map) |

Observed records (profile 1): 0..4 = mouse left/right/middle/mb4/mb5 (`01 00 f0..f4
00`), 5 = DPI cycle (`07 00 03 00`), 6/9/10 = `0c 00 00 00` (left-click synonym),
7 = `0b 00 02 02`, 8 = `0a f0 21 03` (left multiclick x3), 14/15 = scroll down/up
(`04 00 02 00` / `04 00 01 00`), 11..13 = zero (none).

Profile 0 retains the fuller factory map: rate cycle (`05 00 03 00`), media
(`03 00 23 02` = AC Home), and a normal wheel direction.

A firmware-level chord (side button + right button = DPI step) is not represented
in this table; see [EXPERIMENTS.md](EXPERIMENTS.md). Only the scroll direction
records (14/15) are written by this project.

### Wheel direction

Scroll records 14 and 15 live at byte offsets 56 and 60 in the button block. Each
record is `type(0x04) pad event pad`; event 0x01 = up, 0x02 = down. Normal layout
is record 14 = up, record 15 = down. Swapping the two event bytes inverts scrolling
(the macOS/GNOME "natural scrolling" sense). `show` reports `normal` or `natural`.

The `wheel` subcommand flips the current direction by swapping only those two event
bytes. It preserves every other byte and verifies a full readback after writing via
command 0d. No activation command is sent. The direction is also reported by `show`.

**Firmware bug and mitigation:** writing the button block (0d) has been observed to
zero parts of the global profile-0 configuration (the sensor SROM ID, sensor firmware
size, and the sensor register area), which stops X/Y tracking until profile 0 is
restored. All write commands (`dpi`, `slot -c`, `rate`, `wheel`) snapshot profile 0
before the write and, if the block changes afterward, write the snapshot back. No
offsets or values are hardcoded; the snapshot is read at runtime, so the mitigation
applies to other mice of this firmware family too. A warning is printed before the
`wheel` write (the operation known to trigger the bug).

The vendor driver reportedly had a bug that left this state inverted and could not
restore it. The recorded write flipped `natural -> normal`; the direction field is
now a supported, reversible setting.

## Snapshot format

The `snapshot`/`restore` commands use a full-device snapshot, replacing the earlier
per-profile backup files. Layout is explicit, not an ABI-dependent C struct:

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 9 | ASCII A22ASNAP |
| 9 | 2 | Format version, little endian; 01 00 |
| 11 | 128 | Profile 0 config block |
| 139 | 128 | Profile 0 button block |
| ... | ... | Repeating config+button pairs for profiles 1..5 |
| 1547 | 4 | FNV-1a32 of bytes 0..1546, little endian |

Total size is 1551 bytes. FNV basis=2166136261, multiplier=16777619, wrapping
uint32_t. The checksum detects accidental corruption, not malicious edits. The magic
implies A22A but does not identify a physical unit; no serial number is stored.
`snapshot FILE` writes mode 0600; `restore FILE` validates magic, version and checksum
before writing anything, then writes profile 0 config first (sensor init fields),
then profiles 1..5 config, then all button blocks.
