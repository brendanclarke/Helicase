# Session 075 Handoff Log

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Branch**: `dev-ph6-copyclear`
**Session dates**: 2026-10-01 / 2026-10-03
**Base commit**: `50610dd` (S074 code close); the S074 closeout docs were
committed as `a0c4d65` and `b33c94e` ("doc cleanup"); `00bd078` holds the
S075 pre-implementation documents.
**State at close**:
- HEAD `76aef20` ("s075 automation bug post-implementation"). Every S075 code
  change is committed. A rebuild of HEAD at close reproduces the F3 numbers
  exactly (§1).
- This closeout (this log, the index entry, the new `COPYCLEAR_UTILITIES.md`,
  the specification updates, `MEMORY.md`, the volatile notes,
  `SCOPING_TARGETS.md`, and three code-comment pointers that named root
  documents about to be deleted, §12) is uncommitted. The user manages
  commits.
- The ten root `S075_*.md` documents are superseded by this log and the
  specifications; the user will delete them (§17).

**Authority**:

| Document | Role |
|---|---|
| This log | Implementation record, every decision round, deviations, measurements, hardware feedback, open items |
| `specification_reference/COPYCLEAR_UTILITIES.md` (new) | The copy/clear feature reference: behaviour, menus, gestures, architecture, Pattern engines, fan-out, names, suspension, trace stage `c`, RAM, how to modify |
| `specification_reference/PATTERN_DYNAMIC_STACK.md` | Swap block, raw block API, exclusive boundary, sliding compaction (§3, §12.17); automation playback and the S075 F3 priority rules (§6) |
| `specification_reference/BANK_PRESET_ARCHITECTURE.md` | FX-send Normal/Morph endpoints, Scene defaults, VOICE Morph views, F3 priority rules, copy/clear edit-mask rules |
| `specification_reference/MODULE_INTERCHANGE_SPEC.md` | CopyClear module map; F2/F3 APIs (FX-send Morph, overlay, automation guard, MIDI entry) |
| `specification_reference/AUTOSAVE.md` | Scene cell 7 reserved, cells 45..50 FX-send Morph, copy/clear suspension gates |
| `specification_reference/FILESYSTEM_SPEC.md` | `sceneset.scg` (`voice_decimation_all` retired, `fx_send_morph`), name-buffer loan, `HNcU` |
| `specification_reference/DEV_MODES.md` | Trace stage `c` |
| `specification_reference/STORAGE_SRAM_MANIFEST.md` | S075 RAM ledger, swap block, name-buffer loan |
| `dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` | Effect copy/clear and fan-out, A15 closed, pan rule, CrumpBit defaults, full views, SHIFT+TRACK overlay |
| `dsp_instruments_effects/EFFECTS_MIXER_DSP_REFERENCE.md` | Pan laws centred on 63; global decimation multiplier removed |
| `dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | Runtime write path and automation priority (F3) |

---

## End of session

```
DATE: 2026-10-03
SESSION GOAL: Start Phase 6 with copy and clear operations for step, bar,
              track, automation, Instrument, Scene and Scene components
              (S075_PH6_COPY_CLEAR.md). Added during the session by the user:
              retire global `srt` (Effect Morph on PERF), a DEV trace for
              copy/clear, two rounds of hardware follow-ups (F1, F2), and an
              automation bug found while testing F2 (F3).
COMPLETED:
  - Startup survey and decisions C1-C14, full specification (revisions 1-3,
    decision rounds 2 and 3), line-level schedule (Stages 1-10).
  - Base implementation (finished from a partial agent pass): new module
    Core/Menu/CopyClear/ (session, copyOps, clearOps, copyClearService),
    copyClearTools removed, Pattern swap block + raw block API + exclusive
    boundary + sliding compaction, Scene-level executors, HCNAMES `HNcU`
    write, AutoSave/maintenance suspension, 9 kB name-buffer loan.
  - Global `srt` retired; PERF `fxm` cell = Effect Morph.
  - Trace debug plan (stage `c`, D1-D5) and F1 follow-up (Q1-Q7, A1-A5,
    B1-B2) implemented together.
  - F2 follow-up (F2-Q1..Q9, R2-1..R2-3) implemented; SHIFT+TRACK overlay
    follow-up (last TRACK pressed, no mutes while held) implemented directly.
  - F3 automation-priority fix (Q1, Q2, P1) implemented.
  - Closeout: this log, index, COPYCLEAR_UTILITIES.md, spec updates, MEMORY,
    volatile notes, SCOPING.
VERIFIED ON HARDWARE: Partly, by the user.
  - Base pass: tested; feedback became F1 (12 items).
  - F1: tested; the next feedback (F2 notes) asked only for new changes.
  - F2: tested; feedback was the overlay follow-up and the automation bug
    (pre-existing, became F3).
  - F3: "seems ok".
  - Not reported: the overlay follow-up re-test; the per-case matrices of
    every schedule (COPYCLEAR_UTILITIES.md §16 keeps the combined list);
    the production (DEV_MODE_LOGGING 0) build after F1; a validator PASS on
    an F2 card.
CHANGES THIS SESSION (details §5-§11, file list §14):
- New: Core/Menu/CopyClear/ (4 file pairs). Removed: Core/Menu/copyClearTools.c/h
- Pattern: PatternData.c/h (swap block bounds, raw block API, copy no-ops
  removed), PatternStackService.c/h (exclusive boundary, compaction,
  evacuation, repair gate, trace summary only)
- Scene/Bank: SceneData.c/h (settings defaults/commit, Kit commit/reset,
  fx_send_morph, St1 default, compressor defaults), BankData.c/h (fan-out
  mask, mask exchange/reset), Autosave.c/h (cell 7 reserved, cells 45..50),
  AutosaveTrace.h (stage `c`), SceneModTargets.c/h (`srt` placeholder, fxm
  and 7dc id helpers)
- Preset: presetManager.c/h (instrument copy, workers idle, FX-send Morph,
  display getter, one-parameter edit apply, MIDI entry, `srt` removal),
  presetMorphEngine.c/h (resolved amount, guarded base write, per-parameter
  apply), ParameterArray.h (PAR_EFFECT_MORPH in the old `srt` slot)
- Effects/DSP: EffectsManager.c/h (fan-out from any origin, paste/reset/seq
  ops), EffectTypes.h (pan default 63), CrumpBit (pan law 63, defaults,
  `snc`), mixer.c/h (`srt` multiplier gone, pan law 63), InstrumentManager.c
  (`srt` LFO target removed)
- UI: buttonHandler.c/h (router, BUT_COPY, SHIFT Morph, SHIFT+TRACK overlay),
  ledHandler.c (overlay gates), menu.c/h (overlay, copy/clear hooks, FX-send
  endpoints, clamp wrapper, `fxm`), menuEffects.c (full views, copyField),
  MenuText.h, menuPages.h
- Sequencer/MIDI: sequencer.c/h (automation hold query, `srt` removal),
  MidiParser.c (external CC entry, dispatcher), MidiMessages.h
- Storage: filesystem.c/h (suspension gates, name-buffer loan, HNcU, defaults,
  fx_send_morph line), storageTypes.c (`srt` key ignored, fx_send_morph key)
- Build/boot: Makefile, config.h (swap block constants), main.c, timebase.c
- Tools: decode_devlogs.py (stage `c`), verify_bank_autosave.py (rebuilt for
  format 3 / 161 rows / 51 cells), populate_scene_directory.py,
  convert_legacy_kits.py (comment)

KNOWN ISSUES INTRODUCED:
- None functional known. Effects saved before F2 at pan 64 now show `1` and
  sit one step right (user accepted; stored content is not migrated).
- MIDI CC values are now retained (saved, AutoSave-marked) and the VOICE page
  shows them only at its next repaint (decision F3 0.2.7).
KNOWN ISSUES RESOLVED:
- Track and bar copy gestures did nothing; autom.1/autom.2 cleared the whole
  track; copyClear_copyPattern() had no caller; copyClear_clearTrackAutom()
  was an empty stub (startup survey items 1-4).
- FX locks could not be removed (Phase 5 debt A15).
- `DTYPE_PM63` pan quirk: Effect pans showed `1` at centre (S074 open item).
- verify_bank_autosave.py expected 129 HCNAMES rows (S072 carried debt).
- A menu edit, a Morph change, an LFO on Morph, `Nvm` automation or a MIDI CC
  reset automated instrument parameters mid-note (pre-S075 defect, F3).
- Effect `typ` full view showed flash garbage ("Off 1 Mo"): menuEffects
  copyField read past a short string's terminator.

NEXT SESSION RECOMMENDED GOAL: hardware-verify the overlay follow-up and the
  remaining copy/clear/F2/F3 cases (COPYCLEAR_UTILITIES.md §16), measure the
  production build, then choose the next Phase 6 item with the user
  (SCOPING_TARGETS.md Phase 6; 6.0 lists the deferred Phase 4 items first).
BLOCKERS: none. Before the first boot of this firmware on an older card,
  delete `.hcprms1`/`.hcprms2` and other temporary records (F2 policy: no
  AutoSave migration for cells 45..50).

CRITICAL REMINDERS FOR NEXT SESSION:
- Automation always wins until the voice's next trigger. Every Morph-base
  runtime write goes through presetMorph_writeRuntimeBase(); never add a
  runtime write of a morphable instrument parameter that bypasses
  seq_automationHoldsParameter().
- A menu edit only sets an endpoint; the runtime takes the interpolation at
  the resolved Morph amount (presetMorph_applyParameterNow()).
- External MIDI CC is lowest priority: stored endpoint, Morph sweep applies.
- Only the exclusive holder (patSvc_beginExclusive()) may use the PatternData
  raw block API; the top 132 B of every pool is the swap block.
- Pattern data never fans out; Scene children fan out through the
  destination Scene's own edit-mask entry.
- While a copy/clear operation runs, AutoSave, trace, settings and Pattern
  repair do not start; the 9 kB name cache may be lent.
