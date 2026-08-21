# Scene Load — Pattern Not Reaching Playback

Investigation, fix plan, and implementation record. Written 2026-08-21.

**Status: fix IMPLEMENTED, clean-building, UNVERIFIED on hardware.** The root
cause is confirmed by decoded trace evidence (see "Hardware evidence" below),
but the code change has not yet been run on the device. Nothing in this
document should be treated as hardware-proven except the items explicitly
labelled as trace- or card-verified.

---

## START HERE NEXT SESSION

### What to do first

1. **Flash `build/LXRV2_lxr02.img`** (already built clean, `DEV_LOGGING_IWDG=0`).
   Copy to the SD card root, hold the main encoder, power on.
2. **Confirm it boots at all.** The previous flash hung indefinitely on the
   splash — that was a watchdog-init bug, now fixed *and* disabled by default.
   If it still hangs, the IWDG is no longer a candidate explanation and the
   original intermittent boot hang has resurfaced; capture the card and start
   from the boot-hang section below rather than assuming the Pattern fix.
3. **Run the verification checklist** ("Verification after the fix"). The single
   most important item: boot from Bank 012 and, *without* pressing any PERF
   Scene button, load an intact Scene — the pattern should now play and display
   immediately.
4. **Decode the card** with `python3 tools/devlog_unpack.py` (compact) or
   `tools/decode_devlogs.py` (verbose) and check the trace expectations in
   "Trace check for the retest".

### Where things stand

| Item | State |
| --- | --- |
| Pattern fix (Bank Load realign) | Implemented, clean build, **untested** |
| Root cause | **Confirmed** by trace (`R DONE=1 mask=0x0020 {5}`) |
| Card structural damage | **Repaired** — 41/41 root Scenes, 208/208 Bank children pass audit |
| IWDG boot-hang regression | Fixed; flag now defaults to **0** (off) |
| `DEV_LOGGING_IWDG` feature | Never successfully run on hardware |
| Partial-save hardening | **Not started** — 4 ranked options recorded |
| `Bank/old012-c76c/` orphan | Left in place, awaiting a delete decision |

### Files changed this session

**Pattern fix (the thing under test):**
- `Core/Sequencer/sequencer.h` — new `seq_alignActivePatternToScene()` declaration
- `Core/Sequencer/sequencer.c` — its implementation
- `Core/Hardware/SD/filesystem.c` — call site at the Bank Load phase-20 commit (`:10837`)

**IWDG regression fix (disabled by default, not under test):**
- `config.h` — `DEV_LOGGING_IWDG` now `0`, with the regression rationale
- `Core/Hardware/SD/filesystem.c` — rewritten `filesystem_devIwdgStart()`, new
  `fs_devwdg_armed` gate, added `filesystem_blockPoll()` feed
- `Core/Hardware/SD/filesystem.h`, `STM32F765VIHx_FLASH.ld`, `main.c` — from the
  original IWDG addition

**Docs:** this file, `MEMORY.md`, `DEV_MODES.md`, `SRAM_MANIFEST.md`
**Tooling:** `tools/devlog_unpack.py` (new compact trace decoder)
**Card:** `SD_CARD/Scene/` repairs (see "Card repair performed")

### Two traps that cost time this session — do not repeat

1. **`make clean` after ANY header edit.** The Makefile has no header
   dependency tracking, so editing `config.h` rebuilds nothing and produces a
   binary with the old flag value and identical sizes. Never report build
   numbers from an incremental build across a header change.
2. **`ScnL48` is not specific.** Every Scene Load failure reports it, because
   the code is built from the terminal phase (`0x48` = 72) rather than the
   failing one. It means "Scene Load failed somewhere", nothing more.

### Result log — fill in next session

```
Date:
Firmware flashed (img size / build):
Boots?                                  Y / N
Pattern plays after Scene Load
  with NO PERF press?                   Y / N     <-- the actual fix test
STEP LEDs correct at boot (Scene 5)?    Y / N
PERF Scene switching still OK?          Y / N
Runtime Bank Load repaints STEP row?    Y / N
MIDI program change on boot?            Y / N  (expected: N)
Repaired Scenes load without ScnL48?    Y / N
Trace: R record still DONE=1 {5}?       Y / N
Trace: any E or X records?              Y / N  (expected: N)
Notes:
```

---

## Summary

`Load:[Scene  ]` **does** correctly read `pattern.pat` and **does** write it into
resident Scene SRAM. Nothing in the file format, the parser, the writer, or the
Scene load state machine is broken.

The defect is that **playback and the STEP UI do not read the Scene the loader
wrote to.** Pattern is the only Scene-owned payload addressed through
`seq_activePattern` / `menu_shownPattern` instead of `scene_getActiveIndex()`,
and those two indices are never realigned when Bank Load changes the active
Scene. The pattern lands in `scenes[5].pattern` while the sequencer keeps
playing `scenes[0].pattern`.

This is exactly the "loaded into the wrong Scene" hypothesis, and it is
reproducible from the current card contents without any code change.

## Evidence

### The payload path is provably correct

