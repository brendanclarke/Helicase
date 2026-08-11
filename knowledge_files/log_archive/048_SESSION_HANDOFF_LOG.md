# Session 048 Handoff — HCNAMES Source Authority and Instrument-Load AutoSave

**Date:** 2026-08-11  
**Repository:** `/Users/bc/Helicase Project/Helicase-check-fs/Helicase`  
**Starting revision:** `63bdd6e` (`hcnames extention attempt`)  
**End state:** intentional dirty worktree containing the Session 048 source and
documentation changes. Preserve unrelated user-owned `SD_CARD/` and Spotlight
changes; this closeout did not create a commit.

## End-of-session block

```text
DATE: 2026-08-11
SESSION GOAL: Make HCNAMES the sole active source/provenance authority, then
implement and prove the first isolated AutoSave Phase-2 mutation boundaries:
normal root Instrument Load and compatible InstrumentMrp Load.

COMPLETED:
- Replaced settings/SceneData source ownership with a paired 129-row HCNAMES
  register and its approved 258-byte filesystem-owned source cache.
- Made settings.cfg source-free; it now writes only the 17 current allowlisted
  system keys, including active_bank and autosave.
- Marked a root-pool Instrument immediately after retained commit: three type
  bytes, every Normal descriptor byte, and every Morphable Morph byte.
- Marked compatible InstrumentMrp immediately after endpoint copy: only
  Morphable destination Morph bytes.
- Repaired normal Load/Save exit release so the existing trace and writer
  schedulers can run, and added compact cause/evidence trace stages.

VERIFIED ON HARDWARE:
- Root Instrument: J=0x03 and I=0x07 show the requested/called marker, valid
  map/tracking, and 76 expected/accepted bytes; A/V/M/C/P/T published
  generation 2 successfully.
- Combined root Instrument + InstrumentMrp: generation 3 persisted Drum 1
  Normal/Morph values from brezeld3.drm and changed Drum 2 Morph only to
  casiopd3.drm values; type, HCPRMS name allocation, and Normal stayed equal
  to generation 2.
- The flashed SD image matched the current build image hash
  3b2dc30a10086996aa7f95ace4391750083ce65bfcd4ead0401ebdb49931a14d.

KNOWN ISSUES INTRODUCED:
- InstrumentMrp's top `kit` row is blank. It needs a Morph-only temporary
  snapshot and current HCNAMES name; it must not use normal Instrument restore
  semantics. This is deferred in SCOPING_TARGETS.md.

KNOWN ISSUES RESOLVED:
- A direct HCNAMES flush callback left the facade at DONE, blocking autonomous
  trace/AutoSave work after ordinary Load/Save exit.
- A physical mode switch during busy Instrument I/O/apply could be lost rather
  than beginning the ordinary exit once the owner became safe.
- Root Instrument provenance no longer relies on a mutable Menu temporary
  latch; Menu reads the immutable filesystem request-local origin flag.

NEXT SESSION RECOMMENDED GOAL: Implement and test, separately and in order,
normal Kit Load, root Scene Load without Pattern, and selective Bank Load
mutation marking. Each is one owner boundary and one hardware fixture.

BLOCKERS: AutoSave has no boot reader; reversible `kit` restore still lacks
its own fixture; and the 64-record trace can wrap during multi-Instrument
sessions, so durable A/B comparison remains the persistence proof.

CRITICAL REMINDERS:
- HCNAMES, not HCPRMS or settings.cfg, owns active names and source tokens.
- Do not combine Kit/Scene/Bank marking with writer exclusion, boot reader,
  Save-side marking, KitMrp, recursive delete, or active-Scene Bank behavior.
- Preserve approved RAM: source cache 258 B, page-exit queue 1 B, and trace
  storage only in DEV_MODE_LOGGING builds.
```

## 1. HCNAMES source authority

`/.hcnames` is the single active resident identity/provenance register. Its
fixed 129 logical rows are Bank, sixteen Scenes, sixteen embedded Kits, and
six Instruments per Scene. Every current write emits:

```text
<trimmed-name><TAB><source-token>\n
```