- No backward compatibility for S075 F2 storage (user): delete old AutoSave
  records before testing on old cards.
- Constant-CPU rule, RAM approval policy, `.img` SHA-256 (not `.bin`),
  `make all` (not bare `make`), commits are the user's, no `.claude` memory.
```

---

## 1. Build metrics

| Point | text | data | bss | Flash payload | Headroom | ITCM | DTCM statics | FXBUF | `.img` bytes / SHA-256 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| S074 close (`50610dd`) | 502,512 | 416 | 426,392 | 502,928 | 250,736 | 4,168 | 4,480 | 126,592 | 502,944 / `63eec2a6…aeb0` |
| Base pass, Stages 1–10 (2026-10-01, committed in `7158c28`) | — | 412 (−4) | +96 vs `00bd078` | 527,804 | 225,860 | 4,168 | 4,472 (−8) | 126,592 | 527,820 |
| F1 + trace, DEV (`dbc8104`) | 530,592 | 416 | 426,616 | 531,008 | 222,656 | 4,168 | 4,472 | 126,592 | — |
| F1 + trace, production (`DEV_MODE_LOGGING 0`) | 516,688 | 408 | 409,840 | 517,096 | 236,568 | — | — | — | — |
| F2, DEV (`ee3c665`) | 531,936 | 416 | 426,712 | 532,352 | 221,312 | 4,168 | 4,472 | 126,592 | 532,368 |
| + overlay follow-up (TRACK re-pairing) | 532,096 | 416 | 426,712 | 532,512 | 221,152 | 4,168 | 4,472 | 126,592 | — |
| **Close: F3, DEV (`76aef20`)** | **532,408** | **416** | **426,712** | **532,824** | **220,840** | **4,168** | **4,472** | **126,592** | **532,840 / `d1c0aac2…9ceb`** |

Notes:

- **Payload = `text` + `data`**; the `.img` adds a 16-byte header. Record the
  `.img` hash: `lxr02.bin` is unstamped. Full close hash:
  `d1c0aac283e0631ccb074bafa1aa43a6884da0bba9493c4ebe685b78c5709ceb`.
- **Configuration at close:** `DEV_MODE_DIAGNOSTIC 0`, `DEV_MODE_LOGGING 1`,
  `DEV_STALL_DETECTION 1` (unchanged from S074).
- **Close section sizes** (`arm-none-eabi-size -A`): `.text` 527,240,
  `.itcm` 4,168, `.dma_nocache` 3,100, `.data` 416, `.bss` 293,060, `.dtcm`
  512, `.dtcmz` 3,960, `.dtcm_fxbuf` 126,592. `bss` from `size` includes the
  NOLOAD arena.
- **`scenes`** = `0x65E0` (26,080 B; 1,630 B per Scene; `scene_settings_t`
  50 B). Measured with `arm-none-eabi-nm -S` as the F2 build gate.
- **RAM changes this session (all approved by the user):**

  | Pass | SRAM1 | DTCM | Owner detail |
  |---|---:|---:|---|
  | Base (Stages 1–10) | +92 B net (`.bss` +96, `.data` −4) | −8 B | copy/clear session 25 B, service 59 B (queue 26, register 18, run 6, flags/claim/retry 4, buffer pointer 4, alignment), `service_exclusive_scene` 1 B, `fs_name_cache_borrowed` 1 B; offset by `voice_decimation_all` (16 B in `scenes`) and `mixer_decimation_rate[6]` (DTCM). Original approval: +100 B. |
  | F1 | +124 B net | 0 | early source masks +64 B (O1), restore masks +64 B and flags +1 B (A3), trickle credit +2 B, LED group-blink state −7 B |
  | Trace (DEV only) | +24 B | 0 | `ccSvc_traceState` 22 B (D1 20 B + A4 4 B), `fs_cc_suspended_prev` + `fs_cc_refusal_reported` 2 B; 0 B in production |
  | F2 | +101 B | 0 | `scenes` +96 B (`fx_send_morph[6]` × 16), `menu_fxVoiceMixOverlay` 4 B, `buttonHandler_fxVoiceMixTrackMask` 1 B |
  | Overlay follow-up, F3 | 0 | 0 | — (F3 adds ~24 B of stack on the MIDI clamp path only) |

- **Pool data, not RAM:** the swap block reserves the top 33 chunks (132 B)
  of each of the 16 Scene pools.
- **Working storage:** the 9,000 B `fs_list_cache_name` is lent to copy/clear
  during an operation (worst case 8,572 B used).
- **Warnings:** none from S075 files. Pre-existing: newlib
  `_close/_lseek/_read/_write`, LTO serial note, unused filesystem helpers,
  `EuklidGenerator.c`, `PatternData.c` packed-member.

---

## 2. What Session 075 delivered (summary)

1. **Copy and clear** of Pattern data (step, step range, bar, bar range,
   track) and of Scenes and their children (Instrument, Kit, Effect, FX
   sequence, Scene settings, Pattern, whole Scene), plus endless-pot clears
   of automation. Gestures: COPY held + object; SHIFT + COPY for clears. Work
   runs in a bounded background service after the button is released. All
   behaviour is in `COPYCLEAR_UTILITIES.md`.
2. **Pattern-pool infrastructure:** a permanent 132 B swap block per pool,
   an exclusive per-Scene write boundary for any resident Scene, a raw block
   API that keeps the publication order, sliding compaction and swap
   evacuation.
3. **Global `srt` retired**; PERF's last cell is now `fxm` (Effect Morph).
4. **A DEV trace stage `c`** that witnesses every operation, job, drop,
   anomaly, loan and suspension edge.
5. **F1:** the copy/clear UX corrected after the first hardware test
   (LEDs, row gestures, ranges in every bar, kept clear selection, labels,
   probability rules, immediate trigger feedback, start latency).
6. **F2:** product defaults (St1 routes, compressor off/0/0/off), the
   `Copy`/`Clear` header, Effect full views, one pan rule, CrumpBit
   defaults, morphable FX send, VOICE hold-SHIFT Morph view, and the
   Effect-page SHIFT+TRACK voice-mix overlay.
7. **F3:** an older defect fixed: edits no longer reset automated parameters
   mid-note. Automation, then menu edits, then MIDI, in that priority.

---

## 3. Working method used this session

- **Plans first, then line-level schedules.** Every piece had a plan with
  numbered questions that the user answered (C1–C14, round 2, F1–F5 of
  round 3, D1–D5, Q1–Q7, A1–A5, B1–B2, F2-Q1..Q9, R2-1..R2-3, F3 Q1/Q2/P1),
  then a schedule citing every change by file, line and action, with the
  comment blocks to place beside the code in `.c` and `.h`.
- **Who implemented what.**
  - An implementing agent made a partial first pass of Stages 1–9; the
    assistant reviewed it (§6.1), finished and corrected it, and wrote the
    Stage 10 documentation.
  - The F1 + trace, F2 and F3 schedules were applied by the user or an
    implementing agent they directed, each recording a work log in its
    schedule; the assistant reviewed and answered follow-ups.
  - The SHIFT+TRACK re-pairing follow-up was implemented directly by the
    assistant on request ("go implement that and I'll re-test").
- **User rules in force this session** (in addition to `MEMORY.md`):
  - comments at detailed contract level beside every change in `.c` and
    `.h`;
  - do not introduce new semantic terms the user has not used;
  - RAM approval per allocation (byte count, region, lifetime, owner);
  - Pattern writes keep the publication order; SceneData is the only writer
    of retained Scene data;
  - "no code change this turn" means schedule only;
  - no backward compatibility for F2 storage: the user deletes AutoSave and
    temporary records instead (R2-2);
  - commits and hardware checks are the user's.
- **Corrections the user made along the way** (kept as rules):
  - never flash the copy source; the screen says what is copied (F1-A);
  - all pans operate and display the same; mono and stereo maths may differ
    (F2-Q4 addendum);
  - a hacky SELECT block was rejected: Effect-type SELECT functions must
    keep working under the overlay, only screen changes are suppressed
    (R2-1);
  - automation always wins; a menu edit never overrides the Morph
    interpolation (F3 Q1).

---

## 4. Session start, the startup survey and decisions C1–C14

### 4.1 State at start (`S075_PH6_COPY_CLEAR.md`, written at the S074 close)

- Branch `dev-ph5-effects`/`50610dd`; the work moved to `dev-ph6-copyclear`.
  Link `text=502,512 data=416 bss=426,392`; flash 502,928 / 753,664 B.
- Free SRAM1 is reserved for Pattern data and free DTCM for audio buffers
  (`STORAGE_SRAM_MANIFEST.md` §10); the copy/clear design therefore had to
  avoid data clipboards.

### 4.2 Goal (user, 2026-09-30)

"Phase 6 begins with copy and clear operations for step, bar, track,
automation, Instrument, Scene and other Scene components."

### 4.3 What existed before S075 (the old LXR-derived surface)

| Gesture | Code | Behaviour then |
|---|---|---|
| COPY press | `buttonHandler.c` `case BUT_COPY` | `copyClear_Mode = MODE_COPY_TRACK`; COPY LED blinked; SELECT and VOICE LEDs cleared |
| COPY + VOICE *a*, VOICE *b* | `handleVoiceButton()` → `copyClear_copyTrack()` → `pat_copyTrack()` | **a no-op** (only the LEDs changed) |
| COPY + SELECT *a*, SELECT *b* | `copyClear_copyBar()` → `pat_copyBar()` | **a no-op**, in every mode |
| COPY + SEQ | not intercepted | fell through to step toggle, STEP select, PERF Scene switch or FX lock hold |
| SHIFT + COPY | `copyClear_armClearMenu()` (direct `lcd_clear()`/`lcd_string()` writes, "TODO this wastes RAM") | `clear [track]?` menu: `track`, `pattern`, `autom.1`, `autom.2`; click or a second SHIFT+COPY executed |
| SHIFT + COPY while recording and running | `seq_setErasingMode()` | erase mode (kept unchanged by S075) |

Problems found in the survey (all fixed by S075):

1. **`autom.1`/`autom.2` cleared the whole track:** `copyClear_executeClear()`
   had `default:` and `case CLEAR_TRACK:` on the same branch. The two targets
   were the original LXR automation lanes, which the v4 Pattern model does
   not have; `copyClear_clearTrackAutom()` was an empty stub.
2. Track and bar copy gestures silently did nothing (`pat_copyTrack/Bar`
   were Session 062 no-ops).
3. `copyClear_copyPattern()` had no caller.
4. The copy source and destination were raw button indices (`int8_t`, −1 =
   none) masked with `& 0xF` / `& 0x07` in `copyClearTools.c`.
5. The clear menu wrote the LCD directly, outside the Menu repaint model.

Storage facts that drove the design (`PATTERN_DYNAMIC_STACK.md`):

- One Pattern per Scene; `pat_regions[16]` × 10,519 B.
- Address entry: bit 15 trigger, bit 14 block present, bits 13..0 pool offset
  relative to that Scene's pool (`0x3FFF` empty) ⇒ **a whole-Pattern copy
  between Scenes is valid as a byte copy of the region** (offsets are
  region-relative, back-references are Scene-independent). Pattern Load's
  fan-out already does exactly that.
- A step, bar or track copy is **not** a byte copy: each destination step
  needs its own block with its own back-reference, published in order
  (allocate, write, PRIMASK swap, free old). The pool can run out part-way.
- The Pattern Stack Service serialises pool mutations: one target Scene,
  bounded work per tick, trailing-slack reservations, `patSvc_idle()` at
  replacement boundaries.

Scene-level facts recorded for the design (sizes at the S074 close):
`scene_settings_t` 45 B (per-cell AutoSave), `effect_record_t` 420 B
(`scene_commitEffectRecord()`, whole Effect region marker), `kit_t` 1,160 B
(Kit Load commit, `autosave_markKitDirty()`), Instrument slot = type +
Normal + Morph images + the runtime `morph_interpolation[]` (whole-Instrument
marker); HCNAMES rows 1+s Scene, 17+s Kit, 33+6s+slot Instrument, 129+s
Pattern, 145+s Effect. LFO target-voice selectors are one-based slots; Kit
Save writes `self` for the source slot. Type changes require
`bank_revalidateVoiceEditMasks()`.

### 4.4 Clipboard models considered

| Model | RAM | Verdict |
|---|---:|---|
| A. Direct gesture (source and destination in one COPY hold) | ~2 B | the base the user chose, extended with ranges and many destinations |
| B. Coordinate clipboard (remember where the source is) | ~8 B | recommended by the brief; the final design reads the source at each paste, which is the same idea inside one hold |
| C. Data clipboard (snapshot) | 132 B … 10.5 KB | rejected: free SRAM1 is reserved for Pattern data; the Pattern AutoSave snapshot must not be borrowed. The final design borrows the 9 kB name cache only when a paste overlaps its own source. |

### 4.5 Startup decisions (user answers, folded into the full spec §12.1)

| # | Question | Decision |
|---|---|---|
| C1 | Destinations and fan-out | One destination per press (many presses per hold); Pattern data never fans out; Scene children fan out through the edit mask |
| C2 | Clipboard model | The source is read when each paste starts; copied into the 9 kB name buffer where needed |
| C3 | Cross-Pattern step/bar/track pastes | Yes, by navigating while COPY is held; written to the active Scene |
| C4 | HCNAMES on copy and clear | Copy name and source token, clear `R`; names kept on clear; one `.hcnamtmp` swap at the end |
| C5 | Undo | None |
| C6 | Replace or merge | Both offered |
| C7 | Length/scale/shuffle | Pastes extend length; `copy track` carries length/scale/shuffle; `clear track` resets them |
| C8 | Automation across tracks/Scenes | Targets follow the destination, best effort, same dtype, else dropped |
| C9 | Old clear menu | Deleted |
| C10 | Instrument clear; slot-6 decay pair | No Instrument clear; the decay pair travels only slot 6 → slot 6 |
| C11 | Scene-settings granularity; Effect Morph | Scene settings = all of `sceneset.scg`; `copy effect` copies the record only |
| C12 | `clear scene` | The active Scene keeps its Kit; any other Scene is emptied (present bit off) |
| C13 | UI model | Gestures with a selection menu |
| C14 | Confirmation | Clears start at `cancel` and apply on release; copies need none |

---

## 5. The full specification (revisions 1–3, 2026-10-01)

`S075_PH6_COPY_CLEAR_FULL_SPEC.md` merged the user's own copy/clear
specification with the startup brief and was checked against the code
(`b33c94e`). Its content is now `COPYCLEAR_UTILITIES.md` (the reference) plus
the history below.

### 5.1 Terms the user set (kept in code comments and docs)

Copy/clear button, copy operation, clear operation, menu interaction,
source, selection, paste, **active Scene** (the Scene being viewed and
written to, not playback), edit mask, fan out, queue (4), register (8),
9 kB name buffer, swap block, source indicator. Tracks 1..7 in docs (0..6 in
code); **track 7 behaves as track 6** wherever an Instrument or per-slot
setting is involved.

### 5.2 Second round (user, 2026-10-01)

| Item | Decision |
|---|---|
| `srt` | Retired; Effect Morph takes its PERF cell (§7) |
| Swap block | Permanent 132 B reserve in every pool, kept for any future feature that needs a guaranteed rewrite area |
| Paste method | Always step by step, queued |
| Non-present persistence | Accepted: settings or an Effect pasted into a non-present Scene are AutoSaved only once a Kit makes it present |
| PERF Scene paste | Writes the pressed Scene; no Scene switch |
| Pots while a clear menu is shown | Ignored |
| Indicator formats | Accepted |
| Retarget by VOICE page position | Allowed when the dtype matches |
| Fan-out | Scene children fan out; Pattern never; Scene and settings copies/clears set or reset the mask |
| FX sequence clear | Lock masks and lane values zeroed |
| `clear scene`, `clear scene settings` | Both reset the mask to the Scene itself |
| Source token on copy | Copied with `R` cleared (clearing `R` stops the boot reader from reloading the library object over pasted content; the copied token keeps the row's origin accurate) |

### 5.3 Third round (user, 2026-10-01)

| Item | Decision |
|---|---|
| F1 PERF Effect Morph cell | Label `fxm`; double-speed endless pot like Scene Morph |
| F2 `srt` | Eliminated: not loaded from Scenes that have it, not saved in new ones; per-voice sample-rate reduction is the only one left unless a future Effect type adds one; Scenes that used it sound different (accepted) |
| F3 Fan-out | Any paste of a Kit, Instrument, Effect or any sub-component of those also applies to every Scene in the destination's edit mask |
| F4 Pot clear over Effect Morph (PERF and EFFECTS) | Clears both the FX sequence lane and the Pattern automation; the FX part fans out, the Pattern part never does |
| F5 Fan-out through the pressed Scene's own mask | Intended: the edit mask locks Scenes together for everything except the Pattern |

### 5.4 RAM approval for the feature

+100 B approved for the base design (including the 18 B pot-clear register);
larger working storage through the 9 kB name buffer. Later approvals: F1
(+129 B masks, +2 B governor), trace (DEV +24 B), F2 (+101 B).

---

## 6. Base implementation (Stages 1–10, `S075_PH6_COPYCLEAR_IMPLEMENTATION.md`)

### 6.1 Stage plan

| Stage | Content | Gate (user hardware check) |
|---|---|---|
| 1 | LED group blink (source indication) | existing LEDs unchanged — **reverted in F1** |
| 2 | Retire `srt`; PERF `fxm` cell | `fxm` edits and fans out; old Scenes load; old `srt` automation inert |
| 3 | CopyClear skeleton, routing, menus, suspension, name-buffer borrow; old code removed | no leaked edges in any mode; AutoSave/trace/settings pause and resume |
| 4 | Pool reserve, exclusive boundary, raw block API, pastes | every paste selection on a playing Pattern; ranges, wrap, retargeting, nearly-full pool |
| 5 | Pattern clears | every clear selection; PERF clears on active and other Scenes |
| 6 | Pot-clear register, underline suppression | 8-entry limit; values never change |
| 7 | FX sequence copy/clear, Effect fan-out | fan-out; type mismatch dropped |
| 8 | Scene-level pastes and clears | Instrument, Kit, Effect, settings, Scene, Pattern copies; Scene clears; masks; present rule |
| 9 | Identity rows (HCNAMES) | rows after every kind |
| 10 | Documentation and tools | — |

Build gate after every stage: `make all && make img`, `link_budget.py`, the
`.img` SHA-256; a stage that grows RAM beyond the ledger stops for approval.

### 6.2 Review of the partial implementation (2026-10-01)

The schedule's baseline was `00bd078` plus an uncommitted partial pass by an
implementing agent. The review found:

| Stage | Found | Action |
|---|---|---|
| 1 LED | `led_setBlinkGroup()` cleared `LED_LAYER_BLINK` on a removed member even when a blink slot still owned that LED; one comment indent | fixed (then the whole layer was reverted in F1) |
| 2 `srt` | done as scheduled, **but** `MidiParser.c` had removed the per-voice `VOICE_DECIMATION1..6` CC assignment together with the global case (per-voice decimation CCs did nothing); `copyClearTools.c/h` still on disk | per-voice CCs restored; files deleted (`git rm`) |
| 3 Session | departed from the spec: OK/CANCEL menus instead of selection lists, sources on press (no range rule), MODE/BAR/SHIFT all consumed (no navigation), copy LED blinking at press, clears queued on copy/clear release, encoder click posting a clear, suspension from the press, register wiped on release; filesystem suspension gates, the borrow API and the repair-epoch gate missing | the four CopyClear pairs rewritten to the schedule; gates and borrow added |
| 4 Pattern | reserve constants, allocator bounds, in-place append bound, service bounds and exclusive begin/end done; **but** `pat_copyStep/Track/Pattern/Bar` had been made real (erase-then-write, unbounded) instead of removed; `pat_rawReadBlock()` returned 1 for an empty step; `pat_rawPublishEmpty()` published twice; `pat_rawRegionRewriteBlock()` read the silenced destination and could never work; compaction and swap evacuation were stubs; no paste engine | copies removed; raw API corrected; compaction/evacuation and the engine implemented |
| 5–6 | pot-target resolver, underline hooks, register shell present; the register drain used queued single-step removals and was wiped on release | re-implemented in the service |
| 7 FX | fan-out mask change and the four functions present; `effects_pasteRecord()` refused a paste across Effect types (so `copy effect` could never change type); no prototypes | fixed; prototypes added |
| 8–9 | not started | implemented |
| 10 | not started | updated |

### 6.3 Deviations from the schedule (decided while implementing)

1. **Growing pastes and compaction.** The schedule placed a growing step in
   the swap block and then compacted. A block in the swap block cannot also
   be compaction's scratch, and compaction that only moves blocks into lower
   disjoint runs does not guarantee a run of a given size. Implemented
   instead: `patSvc_exclusiveCompactStep()` is a **sliding** compaction (the
   block just above the lowest free chunk moves down into it, through the
   empty swap block when the runs overlap; repeated calls leave all free
   space as one run below the swap block). The paste check requires, for
   each growing step, that the free chunks before that step are at least the
   new size (old and new blocks coexist until publication); steps that do
   not grow always fit through the swap block. Effect: a paste into a nearly
   full pool can be dropped where a net-growth check would have accepted it,
   by at most one block (≤ 132 B of 8,060 B). A paste still completes whole
   or is dropped whole.
2. **New operation while work is queued.** A copy/clear press is refused
   silently while the previous operation's pastes/clears are still queued
   (they read that operation's source). A pending register drain or name
   write does not block a new operation.
3. **Name write and jobs.** Jobs do not start while the HCNAMES write is in
   flight (it uses the same scratch offsets as a paste snapshot).
4. **Name-buffer protection.** While lent, `filesystem_clearNameCacheStorage()`
   and `filesystem_prepareLibraryNameCache()` do nothing and
   `filesystem_start()` refuses every operation except the copy/clear name
   write (callers see a busy facade). Covers entering LOAD/SAVE right after
   releasing copy/clear while background work runs.
5. **`scene_commitKit()`** added to SceneData so Kit pastes do not assign
   `scene_t` fields outside SceneData.
6. **Remap value 0xFE** marked a content-changed row whose name is kept
   (**removed in F1**, §9.4).
7. **SHIFT** edges are not consumed (they keep their MODE-modifier and LED
   roles and their press/release pairing); only the LED is latched during a
   clear operation.
8. **Menu overlay** draws after the page renderer in `menu_repaint()`
   (instead of returning before it); both rows are fully replaced.

### 6.4 Build and resources (2026-10-01)

- No new warnings; image 527,804 B. Against `00bd078`: `.bss` +96 B,
  `.data` −4 B, `.dtcmz` −8 B (SRAM1 net +92 B, inside the approved +100 B).
- `grep` for `copyClearTools|copyClear_Mode|PAR_VOICE_DECIMATION_ALL|
  voice_decimation_all|DECIMATION_ALL\b` leaves only the accepted tombstones
  (`MidiMessages.h` enum position, `storageTypes.c` ignore branch).

### 6.5 Open items noted at the base pass (still to watch on hardware)

- Full-pool pastes (growing steps) and the time sliding compaction takes on a
  fragmented pool (one slide per tick).
- Whole-Pattern copy with retargeting into the playing Scene: destination
  entries are silent for up to 28 ticks (896 entries / 32 per tick).
- Entering LOAD/SAVE immediately after a long pot-clear register drain (the
  browser waits until the name buffer is returned).

---

## 7. Global `srt` retired; Effect Morph on PERF (Stage 2)

**What `srt` was:** `scene_settings_t.voice_decimation_all` (0..127, default
127), applied as `mixer_decimation_rate[6]`, a multiplier on every voice's
decimation counter (`cnt += rate[voice] * rate[6]`); the last PERF cell
(`PAR_VOICE_DECIMATION_ALL`); a Scene automation and LFO target
(`SCENE_MOD_TARGET_KIND_DECIMATION_ALL`, ID 390); the `sceneset.scg` key
`voice_decimation_all`; AutoSave Scene cell 7; MIDI CC `VOICE_DECIMATION_ALL`.

| Area | Change |
|---|---|
| Mixer | `mixer_decimation_rate[]` shrinks to 6 entries; the `* rate[6]` multiply is gone. A Scene at 127 is bit-identical (×1.0f is exact): sound class S0. Per-voice decimation is the only sample-rate reduction left. |
| Sound | Scenes saved with `srt` below 127 lose that extra decimation (accepted) |
| SceneData, Preset | field, setter and runtime apply removed (`preset_applyVoiceDecimationAllRuntime()` gone) |
| AutoSave | Scene cell 7 is reserved: written 127 (neutral for older firmware), ignored on restore; layout, mask and format version unchanged |
| `sceneset.scg` | not written; accepted and ignored on load; later writer lines moved up by one (the parser is key-based); older firmware defaults a missing key to 127 |
| Scene target | the `srt` row stays as a retired placeholder (`SCENE_MOD_TARGET_KIND_RETIRED`, no use flags) so the IDs of `7dc`, `Nou`, `Nfx` and `fxm` do not move; pickers skip it, validation rejects it; existing Pattern entries and LFO tokens naming it do nothing |
| MIDI | `VOICE_DECIMATION_ALL` handling removed (enum position kept); per-voice `VOICE_DECIMATION1..6` kept |
| Menu | `menu_parseGlobalParam()` branch and the O1 Settings-Load equalisation of `srt` removed |
| PERF cell | `fxm`: the active Scene's Effect Morph (`effect_morph_amount`, 0..255), committed like the Effect page's `mrp` through `effects_setMorphAmount()` (fans out through the edit mask), AutoSave cell 40, double-speed endless pot; mirror refreshed on PERF entry (`preset_syncEffectMorphMirror()`) |
| Tools | `verify_bank_autosave.py` cell 7 = 127; `populate_scene_directory.py` stops writing the key; `convert_legacy_kits.py` comment on the legacy position |

---

## 8. Copy/clear trace (`S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md`)

### 8.1 Why

Copy/clear drops pastes and clears silently by design and does most work
after the button is released, so the panel cannot show whether a paste was
dropped, why, or how long it took. Risk map:

| # | Risk | Evidence |
|---|---|---|
| R1 | silent drops (pool full, Advanced limit, Effect type mismatch, swap block, queue full, identical paste) | `JOB_END` DROP + reason; `QUEUE_FULL`; `PASTE_NOOP`; `CHECK_FAIL` |
| R2 | growing-step check rejecting pastes a net-growth check would accept (deviation 1) | `CHECK_FAIL` free/new/old chunks |
| R3 | slow compaction | `JOB_STATS` slides; `JOB_END` ticks |
| R4 | guarded "cannot happen" paths | `ANOMALY` code + coordinates |
| R5 | job stuck waiting | `JOB_STALL` after 10 s + run phase |
| R6 | suspension edges | `SUSPEND` begin/end |
| R7 | name-buffer loan blocking other filesystem work | `SCRATCH` borrow/return; first `FS_REFUSED` per loan |
| R8 | name write | `NAMES` request/result/give-up |
| R9 | pot-clear register | `REG_ADD`, `REG_REFUSED`, `REG_DONE` |
| R10 | fan-out reach | `FANOUT` mask; `MASK_SET` |
| R11 | retargeting drops | `JOB_STATS` retarget-dropped |
| R12 | refusals at the press | `OP_REFUSED` |

### 8.2 Design and decisions

- Channel: the AutoSave trace ring (`/asavetrc.bin`), already the general DEV
  lifecycle trace with a decoder; PatternTrace (32 records, no decoder) not
  used. One new stage `c` (all 26 upper-case letters were taken); the flags
  byte is the event, `value32` an event-specific layout.
- Bounded volume: no per-step or per-tick records; ~10 records per
  operation frame, 2–5 per job, 2–3 per pot clear.
- DEV-only: one `static inline ccTrace()` whose body is empty without
  `DEV_MODE_LOGGING`; counters in a DEV-only struct; 0 B in production.
- Correlation: a 4-bit operation sequence in `OP_START`/`OP_FINISH`.

| # | Question | Decision (user, 2026-10-01) |
|---|---|---|
| D1 | +20 B SRAM1 in DEV builds only | approved (later +4 B, A4) |
| D2 | ring size while testing | keep `AUTOSAVE_TRACE_RECORD_COUNT` at the temporary 2,048 |
| D3 | teardown with a claim held | record only (`TEARDOWN_CLAIM_HELD`), no release |
| D4 | per-move PatternTrace `R` records for copy/clear moves | stop them; counts go to `JOB_STATS`/`REG_DONE`; reactive and repair relocations keep `R`/`L` |
| D5 | lifetime of the `c` records | keep after S075 testing |

The full event/drop/anomaly layout tables are in `COPYCLEAR_UTILITIES.md`
§14 (moved there from the plan; `AutosaveTrace.h` and
`tools/decode_devlogs.py` mirror them). Implemented together with F1
(§9.6).

---

## 9. Hardware round 1 → F1 follow-up (`S075_PH6_COPYCLEAR_FOLLOWUP_F1.md`, 2026-10-02)

### 9.1 Feedback (user, first hardware test)

| # | Feedback | Type | Change |
|---|---|---|---|
| 1 | Source steps/bars/track/Scene must not flash; that was never asked for; the screen says what is copied | correction | F1-A |
| 2 | Copy: only the copy/clear LED flashes, continuously, while the copy menu is open. Clear: only copy/clear and SHIFT flash | correction | F1-B |
| 3 | A paste blinks its destination once when accepted, but every visible pasted-onto LED, not just the pressed step; same for clears, including ranges | correction | F1-F |
| 4 | The source indicator always starts at the 9th character of the top row | cosmetic | F1-D |
| 5 | SEQ gestures: menu on the first step press; a second press shows the range; later presses re-pair with the most recent held step; releases change nothing on screen; release of all sets the source; the next press pastes | correction | F1-C |
| 6 | Bug: a step range cannot be a source in bars 2..8 | bug | F1-C |
| 7 | Step copy labels: `step -> repl`, `step -> merge`, `auto -> repl`, `auto -> merge` | wording | F1-E |
| 8 | Clear step: in bar 2..8 the first step 017 then the last gives `s001-032`; after release, later presses pair with released steps (`s016-025`) | bug | F1-C |
| 9 | Clear: with no step held, press and release selects that step; if the menu shows `cancel` only the screen updates, otherwise the step is cleared; only a press while another is held makes a range | rule | F1-C, F1-G |
| 10 | The clear menu must not jump back to `cancel` on a new step; only on copy/clear release or a different type of clear | correction | F1-G |
| 11 | Clear order: (1) blink, (2) clear the trigger bits, (3) run the pool work (may take seconds), so it never looks hung | rule | F1-H |
| 12 | Pot clear sometimes ran at once, sometimes after 3–4 s; start at once with ≤ 0.1 % CPU, speed up once AutoSave and maintenance pause | question | F1-I |

### 9.2 Root causes

- **Bars 2..8:** `cc_rowStack[8]` held sixteen 4-bit slots, but SEQ presses
  pushed the **absolute** step (16·bar + index). From bar 2 the value was
  truncated (`16 & 15 = 0`), `cc_rowRemove(16)` found nothing, the stack
  never emptied and the copy source never committed. Fix: store the row
  index (0..15) and convert to an absolute step only when start/end are
  formed (`cc_rowAbsolute()`).
- **Pot-clear delay:** `ccSvc_tick()` started every job and register pass
  only after borrowing the 9 kB name buffer, which needs an idle filesystem
  facade. An in-flight AutoSave drain, Pattern drain, trace append or
  settings write finished first (user rule: in-flight writers complete).
  The register drain, Pattern clears, whole-Pattern copy/reset and Scene
  commits never use the buffer.

### 9.3 Changes F1-A … F1-K

| Id | Change |
|---|---|
| F1-A | `ledHandler.c/.h` reverted to their pre-S075 text (`git checkout 00bd078`): group-blink state (`led_blinkGroupMask[3]`, `led_blinkPhase`), helpers and `led_setBlinkGroup()` gone; S070 layer stack unchanged. Session callers removed. |
| F1-B | One helper `cc_applyButtonLeds()`: copy without menu → copy LED steady; copy menu open (provisional or set source) → copy LED blinking; clear → copy and SHIFT blinking. Called by press, menu open and `copyClear_postEvent()`. |
| F1-C | Row gestures (SEQ steps, SELECT bars, EFFECTS SEQ FX steps): menu on the first press with a provisional source; a press while another row button is held makes a range from the most recently pressed held button; releases change nothing until the last release, which commits the copy source (selection kept) or queues the clear. Stack holds raw indices. |
| F1-D | Indicator from column 8 (the 9th character) whatever the header length. |
| F1-E | Labels: `step -> repl/merge`, `bar -> repl/merge`, `auto -> repl/merge` (mapping unchanged). |
| F1-F | `cc_flashObject()` replaces `cc_flashDestination()`: one `led_flashGroup()` per row over every visible destination LED (steps of the visible bar, bars, FX steps, the TRACK LED for track pastes/clears, the Scene in PERF). |
| F1-G | Clear selection kept per button group (SEQ steps, SELECT bars, TRACK, PERF SEQ Scenes; read from the previous object's kind); a new group or copy/clear release starts at `cancel`; a kept selection missing from the new menu (TRACK `send` outside EFFECTS) falls back to `cancel`. Pot clears: no menu ever; once an object opened a menu, pots do nothing until release. |
| F1-H | `ccClear_triggersOffNow()`: on acceptance, trigger bits of every object step are written off at once (`pat_setStepActive()`, one halfword RMW each) for selections that end trigger-off; then the LEDs repaint; then the queued pool work. A clear dropped later (only `EVACUATE_FAILED` on `… notes`/`… automation`) may leave its triggers off (accepted, A5). |
| F1-I | Start latency: the name buffer is borrowed lazily (overlapping pastes and names only); non-overlapping pastes read the source live and retarget per step; FX step paste snapshot on the stack (288 B); Scene-level executors record names in a final phase that waits for `ccSvc_namesReady()`; trickle governor (2 µs per 2 ms tick = 0.1 % CPU, cap 40 µs) while an older writer still owns the facade. Early trigger bits for pastes (§9.5). |
| F1-J | `clear … notes`: trigger off, note and velocity specials removed, **probability kept** (it gates the step's automation), automation kept. |
| F1-K | `auto -> repl` replaces the destination's probability special with the source's (set, changed or removed); `auto -> merge` leaves probability alone; the skip test also compares probability flags. |

### 9.4 F1 decisions (user, 2026-10-02)

| # | Question | Decision |
|---|---|---|
| Q1 | Bar copy labels | `bar -> repl`, `bar -> merge`, `auto -> repl`, `auto -> merge` |
| Q2 | When does the clear selection return to `cancel`? | Only on copy/clear release or a press in a different **button group** (TRACK, SEQ steps, SELECT bars, PERF SEQ Scenes); applies to every group, including destructive Scene clears |
| Q3 | Pot clears while a clear menu is up | A menu opened by an object stays until release; pots then do nothing. Pot clears happen in a fresh SHIFT + copy/clear hold, never open a menu, drop the `_` underline at once and remove automation in the background |
| Q4 | Overlapping pastes waiting for the buffer | Accepted, but SEQ LEDs/triggers must update quickly |
| Q5 | Provisional source in the trace | Final source only |
| Q6 | Same group across modes | Yes (steps in VOICE and STEP are one group) |
| Q7 | `copy track` blink | Destination TRACK LED only |
| A1 | `clear track` blink | TRACK LED only; the current track's step LEDs go dark at once |
| A2 | Early-trigger storage | O1 (128-bit source mask per queue slot), +64 B approved |
| A3 | Paste dropped after early triggers | Restore; extra RAM approved (+64 B restore masks, +1 B flags) |
| A4 | Trace DEV RAM +4 B | Approved |
| A5 | `… notes` and dropped clears | A clear dropped in low storage may leave triggers off (accepted). `… notes` clears every special except probability (when set), keeps automation, trigger off |
| B1 | Restore when the user toggled a step meanwhile | Such steps keep the user's state |
| B2 | Automation pastes and probability | `auto -> repl` replaces probability; `auto -> merge` does not touch it |

### 9.5 Early trigger bits (§11.4 of the F1 plan, as implemented)

- Step, step range, bar, bar range and `copy track` pastes with `… -> repl`
  (trigger := source) or `… -> merge` (trigger := destination OR source)
  write their destination trigger bits at the press and repaint the step
  LEDs; the block (specials, automation) follows in the job. `auto -> …`
  pastes write nothing early.
- Per queue slot: `ccSvc_earlySrc[4][16]` (source trigger of the n-th pasted
  step, read before the early write) and `ccSvc_earlyPrev[4][16]` (previous
  destination triggers); `ccSvc_earlyFlags` bit i = slot i has early
  triggers, bit 4+i = slot i uses its source mask.
- On `CC_RUN_DROP`, `ccSvc_restoreEarlyTriggers()` restores each destination
  step whose live trigger still equals the value written at the press; a
  step the user toggled since is left alone (B1). The paste is then undone
  whole.

### 9.6 Combined F1 + trace schedule (`S075_PH6_COPYCLEAR_F1_AND_TRACE_IMPLEMENTATION.md`)

Stages A–K: A `ledHandler` revert; B `AutosaveTrace.h` stage `c`; C
`copyClearService.h` contracts and trace helper; D `copyClearService.c` (lazy
borrow, live paste path, early triggers and restore, governor, F1-J, names,
trace hooks); E `copyClearSession.c/.h`; F `copyOps.c/.h`; G `clearOps.c/.h`;
H `PatternStackService.c` (exclusive-move trace removed, D4); I `filesystem.c`
(suspension edges, refusal witness); J `tools/decode_devlogs.py`; K docs.

Deviations from the two plans (decided in the deep dive):

| # | Deviation |
|---|---|
| 0.2 | The early-trigger **source** mask is captured for every early-trigger paste, not only overlapping ones: a later paste's early write can change the source of an earlier queued paste (A: X→Y queued; B: Z→X written at once); A must copy X as it was at A's press. No extra RAM; also makes the drop restore exact. |
| 0.3 | Early paste triggers are written by the service (`ccSvc_pasteTriggersNow(slot)`), not by `copyOps.c`; masks, geometry and the overlap test stay private to `copyClearService.c`. |
| 0.4 | The governor gates the **whole job call** per tick (charged its measured TIM2 time) instead of each work unit; the engines keep their per-tick limits (8 steps, 16 snapshot reads, 32 region entries). |
| 0.5 | The trickle flag is a bit of `ccSvc_flags`; governor RAM is the 2 B credit only. |
| 0.6 | DEV trace state packs `op_seq`, `names_result` and `stalled` into one byte (22 B struct + 2 B latches = the approved 24 B). |
| 0.7 | `ccSvc_nameCopy()` returns 0 when the buffer is not borrowed; `ccSvc_nameContentChanged()` never borrows; `CC_REMAP_CHANGED` (0xFE) removed. |
| 0.8 | `cc_commitSource()` keeps an already-open provisional copy menu and its selection; it opens the menu only for TRACK/Scene sources (set on press). |

Trace additions from F1: `JOB_TRICKLE` (0x17), `EARLY_RESTORED` (0x19),
`EARLY_TRIG` (0x24); `NAMES` bits 16..23 became reserved 0.

Implementation note (2026-10-02 compile review): the Kit executor could
finish before its lazy HCNAMES phase; it now advances through the worker
phase to phase 2 and waits for `ccSvc_namesReady()`.

Builds: DEV `text=530,592 data=416 bss=426,616` (flash 531,008, headroom
222,656); production `text=516,688 data=408 bss=409,840` (flash 517,096; no
`ccSvc_traceState`/`fs_cc_*` symbols). `decode_devlogs.py` compiled and a
synthetic stage-`c` decode passed.

---

## 10. Hardware round 2 → F2 follow-up (`S075_PH6_COPYCLEAR_F2.md`, `…_F2_IMPLEMENTATION.md`, 2026-10-03)

### 10.1 Notes and root causes

| # | Note (user) | Root cause | Change |
|---|---|---|---|
| 1 | After `clear scene`/`clear scene settings` voice 1 came up L1 and voice 6 St2; all should be St1 | `scene_defaultVoiceAudioOut()` returned 2 (L1) for slot 0 and 1 (St2) for slot 5 (the old "Slak" convention); `filesystem_defaultVoiceAudioOut()` copied it. Route ids: 0 St1, 1 St2, 2 L1, 3 R1, 4 L2, 5 R2 (`MenuText.h`) | F2-A: both return 0 |
| 2 | Header should read `Clear`/`Copy`, not `CLR`/`COPY` | `copyClear_formatMenu()` text | F2-C |
| 3 | Clearing a Scene or its settings should leave the compressor off/0/0/off | `SCENE_BUS_COMP_DEFAULT_AMOUNT`/`_TIME` were 48 | F2-B: both 0 (every default path) |
| 4 | Effect `run` full view showed the long name left and the short name right | `menuEffects_paintEditView()` wrote `menuEffects_formatValue3()` at column 13 for `run`, `typ`, `scl` | F2-D |
| 5 | `typ` full view showed `Off 1 Mo     off`, `StFilter   * flt` | the same, plus the `*` changed-mark, plus a real bug: `menuEffects_copyField()` tested `src[i]` for every column and read past a short string's terminator into the following flash bytes | F2-D: `copyField` stops at NUL and pads; full views show the long name only; no `*` (the encoder click is the gate, F2-Q3) |
| 6 | Effect pan should reset to 0 (showed 1); CrumpBit defaults mix 0, fbk 64, rte 64, dpn 0; rename `syn` → `snc` | pan rows use `DTYPE_PM63` (display value − 63) but the default and the stereo DSP centre were 64 | F2-E |
| 7 | Effect page: SHIFT+TRACK shows that voice's `+` mix screen while TRACK is held | — | F2-F overlay |
| 8 | VOICE mode: holding SHIFT shows/edits Morph endpoints like the Effect page | VOICE Morph view was only the SHIFT+MODE VOICE latch | F2-G |
| 9 | FX send should be morphable (both endpoints in Scene settings, following the voice's Morph amount) | one byte per voice | F2-H |

### 10.2 F2 decisions (user, 2026-10-03)

| # | Decision |
|---|---|
| F2-Q1 | St1 on every default path, including the legacy kitset import fallback (which fires only for a missing/invalid route) |
| F2-Q2 | Compressor off/0/0/off on every default path |
| F2-Q3 | No `*` mark; the encoder click is enough of a gate |
| F2-Q4 | **One pan UX:** stored 0 = fully left = `-63`; 127 = fully right = `64`; default = absolute centre, stored 63 = `0`. "There shouldn't be multiple UXes for pan." Instrument defaults are not touched. Addendum: "instrument pan is mono, the underlying math is slightly different for stereo, but should display and default to the same." |
| F2-Q5 | +5 B for the overlay approved |
| F2-Q6 | SHIFT+TRACK changes the active track (fine); an Effect type's own SHIFT+TRACK action takes priority; SHIFT held = Morph view, released = gone while TRACK alone is held; Effect LEDs kept |
| F2-Q7 | Keep the SHIFT+MODE VOICE latch; SHIFT release returns to the latch state |
| F2-Q8 | +96 B for `fx_send_morph` approved |
| F2-Q9 | `clear send` zeroes both FX-send endpoints |
| R2-1 | (after the first schedule) SHIFT+TRACK is the entry; holding TRACK keeps the Scene-settings screen. **Effect-type SELECT functions keep working while TRACK is held; only menu screen changes are suppressed.** The first schedule's "SELECT inert" was called "super hacky" and replaced. |
| R2-2 | **No backward compatibility:** AutoSave records and other temporary records are deleted after the update. The first schedule's AutoSave marker cell 51 (constant 0xA5, boot-latch case-2 replay copying Normal → Morph for pre-F2 records) and the `sceneset.scg` missing-key copy rule (a `seen_fx_send` bit set) were removed from the plan before implementation. |
| R2-3 | Fix `tools/verify_bank_autosave.py` so every check is correct after the implementation |

### 10.3 What F2 changed (schedule Stages A–K)

- **A — St1 defaults:** `scene_defaultVoiceAudioOut()` and
  `filesystem_defaultVoiceAudioOut()` return 0; `populate_scene_directory.py`
  `DEFAULT_AUDIO_OUT = [0]*6`. `mixer_init()` already zeroed runtime routes.
- **B — compressor defaults** 0/0 (`SceneData.h`), comment texts updated.
- **C — header** `Copy`/`Clear`; indicator still from column 8.
- **D — Effect full views:** `menuEffects_copyField()` fix; TYPE/RUN/SCALE
  show only the long name; LENGTH/MORPH show only the number at column 13.
- **E — pan and CrumpBit:** `EFFECT_COMMON_DEFAULT_PAN` 63; Effect-return
  stereo balance in `mixer.c` and CrumpBit delay pan in `CrumpBitEffect.c`
  centred on 63 (`gL = p ≤ 63 ? 1 : (127 − p)/64`, `gR = p ≥ 63 ? 1 : p/63`);
  mono laws (`squareRootLut`) unchanged; CrumpBit rows mix 0, feedback 64,
  rate 64, delay pan 63, sync label `snc` (file key `crump_sync` unchanged).
  `DTYPE_PM63` stays (a `DTYPE_PM64` option was withdrawn).
- **F — FX-send Morph data:** `scene_settings_t.fx_send_morph[6]` (after
  `fx_send_amount`); `scene_setVoiceFxSendMorph()`/`scene_getVoiceFxSendMorph()`
  (AutoSave cells 45..50, `AUTOSAVE_SCENE_PARAM_COUNT` 51,
  `AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES` 51); `scene_commitSettings()` carries
  it; stage/boot/fresh defaults 0; `sceneset.scg` writer line 14
  `fx_send_morph` (appended, no renumbering) and parser key (missing → 0);
  `presetMorph_getResolvedVoiceAmount()` (step override or retained base, plus
  active-Scene LFO contributions); `preset_getEffectiveFxSendAmount()` =
  step override, else `round(normal + (morph − normal)·amount/255)` with an
  equal-endpoint fast path; `preset_getFxSendDisplayAmount()` (step override
  else Normal endpoint); `preset_setVoiceFxSendMorph()`; `clear send` zeroes
  both endpoints and sets fader `pre`.
- **G — menu FX-send cell:** the Morph view (`voiceModeShowMorph`) shows and
  edits the Morph endpoint; the Normal view shows the step override or the
  Normal endpoint; never the interpolated send (like instrument cells); both
  fan out over the edit mask.
- **H — VOICE hold-SHIFT Morph view:** SHIFT press in VOICE mode calls
  `menu_setVoiceModeShowMorph(1)`; release restores
  `buttonHandler_morphVoiceModeActive` (the latch).
- **I — Effect-page SHIFT+TRACK voice-mix overlay** (§10.4).
- **J — tools:** `populate_scene_directory.py` writes `fx_send_morph`;
  `verify_bank_autosave.py` rebuilt (§10.6).
- **K — docs** (filesystem, AutoSave, Bank/Preset, SRAM, DSP references,
  spec, scoping, memory).

### 10.4 The SHIFT+TRACK voice-mix overlay (as built, including the follow-up)

- **Entry:** in EFFECTS mode, SHIFT+TRACK *n*. The Effect type's TRACK hook
  runs first and wins if it handles the press. Otherwise the active track
  becomes *n* (LED flash, stopped-transport preview as before) and
  `menu_fxVoiceMixOverlayBegin(n)` shows VOICE *n*'s mix sub-page (SELECT 8,
  `MENU_VOICE_MIX_SUBPAGE` 7) on its first Scene-setting screen (`+`: out, FX
  send, fader, voice Morph), in the Morph view while SHIFT is held.
- **Held set (follow-up, user):** while any overlay TRACK is held
  (`buttonHandler_fxVoiceMixTrackMask` nonzero), every TRACK press with or
  without SHIFT joins the set, and on the Effect page the screen switches to
  the track pressed last (the active track follows). **No TRACK press mutes
  until every held TRACK is released and the overlay is gone.** In other
  modes (after a MODE press) such presses are consumed with no action.
- **Exit:** the last overlay TRACK release calls
  `menu_fxVoiceMixOverlayEnd()`, which restores the Effect `menuIndex`, edit
  mode and the VOICE mix screen memory, applies latched Effect service
  actions (`REPAIR`, `EXIT_EDIT`), re-asserts the Effect Morph view from
  SHIFT, and repaints. A real page/mode switch ends the overlay without
  restoring (running `menuEffects_leave()` when the destination is not the
  Effect page). Event-ring overflow clears the mask and ends the overlay.
- **Why not `menu_switchPage()`:** a page switch runs `menuEffects_leave()`
  (clears Effect SEQ blinks, SELECT LEDs, hold and Morph state) and repaints
  Pattern LEDs; the overlay swaps `menu_activePage`/`menuIndex` directly and
  saves the Effect position in a 4-byte record (`flags`, `fx_menu_index`,
  `fx_edit_mode`, `mix_screen`).
- **Effect LEDs stay live:** `menu_serviceRuntimeWidgets()` keeps calling
  `menuEffects_service()` under the overlay (FX SEQ row, hold flashes,
  type-owned SELECT LEDs). Three `ledHandler.c` gates (`led_updateCurrentStep()`,
  `led_updateRecordedMainStep()`, follow-mode repaint in
  `led_notifyPatternChanged()`) treat the overlay like `EFFECT_PAGE`, so
  Pattern chase/record/follow feedback does not draw over the Effect row.
  `va_updateHeldState()` is skipped under the overlay (SEQ holds belong to
  the Effect lock editor).
- **SELECT (R2-1):** type SELECT functions run (with or without SHIFT); a
  hook's "show home" only re-renders the SELECT LEDs for the saved Effect
  sub-page (`menu_effectShowHome()`); default SELECT navigation is skipped.
- **SHIFT:** press under the overlay → voice Morph view; release → Normal
  view (the VOICE latch does not apply on the Effect page).

### 10.5 Layout measurement (F2 build gate)

Measured with arm-none-eabi-gcc 14.2 and the build's flags before writing the
schedule: `sizeof(scene_t)` 1624, `_Alignof(scene_t)` 2,
`offsetof(scene_t, effect)` 44, `sizeof(scene_settings_t)` 44,
`sizeof(effect_record_t)` 420, `instrument_type_t` 1 B (short enums). All
settings members are bytes, so `fx_send_morph[6]` grows `scene_t` by exactly
6 B → `scenes` `0x6580` → `0x65E0` (+96 B). The filesystem stage-cache
assert becomes 2,014 ≤ 2,048 B. Verified on the F2 link.

### 10.6 `tools/verify_bank_autosave.py` rebuilt (R2-3)

The validator was stale in four ways and incomplete in one: record format
version 1 (firmware writes 3); 129 HCNAMES rows (161); a type column required
on every row ≥ 33 (only Instrument rows 33..128 have one); Scene parameters
read at Scene byte 8, 40 cells (byte 10, 51 cells); every `sceneset.scg` key
required (only `format`/`version` are; missing keys take the stage defaults:
Morph 0, St1, sends 0, fader pre, MIDI channel track + 1, note 63, Effect
Morph 0, compressor off/0/0/off clamped like `scene_busCompClamp()`,
FX-send Morph 0; cell 7 = 127). Checked and unchanged: record size 34,768,
Bank offset 3,920, Scene offset 4,048 / 1,920 B, Kit at Scene + 640,
Instruments at Kit + 128 + 192 × slot, header offsets and the CRC32C rule.
The checked-in `SD_CARD/` is not a coherent post-F2 Bank snapshot, so a PASS
on a real F2 card is still pending.

### 10.7 F2 builds

DEV `text=531,936 data=416 bss=426,712`, payload 532,352, image 532,368;
`scenes` `0x65E0`; `make -C tools/dsp_test mixer special_tags` passed
(mixer differing samples 0; special tags 155 rows, 0 mismatches). Overlay
follow-up: `text=532,096` (+160), bss unchanged.

---

## 11. F3: a menu edit reset automated parameters mid-note (`S075_F3_AUTOMATION_BUG.md`, `S075_F3_AUTOMATION_BUGFIX_IMPLEMENTATION.md`)

### 11.1 Symptom (user, hardware)

Coarse pitch automated in the Pattern; while the note played, editing a
different parameter on the VOICE page (pan, filter cutoff, any parameter)
set the pitch back to its un-automated value at once.

### 11.2 Root cause (pre-S075; S075 did not introduce it)

- Step automation of an instrument parameter is a runtime overlay that lasts
  until the voice's next trigger: `seq_drainPendingAutomation()` writes the
  value with `instrumentManager_writeRuntime()` and sets bit `local` in
  `seq_automation_dirty[slot]`; `seq_restoreAutomatedParameters()` (trigger
  funnel, `MidiVoiceControl.c`) writes `morph_interpolation[local]` back and
  clears the bitmap; `seq_restoreAllAutomation()` does the same at transport
  and Pattern boundaries. The bitmap is the only record that a value is held.
- **The Morph worker ignored it.** `preset_setInstrumentParameter()` stored
  the endpoint and called `presetMorph_requestVoice()`, which queued the
  **whole voice**; `presetMorph_tick()` rewrote every morphable descriptor's
  base with `preset_applyInstrumentRuntimeValue()`, overwriting the held
  pitch. `presetMorph_applyVoiceNow()` had the same unguarded write. (Commits
  `a443bd38` for the worker write, `de736f56` for the request; the bitmap
  from S065/S070.)
- **Second defect:** the menu's direct runtime write (Normal view, active
  Scene, **retained** voice Morph 0) ignored the bitmap, and tested the
  retained amount instead of the resolved one, so with an `Nvm` step override
  or a Morph LFO it wrote the raw Normal endpoint over the interpolation until
  the worker corrected it.

### 11.3 What was affected

Every automatable morphable instrument parameter (all automatable instrument
parameters are morphable: `instrumentManager_targetValid()` requires
MORPHABLE), track 7 via slot 6, whenever anything queued the voice or
applied it synchronously: menu edits, voice Morph edits (`vm`, PERF),
global Morph, an LFO on a voice's Morph (continuously), `Nvm` automation on
the same or a later step, track-7 decay edits, Instrument/Kit load or apply
while playing, Scene settings apply and Morph rebuild, quiet-threshold Scene
switches. **Not affected:** Effect parameters (EffectsManager composes the
Pattern overlay as a layer every block), Scene targets `Nfx`/`Nou`/`Nvm`/`fxm`
(separate overlays read through effective getters), the track-7 decay step
override, non-morphable cells.

### 11.4 Decisions (user, 2026-10-03)

| # | Decision |
|---|---|
| Q1 | **Automation always wins.** The menu shows which parameters are automated; an edit with no audible change is explained on screen. A menu edit never overrides the Morph interpolation: it only sets an endpoint; with Morph above 0 the sound changes by less than the edit, which is correct. |
| Q2 | Instrument type change while values are held: out of scope ("all bets are off"). |
| P1 | **MIDI takes the lowest priority.** A CC only enters the parameter; the Morph sweep updates the interpolation whenever it gets to it. |

### 11.5 The fix (Stages A–E)

- **A** `seq_automationHoldsParameter(slot, local)` (`sequencer.c/.h`):
  read-only view of `seq_automation_dirty[]`; foreground-only (drain,
  trigger funnel, transport restores and the Morph worker all run in the
  main loop).
- **B** `presetMorph_writeRuntimeBase()` (static): the one Morph-base runtime
  write; skips an inactive Scene and any held parameter. Used by
  `presetMorph_tick()`, `presetMorph_applyVoiceNow()` and the new
  `presetMorph_applyParameterNow(scene, slot, local)`, which re-interpolates
  one parameter at `presetMorph_getResolvedVoiceAmount()` and stores
  `morph_interpolation[local]` (the value the trigger restores).
- **C** `preset_setInstrumentParameter()` loses `record_automation`; on the
  active Scene it calls `presetMorph_applyParameterNow()` and no longer queues
  the whole voice. New `preset_setInstrumentParameterFromMidi(slot, index,
  value)`: morphable → store the active Scene's Normal endpoint (AutoSave and
  card-clean marked) and `presetMorph_requestVoice()`; non-morphable →
  `preset_setSupplementalParameter()` (store + apply, as a menu edit).
- **D** `menu.c` caller updated; `menu_clampInstrumentValue(slot, index,
  value)` exposes the VOICE-page clamp (`menu_clampCellValue()` on a
  transient instrument cell) so a raw 127 never persists out of range.
- **E** `MidiParser.c`: `midiParser_ccDispatch(msg, update, external)`
  carries the origin; the global-channel CC entry and NRPN data entry pass
  `external = 1` and reach `midiParser_enterTaggedParameter()` (key lookup on
  the active Scene's slot type → clamp → MIDI setter); the public
  `midiParser_ccHandler()` wrapper (legacy internal callers:
  `preset_applySoundParameter()` from `menu.c` and the buttonHandler
  armed-step reset) keeps the runtime-only write.

Deep-dive decisions recorded in the schedule (§0.2): MIDI enters the active
Scene's Normal endpoint only (no Morph view, no fan-out) and is now retained;
only external MIDI changes path; values are clamped; non-morphable keys go
through the supplemental path; the menu edit no longer queues the whole
voice; no LCD repaint on MIDI input. Tagged-CC registry audit: 36 unique
keys, all resolve, none is a no-bind (`ROW_NOBIND`) descriptor, so no mapped
CC becomes a no-op.

Build: `text=532,408 data=416 bss=426,712`, payload 532,824, headroom
220,840, image `d1c0aac2…9ceb`; 0 B SRAM. User: "seems ok".

---

## 12. Documentation changes (closeout)

| File | Change |
|---|---|
| `log_archive/000_SESSION_INDEX.md` | 074 and 075 quick-reference rows; S075 key facts; 075 summary |
| `log_archive/075_SESSION_HANDOFF_LOG.md` | this log |
| `specification_reference/COPYCLEAR_UTILITIES.md` | **new**: the copy/clear reference (replaces the full spec, the schedules' behaviour and the trace layout tables) |
| `PATTERN_DYNAMIC_STACK.md` | status/header to S075; copy implemented; automation playback corrections and the F3 priority rules (§6.1–§6.2); swap-block, early-trigger and trickle notes in §12.17; Session 075 history |
| `MODULE_INTERCHANGE_SPEC.md` | header; F2/F3 APIs (SceneData FX-send Morph, Preset getters/setters, presetMorph resolved/apply-one, Sequencer hold query, MidiParser dispatcher, Menu overlay and clamp, ledHandler gates, buttonHandler overlay); stale rows fixed (`record_automation`, `preset_getEffectiveFxSend`, removed `srt` runtime, compressor defaults) |
| `BANK_PRESET_ARCHITECTURE.md` | header; Scene defaults; VOICE Morph views; FX-send function names; F3 rules; pointer to COPYCLEAR_UTILITIES |
| `dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` | full-view rule, SHIFT+TRACK overlay, S075 history repointed |
| `dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | header; write path and automation priority (F3); history |
| `dsp_instruments_effects/CPU_USE_DSP_AUDIT.md` | Session 075 additions |
| `DEV_MODES.md` | stage `c` pointer to the full layout tables |
| `AUTOSAVE.md`, `FILESYSTEM_SPEC.md`, `OSC_INTERP_AUDIT.md`, `EFFECTS_MIXER_DSP_REFERENCE.md` | status headers to the S075 close (AutoSave: cell 7, cells 45..50 without migration, suspension; filesystem: `srt` key retired, `HNcU`, name-buffer loan, `fx_send_morph`, retained MIDI entries) |
| `STORAGE_SRAM_MANIFEST.md` | close numbers (F3 build), image end `0x0808A158` |
| `MEMORY.md` | current state, S075 notes, spec index, layout tree |
| `knowledge_files/volatile/` | S075 close carry-over |
| `SCOPING_TARGETS.md` | Phase 6 status and S075 pointers |
| `Core/Bank/Scene/AutosaveTrace.h` (2 comments), `Core/Menu/CopyClear/copyClearSession.h` (1 comment) | the pointers to root `S075_*.md` documents now name `COPYCLEAR_UTILITIES.md` (comment-only; behaviour unchanged) |

