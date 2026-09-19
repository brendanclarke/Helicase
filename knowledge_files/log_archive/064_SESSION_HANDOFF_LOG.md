# Session 064 Handoff — Dynamic Pattern AutoSave

DATE: 2026-09-12 through 2026-09-14  
SESSION GOAL: Implement crash-tolerant background AutoSave and boot restore for every resident Scene's dynamic PAT4 Pattern, integrate Pattern identity with HCNAMES/HCPR, then close the feature with hardware evidence.  
COMPLETED: Per-Scene PAT4 A/B AutoSave, dirty/snapshot ownership, background writer, boot reader, HCNAMES/HCPR v2 integration, Bank-Load Pattern-name repair, fixtures, host validation, and functional hardware closeout.  
VERIFIED ON HARDWARE: Yes. Single-Scene restore, ping-pong parity, Bank-Load all-child marking, refreshed publication, absent-Scene behavior, HCPR v2 geometry, and a full 16-Scene background-drain run passed. Deterministic power-interruption and record/erase-gate tests were deliberately deferred.

CHANGES THIS SESSION:
- `Core/Bank/Scene/Autosave.c/.h`: 16-bit Pattern dirty ownership, whole-Scene marking integration, HCPR format v2 and 145-row HCNAMES API/schema alignment.
- `Core/Bank/Scene/Pattern/PatternData.c/.h`: one 10,519-byte AutoSave snapshot and snapshot accessors; all Pattern mutation paths converge on the Pattern dirty marker.
- `Core/Hardware/SD/filesystem.c/.h`: Pattern A/B writer, validator, boot reader, generation state, `@` provenance, HCNAMES publication, scheduling/gating, load-generation reset, and Bank-Load Pattern-name caching fix.
- `Core/Bank/Scene/Preset/presetManager.c`: successful Scene/Bank load publication marks complete Scene-with-Pattern scope.
- `Core/Sequencer/sequencer.h`: exposes the existing record/erase state needed by snapshot admission.
- `main.c`: invokes the Pattern AutoSave boot reader in the boot restore sequence.
- `tools/decode_devlogs.py`: synchronized the diagnostic operation-name table
  with the 50-entry filesystem operation enum after the Session 061/063/064
  insertions, so error records after operation 8 are labeled correctly.

KNOWN ISSUES INTRODUCED: None identified. The logging trace ring is bounded and the stress capture reported dropped diagnostic records; durable PAT4/HCPR/HCNAMES artifacts remain authoritative. The stale host decoder operation table discovered during closeout was repaired.  
KNOWN ISSUES RESOLVED: Dynamic Patterns now survive background AutoSave and reboot; HCPR identity covers Pattern rows; Bank Load now publishes each child's Pattern name/source correctly. The apparent Card A `BKKit14` stop was traced to malformed generated test files/instructions, not product firmware.  
NEXT SESSION RECOMMENDED GOAL: Implement Phase 4.5 Pattern copy operations (`pat_copyTrack`, `pat_copyPattern`, `pat_copyBar`) with independent pool-block duplication. Optionally return to the deferred Pattern AutoSave fault/gate/performance cases after deterministic hooks and Live Record exist.  
BLOCKERS: None for the accepted functional Pattern AutoSave feature. Deterministic TC5 power-cut timing and TC9 record/erase admission evidence require test instrumentation; Live Record is not yet a valid dependency for ordinary acceptance testing.

CRITICAL REMINDERS FOR NEXT SESSION:
- Pattern payload never belongs in HCPR; scalar HCPR remains exactly 34,768 bytes and Pattern uses 32 separate hidden PAT4 files.
- Clear the selected Pattern dirty bit before snapshot ownership. A mutation during streaming must re-set it, and any writer error must re-arm it.
- Never snapshot while `seq_recordActive` or `seq_eraseActive`; the snapshot copy does not mask TIM3 or other interrupts.
- Only Pattern HCNAMES rows may interpret `FS_RESIDENT_SOURCE_PATTERN_AUTOSAVE` (`0x1ffc`) as `@`; Instrument-direct `@` remains `0x1ffd`.
- A Pattern AutoSave boot candidate applies only when its HCNAMES row is `@` and generation is nonzero. Explicit root Pattern and Scene/Bank directory loads reset that Scene's Pattern generation baseline to zero.
- FAT timestamps are not evidence: this hardware has no RTC. Use generation, CRC, exact geometry, content comparison, and trace ordering.

