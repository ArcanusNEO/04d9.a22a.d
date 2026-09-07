# Investigation and validation record

Date: 2026-09-07. Evidence below is transcribed from the live investigation's tool
output and owner confirmations, not a USBPcap/usbmon capture. No vendor driver,
retail model information, PCB photograph, MCU marking or firmware image was available.

## Sequence and confidence

| Stage | Action | Result |
| --- | --- | --- |
| Enumeration | lsusb and cached sysfs HID descriptors | A22A revision 0101, three interfaces, API-B-like transport |
| Initial Feature read | GET_FEATURE only | Eight zero payload bytes |
| Echo | API B 00 with RATB marker | Exact marker echo |
| State | 82, 83, 84 | Profile 1, rate code 01, slot 1 |
| Blocks | 8c profile 1, 8d profile 1 | 128 bytes each, two 64-byte inputs |
| Global candidate | 8c profile 0 | 128 bytes; candidate enabled-profile mask 3f |
| Stability | Two additional profile-1 reads | Identical SHA-256; active profile remained 1 |
| Initial DPI | Owner-supplied fact | 1200; profile X low=12, Y low=20 |
| First write | Owner authorized 3200, retain result | Full readback before/after activation passed; owner confirmed success |
| Second write | Owner authorized 1600 | Full readback before/after activation passed; owner reported no problem |
| Handoff | Save source, tests and documentation | No further configuration change; last intended setting remains 1600 |

User confirmation is practical functional validation. It is not a calibrated raw
X/Y count measurement, proof of a particular sensor model, or power-cycle testing.
Only profile 1 / slot 1 was written. No restore command has been exercised on hardware.

## Early feature transactions

Buffers include the zero hidraw placeholder:

```text
Echo TX: 00 00 52 41 54 42 00 00 d6
Echo RX: 00 00 52 41 54 42 00 00 00

Profile TX: 00 82 00 00 00 00 00 00 7d
Profile RX: 00 82 01 00 00 00 00 00 00

Rate TX: 00 83 01 00 00 00 00 00 7b
Rate RX: 00 83 01 01 00 00 00 00 00

Slot TX: 00 84 01 00 00 00 00 00 7a
Slot RX: 00 84 01 01 00 00 00 00 00

Profile block TX: 00 8c 01 00 00 00 00 00 72
Profile block RX: 00 8c 01 80 00 00 00 00 00

Button block TX: 00 8d 01 00 00 00 00 00 71
Button block RX: 00 8d 01 80 00 00 00 00 00
```

Each block acknowledgement was followed by exactly two 64-byte inputs.

## Baseline profile 1, 1200 DPI

Each row contains 16 bytes; left column is the decimal offset. This is historical
configuration evidence, **not** a firmware image or an instruction to replay data.

```text
000: ff ff 00 00 00 00 00 00 00 00 00 00 00 00 00 00
016: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
032: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
048: 2e 10 42 00 00 00 00 00 00 00 00 00 00 00 00 00
064: 8f 04 0a 0a 19 19 01 01 ff 00 64 64 01 c0 f0 03
080: 64 00 20 20 0c 08 10 18 30 3e 50 e0 14 28 3c 50
096: a0 40 61 77 ff ff ff 14 ff 00 00 00 ff 00 00 00
112: ff ff 00 ff ff ff 00 00 ff ff ff 80 00 ff ff ff
```

Original 128-byte SHA-256, identical in two repeat reads:

```text
1684df1955bd903ff6da5df1695c30935c8fe7b1d8c3d1b045b4e292dffdb411
```

Original X low values: 12, 8, 16, 24, 48, 62, 80, 224.
Original Y low values: 20, 40, 60, 80, 160, 64, 97, 119.
Candidate high masks: 20/20 hex. The selected slot's bits were clear; bit 5 is set
for inactive slot 6, which the CLI neither interprets as a confirmed ninth bit nor edits.
Only slot 1 was enabled by the reference count-and-mask interpretation.

## Baseline profile 0