- Save emits real data. `SD_CARD/Scene/000 Barf/pattern.pat` contains
  `version=3` plus seven populated 32-hex-character rows, not a placeholder.
- Writer (`storageTypes.c:1797`, `storage_formatPatternStubLine`) emits
  `step_on[track][byte]` as `hex[v>>4], hex[v&0x0f]`.
- Reader (`storageTypes.c:1726`, `storage_patternStubParseLine`) rebuilds
  `(hi<<4)|lo` and calls `pat_patternSetSetStep(pattern, track, byte*8+bit,
  (v>>bit)&1)`.
- `pat_patternSetGetStep()` reads `(step_on[track][step>>3] >> (step&7)) & 1`.
  With `step = byte*8+bit`, `step>>3 == byte` and `step&7 == bit`. The round
  trip is bit-exact.
- Load phases 44→46→53→54/55 (`filesystem.c:9572`-`9881`) open the discovered
  `.pat`, probe for the `format=` prefix, parse each line into
  `filesystem_directPatternTarget()`, and finalize. Phase 61
  (`filesystem.c:9973`) mirrors the result to any additional selected Scenes.
- `filesystem_commitSceneStage()`'s `pat_initPatternSet()` zeroing
  (`filesystem.c:12119`) runs at phase 33, *before* the pattern read — it is a
  reset, not a post-load clobber.
- `FS_INTERNAL_OP_LOAD_SCENE` dispatches to `filesystem_loadSceneDirectory_tick()`
  (`filesystem.c:20186`), and `op_scene_load_scene_mask` is set from the
  request (`filesystem.c:21329`) after `filesystem_start()` clears scratch.

### The desync, and why it is live on this card

Two indices decide which Scene playback and the STEP LEDs read:

| Consumer | Reads | Site |
| --- | --- | --- |
| Sequencer playback | `pat_isStepActive(track, step, seq_activePattern)` | `sequencer.c:344` |
| STEP LEDs / step buttons | `pat_isStepActive(..., menu_getViewedPattern())` | `ledHandler.c:1052`, `1125`; `buttonHandler.c:504` |
| Scene Load destination | `1u << scene_getActiveIndex()` | `menu.c:4644` → `menu_kitLoadSceneMask` |

`seq_activePattern` (`sequencer.c:98`) and `menu_shownPattern` (`menu.c:1007`)
are both BSS-zero at boot. The **only** code that assigns them is
`menu_perfModeSceneButtonPressed()` (`menu.c:4480`-`4491`), which correctly
calls `scene_selectActive()`, `menu_setShownPattern()`, and
`seq_selectActivePattern()` together.

Bank Load's metadata commit changes the active Scene **without** that pairing:

```c
/* filesystem.c:10782, Bank Load phase 20 */
bank_setHasResidentBank(1u);
scene_selectActive(op_bank_active_scene);   /* <-- seq_activePattern and
                                                   menu_shownPattern are not
                                                   realigned here */
```

`scene_selectActive()` (`SceneData.c:116`) is documented as
"Selection changes identity, never data" and deliberately has no other effects.

On the card as it currently stands:

- `SD_CARD/settings.cfg` → `active_bank=12`
- `SD_CARD/Bank/012 LoadTst/bankset.bcg` → `active_scene=5`,
  `scene_mask_voice_edit=0020` (bit 5)

So every boot from this card produces:

| State | Value |
| --- | --- |
| `scene_getActiveIndex()` | 5 |
| `seq_activePattern` | 0 |
| `menu_shownPattern` | 0 |

`Load:[Scene]` then targets `1u << 5`, the pattern is written correctly into
`scenes[5].pattern`, and playback reads `scenes[0].pattern` — which is empty or
stale. The load looks like it silently did nothing.

### Why only Pattern appears broken

Every other Scene payload is applied through `scene_getActiveIndex()` —
`preset_applySceneSettings(scene_getActiveIndex())` (`presetManager.c:1318`,
`1351`), `drumset_apply_scene = scene_getActiveIndex()` (`presetManager.c:1352`),
and the HCNAMES/name rows. Those all resolve to Scene 5 and are correct, so the
Kit, voices, mix settings, and displayed name all update normally. Pattern is
the single payload routed through the desynced indices, which is why it is the
only thing that appears not to load.

## Hardware evidence — diagnosis confirmed by trace

`SD_CARD/asavetrc.bin` (decoded with `tools/devlog_unpack.py`) independently
confirms the diagnosis. Every root Scene Load in the capture succeeded, and
every one targeted Scene 5:

```
000229 t=11586 R f=0x01 v=0x00000020 | DONE=1 mask=0x0020 {5}
000767 t=49843 R f=0x01 v=0x00000020 | DONE=1 mask=0x0020 {5}
001805 t=24944 R f=0x01 v=0x00000020 | DONE=1 mask=0x0020 {5}
002565 t=16605 R f=0x01 v=0x00000020 | DONE=1 mask=0x0020 {5}
003077 t=38949 R f=0x01 v=0x00000020 | DONE=1 mask=0x0020 {5}
003586 t=24881 R f=0x01 v=0x00000020 | DONE=1 mask=0x0020 {5}
004099 t=46338 R f=0x01 v=0x00000020 | DONE=1 mask=0x0020 {5}
005079 t=11984 R f=0x01 v=0x00000020 | DONE=1 mask=0x0020 {5}
```