---

## 1. Scope and decision record

Session 063 established the complete dynamic Pattern interchange object: one
10,519-byte `pat_scene_region_t` serialized as one exact 10,656-byte PAT4
file. Session 064 deliberately did not place that object inside the scalar
HCPR record. Instead, each of 16 resident Scenes owns an independent root A/B
pair:

```text
/.pat00a  /.pat00b
...
/.pat15a  /.pat15b
```

Each file is an ordinary complete PAT4 image. The header generation at offset
10 selects the newest valid candidate; even generations target `a`, odd
generations target `b`, and `a` wins an equal-generation tie. A file is valid
only if it is exactly 10,656 bytes, has magic `PAT4`, format version 1,
`stack_size == PAT_STACK_SIZE` (256), a matching whole-file CRC32C with bytes
14..17 treated as zero, and no trailing byte. Firmware does not perform an
extra semantic allocator graph audit on load; the host acceptance validator
did. Library PAT4 saves use generation zero; hidden AutoSave
candidates use nonzero generations.

This separation preserves the established scalar transform writer and avoids
adding sixteen record-sized copies to SRAM. The scalar HCPR pair remains
exactly 34,768 bytes (64-byte header, 3,856-byte mask, 30,848-byte payload).
Its format byte changed from 1 to 2 when the HCNAMES-facing identity API
expanded from 129 to 145 rows; scalar payload name/source cells still cover
rows 0..128, while Pattern identity remains solely in HCNAMES/PAT4. The HCPR
payload and mask geometry did not change.

## 2. Retained ownership and memory

The approved permanent/retained additions are exactly 10,586 bytes:

| Owner | Symbol/state | Bytes | Lifetime |
| --- | --- | ---: | --- |
| PatternData | `pat_autosave_snapshot` | 10,519 | Permanent, one in-flight Scene snapshot |
| Autosave | `autosave_pattern_dirty_mask` | 2 | Permanent, one bit per Scene |
| Filesystem | `fs_pattern_generation[16]` | 64 | Permanent, next-generation baselines |
| Filesystem | `fs_pattern_drain_scene` | 1 | Permanent operation selector |

No second live Bank, per-Scene snapshot set, PAT4 staging image, or additional
filesystem handle was added. The 145-row HCNAMES source register and name
mirror expanded in place as part of the identity-schema change; HCPR format
helpers consume caller-owned identity arrays rather than adding an AutoSave
resident array.

The final Session 064 rebuild at `d5af5fd` reports:

```text
text=426,756  data=412  bss=289,964  total=717,132
binary=427,168 bytes  packaged image=427,184 bytes
binary SHA-256: 24c4de42ffb66001ee48ac0e9ba0e41c6833c39d66afbcc2c19eff55b6114e37
image  SHA-256: c6d5c551e5f320c7c97427528dc937da7d6b3b297779cb70f945606e646cd481
```

Linked symbol evidence: `pat_regions` = 168,304 bytes (`16 × 10,519`),
`pat_autosave_snapshot` = 10,519, Pattern dirty mask = 2,
generation array = 64, and drain-Scene selector = 1.

## 3. Dirty marking and snapshot boundary

Pattern dirtiness is deliberately independent of the scalar 3,856-byte
mutation mask. `autosave_markPatternDirty(scene)` sets one of 16 bits and
clears that Pattern HCNAMES row's refreshed witness in RAM. Every PatternData
mutation funnels through local `pat_markSceneDirty()`, which also invalidates
the Bank clean-Scene witness. Fourteen mutation sites cover step toggles and
erase, clears, dynamic-special writes, and Pattern/track parameter setters.
Whole-object Scene/Bank commits use `autosave_markSceneWithPatternDirty()`;
scalar-only readers and setters retain the without-Pattern boundary.