---

## 13. Open items and carried debt

### 13.1 From Session 075

- **Hardware verification still to report:**
  - the overlay follow-up (TRACK re-pairing, no mutes while held);
  - the combined copy/clear, F1, F2 and F3 case lists
    (`COPYCLEAR_UTILITIES.md` §16);
  - a `verify_bank_autosave.py` PASS on a card written by this firmware
    after deleting the old AutoSave records.
- **Production build** (`DEV_MODE_LOGGING 0`) last measured at F1; re-measure
  (expected ≈ F1 production + F2 +101 B + F2/F3 code).
- **Card preparation:** delete `.hcprms1`/`.hcprms2` (and other temporary
  records) before the first boot of this firmware on an older card (R2-2:
  cells 45..50 are not migrated; an older record would restore Morph send 0).
- **Effects saved at pan 64** show `1` and are 0.14 dB down on the left;
  re-set them if wanted (no migration, user).
- **Copy/clear known limits** (by design or accepted): the growing-step
  check can drop a paste a net-growth check would accept (≤ 1 block); a
  whole-Pattern copy into the playing Scene silences destination entries for
  up to 28 ticks; LOAD/SAVE right after a long register drain waits for the
  name buffer; a power loss between the name write and the next AutoSave
  shows new names over old content; settings/Effect pasted into a
  non-present Scene are AutoSaved only once a Kit makes it present; a clear
  dropped after its early write leaves triggers off; at 0.1 % CPU a large
  clear can take a second or two while an older drain finishes.
