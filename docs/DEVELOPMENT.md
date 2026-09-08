# Development and build guide

## Scope and environment

Project root: `/home/lucas/src/04d9.a22a.d`.
Linux userspace C11 with libc and Linux UAPI headers. GCC 15.2.0 was used during
the investigation. GNU Make provides convenience targets; no package downloads,
kernel modules, external libraries, generated protocol code or firmware are needed.

The source files were transferred unchanged from the successful temporary driver.
Do not confuse the old `/tmp/opencode/` binaries or old notes with this project's
maintained documentation. Old notes predated the two successful physical writes.

## Code style

Source is formatted with clang-format using the project `.clang-format`
(`BasedOnStyle: GNU`, `SortIncludes: Never`). Format before committing:

```sh
clang-format -i src/main.c test/main.c
clang-format --dry-run --Werror src/main.c test/main.c
```

Conventions:

- C11, built with `-Wall -Wvla -Wno-parentheses` and kept warning-clean.
- Static helper functions per protocol stage; no public library ABI yet.
- Explicit magic/checksum fields in binary formats, not compiler-dependent structs.
- No hardcoded device-specific values; read configuration at runtime so the tool
  applies to other mice of the same firmware family.
- Never store or match a serial number; the target device has none.

## Build commands

Run these from the project root:

```sh
make
make test
make sanitize
```

| Target | Action | Hardware access |
| --- | --- | --- |
| all (default) | Build src/main | None |
| test | Build/run encoder self-test and mocked transport executable | None |
| sanitize | Separate mocked executable with ASan and UBSan | None |
| clean | Remove src/ and test/ artifacts only | None |

The build is a recursive Make (libcaster-style): the top-level Makefile delegates
to `src/Makefile` and `test/Makefile`, each using `MAKEFLAGS += -r`, `.SUFFIXES`,
automatic `.d` dependency files and `-include $(DEP)`. Flags are `-O3 -fno-plt
-pipe -D_GNU_SOURCE=1 -fwrapv -fms-extensions -Wall -Wvla -Wno-parentheses`;
`CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS` and `LDLIBS` are overridable. `LDLIBS`
links `-lm`. GCC is not required; GNU Make's built-in `CC` normally resolves to
`cc`.

```sh
make clean
make CC=gcc CFLAGS='-O0 -g3' test
make sanitize CFLAGS='-O1'
```

Changing compiler flags alone does not invalidate existing Make targets; use clean
when changing toolchain/configuration. Do not run clean concurrently with builds.
No install target is provided; run the built executable directly.

Manual build, if Make is unavailable:

```sh
cc -O3 -fno-plt -pipe -D_GNU_SOURCE=1 -fwrapv -fms-extensions -Wall -Wvla -Wno-parentheses src/main.c -o /tmp/main-manual
cc -O3 -fno-plt -pipe -D_GNU_SOURCE=1 -fwrapv -fms-extensions -Wall -Wvla -Wno-parentheses -Isrc test/main.c -o /tmp/main-test-manual
/tmp/main-manual --self-test
/tmp/main-test-manual
```

These examples create only their stated temporary binaries; choose unused names if
those paths already contain something important. Never run builds as root.

## Code map

| Function / file | Responsibility |
| --- | --- |
| open_mouse | Match VID/PID, exact HID descriptor, revision; require one device; advisory lock |
| packet / send_command / get_feature / query | Eight-byte protocol in nine-byte hidraw buffers |
| current | Read wire profile and one-based active resolution slot |
| read_block / read_profile / read_buttons | Validate acknowledgement, read two exact input chunks |
| ready / write_block / write_profile / write_buttons | Synchronize 128/64/0 remaining counts around two output chunks |
| raw_dpi / edit_dpi | Low-byte DPI interpretation and one-byte X edit |
| scroll_set_direction / scroll_is_natural | Wheel direction decode and flip |
| get_active_profile / set_active_profile | Profile query (82) and switch (02) with readback |
| snapshot_global / restore_global_if_changed | Firmware-bug guard around writes |
| pack_snapshot / valid_snapshot / unpack_snapshot / read_snapshot / save_snapshot / load_snapshot | Full-device snapshot, checksum, save/restore |
| commit_profile | Guarded profile write: preflight, write, readback, activate |
| print_usage / print_state | Usage text and post-change state reporting |
| main | CLI dispatch, layout guards, no-op, preflight, wheel bug mitigation |
| self_test | Pure packet, encoding/preservation and snapshot tests |
| test/main.c | Includes src/main.c under mocked syscall names; never opens a device |

Small static functions keep protocol stages understandable without introducing a
public library ABI before a second user exists. Preserve the minimal single-slot
scope rather than turning this into a generic arbitrary-command sender.

## Test coverage and gaps