`R` is `on_scene_load_complete()`'s witness: flag bit 0 set means the callback
observed `FS_STATUS_DONE`, and `value` is the destination mask. All eight reads
are `DONE=1`, mask `0x0020` = Scene 5 only. The following `D`/`I`/`L` records
show the full Scene 5 payload being marked dirty (`L Kit Scn5`, `L Scene Scn5`).

**The load works and writes Scene 5. Playback reads Scene 0.** That is the
entire defect. The capture contains zero `E` (operation error) and zero `X`
(phase stall) records, so nothing in the load path failed.

## Second, unrelated defect found: `Err ScnL48`

The first confirmation attempt returned `Err ScnL48`. This is **not** the
pattern defect and does not contradict the evidence above — it is a separate
pre-existing data problem on the card.

### Decoding the code

`fs_error_code` is built by `filesystem_makeAutoErrorCode()`
(`filesystem.c:3046`) as an operation prefix plus the phase in **hexadecimal**.
`ScnL` is `FS_INTERNAL_OP_LOAD_SCENE` (`filesystem.c:3018`), and `0x48` = **phase
72**, the Scene loader's `RETURN ROOT + FINISH` exit reached with
`op_close_status == FS_STATUS_ERROR`.

### Cause: Scene Load is all-or-nothing on child discovery

Phase 11 (`filesystem.c:8984`-`8992`) rejects the entire Scene if the folder
scan did not find all three children — a `Kit <name>/` directory, a `.pat`, and
a `.fx`:

```c
if (op_close_status != FS_STATUS_DONE ||
    op_scene_child_open_name[0] == '\0' ||
    op_scene_pattern_open_name[0] == '\0' ||
    op_scene_effect_open_name[0] == '\0') {
    filesystem_setPresetNameInvalid();
    op_close_status = FS_STATUS_ERROR;
    op_phase = 72;              /* -> "ScnL48" */
    return;
}
```

Six Scene folders on the card are incomplete and will always fail this way:

| Scene folder | `effects.fx` | `Kit <name>/` contents |
| --- | --- | --- |
| `002 Hard` | **missing** | 7 files (ok) |
| `003 RedSnap` | **missing** | 7 files (ok) |
| `006 Brezel` | **missing** | **empty** |
| `007 Chip` | **missing** | **empty** |
| `009 Forest` | **missing** | 7 files (ok) |
| `011 FilMod` | **missing** | 7 files (ok) |

An intact Scene (`001 Slak`, `015 Machine`, `000 Barf`, …) has `sceneset.scg`,
`pattern.pat`, `effects.fx`, and a populated `Kit <name>/`. `effects.fx` itself
is a pure placeholder:

```
format=helicase.effect
version=1
placeholder=1
```

### Diagnostic weakness worth fixing

Every failure branch in `filesystem_loadSceneDirectory_tick()` routes to phase
72 before `filesystem_finish()` builds the code, so **all** Scene Load failures
report the identical `ScnL48` regardless of cause. The `E` trace record has the
same limitation, since it also captures `op_phase` at completion. Consider
capturing the failing phase at the point of failure (or emitting a distinct
named code per rejection reason at phase 11) so these are separable without
source reading.

### Optional robustness fix

Hard-failing an entire Scene Load because a three-line placeholder file is
absent is arguably too strict — no musical data is lost by its absence, and the
loader already tolerates a v1 placeholder `pattern.pat`. Consider treating a
missing `.fx` as "no effects to restore" and skipping phases 56-60, while
keeping the missing-Kit and missing-`.pat` cases fatal. Track separately from
the pattern fix; do not bundle them.

## Save-path audit — how the damage happened

`filesystem_saveSceneDirectory_tick()` writes a Scene as a strictly linear,
**non-atomic** sequence of roughly twelve file operations:

| Phase | Operation |
| --- | --- |
| 5 | `filesystem_deleteSlotDirectory_tick()` — **deletes the existing slot tree** |
| 8 | `afatfs_mkdir_lfn()` — creates `Scene/NNN Name/` |
| 11 | writes `sceneset.scg` |
| 15 | creates `Kit <name>/` |
| 18 | writes `kitset.kcg` |
| 20-27 | writes the six instrument files |
| 29-32 | writes `pattern.pat` |
| **33-36** | **writes `effects.fx` — LAST** |
| 37 | `chdir` root, then HCNAMES + `.hcindex` rebuild |

**No branch skips `effects.fx` on a successful save**, and Bank Save routes its
children through this same function (phase 37 branches on
`FS_INTERNAL_OP_SAVE_BANK`), so Bank children are covered identically. In that
narrow sense the save paths are correct.

**But every intermediate failure calls `filesystem_finish(FS_STATUS_ERROR)` and
leaves the partially-written directory on the card with no cleanup**, and phase
5 has already destroyed the previous good copy. Because `effects.fx` is the
last of twelve operations, it is the single most likely file to be missing
after an interrupted save — and Load's all-or-nothing phase-11 check then makes
that Scene permanently unloadable, reporting only the generic `ScnL48`.