- **MIDI:** CC-entered values reach the LCD only at the next repaint; the
  legacy internal CC path (`preset_applySoundParameter()`) is still
  runtime-only.
- **Pattern Load fan-out tear** (S075 finding, `SCOPING_TARGETS.md`
  "Session 075 findings"): Pattern Load phase 51's `memcpy` into other
  selected Scenes is not publication-ordered and can tear one playback tick
  of a playing destination. Fix: the sentinel-first order copy/clear's
  whole-region copy already uses. Not changed.
- **O1 (S074):** Settings Load still equalises per-voice Morph across the
  VOICE edit mask (the `srt` part is gone); fix with the refresh-only
  pattern.
- **Trace:** `AUTOSAVE_TRACE_RECORD_COUNT` is still the temporary 2,048 (D2).

### 13.2 Still open from earlier sessions

- S074: the unexplained boot timeout; the `cpu` widget with `cmp` on; BC11;
  F4 trace-ring priorities; saturator α 0.35 A/B; CrumpBit minimum-share run;
  stale comments (`BusCompressor.h` 24 B, `BusCompressor.c` +0.45 %,
  `CrumpBitEffect.h` 73,632 B, the S074 C4 comment in `menu.c`, `main.c`
  about 532); `decode_devlogs.py` misreads `pattrace.bin`.