The source grammar is `-` (inherit), `?` (unknown), `000` through `999`
(direct root object in the namespace implied by the row class), and `@`
(direct root-Instrument stem). Legacy name-only rows read as unknown;
malformed extended records fail closed instead of silently inheriting.

`filesystem.c` owns `fs_resident_source[129]`, the approved 258-byte source
cache. It replaces the retired 32-byte SceneData source array. The cache stays
resident while the shared 9,000-byte HCNAMES/index display cache is reused.
`filesystem_resolveResidentSource()` walks Instrument -> Kit -> Scene -> Bank
without I/O; a later AutoSave reader will own source-target open/retry logic.
No AutoSave boot reader was added here.

Source propagation is staged at the successful root-load boundary: Instrument
sets `@`; Kit sets its direct Kit row and inherited Instruments; root Scene and
Bank use their direct/inherited rows while preserving unselected Bank children.
Save/rename, temporary `kit` restore, and Morph projection do not invent new
source provenance. The existing HCNAMES preserve/overlay/rewrite close-and-sync
gate publishes the staged pair with its name transaction.

`settings.cfg` now writes 17 allowlisted lines. It has `active_bank` and
`autosave`, but no `scene_source_NN` data. Old scene-source keys are accepted
only to be ignored during migration. `active_bank` remains the boot selector,
not a competing provenance register. The copied Session 048 HCNAMES shows rows
such as `brezeld3<TAB>@`; its settings file has no Scene-source fields.

This confirms the active writer/register contract. It does not test the future
reader's missing/malformed direct-target fallback matrix or claim boot restore.

## 2. Instrument and InstrumentMrp AutoSave boundary

The canonical dirty mask is an OR-set, not a menu-exit queue. Marking occurs
when data has entered retained `scene->kit.instruments[slot]`, so several
loads inside one nested session leave the final retained owner marked before
the later writer samples it. The existing Load/Save gate still suppresses new
writer starts while the page is active.

Menu reads `filesystem_loadedInstrumentWasTemporary()` before any later request
can reuse operation state. It passes a call-local
`mark_autosave_whole_instrument` decision to the shared Preset apply path:
normal root-pool load enables it; hidden typed `.hctmp.<ext>` restore supplies
zero. A mutable browser selection or temporary-session latch is not valid
completion provenance.

After each root destination is published Bank-present and assigned its staged
Instrument, `autosave_markWholeInstrumentDirty()` marks the three-byte type,
every owned Normal descriptor, and every Morphable Morph descriptor. It
excludes the HCPRMS name allocation, padding, runtime overlays, HCNAMES
identity/source, and non-Morphable Morph reserve.

`preset_startInstrumentMorphApply()` calls
`autosave_markInstrumentMorphDirty()` only after a compatible same-type copy
succeeds. It marks only Morphable destination Morph endpoints and runs before
active-Scene runtime refresh, so inactive resident Scene data is also covered.
Mismatch, invalid input, staging, Save, and HCNAMES ownership are no-change.
HCPRMS name bytes intentionally stay non-authoritative: HCNAMES remains the
only active source/name register.

## 3. Trace and normal-exit corrections

`AutosaveTrace` now includes three compact logging-only records in its existing
64-by-8-byte ring: `I` summarizes whole-Instrument map/tracking/publication
counts; `J` witnesses whether the root commit requested and called the marker;
and `N` timestamps nested Instrument entry, HCNAMES read/flush, temporary
snapshot, and typed-index phases. `N` observes the intermittent first-entry
blank/slow `kit` display without changing menu behavior.

The trace append scheduler is considered before the autosave writer after
settings work declines an idle facade, but it never opens its file while a
Load/Save page owns the facade. This preserves foreground priority and leaves
an early durable witness after normal exit.

Two targeted field fixes were required:

1. `menu_residentNameScratchFlushComplete()` was a direct callback and observed
   terminal status without `filesystem_ack()`. It now records success/failure,
   acknowledges either terminal status, then performs its prior UI path. The
   facade therefore reaches IDLE and autonomous schedulers may run.
2. The user approved one normal-SRAM1 byte, `menu_pendingPageSwitch` (zero
   empty; page-plus-one encoding), to retain a requested non-Load destination
   while an Instrument operation owns `menu_storageBusy`. The ordinary Menu
   poll consumes it only after the owner releases; it holds no payload/name
   data and is not a writer-exclusion mechanism.

