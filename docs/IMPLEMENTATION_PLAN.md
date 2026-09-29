# Windows feature comparison and Linux completion plan

This plan compares the extracted A3327 Windows package with the A22A Linux
hidraw tool. It distinguishes the Windows UI's advertised controls from behavior
that has actually been mapped to A22A wire fields. Static package evidence is
in [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md); current device evidence is
in [EXPERIMENTS.md](EXPERIMENTS.md) and [PROTOCOL.md](PROTOCOL.md).

## Capability comparison

| Area | Windows package evidence | Current Linux project | Gap / interpretation |
| --- | --- | --- | --- |
| DPI stages | Eight stage controls; resolution count is configurable | Eight slots and count are represented; `dpi DPI` edits the active slot only | Add an explicit target-slot operation so inactive stages can be edited without changing the active slot |
| DPI range / sensor | Sensor=3327 branch shows a 200 minimum and max 12400; its writer maps n=1 to raw 2 and uses a high-range marker | CLI now uses the Sensor=3327 encoder for 200..12400; 1200, 1600 and 3200 were hardware-tested | High-range physical response is not calibrated; report encoded estimates and gather readback/CPI evidence before calling the full range hardware-verified |
| X/Y scale | X/Y sliders are copied to profile bytes 74/75; Sync mirrors their model values in the Windows app | No scale setter; X stage table is writable and Y stage table is preserved | Add a typed scale setter for bytes 74/75 after validating physical effect; Sync is a client-side mirror, not a known device mode bit |
| Polling rate | 125/250/500/1000 choices | Same four rates, queried/set and read back | No material gap in the observed A22A path |
| Lighting | Twelve zero-based mode indexes; six are visible; intensity/pulse controls and fixed/DPI/cycle color choices | `led mode` writes bytes 71..73; `led color fixed/palette` writes profile-0 bytes 24..47; `led color dpi` edits bytes 104..127 in the active profile | LED effects, radio/enable controls, and color visibility need hardware capture; Windows slider-to-A/B conversion remains unknown |
| Button mapping | UI has Office/Game button layouts and labels for mouse, DPI, report-rate, profile, light, key/media, shortcut and macro actions | Button records are readable; only wheel direction is intentionally written | Add typed, single-record mappings after correlating UI actions to the 16 on-device records |
| Macros / Gun | Macro recorder/editor and a separate Gun editor are present | Macro commands 0x0f/0x8f and Gun/recoil features are absent | Defer. The reference marks macro operations dangerous; UI presence does not prove A22A command safety |
| Profiles / files | Five `File` controls are written to wire profiles 1..5; a separate top-level Profile selector and `.pbin` store group local presets | Firmware profiles 0..5 can be switched and copied; no local naming/import/export feature | Preserve the distinction: wire profiles 1..5 are mapped; local preset directories and files are not |
| Performance options | Pointer speed, Enhance Pointer Precision, scroll speed, double-click speed, LOD and key-response controls | No equivalent options | Determine which controls call Windows system APIs and which send HID commands. Keep OS-global changes outside the device core unless explicitly desired |
| Firmware update | No standalone updater or sensor SROM image found in this installer | No firmware/bootloader/update support | No gap to close; keep firmware operations out of scope |
| HID transport | `hiddll.dll` uses 9-byte Feature calls and 65-byte report reads/writes including report-ID byte | hidraw uses the corresponding 9-byte Feature / 65-byte output calls and 64-byte input payloads | Transport sizes agree; command semantics still require evidence |
| Recovery | Installer includes default templates, not a live device backup | Version-1 snapshot stores six profiles' config and button blocks; owner confirms normal restore works | Snapshot omits per-profile report rate and active profile/slot. Partial-write/error paths still ignore failures and need hardening |

The Windows UI proves that controls exist in the application. It does not prove
that every control is enabled for this A22A revision, that labels correspond to
raw bytes, or that an option is stored on the mouse rather than in Windows or a
local `.pbin` file. Do not copy the `ATTRIBUTE_*` defaults to hardware: they are
not the recorded live baseline.

## Current inconsistencies to resolve

1. **LED encodings are statically mapped, but physical effects are unverified.**
   Mode/parameter writes and the global/per-stage RGB table writers preserve
   unrelated bytes and verify readback. The A/B inputs are not mapped to Windows
   sliders, and radio/enable controls and visible effects still need a controlled
   A22A capture; keep those limits explicit in help and status documentation.
2. **High-range DPI calibration remains unverified.** The Linux encoder now
   mirrors the OEM Sensor=3327 write transform, and `show` decodes bit 6 as an
   estimate. No high-range setting has been written and calibrated on the
   attached device; keep those displayed values explicitly estimated.
3. **Restore's normal path is owner-verified; failure handling is weak.** The
   restore loop in `src/main.c` still ignores write results and performs no final
   whole-device equality check, so interrupted or partial restores can be
   reported as successful.
4. **Version-1 snapshot scope is partial.** It covers six config blocks and six
   button blocks, but not report-rate commands or active profile/slot. The docs
   now describe that scope; preserve v1 compatibility if the format is extended.
5. **Sensor/range assumptions are in tension.** A22A's `Data.ini` says
   `Sensor=3327`; the old PAW3333 hypothesis came from a different reference
   device. Three low DPI writes establish 100-step settings. The old modulo-64
   interpretation was inferred from an uncalibrated relative speed observation
   and conflicts with the OEM Sensor=3327 encoder.