- S072: the Phase 5 Step 6–10 hardware matrices are still unreported.
- Per-track step scale and shuffle playback (`PATTERN_DYNAMIC_STACK.md`
  §6.4).
- S072 carried debt (Makefile comments/default goal, duplicated comment
  lines, unreachable Scene Save phases 33–36). The `verify_bank_autosave.py`
  item is closed (§10.6).

---

## 14. Files changed (`50610dd` → `76aef20`, code and tools)

59 files, +8,146 / −954 lines. New: `Core/Menu/CopyClear/` (`copyClearSession`,
`copyOps`, `clearOps`, `copyClearService`, each `.c/.h`; 3,865 lines of C).
Deleted: `Core/Menu/copyClearTools.c/h`. Largest changes: `PatternData.c`
(+588), `menu.c` (+555), `presetManager.c` (+301), `filesystem.c` (+298),
`PatternStackService.c` (+285), `buttonHandler.c` (+255), `SceneData.c`
(+200), `EffectsManager.c` (+184), `PatternData.h` (+146), `decode_devlogs.py`
(+132), `MidiParser.c` (+119), `presetMorphEngine.c` (+118). Also:
`BankData.c/h`, `Autosave.c/h`, `AutosaveTrace.h`, `ParameterArray.h`,
`presetManager.h`, `presetMorphEngine.h`, `SceneData.h`, `SceneModTargets.c/h`,
`CrumpBitEffect.c`, `CrumpBitParameters.c`, `EffectTypes.h`, `EffectsManager.h`,
`InstrumentManager.c`, `mixer.c/h`, `filesystem.h`, `storageTypes.c`,
`buttonHandler.h`, `ledHandler.c`, `timebase.c`, `MidiMessages.h`,
`MenuText.h`, `menu.h`, `menuEffects.c`, `menuPages.h`, `sequencer.c/h`,
`Makefile`, `config.h`, `main.c`, `tools/convert_legacy_kits.py`,
`tools/populate_scene_directory.py`, `tools/verify_bank_autosave.py`.