## 4. Hardware evidence

### Normal root Instrument

After root Instrument Load to Scene 5/slot 0, normal page exit, and sufficient
idle time, `asavetrc.bin` contained 71 records (568 B): `J=0x03`; preceding
`I=0x07`; packed `I=0x004c4c05` (Scene 5, slot 0, 76 expected/accepted); then
successful `A/V/M/C/P/T` with generation 2. `.hcprms2` was that valid winner.
Its type `drm` and Normal/Morph bytes beginning `03 24 7e` match
`Instrument/Drum/brezeld1.drm`. Its retained HCPRMS name was `rollind1`, which
is correct because HCNAMES row 63 (line 64) was `brezeld1<TAB>@`.

### Combined normal and Morph load

The next fixture loaded a root Drum into Drum 1 and a DrumMrp source into Drum
2. `.hcprms1` became the valid generation-3 winner over `.hcprms2` generation
2. Scene 5/slot 0 Normal and Morph values match `brezeld3.drm`. Scene 5/slot
1 type, HCPRMS name allocation, and Normal bytes are byte-for-byte unchanged;
only its Morph allocation changes, and the complete result matches
`casiopd3.drm`. The trace reaches successful `A/V/M/C/P/T` publication.

The 64 retained `D` records all address Scene-5/slot-1 Morph cells: they are
the tail of one 34-cell Drum Morphable sweep followed by a later complete
34-cell sweep, so earlier root `I/J` records wrapped. The ring cannot identify
why the second sweep occurred. The durable generation-to-generation byte
comparison—not a single-command trace count—is the acceptance proof. Do not
enlarge or redesign the trace without a demonstrated diagnostic need.

## 5. Build, specifications, and working-document disposition

`make -j2 && make img` passed. The logging-on ELF reports `text=375,812`,
`data=396`, `bss=79,300`; SRAM1 static use is 76,124 B and all static RAM is
88,404 B. `SD_CARD/LXRV2_lxr02.img` and `build/LXRV2_lxr02.img` matched SHA-256
`3b2dc30a10086996aa7f95ace4391750083ce65bfcd4ead0401ebdb49931a14d`.

`AUTOSAVE.md`, `FILESYSTEM_SPEC.md`, `MODULE_INTERCHANGE_SPEC.md`,
`DEV_MODES.md`, and `SRAM_MANIFEST.md` now contain the live behavior or linked
figures. The manifest was regenerated from this ELF; source cache is 258 B and
`menu_pendingPageSwitch` is 1 B. No forced logging-off size regeneration was
performed after this final logging-on source build.

`INSTRUMENT_LOAD_AUTOSAVE.md` and `HCNAMES_SOURCES_EXTENSION.md` are removable
working records: their decisions, evidence, limitations, and next steps are
preserved here and in the specifications. `SESSION_046-048_PLAN.md` is also
closed and removable. `AUTOSAVE_PHASE2_PLAN.md` remains active because it
records the sequencing constraints for Session 049 and later.

## 6. Session 049 scope and exclusions

Implement one boundary, build, hardware-test, copy both HCPRMS files, HCNAMES,
settings, and trace, and decode the durable result before starting the next:

1. **Normal Kit Load:** mark implemented Kit values plus all six complete
   Instrument regions only after actual successful retained Kit commit.
2. **Root Scene Load without Pattern:** mark only committed non-Pattern Scene
   payload after the staged Scene+Kit commit; browser preview/failure marks
   nothing and Pattern stays excluded.
3. **Selective Bank Load:** mark exactly the successfully committed child mask,
   never all sixteen Scenes. Preserve unselected resident payload/HCNAMES
   blocks. Bank Save is not a Session-049 mutation producer.

Do not combine that work with Load/Save writer exclusion, a boot reader,
Save-side marking, KitMrp, recursive `afatfs_deleteTree()` repair, or runtime
Bank active-Scene preservation. The InstrumentMrp blank `kit` row stays
deferred: it needs a Morph-only temporary snapshot and the current HCNAMES
name, never a full normal Instrument restore.
