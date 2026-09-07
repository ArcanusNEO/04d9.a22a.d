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
| 82 | Active profile | ARG0 response is wire profile index | Used |
| 83 | Active rate | ARG0 profile; ARG1 response is rate code | Historical probe only |
| 84 | Active resolution | ARG0 profile; ARG1 response is one-based slot | Used |
| 8c | Read profile | ARG0 profile; reply ARG1=80 hex; two 64-byte inputs | Used |
| 8d | Read buttons | Same block transport as 8c | Historical probe only |
| 0c | Write profile | ARG0 profile, ARG1=80 hex, synchronized output chunks | Used; two writes passed |
| 04 | Select resolution | ARG0 profile, ARG1 slot | Used to reselect current slot |
| 83 | Get report rate | ARG0 profile; ARG1 response is the rate code | Used |
| 03 | Set report rate | ARG0 profile, ARG1 rate code | Used; 1000 -> 500 confirmed |

Rate encoding: 01=1000 Hz, 02=500 Hz, 04=250 Hz, 08=125 Hz. The active profile's
rate is a standalone command; it does not rewrite the 128-byte configuration block.
Observed: profile 1 returned code 01 (1000 Hz) initially, then code 02 (500 Hz)
after the owner-authorized `rate 500`. Profile 0 returned code 02 (500 Hz) on query.
Profile indices are zero-based in the reference; resolution slots are one-based.
The code accepts profile indices 0..5 and slots 1..8, but active profile 1 / slot 1
is the only tested write destination.

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
2. Create/flush/report a backup, then recheck the active state and full original block.
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

S2 defines writes 02 (active profile), 0d (button block), 0f (macro),
and read 8f (macro). They have not been exercised on this A22A in this project.
Command 03 (rate) is now implemented and validated; command 02 (active profile)
remains unimplemented. The reference explicitly labels 0e and 0f with ARG0 > 50
dangerous. This is not a complete list of unsafe combinations. No generic
raw-command CLI is provided.

## Profile layout

The 128-byte layout below comes from S2's packed struct. Field names alone do not
confirm A22A semantics; only the selected X low byte has been changed experimentally.
Profile 0 carries candidate global fields that are mostly absent in profile 1.

| Offset | Length | Reference meaning / evidence |
| --- | ---: | --- |
| 0..1 | 2 | Unknown global bytes |
| 2 | 1 | Enabled profiles; profile 0 observed 3f |
| 3..4 | 2 | Unknown |
| 5 | 1 | Candidate sensor SROM ID, not a sensor model ID |
| 6..7 | 2 | Candidate sensor firmware size, little endian |
| 8..15 | 8 | Candidate password field; not used for local API B transport |
| 16..23 | 8 | Candidate DPI indicator enable data |
| 24..47 | 24 | Candidate eight illumination RGB entries |
| 48..63 | 16 | Opaque sensor register configuration pairs |
| 64 | 1 | Enabled rates; observed 8f, not only the four known low bits |
| 65..69 | 5 | Unknown |
| 70 | 1 | Candidate resolution count; profile 1 has 1 |
| 71 | 1 | Candidate illumination mode |
| 72 | 1 | Candidate illumination intensity |
| 73 | 1 | Candidate illumination speed |
| 74..75 | 2 | Candidate X/Y scales, both 100 decimal in current profile |
| 76..81 | 6 | Unknown; no confirmed XY-mode flag |
| 82..83 | 2 | Candidate X/Y ninth-bit masks; interpretation unverified |
| 84..91 | 8 | X DPI low-byte table; slot 1 at 84 confirmed writable |
| 92..99 | 8 | Y DPI low-byte table; unchanged throughout tests |
| 100 | 1 | Candidate enabled-resolution bitmap, observed ff |
| 101..102 | 2 | Unknown |
| 103 | 1 | Candidate debounce milliseconds, current profile 20 |
| 104..127 | 24 | Eight candidate DPI RGB colors, three bytes each |

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

The CLI therefore accepts `set`/`plan` DPI from 100..6300 by 100, writes only
`84 + slot - 1`, and preserves both candidate high masks. If the selected X
high-mask bit is set, writes are refused. Displayed values apply the six-bit mask,
including untested/inactive slots, so a stored 224 (raw 224) is reported as
224 & 63 = 32, i.e. 3200 CPI, not 22400. Count and enable mask are displayed using
the reference interpretation but never normalized/written.

Restore may reinstate a backed-up low-byte value outside the set command's range
(it also re-applies the six-bit mask for display). It still requires the inspected
scales, compatible layout and clear selected X high-mask bit, and permits no
unrelated byte differences.

### Button records

The historical 8d read returned 16 four-byte records followed by 64 zero bytes.
Record 0: 01 00 f0 00 (left), 1: f1 (right), 2: f2 (middle), 3: f3 (side 4),
4: f4 (side 5), all with the same surrounding bytes. Record 5: 07 00 03 00
(DPI cycle). Records 14/15: 04 00 02 00 / 04 00 01 00 (down/up scroll).
These meanings align with S2, but physical-control mapping was not systematically
tested. Other records include unknown special actions.

The button block is read with 8d and written with 0d, using the same two-chunk
transport as the profile block. Only the scroll direction records are currently
written by this project.

### Wheel direction

Scroll records 14 and 15 live at byte offsets 56 and 60 in the button block. Each
record is `type(0x04) pad event pad`; event 0x01 = up, 0x02 = down. Normal layout
is record 14 = up, record 15 = down. Swapping the two event bytes inverts scrolling.

The `wheel` subcommand flips the current direction by swapping only those two event
bytes. It preserves every other byte, backs up the block, and verifies a full
readback after writing via command 0d. No activation command is sent. The direction
is also reported by `show`.

The vendor driver reportedly had a bug that left this state inverted and could not
restore it. The recorded write flipped `inverted -> normal`; the direction field is
now a supported, reversible setting.

## Backup format

Exactly 144 bytes; explicit byte layout, not an ABI-dependent C struct:

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 8 | ASCII A22ADPI1 |
| 8 | 2 | bcdDevice, little endian; 01 01 |
| 10 | 1 | Profile index |
| 11 | 1 | One-based slot |
| 12 | 128 | Full profile before modification |
| 140 | 4 | FNV-1a32 of bytes 0..139, little endian |

FNV basis=2166136261, multiplier=16777619, wrapping uint32_t. This detects accidental
corruption, not malicious edits. The signature implies A22A but does not identify a
physical unit. Files are created exclusively by mkstemp with mode 0600, flushed
before writing the mouse, and never deleted automatically. A backup represents
configuration, not firmware or a complete device image.