---

## 15. Architectural invariants introduced

1. **Copy/clear owns every front-panel edge while COPY is held** and pairs
   each consumed press with its release (edge masks, cleared on ring
   overflow). No consumed edge may reach step toggles, holds, Scene switches,
   mutes or audition.
2. **Pattern data never fans out; Scene children do** (Instrument, Kit,
   Effect, FX sequence, `send`), through the destination Scene's own
   edit-mask entry filtered by `scene_editLayoutMatches()`. `copy scene` /
   `copy scene settings` exchange the mask entry; `clear scene` /
   `clear scene settings` reset it to self.
3. **One Scene's Pattern is written at a time**, by the holder of
   `patSvc_beginExclusive(scene)`; any Scene may be read. The raw block API
   is legal only inside that boundary and keeps the publication order
   (new bytes into unreferenced space, one PRIMASK store of the complete
   address entry, then free).
4. **The swap block** (top 33 chunks of every pool) is never allocated by
   ordinary paths; it is the guaranteed rewrite area for a step whose new
   block does not fit elsewhere. `pat_tryAppendAutomation()` never grows into
   it.
5. **Suspension:** while `copyClear_backgroundSuspended()` is nonzero no
   AutoSave, Pattern AutoSave, trace flush, settings write or repair epoch
   starts; a writer already running finishes.