Self-tests check checksum examples, eight slots across all positive eight-bit raw
values, preservation of all unrelated bytes, snapshot roundtrip and detection of a
single-bit corruption in each snapshot byte. The broad offline encoding sweep is
not permission to write all those values to hardware; the `dpi` subcommand retains
its 100..6300 guardrail.

Pure-logic tests additionally cover the rate code map, wheel direction
normal/natural byte encoding, and the 6-bit DPI wrap boundary (64->0, 80->16,
255->63).

Mocked transport tests cover exact two-chunk success, remaining-count synchronization,
invalid read/write acknowledgements, partial output/input, input timeout and stale
queued data. Mock calls replace ioctl, read, write and poll at compilation time.

ASan/UBSan passed for the saved code. Hardware evidence includes
successful `show`, reads and two actual DPI changes with full readback and owner
confirmation. The snapshot/restore roundtrip was verified against the live device, and a baseline
snapshot (`recovery/a22a-baseline.snap`) is committed as a recovery point.

Gaps: discovery failures are not fully mocked; CLI/filesystem/snapshot failure paths
are not comprehensively tested; the physical restore path, unplug failures, different
revisions, independent XY, precision CPI measurements and persistence remain untested.
Sanitizers and mocks do not establish firmware safety.

## Live checks

Read-only-at-the-configuration-level checks:

```sh
sudo ./src/main show
```

Expected last accepted state: profile 1, slot 1, X low=16 (~1600), Y low=20,
count=1, enabled mask=ff, candidate scales=100/100. Hardware state can change;
do not treat this historical expectation as a reason to overwrite a different state.

Feature queries require SET_FEATURE; "read-only" does not mean absence of host-to-
device USB transfers. No automatic live test target exists, and no `make` target
should execute a privileged hardware operation.

For a new write experiment, agree on target and final state, close vendor software,
avoid hardware DPI/profile buttons, keep the cable attached and save a `snapshot`
as a recovery point first. Writing subcommands (`dpi`, `slot`, `rate`, `wheel`,
`profile -d`) apply immediately without a confirmation gate. Read again in a new
process and obtain functional confirmation. Do not repeat a write just to produce a
nicer log; repeated configuration writes may consume flash life.

## Failures and recovery limits

- Permission/discovery failure: verify device and revision, use sudo if appropriate;
  do not solve it with blanket chmod or relaxed descriptor matching.
- Unexpected layout/high bit/scale: stop and investigate. Do not normalize unknown bytes.
- State changed during preparation: no write occurred.
- Write/readiness/partial transfer failure: state may be partial. Use a `snapshot`
  taken beforehand to recover; do not blindly retry, switch profiles or assume a
  safe automatic rollback.
- Readback mismatch: no subsequent activation is attempted. Investigate before writing more.
- Activation failure: table may be written even though effective DPI is uncertain.
- Restore refused: this is deliberately not full recovery; unrelated differences or
  changed active slot/profile cannot be bypassed safely without new evidence.

The advisory lock cannot exclude vendor apps that ignore it. The program validates
state just before writing but cannot make user input/device state changes atomic.
Process interruption and power loss are not recoverable transactions. There is no
verified firmware rescue procedure in this project.

## Next development work

See [STATUS.md](STATUS.md) for the implemented/not-implemented feature list and
field-understanding summary. Priority work:

1. Button remapping — the button block transport is proven and single-key maps are
   already decoded; the remaining step is a write path for records other than scroll.
2. Add stronger offline CLI, discovery, snapshot I/O and late-transfer failure tests.
3. Confirm shared XY quantitatively with raw HID counts over measured horizontal and
   vertical travel. OS cursor movement is unsuitable because of acceleration/scaling.
4. Validate supported DPI steps/range and high-mask semantics with separately agreed
   experiments. Preserve unknown fields and record each tested value.
5. Capture the vendor application (if available) to confirm illumination and DPI
   indicator color encodings before implementing them.
6. Investigate whether 04 activation is required and whether settings survive power
   cycles, only when a disruptive test is acceptable.

Do not transplant the reference driver's whole-profile normalization.

## Known limitation / deferred items

- Combination-key slot switching (side button + right button) is a firmware-level
  DPI step shortcut, not represented in the 16 single-button records (type 0x0c is
  a left-click synonym, not a chord). A read-only probe confirmed it changes the
  sensor resolution directly, leaves no HID-observable state, and does not change
  the active slot. It cannot be disabled or modified through HID. See
  [EXPERIMENTS.md](EXPERIMENTS.md). Do not guess unknown bytes.
- `sensor_srom_id = 0x03` and register pairs `(0x2e,0x10)/(0x42,0x00)` remain
  undecoded without the NDA sensor datasheet.

No firmware dumping/flashing, bootloader entry, random command probing, generic raw
write interface or implicit installation is within the current project goals.