This is the documented non-atomic contract (MEMORY.md: *"This is not a
crash-recoverable transaction; temporary/old promotion names are not used"*),
but the consequence had not been connected to Load's rejection rule.

The damage found on the card maps cleanly onto abort points in that exact
sequence, which is strong confirmation:

| Scene | State | Aborted around |
| --- | --- | --- |
| `002 Hard`, `003 RedSnap`, `009 Forest`, `011 FilMod` | complete except `effects.fx` | phase 32-36 (last file) |
| `007 Chip`, `012 Emott` | `Kit <name>/` created but empty | phase 15-18 |
| `006 Brezel` | no `sceneset.scg`, empty Kit | phase 11-15 |

### Recommended hardening (ranked, not yet implemented)

1. **Make Load tolerant of a missing `.fx`.** Cheapest real fix; removes the
   most common failure mode entirely. `effects.fx` carries no musical data.
2. **Write `effects.fx` immediately after `sceneset.scg`** instead of last.
   One-line reorder that shrinks the exposure window for the most likely
   casualty; does not help partial-Kit cases.
3. **Clean up on save failure.** On any `filesystem_finish(FS_STATUS_ERROR)`
   after phase 8, delete the partial directory so a failed save leaves nothing
   rather than an unloadable stub.
4. **Atomic commit** (write to a temporary name, rename last). The real fix,
   and the largest; it reopens the Session 053 decision to drop tmp/old
   promotion, so it needs its own scoping.

Distinguish these from the Pattern fix — they are separate defects that
happened to surface during the same test.

## Card repair performed (2026-08-21)

All 41 root Scenes and all 208 numbered Bank children now pass a full
structural audit. Repairs, all reversible by re-saving the Scene:

- **Added `effects.fx`** to `002 Hard`, `003 RedSnap`, `006 Brezel`,
  `007 Chip`, `009 Forest`, `011 FilMod`. Byte-identical (47 B) to the
  firmware-written reference in `001 Slak`.
- **Restored the six instrument files + `kitset.kcg`** into the empty embedded
  Kit folders of `006 Brezel`, `007 Chip`, `012 Emott`, copied from the root
  Kit library entries `Kit/006 Brezel`, `Kit/007 Chip`, `Kit/012 Emott` —
  which match by **both slot number and name**. Verified against intact Scenes
  that the embedded `kitset.kcg` is the root one **minus** the legacy
  `audio_out=` lines (routing is Scene-owned in `sceneset.scg`), so those lines
  were stripped; the result is structurally identical to firmware output. The
  six instrument files are byte-identical between root and embedded copies on
  every intact Scene checked.
- **Reconstructed `sceneset.scg`** for `006 Brezel`, which had none.
  `audio_out=0,1,0,0,0,1` was *recovered* from the legacy `audio_out=` lines in
  `Kit/006 Brezel/kitset.kcg` (exactly the fallback the parser implements).
  All other fields are **firmware defaults**, not recovered values:
  `voice_decimation_all=127`, `midi_channel=1..7`, `midi_note=63`,
  `morph_amount=0`, `voice_morph_amount=0`, `fx_send_amount=0`,
  `fader_setting=0`. Only `format`/`version` are required by
  `storage_scenesetFinalize()`; the rest are optional with safe defaults. If
  that Scene's mix/morph settings were non-default they are unrecoverable —
  re-save the Scene to correct them.

### Not touched — needs a decision

`Bank/old012-c76c/` is orphaned debris from an older overwrite implementation
(`old` + slot `012` + hash suffix; the current code no longer uses tmp/old
promotion names). It is **not** referenced by `Bank/.hcindex`, and its name does
not parse as `NNN Name`, so the Bank scanner ignores it — it causes no error
today. It contains one empty child (`12 Slak/`) and one populated child
(`04 Slak/`). Deleting it is destructive and was left alone pending
confirmation.

## Corrected confirmation test

The original test was run against one of the six damaged folders, so it was
inconclusive. Re-run it on a **known-intact** Scene — `000 Barf`, `001 Slak`, or
`015 Machine`:

1. Boot normally from the current card (Bank 012, `active_scene=5`).
2. **Without** pressing any PERF Scene button, `Load:[Scene  ]` slot `000 Barf`
   (verified intact, non-empty pattern). Expect: loads without error, Kit/name
   update correctly, but the pattern does not play or show.
3. Leave the Load page, enter PERF mode, and press the SEQ button for Scene 5.
   This is the only path that calls `seq_selectActivePattern(5)` and
   `menu_setShownPattern(5)`. Note that SEQ buttons **on the Load page** select
   the load destination mask instead and will not cure the desync.
4. Return to `Load:[Scene  ]` and load `000 Barf` again. The pattern should now
   play and display.

A second confirmation needs no load at all: immediately after boot, the STEP
LEDs show Scene 0's pattern while the displayed Kit/Scene name is Scene 5's.

## The fix

### Chosen approach — realign playback/view at the one desync source

Bank Load is the only place an active-Scene change is not paired with a
playback/view realign. Fix it there, adjacent to the existing call.

`seq_selectActivePattern()` cannot be reused as-is: besides setting the
indices it calls `led_notifyPatternChanged()`, `seq_sendProgChg()` (emits a
MIDI program change on the wire) and `voiceControl_noteOff(0xFF)`. Firing those
from a boot-time Bank Load is an unwanted side effect. Add a narrower API
instead.

**1. `Core/Sequencer/sequencer.c` / `.h` — new realign entry point**

```c
void seq_alignActivePatternToScene(uint8_t scene_index);
```

Sets `seq_activePattern` and `seq_pendingPattern` to `scene_index`, clears
`seq_loadPendigFlag` / `seq_newPatternAvailable`, and calls
`seq_realignActivePatternToMasterClock()`. It deliberately does **not** notify
LEDs, send a program change, or force note-offs — it is a state-restore for a
Scene selection that some other owner already made, not a performance action.
Guard with `pat_patternValid(scene_index)` exactly as
`seq_selectActivePattern()` does.

**2. `Core/Hardware/SD/filesystem.c:10782` — pair the realign with the commit**

```c
bank_setHasResidentBank(1u);
scene_selectActive(op_bank_active_scene);
/*
 * Pattern playback and the STEP view are indexed by Sequencer/Menu state, not
 * by scene_getActiveIndex(). Realign both with the Bank's committed active
 * Scene so a later Scene Load writes the Scene that is actually playing and
 * displayed. Without this the two remain BSS-zero until the first PERF Scene
 * press, and Pattern silently loads into a Scene nothing reads.
 */
seq_alignActivePatternToScene(op_bank_active_scene);
menu_setShownPattern(op_bank_active_scene);
```

`filesystem.c` already includes both `menu.h` (line 76) and `sequencer.h`
(line 78) and already calls into both, so this adds no new dependency edge.

**3. Repaint**

`menu_setShownPattern()` documents that it "does not repaint LEDs or reload
PatternData params; callers must do that explicitly." Boot repaints through the
normal `menu_start()` / `menu_repaintAll()` path, and runtime Bank Load
repaints via `menu_startSoundApply()` in `PRESET_OP_BANK_LOAD`
(`menu.c:7903`-`7909`). Verify on hardware that the STEP row repaints after a
**runtime** Bank Load; if it does not, add a `menu_refreshPerfSceneLeds()` /
`led_updatePatternTrackView()` call in that completion branch rather than
inside `filesystem.c`.

### Scope

- Scene Load itself needs **no change**. Once the indices agree it already
  works, which is why the confirmation test above passes after a PERF press.
- Do not change `scene_selectActive()`. Adding Sequencer/Menu coupling to
  SceneData would invert the dependency direction and break its documented
  "identity, never data" contract.

## Alternative considered and rejected for now

**Single source of truth:** change `sequencer.c:344` and the `ledHandler` /
`buttonHandler` read sites to use `scene_getActiveIndex()` directly and retire
`seq_activePattern` / `menu_shownPattern` as separate state. This removes the
whole class of desync permanently and is probably the correct end state.

Rejected for this pass because `seq_activePattern` is also the target of
`seq_pendingPattern` boundary switching (`sequencer.c:375`-`384`), so collapsing
them changes PERF queued-Scene-change semantics — a behavioural change beyond
the reported defect. Worth raising in `SCOPING_TARGETS.md` as a follow-up.

## Related observation (not the bug, no fix proposed)

`filesystem.c:9434`-`9477`, phase 33: `filesystem_commitSceneStage()` is called
before `if (!afatfs_chdir(NULL)) return;`, which re-enters the same phase until
the chdir completes. The commit therefore re-runs and re-zeroes
`target->pattern` on each retry tick. This is currently harmless because the
pattern read happens later at phases 44+, but it makes the phase order
load-bearing in a way the code does not state. If phases are ever reordered so
any pattern data exists before phase 33 completes, this silently destroys it.
Consider hoisting the commit behind a completed-once guard when that area is
next touched.

## Implementation record (2026-08-21)

The plan above was implemented as written, across three source files. (A
further set of files — `config.h`, `filesystem.c`'s watchdog block,
`STM32F765VIHx_FLASH.ld` — changed for the separate IWDG regression documented
later in this document; those are not part of the Pattern fix.)

### 1. `Core/Sequencer/sequencer.h` — new API declaration

Declared `void seq_alignActivePatternToScene(uint8_t scene_index);` immediately
after `seq_selectActivePattern()`, with a full contract comment block covering
why it exists separately, its inputs/outputs, what it deliberately does **not**
do (no LED notify, no `seq_sendProgChg()`, no `voiceControl_noteOff()`), and its
clients.

### 2. `Core/Sequencer/sequencer.c` — implementation

Added `seq_alignActivePatternToScene()` directly after
`seq_selectActivePattern()` so the two sit side by side and the difference
between them is visible in one screen. It validates with `pat_patternValid()`,
assigns `seq_activePattern`/`seq_pendingPattern`, clears `seq_loadPendigFlag`
and `seq_newPatternAvailable`, and calls
`seq_realignActivePatternToMasterClock()` — the same state work as
`seq_selectActivePattern()` minus all three performance side effects.

Rejecting an invalid index rather than clamping is deliberate: an out-of-range
Bank manifest `active_scene` leaves playback untouched instead of pointing at a
nonexistent Scene.

### 3. `Core/Hardware/SD/filesystem.c` — Bank Load call site

In the Bank Load phase-20 metadata commit, immediately after the existing
`scene_selectActive(op_bank_active_scene)`:

```c
seq_alignActivePatternToScene(op_bank_active_scene);
menu_setShownPattern(op_bank_active_scene);
```

with an adjacent comment block recording the defect history, why
`scene_selectActive()` alone is insufficient, why no repaint is triggered from
`filesystem.c`, and the affiliate list.

`filesystem.c` already included `menu.h` (line 76) and `sequencer.h` (line 78)
and already called into both, so no new dependency edge was introduced.

### Init-ordering check

`filesystem.c` now calls into Sequencer and Menu during **boot** Bank Load, so
both must already be initialised. Verified in `main.c`:

| Step | Line |
| --- | ---: |
| `seq_init()` | 439 |
| `menu_init()` | 496 |
| `preset_loadBank()` (boot Bank Load) | 813 |
| `audioCodec_init()` | 1022 |

Both subsystems initialise well before the phase-20 commit, and the whole
sequence is pre-audio, so `seq_realignActivePatternToMasterClock()`'s touch of
`seq_ledState` and `menu_getActiveVoice()` is safe.

### Build result

> **Superseded.** The numbers originally recorded here came from an
> *incremental* build and are not trustworthy — see the build-system footgun
> below. Authoritative clean-build figures are in "Verified build results" near
> the end of this document. Retained points that still hold:

- **The Pattern fix allocates no RAM**, so the MEMORY.md RAM-approval policy is
  not engaged by it. (The `bss` delta between the two clean builds listed later
  is attributable to the IWDG feature's own state, not to this fix.)
- The five `-Wunused-function` warnings in `filesystem.c` are pre-existing and
  unrelated — they predate this change.
- `seq_alignActivePatternToScene` does not appear as a standalone symbol in
  `nm` output because LTO inlined its single call site. Presence is confirmed
  instead by the call site at `filesystem.c:10837` and a clean compile.

### What was deliberately NOT changed

- **Scene Load** — untouched. It was always writing the correct Scene; the
  confirmation test's step 4 passing after a PERF press is exactly why.
- **`scene_selectActive()`** — untouched. Adding Sequencer/Menu coupling to
  SceneData would invert the dependency direction and break its documented
  "identity, never data" contract.
- **`seq_selectActivePattern()`** — untouched, so front-panel PERF switching
  keeps its existing LED, program-change, and note-off behaviour.
- **The `.fx` / partial-save hardening** (four ranked options above) — separate
  defects, deliberately not bundled.
- **The phase-33 re-entrancy observation** — left as a note.

## Verification after the fix

1. Boot from Bank 012 (`active_scene=5`). Before any PERF press, confirm the
   STEP LEDs show Scene 5's pattern, not Scene 0's.
2. `Load:[Scene  ]` a slot with a known pattern; confirm it plays and displays
   immediately, with no PERF press needed.
3. Press PERF Scene buttons across several Scenes and confirm switching still
   behaves as before (no regression from the new realign API).
4. Load a Bank at runtime (not just boot) and confirm the STEP row repaints to
   the new active Scene.
5. Re-run the `Save → power cycle → Load` round trip and confirm the pattern
   survives, closing the original "make it stick" requirement.
6. Confirm no MIDI program change is emitted on boot Bank Load (the reason
   `seq_selectActivePattern()` was not reused).
7. Confirm the six repaired Scenes (`002 Hard`, `003 RedSnap`, `006 Brezel`,
   `007 Chip`, `009 Forest`, `011 FilMod`) now load without `ScnL48`, and that
   `012 Emott` loads with its restored embedded Kit.
8. For `006 Brezel` specifically, check whether its mix/morph settings sound
   right — its `sceneset.scg` was reconstructed with defaults for every field
   except `audio_out`. If wrong, re-save the Scene to correct it.

### Trace check for the retest

Decode `asavetrc.bin` with `tools/devlog_unpack.py` after the run and confirm:

- `R f=0x01 v=0x00000020 | DONE=1 mask=0x0020 {5}` still appears for each root
  Scene Load — same success and same destination as before the fix. The fix
  changes *who reads* Scene 5, not where the load writes, so this record should
  be **unchanged**. A change here means something unintended moved.
- Still zero `E` (operation error) and zero `X` (phase stall) records.

If the pattern now plays but that `R` record changed, stop and re-read — the
fix was supposed to leave the load path completely alone.

## Boot-hang regression (2026-08-21) — my bug, in the IWDG code

The first flash of the pattern-fix build hung indefinitely on the boot splash:
no timeout, no `bootlog.bin`, no trace file. **This was not the Pattern fix.**
It was a defect in the `DEV_LOGGING_IWDG` watchdog feature added earlier in the
same session, and it is worth recording in full because it is a textbook
ordering trap.

### Root cause

`filesystem_devIwdgStart()` was written as:

```c
DEV_IWDG_KR = 0x5555UL;              /* unlock PR/RLR                   */
DEV_IWDG_PR = DEV_IWDG_PRESCALER_DIV256;
DEV_IWDG_RLR = DEV_IWDG_RELOAD_MAX;
while (DEV_IWDG_SR & 0x3UL) {}       /* <-- spins forever               */
DEV_IWDG_KR = 0xAAAAUL;
DEV_IWDG_KR = 0xCCCCUL;              /* the key that starts the LSI     */
```

`IWDG_SR`'s PVU/RVU bits only clear once a PR/RLR write has propagated into the
**LSI clock domain**. The LSI is off after reset, and this firmware enables it
nowhere — a `grep` for `LSION` across `Core/` and `main.c` returns only this
feature's own code. The `0xCCCC` key is what implicitly starts the LSI, and it
sat on the *far side* of the spin. So the LSI never ran, PVU/RVU never cleared,
and the loop never exited.

Every observed symptom follows from that one line:

| Symptom | Why |
| --- | --- |
| Indefinite hang, no progress | Unbounded spin with a permanently-set condition |
| No watchdog reset to rescue it | `0xCCCC` never reached, so the IWDG never started |
| No `bootlog.bin`, no trace | Runs right after `filesystem_bootLoggingBegin()`, before the card is mounted |

The bitter irony: the feature added to catch an indefinite boot hang caused one.

### Fix

`filesystem_devIwdgStart()` was rewritten with three properties, each documented
in an adjacent comment block:

1. **Correct order.** Enable the LSI via `RCC_CSR.LSION` and confirm `LSIRDY`
   *first*, so every later register handshake has a running clock domain to
   complete against. Then unlock, program PR/RLR, then start.
2. **Bounded waits.** Both handshakes time out against the TIM6 1 kHz
   `time_sysTick` (10 ms each, vs. a specified LSI startup in the hundreds of
   microseconds). By construction the function can no longer spin forever
   regardless of LSI, register, or silicon behaviour.
3. **Fail-safe on a dead LSI.** If `LSIRDY` never appears it returns having
   started *nothing*. This is deliberate: starting the IWDG with its
   reset-default PR/RLR arms a ~512 ms period (`PR=0` → /4, `RLR=0xFFF` at
   ~32 kHz), which the pre-audio SD ladder would blow straight through,
   turning a diagnostic aid into a permanent reset boot-loop. No watchdog is
   strictly better than a watchdog that reboots the instrument twice a second.

A new `fs_devwdg_armed` byte gates the feed so a boot that deliberately skipped
the watchdog never pretends to have one. Reset-cause sampling was moved *before*
`filesystem_devIwdgStart()`, since `LSION` and the reset flags share `RCC_CSR`.

### Second hazard found while fixing it — sample install

Auditing feed coverage surfaced a distinct and more damaging problem. The feed
lived only in `filesystem_tick()`, but the modal sample install
(`filesystem_installSampleFolderBlocking()` → `filesystem_blockOpen` /
`blockChdir` / `installOneSample`) runs entirely through
`filesystem_blockPoll()`, whose own comment states that the blocking helpers
*"bypass `filesystem_tick()`"*.

Sample install erases six 256 KB flash sectors and streams megabytes over
bit-bang SPI, which can easily exceed the ~32.8 s period. With the watchdog
enabled, that operation would have been **reset part-way through a
`sampleFlash` erase/program** — risking corruption of the sample-FLASH region
rather than merely rebooting. A feed was added to `filesystem_blockPoll()` with
a comment explaining exactly why it is mandatory.

### `DEV_LOGGING_IWDG` now defaults to 0

Given that this feature caused one boot hang and concealed a flash-corruption
hazard, it ships **off**. The code is fixed and both `#if` paths are verified to
compile, but a normal build now carries no watchdog risk. Re-enable it
deliberately for a hang-hunting session, and re-audit feed coverage first for
any new long-running blocking operation. The rationale is recorded at the flag
itself in `config.h`.

### Build-system footgun found (affects everyone, not just this change)

**The Makefile has no header dependency tracking** — no `-MMD`, no `-MP`, no
`-include *.d`. Editing `config.h` alone therefore does **not** trigger a
rebuild of anything. A `DEV_LOGGING_IWDG` flip followed by a bare `make`
silently produced a binary still containing the old flag value, with identical
reported sizes.

This matters well beyond this session: any flag-driven experiment in `config.h`
(`DEV_MODE_DIAGNOSTIC`, `DEV_MODE_LOGGING`, `AUTOSAVE_TRACE_RECORD_COUNT`, the
timing constants) can appear to have been applied when it has not. It may also
explain earlier uncertainty about whether a given diagnostic build was really
the one flashed.

**Always `make clean` after editing a header**, or add `-MMD -MP` plus an
`-include $(OBJS:.o=.d)` line to the Makefile. All final numbers below come
from clean builds.

### Verified build results (clean builds, both flag states)

| Build | text | data | bss | `.devwdg_noinit` |
| --- | ---: | ---: | ---: | ---: |
| `DEV_LOGGING_IWDG=1` | 380,148 | 396 | 94,760 | 12 B |
| `DEV_LOGGING_IWDG=0` **(shipping)** | 381,196 | 400 | 94,736 | 0 B |

Shipping image: `build/LXRV2_lxr02.img` (381,596 B). Confirmed by `nm` that the
only residual IWDG symbols with the flag off are the two zero-size linker
section markers and the empty `filesystem_devIwdgBootCheck()` stub that `main.c`
calls. The Pattern fix call site is present at `filesystem.c:10837`.

RAM for the feature when enabled totals ~18 B (12 B SRAM2 capsule + 6 B of
SRAM1 scalars including the new `fs_devwdg_armed`), within the 32 B approved.

## Open items after this pass

1. **Hardware verification of this fix** — the whole checklist above.
2. **Partial-save hardening** — the four ranked options in the save-path audit.
   Recommend starting with making Load tolerant of a missing `.fx`.
3. **Scene Load error-code granularity** — every failure reports `ScnL48`
   because the code is built from the terminal phase, and the `E` trace record
   shares the blind spot. Worth capturing the failing phase at the point of
   failure.
4. **`Bank/old012-c76c/`** — orphaned debris, harmless, awaiting a delete
   decision.
5. **Single-source-of-truth refactor** — retiring `seq_activePattern` /
   `menu_shownPattern` in favour of `scene_getActiveIndex()` would remove this
   entire class of desync. Rejected for this pass because it changes PERF
   queued-switch semantics; belongs in `SCOPING_TARGETS.md`.
6. **Phase-33 re-entrancy** — `filesystem_commitSceneStage()` re-runs and
   re-zeroes `target->pattern` on each `afatfs_chdir(NULL)` retry tick.
   Harmless today only because the pattern read happens later.
7. **Makefile header dependencies** — add `-MMD -MP` to `CFLAGS` and
   `-include $(OBJS:.o=.d)`. Small change, removes a whole class of
   silently-wrong builds. Also recorded in `MEMORY.md`'s process reminders.
8. **`DEV_LOGGING_IWDG` hardware validation** — the feature has never
   successfully run on the device. Before trusting it, enable it deliberately,
   `make clean`, and confirm both that boot completes and that a deliberately
   induced stall actually produces a `bootlog.bin` token on the next boot.

## Session context and reasoning notes

Kept so the next session does not re-derive or re-litigate these.

### What was ruled out, and why (do not re-investigate)

- **The `pattern.pat` format, writer, and parser.** Hand-verified bit-exact in
  both directions; `SD_CARD/Scene/000 Barf/pattern.pat` holds real varied data.
  Not a serialization problem.
- **The Scene Load state machine.** Phases 44→46→53→54/55→61 are complete and
  correctly ordered, dispatch is correct, and the destination mask is set
  correctly from the request. Trace shows eight consecutive successful loads.
- **`filesystem_commitSceneStage()`'s zeroing.** Runs at phase 33, before the
  pattern read. Not a post-load clobber.
- **AutoSave.** `autosave_markSceneWithoutPatternDirty()` excludes Pattern by
  design; it is an AutoSave-scope carve-out and does not touch plain Scene
  Save/Load.

### Why the first confirmation test was inconclusive

It was run against one of the six structurally damaged Scene folders, so it
returned `ScnL48` (a data problem) instead of exercising the desync. Those
folders are now repaired. Use a known-intact Scene — `000 Barf`, `001 Slak`, or
`015 Machine`.

### Note on the reconstructed `006 Brezel`

Its `sceneset.scg` did not exist and was rebuilt. Only `audio_out` was
genuinely *recovered* (from the legacy lines in `Kit/006 Brezel/kitset.kcg`,
which is exactly the fallback the parser implements). Everything else is a
firmware default. If that Scene's mix or morph settings were non-default, they
are gone — re-save the Scene. This is the one repair that involved inference
rather than restoration, and it is flagged deliberately.

### Two defects surfaced that are NOT the Pattern bug

Keep them separate when triaging test results:

1. **Partial saves produce permanently unloadable Scenes.** `effects.fx` is
   written last of ~12 operations and the old tree is deleted first, so any
   interruption leaves a stub that Load rejects wholesale. Four ranked fixes
   recorded; none implemented.
2. **`DEV_LOGGING_IWDG` caused the boot hang**, and while fixing it a second
   hazard appeared — the watchdog would have reset the modal sample install
   mid-`sampleFlash` erase/program, risking sample-FLASH corruption. Both are
   fixed in code; the flag ships off.

### Confidence levels — be precise about these

| Claim | Basis |
| --- | --- |
| Pattern loads into Scene 5, playback reads Scene 0 | **Trace-confirmed** (8× `R DONE=1 mask=0x0020`) + source reading |
| Bank 012 selects `active_scene=5` | **Card-verified** (`bankset.bcg`) |
| Card damage pattern matches save abort points | **Card-verified** + save-sequence reading |
| IWDG hang root cause | **Source-confirmed**; symptom match is exact, but not re-run on hardware |
| The fix makes the pattern play | **Unverified** — this is the whole point of the retest |