6. **The 9 kB name cache may be lent** (`filesystem_borrowNameCacheScratch()`);
   while lent, cache disposal is ignored and other filesystem ops are
   refused.
7. **Automation priority:** step automation > menu edit > external MIDI. A
   held automation value is never overwritten before the voice's next
   trigger; menu edits apply one parameter's interpolation; MIDI stores an
   endpoint for the Morph sweep.
8. **One pan rule:** stored 63 is centre and displays 0 for every pan.
9. **Global `srt` is gone;** ID 390 stays a retired placeholder so later
   Scene target IDs do not move.

---

## 16. Next session

1. User hardware checks still open (§13.1), starting with the overlay
   follow-up and an F2 card validator run.
2. Production build measurement.
3. The next Phase 6 item, chosen with the user. `SCOPING_TARGETS.md` Phase 6
   lists §6.0 (deferred Phase 4 items: manual roll triggering, dot/triplet
   subdivisions, automation hold reconciliation, per-track scale/shuffle
   playback, live record of automation) first, then MIDI rework, looper,
   one-shot LFOs and external MIDI sequencing tracks.

---

## 17. Disposable documents

Superseded by this log and the specifications; the user will delete them:

- `S075_PH6_COPY_CLEAR.md` (startup brief; §4 here)
- `S075_PH6_COPY_CLEAR_FULL_SPEC.md` (now `COPYCLEAR_UTILITIES.md`; history
  §4–§5 here)
- `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` (schedule and §14 log; §6–§7 here)
- `S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md` (§8 here; layouts in
  `COPYCLEAR_UTILITIES.md` §14)
- `S075_PH6_COPYCLEAR_FOLLOWUP_F1.md` and
  `S075_PH6_COPYCLEAR_F1_AND_TRACE_IMPLEMENTATION.md` (§9 here)
- `S075_PH6_COPYCLEAR_F2.md` and `S075_PH6_COPYCLEAR_F2_IMPLEMENTATION.md`
  (§10 here)
- `S075_F3_AUTOMATION_BUG.md` and `S075_F3_AUTOMATION_BUGFIX_IMPLEMENTATION.md`
  (§11 here)

The line-level code of each schedule is in the commits: `7158c28` (base),
`dbc8104` (F1 + trace), `ee3c665` (F2), `76aef20` (F3); the pre-implementation
documents are in `00bd078`, `b1216db`, `822bbc9` and `b8f08db` (`git show
<commit>:<file>`).