```text
000: 88 00 3f 00 42 03 fe 0f 00 00 00 00 00 00 00 00
016: 00 01 02 03 04 05 06 07 ff 00 00 ff 00 00 ff 00
032: 00 ff 00 00 ff 00 00 ff 00 00 ff 00 00 ff 00 00
048: ff 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
064: 8f 04 0a 0a 19 19 06 05 ff 04 64 64 01 c0 f0 03
080: 64 00 20 20 14 28 3c 50 a0 40 61 77 14 28 3c 50
096: a0 40 61 77 ff ff ff ff ff 00 00 00 00 ff 00 ff
112: 00 ff ff 00 00 ff ff ff 00 ff ff 55 00 ff ff ff
```

Observed SHA-256:

```text
c04eb9c5119efd41a972413628faf569eaf78b99e7b56b84bb8696a16954281d
```

## Baseline button block, profile 1

```text
000: 01 00 f0 00 01 00 f1 00 01 00 f2 00 01 00 f3 00
016: 01 00 f4 00 07 00 03 00 0c 00 00 00 0b 00 02 02
032: 0a f0 21 03 0c 00 00 00 0c 00 00 00 00 00 00 00
048: 00 00 00 00 00 00 00 00 04 00 02 00 04 00 01 00
064: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
080: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
096: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
112: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

## Authorized DPI writes

Both writes used the C driver now saved in the project. Protocol-derived host
command buffers for this profile/slot are:

```text
Begin profile write: 00 0c 01 80 00 00 00 00 72
Reselect slot:       00 04 01 01 00 00 00 00 f9
```

These write buffers are reconstructed from the saved implementation, not captured
bus traffic. All readiness, length and full-block equality checks in that code passed.

| Write | Offset 84 | Other 127 bytes | Backup of state before write |
| --- | --- | --- | --- |
| 1200 -> 3200 | 0c -> 20 | Unchanged | /tmp/a22a-dpi-backup-9tW8Fj |
| 3200 -> 1600 | 20 -> 10 | Unchanged | /tmp/a22a-dpi-backup-OkJ7B4 |

These are historical temporary paths, not guaranteed to survive cleanup/reboot.
The original binary backups are not bundled into this source project. The baseline
hex above is an evidence record and lacks the backup container required by restore.

Both transactions kept profile index 1 / slot 1, preserved Y low=20, reselected
the same slot and were independently read after process exit. The final intended
profile equals the 1200 baseline above with **only offset 84 replaced by 10 hex**.

## Slot selection and count field

A direct `04 {profile, slot}` selection was initially rejected while the resolution
count (offset 70) stayed at 1. The firmware accepted only slot 1 as active regardless
of the `0xff` enable mask. The count was then written to 8 with owner authorization;
backup `/tmp/a22a-dpi-backup-cKJRD7` records the pre-change count=1 state. Afterward
`slot SLOT` selected slots normally.

The `slot SLOT` subcommand does not auto-modify the count; it sends only the `04`
selection and reports failure if the firmware rejects it. `slot [-c|--count N] SLOT`
sets the count first (when it differs) and then selects the slot.

Owner-observed speed comparison after enabling all slots:

| Slot | raw (X) | raw mod 64 | effective CPI | Owner observation |
| --- | ---: | ---: | ---: | --- |
| 6 | 62 | 62 | 6200 | fast |
| 7 | 80 | 16 | 1600 | slower than slot 6 |

This is the definitive signature of a six-bit resolution register: raw 80 wraps to
16, so slot 7 behaves as 1600 CPI, while slot 6 (raw 62) is 6200 CPI. It confirms the
wrap hypothesis and, together with `raw * 100`, points to a PixArt PAW3333-family
sensor (see [SOURCES.md](SOURCES.md) S6).

## Sensor inference

Recorded as inference, not a read die marking:

- CPI = raw * 100, matching libratbag's PAW3333 entry (200..8000, step 100).
- Six-bit resolution register: raw >= 64 wraps modulo 64; effective 0..6300 CPI.
- This explains 6400 (raw 64 -> 0) feeling slowest, 8000 (raw 80 -> 16) acting as
  1600, and the earlier order-dependent 6400->6300 observations (bit 6 set then
  cleared masks/wraps the six-bit field).
- PMW3320 is ruled out by step 250 vs the observed step 100.
- PAW3333/PAW3335 register maps are NDA; `sensor_srom_id = 0x03` and register pairs
  `(0x2e,0x10)/(0x42,0x00)` are not decoded.

The CLI now limits `dpi`/`plan` to 100..6300 and displays stored raw values through
the six-bit mask, so an inactive 224 is reported as 32 (3200 CPI), not 22400.

## Report rate

Query command `83 {profile}` returns the active rate code in ARG1; codes 01/02/04/08
map to 1000/500/250/125 Hz. Set command `03 {profile, code}` applies a rate directly
and does not rewrite the 128-byte configuration block.

Observed: profile 1 initially returned code 01 (1000 Hz); profile 0 returned 02
(500 Hz). The owner authorized `rate 500`; command 03 {profile=1, code=02} succeeded,
and a subsequent 83 query returned code 02 (500 Hz). The configuration block's
`enabled_rates` field (offset 64, 0x8f) was left unchanged by this command.

The `rate` subcommand now supports 125/250/500/1000 Hz with a pre-query no-op check
and post-write readback verification.

## Wheel direction

The owner reported that the vendor driver's bug had left the middle-click wheel
scrolling inverted and unable to recover. Button-block records 14/15 (offsets 56/60)
held `04 00 02 00` / `04 00 01 00`, i.e. down/up swapped, which `show` decoded as
`inverted`. The `wheel` subcommand flips the direction by swapping only those two
event bytes; the recorded write changed `inverted -> normal` and readback matched.
The backup of the inverted state is `/tmp/a22a-dpi-backup-yjc5e7`.

The interface was then consolidated into a single `wheel` action that toggles the
current direction, rather than requiring an explicit `normal`/`inverted` argument.

## Combination-key chord (side + right = DPI switch)

The owner reported that holding the upper side button plus the right button switches
the DPI slot. Investigation established:

- The button block is a flat 16-record table; no record encodes a two-key chord.
  Reference type `0x0c` is a left-click synonym (`{0x0c,{0}} -> button(1)` in the
  reference driver's quirks map), not a chord. Types `0x05` (rate) and `0x0b`
  (special) have no defined decode in the reference driver.
- Profile 0 (factory/global) differs from profile 1 (active): it retains rate-cycle
  `05 00 03 00`, media `03 00 23 02` (AC Home, consumer 0x0223), and a normal
  (non-inverted) wheel direction.
- A read-only probe (`a22a-probe.c`, later removed) monitored mouse, keyboard and
  vendor IN reports while polling active slot 0x84. During a captured chord press,
  the mouse reported only the ordinary button bitmap `0x12` (right + side5) with no
  extra HID report, no vendor report, and no change to the active slot.

Conclusion: the chord is a firmware-level DPI step shortcut executed inside the MCU.
It rewrites the sensor resolution register directly, bypassing the configuration
table's active slot, and leaves no HID-observable state. It cannot be disabled or
modified through the HID button block. The experimental `dpi-cycle off` subcommand
that zeroed button record 5 was removed after confirming it did not affect the chord.

## Slot high-bit masks

Offset 82 (X) and 83 (Y) are per-slot ninth-bit masks. Observed value `0x20` for
both: only slot 6 has its ninth bit set, giving X=318 and Y=320 (the sole slot using
9-bit encoding). All other slots are 8-bit. This mask interpretation remains
candidate; the CLI preserves the masks and refuses writes whose selected slot has
the high bit set.

## What remains unmeasured

- Physical X/Y counts per inch and their before/after ratios.
- Whether another firmware configuration can enable an independent Y table.
- Whether writing a profile without 04 would apply the new DPI immediately.
- Persistence after unplug/replug, restart, suspend or hardware DPI-button use.
- Recovery after partial transfers, physical disconnects or failed activation.
- The exact PAW33xx part number, which requires opening the mouse or the NDA datasheet.

No additional writes should be inferred as authorized by this record. Future
experiments must state the intended changes, potential persistence, backup and
desired final state. Do not automatically restore 1200: the owner requested that
successful settings remain active, and accepted 1600 as the latest result.