When the filesystem is idle and policy/runtime/card/Bank/menu gates allow it,
the scheduler selects the lowest numbered dirty Scene. Pattern work is the
last background claimant after settings, logging trace, and scalar AutoSave.
Admission is additionally rejected while `seq_recordActive` or
`seq_eraseActive` is set. At admission it clears the selected bit first,
copies the live region into `pat_autosave_snapshot`, advances the nonzero
generation, and starts the writer. No interrupt is masked during `memcpy`.

The clear-before-copy order is the concurrency contract: a later mutation can
set the bit again while the immutable snapshot streams. Completion never
blindly clears the bit. Any start/I/O/close/sync failure re-arms the Scene so
the work retries. A successful write publishes `R` only if the Pattern did
not become dirty again after the snapshot, preventing a stale snapshot from
claiming refreshed current state.

## 4. Writer transaction and HCNAMES publication

The writer streams the fixed PAT4 header and snapshot sections through the
existing bounded filesystem facade, computes CRC32C, patches the CRC field,
closes, and syncs. Only after durable file completion does it stage the
Pattern HCNAMES row as:

```text
<current Pattern name>\t@\tR
```

Pattern `@` is represented in RAM as
`FS_RESIDENT_SOURCE_PATTERN_AUTOSAVE == 0x1ffc`. It is intentionally distinct
from `FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT == 0x1ffd`, even though both render
as `@`; parsers and validators accept the Pattern token only for rows
129..144. HCNAMES safe-write remains temp (`/.hcnamtmp`) → close → sync →
remove old live → rename → final sync. A transient capture containing only
the temp file is therefore timing evidence, not automatically corruption;
the final full-card run contained a valid live HCNAMES file.

## 5. Boot restore and explicit-load precedence

The boot Pattern reader runs after scalar Bank/Scene restoration and evaluates
each present Scene independently. Both hidden candidates are streamed and
validated; the highest generation wins, with A on ties. The winner is applied
only when the Scene's Pattern HCNAMES row says Pattern AutoSave `@` and the
winner generation is nonzero. Otherwise the already initialized or explicitly
loaded Pattern remains authoritative and the generation baseline is zero.

Applying a winner copies the validated PAT4 resident fields into the Scene,
sets the future generation baseline, and preserves its HCNAMES identity.
Missing, corrupt, stale, or nonmatching candidates do not invalidate otherwise
valid scalar Scene state. Explicit root Pattern Load and Scene/Bank directory
Pattern loads reset that destination's hidden-generation baseline to zero, so
a later mutation begins a fresh local AutoSave lineage rather than treating a
previous resident Bank's hidden file as its parent.

## 6. HCNAMES and Bank-Load defect found during testing

HCNAMES now has exactly 145 data rows after its fixed type header:

- row 0 Bank;
- rows 1..16 Scenes;
- rows 17..32 Kits;
- rows 33..128 six typed Instruments per Scene;
- rows 129..144 one Pattern per Scene.

Initial Test Card A output showed Pattern rows becoming `Empty\t@` after Bank
Load. The PAT4 payloads themselves were not the cause. Bank Load's child
identity commit cached the Scene/Kit/Instrument block but omitted the Pattern
row, so later publication used the empty default name while the source was
correctly changed to `@`. The targeted fix added the child's Pattern name to
`filesystem_cacheCurrentBankSceneNameBlock()` alongside its other identity
rows. The repeat run published Alpha/Beta/Gamma/Empty correctly, and the final
16-Scene run published all expected Pattern names with `@|R`.

The later apparent `A Err BKKit14` while proceeding from Bank 000 to Bank 001
was not a second firmware failure. The generated Card A workflow had been
closed at step 7, and its later fixture files/instructions were malformed, so
the test could not validly continue. Card B replaced that workflow for the
functional closeout.