## Technical design

### 0. Make support and safety claims match executable behavior

- Keep LED writes on the recovered OEM mode/parameter encoding. Expose A/B as
  writer inputs rather than claiming a generic brightness/speed mapping. Profile
  readback verifies the bytes, but mark visible behavior unverified until a
  controlled hardware experiment confirms it. Color-table setters preserve the
  profile-0 indicator-enable bytes and do not invent a color-mode selector.
- Keep the CLI's X DPI wording marked as an estimate. Its write mapping follows
  the OEM Sensor=3327 code; physical high-range response still needs validation.
- Keep the owner-confirmed normal restore status, but make every failure path
  explicit. Describe v1 as a config/button snapshot, not a complete runtime image.
- Make restore fail on the first failed read/write/short transfer, then verify
  every restored block before reporting success. The button-write corruption
  mitigation must re-read/repair global profile 0 after each 0x0d transaction
  and verify it again at the end.

### 1. Establish a controlled evidence set

1. Save the existing known-good snapshot and record active profile, slot, rates,
   all six config/button blocks, and current observable behavior.
2. In an isolated Windows test environment, use USBPcap (or an equivalent USB
   capture) while changing exactly one UI control at a time. Start with one
   inactive DPI stage, X only, Y only, X-Y Sync, polling rate, one visible light
   mode, intensity, and one color. Avoid macros, Gun actions, and unexplained
   commands.
3. For each capture, pair host transfers with before/after block reads. Record
   the UI value, exact changed bytes, command/arguments, report-ID handling,
   checksum, activation sequence, firmware readback, and visible result.
4. Treat `Data.ini` and `.pbin` as defaults/profile-file evidence only. Never use
   them as a substitute for the actual device snapshot.

### 2. Keep a narrow, typed device model over the existing transport

Retain the existing API-B transport and opaque 128-byte blocks. Add named
getters/setters only for fields whose encoding is captured and verified. Model
the 16 four-byte button records separately from the trailing reserved bytes.
Every mutating command should use one shared transaction path:

1. Read the full affected block and required global state.
2. Clone it and change only the requested, validated bytes.
3. Re-read before commit and abort if the baseline changed.
4. Send the existing synchronized block write, check every transfer/readiness
   result, and read back exact bytes.
5. Apply any required activation, verify state again, and repair/check profile 0
   after button writes.

Unknown bytes stay opaque and are preserved; no generic raw-command interface is
needed.

### 3. Complete DPI and X/Y scaling in backward-compatible steps

- Keep `dpi DPI` as “set X on the active slot” for compatibility.
- Add a target-slot option so any of the eight stage entries can be changed
  without first selecting that slot. Preserve current active slot and all other
  bytes.
- Add a separate `scale x|y VALUE` operation for profile bytes 74/75. A
  convenience `scale sync on|off` can mirror values in the host-side model as
  the OEM UI does; it must not invent a device mode flag. Confirm the scaling's
  physical effect on hardware before marking the feature verified.
- Do not add a per-stage Y-DPI setter solely from the X-Y scale UI. The OEM page
  has one stage value per slot; offsets 92..99 remain a separate unverified Y
  table and should be preserved until captured behavior proves it is writable.
- Keep the Sensor=3327-specific codec boundary; do not reintroduce a universal
  `raw & 63` rule. The parser follows the OEM's 200..12400 UI range, but only
  1200/1600/3200 have been hardware tested. The physical top end and the even-step
  quantization above 6200 still need live readback and CPI measurements.

### 4. Add lighting only from captured encodings

The CLI uses the OEM mode indexes and per-mode byte-72/73 transforms, writes the
profile-0 fixed/global palette, and edits per-profile DPI RGB entries described
in [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md). Keep static encoding
evidence separate from hardware effect verification. Next, capture the UI
radio/enable controls, slider changes, and per-DPI color selector. Preserve
unrelated profile bytes and require readback plus visible output. Hidden UI modes
are not part of the initial support target.

### 5. Add button actions; keep macro support separate

Map the Windows button labels to on-device action records with captured examples.
Implement simple mouse, DPI, rate, profile and light actions first, one record at
a time. Preserve reserved bytes and keep the existing 0x0d/profile-0 guard.
Macro recording, encrypted macro import/export, and Gun actions require a
separate protocol/security review; they should not be prerequisites for ordinary
button remapping.

### 6. Correct and version recovery

Keep reading v1 snapshots. Define a v2 snapshot with explicit fields for all
profile config/button blocks, each profile's standalone report rate, and active
profile/slot. Validate the entire file before writing. Restore profile-0 sensor
initialization first, write the other config blocks, write button blocks with
the global-state repair, then restore rates and active selectors; finish with
whole-device readback. If any write is partial or unverifiable, report the state
as uncertain and do not claim success.

## Verification gates

- **Offline:** unit tests for field encoding, enum maps, per-slot/per-axis edits,
  reserved-byte preservation, v1/v2 snapshot compatibility and invalid inputs;
  mocked-transport tests for short writes, timeouts, stale state, readiness,
  readback mismatch and restore aborts.
- **On hardware:** snapshot before each experiment; one setting per run; compare
  full blocks before/after; verify active state, visible behavior and recovery.
  Mark a feature “verified” only after a successful readback and observed result.
- **Documentation:** each feature has one status (`observed`, `implemented`,
  `hardware-verified`, or `unverified`) and the CLI/help output agrees with it.
