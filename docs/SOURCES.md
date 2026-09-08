# Sources and provenance

Research and archive date: 2026-09-07. Local experimental findings are recorded in
[EXPERIMENTS.md](EXPERIMENTS.md). Upstream protocol code is a research reference,
not an official specification for A22A. URLs may change; source links below use
the exact inspected revision where possible. Full Holtek PDFs are available locally.

## S1: libratbag Holtek protocol proposal

- https://github.com/libratbag/libratbag/pull/1561
- Author: Michal Lubas (lubasowo0).
- Inspected revision: `e61221e9ed75f311692b5b345df7a9bdfa331d78`.
- Status at initial investigation: open/unmerged proposal; not assumed upstream support.
- Defines unofficial API A and API B names and discusses device/firmware variation.
- Warns that unsynchronized writes can soft-brick devices, including with vendor software.

This was the key starting point. Local descriptors matched B's transport, then Echo,
state queries, block reads and two owner-authorized DPI writes validated a subset.

## S2: Pinned implementation details

| Source | What it establishes |
| --- | --- |
| [driver-holtek8b.c](https://github.com/lubasowo0/libratbag/blob/e61221e9ed75f311692b5b345df7a9bdfa331d78/src/driver-holtek/driver-holtek8b.c) | B command IDs, 128-byte profile/button structs, 64-byte chunks, write-then-select order |
| [holtek8-shared.c](https://github.com/lubasowo0/libratbag/blob/e61221e9ed75f311692b5b345df7a9bdfa331d78/src/driver-holtek/holtek8-shared.c) | Checksum, readiness polling, framing, Echo, rate mapping, sensor-dependent DPI conversion, button records |
| [holtek8-shared.h](https://github.com/lubasowo0/libratbag/blob/e61221e9ed75f311692b5b345df7a9bdfa331d78/src/driver-holtek/holtek8-shared.h) | Nine-byte host Feature struct, API labels, sensor metadata and shared constants |
| [driver-holtek8a.c](https://github.com/lubasowo0/libratbag/blob/e61221e9ed75f311692b5b345df7a9bdfa331d78/src/driver-holtek/driver-holtek8a.c) | A command differences and optional obfuscation; not selected for this A22A |
| [holtek-a09f.device](https://github.com/lubasowo0/libratbag/blob/e61221e9ed75f311692b5b345df7a9bdfa331d78/data/devices/holtek-a09f.device) | V30 revision 0232, API A, PMW3320 and password association |
| [genesis-krypton-750.device](https://github.com/lubasowo0/libratbag/blob/e61221e9ed75f311692b5b345df7a9bdfa331d78/data/devices/genesis-krypton-750.device) | A2F1/PAW3333 API B reference device; not A22A |
| [libratbag-data.c](https://github.com/lubasowo0/libratbag/blob/e61221e9ed75f311692b5b345df7a9bdfa331d78/src/libratbag-data.c) | Sensor/password selection from host-side device metadata |

The reference files contain their own copyright/license notices (MIT SPDX headers
in the inspected driver sources). They are linked, not copied into this project.
Review applicable terms before incorporating upstream implementation text or redistributing.

Important interpretation limits:

- B's `independent_xy` is static sensor metadata. Known entries use shared XY, but
  the uploader writes both tables. This project preserves Y instead, based on local tests.
- `dpi_step=100` is configured for PAW3333 and the fallback; PMW3320 uses 250.
  Shared VID or API family alone does not imply 100-DPI units.
- The upstream uploader also normalizes count/enable fields. This project does not:
  it preserves all profile bytes except the selected X low byte.
- The reference associates B typically with HT68FB560. That is not chip identification.
- A09F's password `2116B6` is six ASCII characters, not three hex bytes. It was not
  needed or tried against A22A. Do not transplant its device entry.
- Source semantics of high masks, sensor registers and lighting remain candidates
  unless the local experiment establishes them.

## S3: A09F device identification and variants

- https://usb-ids.gowdy.us/read/UD/04d9/a09f
- https://linux-hardware.org/?id=usb:04d9-a09f
- https://github.com/libratbag/libratbag/issues/1375 (Motospeed V30, revision 2.32)
- https://github.com/libratbag/libratbag/issues/1740 (Anko 43161362, revision 3.02)
- https://github.com/libratbag/libratbag/issues/1258 (Cyborg F1 report)
- https://github.com/libratbag/libratbag/issues/1812 (Panteon PS140 PRO report)

The USB ID database labels A09F as E-Signal LUOM G10 Mechanical Gaming Mouse.
Other reports use the same PID for different retail products/revisions. The database
label is not the same thing as an actual USB product string or a firmware ABI.

V30's published vendor interface is bidirectional with 32-byte endpoint packets;
the Anko example is output-only with a 64-byte endpoint maximum but a 32-byte Output
report and an 8-byte Feature report. Current A22A has bidirectional **64-byte reports**.
These differences motivated investigating B instead of blindly replaying A's commands.

The investigation found no useful pre-existing A22A-specific driver or OEM protocol
document in the searches performed. This is a search result, not proof none exists.

## S4: Holtek official documents, archived in full

| Local document | Version/date | Relevance and limitations |
| --- | --- | --- |
| [HT68FB540/550/560](references/HT68FB540_550_560v200.pdf) | 2.00, 2023-05-16 | 8-bit Holtek RISC architecture, USB, SPI, memory, programming interfaces; B candidate family |
| [AN0699EN](references/an0699en.pdf) | 1.00, 2025-01-10 | HT68FB550 mouse reference firmware, descriptors, endpoint handling and firmware flow |
| [HT68FB541/571](references/HT68FB541_571v140.pdf) | 1.40, 2021-09-10 | Endpoint hardware limits and comparison to output-only variants |
| [AN0483E](references/an0483e.pdf) | 1.00, 2018-03-09 | HT66FB574/572 color-effect mouse, RGB, sensor, host-app/firmware architecture |

Exact original URLs, byte sizes, page counts, download date and SHA-256 digests are
in [references/README.md](references/README.md). They are official copyrighted
references, not this project's own firmware or documentation license.

HT68FB550/560 are Holtek 8-bit RISC devices, not automatically ARM or 8051 targets.
The 550/560 datasheet's USB section starts around printed page 152; programming
interfaces are discussed earlier. Use printed page labels rather than assuming a
PDF viewer's numbering matches. Identify actual PCB markings before disassembly.

AN0699's sample VID:PID is 04d9:0b00, not A22A. Its bootloader flow must not be
mistaken for safe mouse configuration requests. AN0483 references host-application
and firmware attachments, but those attachments were not obtained or inspected.
HT68FB571 has four endpoints including EP0, a poorer fit to this device's five
observed endpoint addresses. None of these facts uniquely identifies the A22A MCU.

Additional official tooling reference, linked but not archived:

- [USB Code Library Generator Quick Start](https://www.holtek.com/WebAPI/USB_Code_Library_Generator_Quick_Startv100_en.pdf/8f091c78-78f0-4fec-9cee-1404820632e0)
- https://www.holtek.com/esk66fb-200

The generator describes C firmware scaffolding/HT-IDE3000, not an OEM mouse
configuration ABI. No programming tool or firmware updater was run.

## S5: Other protocols and host API documentation

- [OpenRGB Holtek A070](https://github.com/CalcProgrammer1/OpenRGB/tree/master/Controllers/HoltekController/HoltekA070Controller)
- [Linux hid-holtek-mouse.c](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-holtek-mouse.c)
- [Linux hidraw documentation](https://docs.kernel.org/hid/hidraw.html)
- [Linux hidraw UAPI](https://github.com/torvalds/linux/blob/master/include/uapi/linux/hidraw.h)
- [HIDAPI header/documentation](https://github.com/libusb/hidapi/blob/master/hidapi/hidapi.h)

OpenRGB A070 uses a different PID, interface and Feature framing. Its RGB buffers
start with 07 at the HIDAPI report-ID position; it is not a universal Holtek RGB
protocol and was not replayed here. Linux's Holtek mouse driver addresses descriptor
quirks for selected PIDs, not this device's configuration commands.

The C tool uses kernel HIDIOCGRAWINFO, HIDIOCGRDESCSIZE, HIDIOCGRDESC,
HIDIOCSFEATURE and HIDIOCGFEATURE directly. It does not link against HIDAPI.

## S6: Sensor identification (PAW33xx / PAW3333)

Research date 2026-09-07. No local mouse was opened; no NDA datasheet was consulted.
The sensor part is a strong inference, not a read die marking.

- [libratbag PR #1561 diff](https://github.com/libratbag/libratbag/pull/1561.diff) —
  `holtek8-shared.c` sensor table lists only two real sensors for the holtek8b
  family: PAW3333 (200..8000 CPI, step 100) and PMW3320 (250..3500 CPI, step 250).
- The local `raw * 100` evidence selects PAW3333 and rules out PMW3320 (step 250).
- `bcdDevice 0101` maps to `SensorType=PAW3333` in the PR's device data
  (`data/devices/genesis-krypton-750.device`).
- [PR comment](https://github.com/libratbag/libratbag/pull/1561#issuecomment-2693164941) —
  sibling E-Signal LUOM G10 (04d9:a09f) reported as PAW3335.
- [libratbag issue 1858](https://github.com/libratbag/libratbag/issues/1858) —
  another Holtek mouse (04d9:a09e) reported as PAW3327.
- [E-Signal HT68FB560 page](http://www.e-signal.com.tw/ht68fb560-1/) —
  HT68FB560 described as a gaming-mouse reference design "pairs with mainstream
  laser sensors"; no specific part name.
- [QMK pmw3320.h](https://github.com/qmk/qmk_firmware/blob/master/drivers/sensors/pmw3320.h) —
  PMW3320 has 0x42 = burst-read register but no public 0x2e; used to rule it out.
- [QMK pmw3325.c](https://github.com/qmk/qmk_firmware/blob/master/drivers/sensors/pmw3325.c) —
  0x2e appears only as a value, not a register; confirms register maps are NDA'd
  for the PAW33xx family and must not be invented.

The decisive non-invasive signature is the observed six-bit wrap: raw 64 -> 0 and
raw 80 -> 16, matching a 6-bit resolution register driven by `raw * 100`. Profile
fields `sensor_srom_id = 0x03`, `sensor_firmware_size = 0x0ffe`, and register pairs
`(0x2e, 0x10)`/`(0x42, 0x00)` remain undecodable without the NDA datasheet.

Conclusion recorded here: inferred PixArt PAW3333 (PAW33xx family), MCU Holtek
HT68FB550/560 family, SPI sensor, CPI = raw * 100 with 6-bit resolution and mod-64
wrap. Definitive identification requires opening the device or the NDA datasheet.
