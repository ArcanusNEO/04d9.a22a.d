# Feature and field status

This page is the single source of truth for what is implemented, what is not, and
which device fields are understood. Protocol details, offsets and evidence are in
[PROTOCOL.md](PROTOCOL.md) and [EXPERIMENTS.md](EXPERIMENTS.md).

## Implemented

| Feature | CLI | Command(s) | Verified on device |
| --- | --- | --- | --- |
| Show state | `show` (or no args) | 82/84/83/8c/8d | Yes |
| Set current-slot DPI | `dpi DPI` | 0c + 04 | Yes (1200/1600/3200) |
| Preview DPI change | `plan DPI` | 8c | Yes |
| Select active slot | `slot SLOT` | 04 | Yes |
| Set resolution count | `slot -c N SLOT` | 0c + 04 | Yes |
| Set report rate | `rate HZ` | 03/83 | Yes (125/250/500/1000) |
| Switch active profile | `profile N` | 02/82 | Yes (0..5) |
| Flip wheel direction | `wheel` | 0d (+ 0c restore) | Yes (normal/natural) |
| Full snapshot | `snapshot FILE` | 8c/8d (read all) | Yes |
| Full restore | `restore FILE` | 0c/0d (write all) | Yes |
| Self-test / offline tests | `--self-test` / `make test` | — | Yes |

The wheel firmware-bug mitigation (snapshot profile 0 before the 0d write, restore
if corrupted) is part of `wheel` and is verified to recover tracking after the bug.

## Not implemented

| Feature | Reference command/field | Why not / next step |
| --- | --- | --- |
| Button remapping (L/R/middle/side -> keyboard/media/macro) | button block 0d | Transport works; only scroll records written so far. Low risk, high value. |
| Macro recording | 0f / 8f | Reference labels 0f (and 0f with ARG0>50) dangerous. Not attempted. |
| Independent XY DPI | — | No device-side switch found; reference is static sensor metadata. |
| DPI indicator LED colors | profile 104..127 | Layout known; needs vendor-software capture to confirm encoding. |
| Illumination mode/intensity/speed | profile 71/72/73 + 24..47 | Layout candidate; needs capture to confirm encoding. |
| Button debounce | profile 103 | Candidate field; encoding unverified. |
| enabled_rates / enabled_resolutions bitmaps | profile 64 / 100 | Candidate bitmasks; modifying behavior unverified. |
| Enable/disable individual profiles | profile 0 offset 2 | Field proven unstable across profile switches. |
| Disable the side+right DPI chord | firmware-level | Confirmed not reachable via HID button table. |

## Field definitions

Full offset-by-offset tables live in [PROTOCOL.md](PROTOCOL.md). Summary of
understanding, by status:

- **Known (observed on device):** resolution count (profile 70), X DPI table
  (profile 84..91, low byte = DPI/100 with 6-bit wrap), mouse button types
  (button block 0x01 f0..f4), scroll direction (button block 0x04 01/02), the
  left-click quirks type (button block 0x0c), DPI-cycle button (button block 0x07
  0x03).
- **Candidate (from reference driver, not yet validated here):** sensor SROM ID
  (profile 5), sensor fw size (6..7), password (8..15), DPI indicator enable
  (16..23), illumination RGB (24..47), sensor register config (48..63), enabled
  rates (64), illumination mode/intensity/speed (71/72/73), X/Y scales (74..75),
  X/Y high-bit masks (82..83), Y DPI table (92..99), enabled resolutions (100),
  debounce (103), DPI RGB colors (104..127), and button types 0x00/0x02/0x03/0x06/
  0x08/0x09/0x0a.
- **Unknown:** profile offsets 0..1, 3..4, 65..69, 76..81, 101..102; button types
  0x05 (rate) and 0x0b (special); the firmware-level side+right chord.

## Priority

1. Button remapping — transport proven, single-key maps decode already known.
2. DPI indicator colors + illumination — layout known, capture vendor app to confirm.
3. Debounce / enabled bitmaps — need controlled single-field probes with snapshots.
4. Macros — high risk; only with a full snapshot and extreme care.

Do not guess unknown bytes. Use `snapshot` before any experiment, and keep the
generic (no serial, no hardcoded values) design for use on other mice of the family.