## 7. Verification evidence

### Focused functional cases

- Pattern name propagation: first run exposed and localized the missing
  Bank-child Pattern cache; repeat passed with correct names.
- One-Scene drain/reboot: passed. The winner was generation 22 in A over
  generation 21 in B and restored active steps, dynamic note/velocity/
  probability specials, and resident Pattern/track parameters.
- Ping-pong parity: passed.
- Bank Load all-present-child marking: passed for the exercised workflow.
- Refreshed lifecycle: final durable `@|R` publication passed. The transient
  clear/re-dirty race itself was established by code review rather than a
  deterministic manual timing observation.
- Absent-Scene rows and hidden-file scope: passed.
- HCPR version/geometry: version 2 and exactly 34,768 bytes passed.

### Full 16-Scene Card B closeout

The input fixture contained exactly 16 Scene children, 96 embedded Instrument
references that all resolved with current parameter keys, and 17 exact/current
seed PAT4 files. All names were at most eight characters and settings/
manifests were structurally valid.

After boot, enabling Live Record, changing every Scene's Pattern, and changing
at least one scalar parameter in every Scene, the captured card contained 19
hidden Pattern candidates: B generation 1 for all sixteen Scenes plus A
generation 2 for Scenes 0, 1, and 2. Every candidate had exact geometry,
current version/stack, matching CRC, correct generation parity, valid address
entries, allocator bitmap, and pool references. Every Scene winner differed
from its fixture Pattern. Winner byte-difference counts for Scenes 0..15 were:

```text
36, 16, 7, 4, 4, 5, 6, 5, 5, 5, 6, 6, 5, 6, 6, 6
```

HCNAMES had the exact header and 145 rows; every Pattern row had its expected
name/source and `@|R`. HCPR candidates were valid v2 records of 34,768 bytes:
`.hcprms1` generation 39 won over `.hcprms2` generation 38, both with empty
remaining mutation masks. Host comparison found five to seven scalar changes
in every Scene.

The logging capture contained 11,395 well-formed records and all 38 scalar
publications from generations 2 through 39 ended successfully, with no `E`
operation error or `X` phase-stall record. The bounded ring reported 6,513
dropped diagnostic records, so absence of every possible trace transition is
not proof; the independently valid PAT4, HCPR, HCNAMES, generation, CRC, and
content-difference evidence is the acceptance basis.

Result: **functional Pattern AutoSave PASS — CLOSED**.

## 8. Deferred and unclaimed coverage

The following are not product defects and were not claimed as completed:

- deterministic power removal during a Pattern write (TC5);
- deterministic proof that no Pattern drain is admitted while record/erase is
  active (TC9), pending suitable instrumentation and the unfinished Live
  Record feature;
- injected CRC-corruption fallback (TC11);
- supplemental timing/audio performance measurements (TC16).

The Card A workflow also did not validly complete its later interaction cases
because its generated files/steps were bad. Do not reinterpret those missing
results as passes. The accepted Card B artifacts are sufficient for the stated
functional closeout, not for the deferred fault-injection claims.

## 9. Commit/build record and next work

Session commits on `dev-ph4-pattern` span:

- `e268107` — Session 064 start/cleanup;
- `eac294e` — pre-implementation boundary;
- `c968b9f` — Pattern AutoSave implementation, initially untested;
- `d5af5fd` — post-AutoSave testing and HCNAMES Pattern-name propagation fix.

The final forced rebuild succeeded. Existing unrelated warnings remain in
AsyncFATFS, USB packed-pointer code, unused splash glyphs, unused filesystem
helpers, and newlib syscall stubs; no new build error was present.

The natural next Pattern feature is Phase 4.5 copy behavior. The three public
copy functions remain deliberate no-ops until copying duplicates referenced
pool blocks and rebuilds destination offsets/bitmap ownership safely. Pattern
automation count remains reserved/zero and descriptor-backed step automation
playback is still future work.
