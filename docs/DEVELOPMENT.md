# Development and build guide

## Scope and environment

Project root: `/home/lucas/src/04d9.a22a.d`.
Linux userspace C11 with libc and Linux UAPI headers. GCC 15.2.0 was used during
the investigation. GNU Make provides convenience targets; no package downloads,
kernel modules, external libraries, generated protocol code or firmware are needed.

The source files were transferred unchanged from the successful temporary driver.
Do not confuse the old `/tmp/opencode/` binaries or old notes with this project's
maintained documentation. Old notes predated the two successful physical writes.

## Build commands

Run these from the project root:

```sh
make
make test
make sanitize
make analyze
```

| Target | Action | Hardware access |
| --- | --- | --- |
| all (default) | Build build/a22a-dpi | None |
| test | Build/run encoder self-test and mocked transport executable | None |
| sanitize | Separate mocked executable with ASan and UBSan | None |
| analyze | GCC -fanalyzer on both sources, output to /dev/null | None |
| clean | Remove build/ only | None |

Strict flags: `-std=c11 -Wall -Wextra -Wpedantic -Werror`. `CC`, `GCC`, `CPPFLAGS`,
`CFLAGS`, `LDFLAGS`, and `LDLIBS` are overridable. GCC is used for the analyzer
even if another compiler is selected for normal builds. GNU Make's built-in CC
normally resolves to cc; set `CC=gcc` explicitly if desired.

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
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror a22a-dpi.c -o /tmp/a22a-dpi-manual
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror a22a-dpi-test.c -o /tmp/a22a-dpi-test-manual
/tmp/a22a-dpi-manual --self-test
/tmp/a22a-dpi-test-manual
```

These examples create only their stated temporary binaries; choose unused names if
those paths already contain something important. Never run builds as root.

## Code map

| Function / file | Responsibility |
| --- | --- |
| open_mouse | Match VID/PID, exact HID descriptor, revision; require one device; advisory lock |
| packet / send_command / get_feature / query | Eight-byte protocol in nine-byte hidraw buffers |
| current | Read wire profile and one-based active resolution slot |
| read_profile | Validate acknowledgement, read two exact input chunks |
| ready / write_profile | Synchronize 128/64/0 remaining counts around two output chunks |
| raw_dpi / edit_dpi | Low-byte DPI interpretation and one-byte X edit |
| pack_backup / valid_backup / save_backup / load_backup | Fixed layout, corruption check, exclusive temporary backup |
| main | CLI, layout guards, dry run, no-op, preflight, write/readback/activation |
| self_test | Pure packet, encoding/preservation and backup tests |
| a22a-dpi-test.c | Includes implementation under mocked syscall names; never opens a device |

Small static functions keep protocol stages understandable without introducing a
public library ABI before a second user exists. Preserve the minimal single-slot
scope rather than turning this into a generic arbitrary-command sender.

## Test coverage and gaps

Self-tests check checksum examples, eight slots across all positive eight-bit raw
values, preservation of all unrelated bytes, backup roundtrip and detection of a
single-bit corruption in each backup byte. The broad offline encoding sweep is
not permission to write all those values to hardware; CLI set retains its guardrail.

Mocked transport tests cover exact two-chunk success, remaining-count synchronization,
invalid read/write acknowledgements, partial output/input, input timeout and stale
queued data. Mock calls replace ioctl, read, write and poll at compilation time.

ASan/UBSan and static analysis passed for the saved code. Hardware evidence includes
successful show/plan, reads and two actual DPI changes with full readback and owner
confirmation. The project-save/build step did not change the mouse again.

Gaps: discovery failures are not fully mocked; CLI/filesystem/backup failure paths
are not comprehensively tested; the physical restore path, unplug failures, different
revisions, independent XY, precision CPI measurements and persistence remain untested.
Sanitizers and mocks do not establish firmware safety.

## Live checks

Read-only-at-the-configuration-level checks:

```sh
sudo ./build/a22a-dpi show
sudo ./build/a22a-dpi plan 3200
```

Expected last accepted state: profile 1, slot 1, X low=16 (~1600), Y low=20,
count=1, enabled mask=ff, candidate scales=100/100. Hardware state can change;
do not treat this historical expectation as a reason to overwrite a different state.

Feature queries require SET_FEATURE; "read-only" does not mean absence of host-to-
device USB transfers. No automatic live test target exists, and no `make` target
should execute a privileged hardware operation.

For a new write experiment, agree on target and final state, close vendor software,
avoid hardware DPI/profile buttons, keep the cable attached and record the backup.
Run `plan`, then use the explicit `--allow-persistent-write` gate only when authorized.
Read again in a new process and obtain functional confirmation. Do not repeat a
write just to produce a nicer log; repeated configuration writes may consume flash life.

## Failures and recovery limits

- Permission/discovery failure: verify device and revision, use sudo if appropriate;
  do not solve it with blanket chmod or relaxed descriptor matching.
- Unexpected layout/high bit/scale: stop and investigate. Do not normalize unknown bytes.
- State changed during preparation: no write occurred, but a backup may have been created.
- Write/readiness/partial transfer failure: state may be partial. Retain backup and output;
  do not blindly retry, switch profiles or assume a safe automatic rollback.
- Readback mismatch: no subsequent activation is attempted. Investigate before writing more.
- Activation failure: table may be written even though effective DPI is uncertain.
- Restore refused: this is deliberately not full recovery; unrelated differences or
  changed active slot/profile cannot be bypassed safely without new evidence.

The advisory lock cannot exclude vendor apps that ignore it. The program validates
state just before writing but cannot make user input/device state changes atomic.
Process interruption and power loss are not recoverable transactions. There is no
verified firmware rescue procedure in this project.

## Next development work

1. Add stronger offline CLI, discovery, backup I/O and late-transfer failure tests.
2. Confirm shared XY quantitatively with raw HID counts over measured horizontal and
   vertical travel. OS cursor movement is unsuitable because of acceleration/scaling.
3. Identify the MCU/sensor from non-destructive hardware information if available;
   do not guess based on VID, profile fields or another product's driver metadata.
4. Validate supported DPI steps/range and high-mask semantics with separately agreed
   experiments. Preserve unknown fields and record each tested value.
5. Investigate whether 04 activation is required and whether settings survive power
   cycles, only when a disruptive test is acceptable.
6. Add profile/slot selection, rate, RGB or remapping only after targeted evidence and
   tests exist. Do not transplant the reference driver's whole-profile normalization.

No firmware dumping/flashing, bootloader entry, random command probing, generic raw
write interface or implicit installation is within the current project goals.
