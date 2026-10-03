# Session 074 Handoff Log

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Branch**: `dev-ph5-effects`
**Session dates**: 2026-09-29 / 2026-09-30
**Base commit**: `692abf8` ("dsp refactor"; the S073 closeout was committed as
`f3a3105`, then `ca77891` "doc cleanup")
**State at close**:
- HEAD `50610dd` ("bus compressor update for smoothness; autosave torn record
  fix, recoverability, s074, in and tested"). Every S074 code change is
  committed. No source file is newer than the close build.
- The four DSP/Effects specifications moved into
  `knowledge_files/specification_reference/dsp_instruments_effects/`. The
  user made the move and it is uncommitted: the old paths show as deleted, the
  new folder as untracked.
- This session's closeout documentation is uncommitted. The user manages
  commits.
- The ten root `S074_*.md` documents are superseded by this log and the
  specification updates; the user will delete them (§17).
- Card copies are in git: `SD_CARD_ATS_BOOT_BUG/` (committed with `2ec72d6`)
  and `SD_CARD_ATS_CORRECTION_OUTPUT/` (with `50610dd`), including macOS
  `.Spotlight-V100`/`.fseventsd` folders. `ca77891` also committed 943
  `SD_CARD*` files and deleted the S072/S073 root documents and
  `EFFECTS_BUS_FEATURE_PLAN.md` (§15).

**Authority**:

| Document | Role |
|---|---|
| This log | Implementation record, decisions, measurements, hardware results, open items |
| `specification_reference/dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` | Effect system control side, including CrumpBit's registry row, page framework extensions, underline rule, arena gaps closed |
| `specification_reference/dsp_instruments_effects/EFFECTS_MIXER_DSP_REFERENCE.md` | Signal path: CrumpBit DSP, the master bus compressor (model, tuning, band-split saturation), the `xfd` fader mode |
| `specification_reference/dsp_instruments_effects/CPU_USE_DSP_AUDIT.md` | S074 cost additions (items 27–31) |
| `specification_reference/AUTOSAVE.md` | Torn-record fix, Scene parameters 41..44, validation status |
| `specification_reference/DEV_MODES.md` | Progress-aware drain stall rule, `V` flag layout, BC18 check |
| `specification_reference/ASYNCFATFS_REFERENCE.md` | The open-file size exaggeration and what it means for validators |
| `specification_reference/STORAGE_SRAM_MANIFEST.md` | S074 RAM and flash ledger |
| `specification_reference/FILESYSTEM_SPEC.md` | `sceneset.scg` bus compressor keys, `fader_setting` 0..3 |
| `specification_reference/MODULE_INTERCHANGE_SPEC.md` | New APIs (menuEffects page extensions, BusCompressor, `adc_sliderGainMirrored()`) |
| `S075_PH6_COPY_CLEAR.md` | Startup document for Session 075 (Phase 6) |

---

## End of session

```
DATE: 2026-09-30
SESSION GOAL: S074_EFFECT_BUGS_BUFFER_USE.md: (1) Effect-page automation
              underline bug; (2) the first buffer-using Effect (Phase 5 A8).
              Added during the session by the user: a master bus compressor,
              a fourth fader mode `xfd`, an AutoSave boot/publish bug, and a
              softer compressor saturator.
COMPLETED:
  - Effect-page automation underlines (Pattern automation on any track, FX
    locks on any step), plus the SEQ hold/release ordering follow-up
    (S074_EFFECT_P_LOCK_DISPLAY_IMPLEMENTATION.md).
  - Image script fold: build_lxrv2_img.py stamps the boot image check;
    stamp_image_check.py deleted.
  - CrumpBit (`cbt`), the first buffer-using Effect, with the Effect-page
    framework extensions and all three arena gaps closed
    (S074_CRUMPBIT_EFFECT.md, S074_CRUMPBIT_IMPLEMENTATION.md).
  - Master bus compressor `cmp/cam/ctm/csc` (S074_BUS_COMP.md,
    S074_BUS_COMP_IMPLEMENTATION.md), tuning revision 1, and the band-split
    saturation update (S074_COMP_SAT_UPDATE.md).
  - Fourth fader mode `xfd` (S074_FADER_XFD.md).
  - AutoSave stops publishing after a torn record: investigation
    (S074_AUTOSAVE_BOOT_BUG.md), fix schedule and hardware verification
    (S074_ATS_BUG_IMPLEMENTATION.md).
  - Closeout: this log, index, specification updates, MEMORY, volatile
    notes, SCOPING, README paths, S075 startup document.
VERIFIED ON HARDWARE: Yes, by the user.
  - Underlines: "complete and tested OK", including the hold/release order.
  - CrumpBit: accepted as the v1 baseline ("pretty happy with the
    implementation for now").
  - Bus compressor and xfd: "both work"; then tuning revision 1 and the
    band-split saturator ("saturator seems better; let's leave it like that
    for now").
  - AutoSave fix: "in and tested; seems ok"; the card output confirms the
    self-repair (generations 101 -> 144, boot reader back to 1,383 ms).
  - Not reported individually: the per-item matrices in each document; the
    `cpu` widget with `cmp` on; track 7 as sidechain voice 6 (BC11); the
    CrumpBit minimum-share run; the torn-write reproduction and card-pull
    tests of the AutoSave fix.
CHANGES THIS SESSION (details §4-§11, file list §14):
- Menu: menu.c/h, menuEffects.c/h, menuPages.h, MenuText.h (underlines,
  CrumpBit page framework, compressor page)
- Effects: EffectsManager.c/h (registry row, hook/layout contract, handoff
  refresh), FxBuffer.c (init order), CrumpBit/ (4 new files)
- DSP: BusCompressor.c/h (new), mixer.c/h (off-branch ramp reset, bus
  compressor call, xfd), adcPots.c/h (mirrored slider gain)
- Scene data and persistence: SceneData.c/h, Autosave.c/h, AutosaveTrace.h,
  presetManager.c/h, ParameterArray.h, storageTypes.c/h, filesystem.c
- Trigger funnel: MidiVoiceControl.c (sidechain intake)
- Front panel: buttonHandler.c (FX SELECT / SHIFT+SELECT home action)
- Build: Makefile (CrumpBit, BusCompressor), STM32F765VIHx_FLASH.ld and
  flashImage.c/h (comments), config.h (comments), tools/build_lxrv2_img.py,
  tools/stamp_image_check.py (deleted), tools/decode_devlogs.py
- Docs: see §12

KNOWN ISSUES INTRODUCED:
- None functional.
- Cosmetic: two stale comments (BusCompressor.h "24 B DTCM state"; the
  BusCompressor.c loop comment "~+0.45 % CPU"); the C4 search comment in
  menu.c sits after its fields instead of above them.
KNOWN ISSUES RESOLVED:
- Effect-page names never underlined for automation (S073 carry-forward).
- S072 debt 1 (fxbuf_init order), debt 8 (FX return ramp while `off`), and
  EFFECTS_BUS_REFERENCE §13 item 1 (same-type handoff refresh).
- Bootloader past 0x08080000: every S074 image from CrumpBit on ends in
  sector 6 and runs (§7.12).
- AutoSave permanent publication stall after a torn record; the Case-2 mass
  reload it caused at every boot.
- decode_devlogs.py read the pre-S057 3-bit X site layout; named only 3 of
  10 stall sites; did not decode V bits 2..3.

NEXT SESSION RECOMMENDED GOAL: Session 075 starts Phase 6 with copy and
  clear operations for step, bar, track, automation, instrument, Scene and
  Scene components (S075_PH6_COPY_CLEAR.md: an overview and the
  architectural decisions to make first).
BLOCKERS: none. S075 opens with decisions (S075 doc §9).

CRITICAL REMINDERS FOR NEXT SESSION:
- Constant-CPU rule. The bus compressor is the one user-approved exception
  (no work while `cmp` is off); count it against the worst case when on.
- RAM approval for any new or larger allocation (byte count, region,
  lifetime, owner), including ITCM code and the Effect runtime union.
- The DTCM union is 76 B; CrumpBit uses 56 B. The FX arena margin above the
  120 KiB ASSERT is 3,712 B; any new DTCM static shrinks the arena.
- AutoSave validators must prove exact length with a one-byte EOF probe;
  AsyncFATFS stores a cluster-rounded size while a file is open for write.
- The settings compressor page must stay last (MENU_GLOBAL_SCENE_SUBPAGE);
  new global pages go before it.
- The four bus compressor ids never commit through menu_parseGlobalParam().
- Record the `.img` SHA-256, not the `.bin` (the .bin is unstamped).
- Use `make all` (bare `make` can stop at build/main.o).
- Do not use `.claude` auto-memory; project memory is MEMORY.md and
  knowledge_files/volatile/.
- Commits are the user's. Do not suggest when to commit.
```

---

## 1. Build metrics

| Point | text | data | bss | Flash payload | Headroom | ITCM | DTCM statics | FXBUF | `.img` bytes / SHA-256 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| S073 close (`692abf8`) | 486,688 | 416 | 426,336 | 487,104 | 266,560 | 4,168 | 4,448 | 126,624 | 487,120 (`.bin` `1bd8be52…`) |
| Underlines C1–C17 | 487,384 | 416 | 426,336 | 487,800 | 265,864 | 4,168 | 4,448 | 126,624 | 487,816 |
| + hold/release fix F1–F3 + image fold | 487,544 | 416 | 426,336 | 487,960 | 265,704 | 4,168 | 4,448 | 126,624 | 487,976 / `0e004720…ddeea` |
| CrumpBit (`1dbdd70`) | 499,144 | 416 | 426,336 | 499,560 | 254,104 | 4,168 | 4,448 | 126,624 | 499,576 / `ae8ba9c0…205d8b` |
| + bus compressor | 501,920 | 416 | 426,384 | 502,336 | 251,328 | 4,168 | 4,472 | 126,592 | 502,352 / `94893da8…48a8` |
| + `xfd` | 502,072 | 416 | 426,384 | 502,488 | 251,176 | 4,168 | 4,472 | 126,592 | 502,504 / `d6c85388…465d` |
| + tuning revision 1 (`2ec72d6`) | 502,104 | 416 | 426,384 | 502,520 | 251,144 | 4,168 | 4,472 | 126,592 | 502,536 |
| AutoSave fix alone (on `2ec72d6`) | 502,152 | 416 | 426,384 | 502,568 | 251,096 | 4,168 | 4,472 | 126,592 | 502,584 |
| **Close: + saturator + AutoSave fix (`50610dd`)** | **502,512** | **416** | **426,392** | **502,928** | **250,736** | **4,168** | **4,480** | **126,592** | **502,944 / `63eec2a6…aeb0`** |

Notes:

- **Payload = `text` + `data`**; the `.img` adds a 16-byte header. Since the
  image-script fold (§6) the `.bin` is unstamped, so the `.img` hash is the
  one to record. Full close hash:
  `63eec2a602d80f54ea122a7977eb214c178f115be6c7e6a4117b02940663aeb0`.
- **RAM changes this session (all approved):**
  - SRAM1 `scenes[16]`: +64 B (`scene_settings_t` 41 → 45 B, `scene_t`
    1,622 → 1,626 B; `scenes` is now 26,016 B, `0x65a0`);
  - DTCM `.dtcmz`: +32 B for the bus compressor state (24 B, then 32 B with
    the crossover), 3,936 → 3,968 B;
  - the FX arena: −32 B, 126,624 → 126,592 B (base `0x20001160` →
    `0x20001180`, margin 3,712 B above the 122,880 B minimum);
  - DEV-only: the drain stall observer stays 5 B (1 + 2 + 2 instead of
    1 + 4).
  - CrumpBit: 0 B (56 B inside the unchanged 76 B `effects_runtime` union;
    audio uses 70,592 B of the arena share).
  - Underlines: 0 B (reuses the VOICE search state; the 45 B
    `_Static_assert` still holds).
- **Close section sizes** (`arm-none-eabi-size -A`): `.isr_vector` 456,
  `.image_check` 32, `.text` 497,344, `.itcm` 4,168, `.dma_nocache` 3,100,
  `.data` 416, `.bss` 292,732, `.dtcm` 512, `.dtcmz` 3,968, `.dtcm_fxbuf`
  126,592.
- **Image end:** `_eflash_load` = `0x08082C90`, 11,408 B into sector 6
  (§7.12).
- **Warnings:** none from S074 files. The existing newlib
  `_close/_lseek/_read/_write` notes, the LTO serial note, and pre-existing
  filesystem/Pattern warnings are unchanged.

---

## 2. What Session 074 delivered (summary)

1. **Effect-page underlines.** A parameter name on the Effect page is
   underlined when any stored automation addresses it. That means Pattern
   step automation on any of the 7 tracks of the viewed Pattern, or an FX
   lock on any of the 16 steps. It works in both views and costs 0 B of RAM.
   A follow-up removed a brief flash of the wrong glyph on SEQ hold/release.
2. **One image script.** `tools/build_lxrv2_img.py` now stamps the boot image
   check and packages the card image.
3. **CrumpBit (`cbt`)**, the first Effect that uses the DTCM arena (Phase 5
   A8):
   - a simulated 8-bit ADC, data-line bit manipulation (off/invert) and DAC
     with AC coupling;
   - a mono 8-bit tape-style delay with glide and tempo Sync;
   - a custom Effect page (bit overlay row, SELECT buttons toggle bits,
     SELECT LEDs show them);
   - the registry-driven framework extensions that page needed;
   - the three arena gaps closed.
4. **Master bus compressor.** A Scene-owned, LA-2A-flavoured stage on the St1
   or St2 output pair:
   - four controls on the last settings page;
   - a velocity-cubed trigger sidechain;
   - later retuned, then given band-split saturation to remove hi-hat
     harshness.
5. **Fourth fader mode `xfd`.** The fader crossfades a voice between its dry
   output (top) and the FX bus (bottom).
6. **AutoSave hardening.** A torn record left by power loss no longer stops
   AutoSave forever. The device repairs it on the next drain with no user
   action, and the event is logged only in the trace.
7. **Phase 5 is largely complete** (Effects bus and modular Effect system,
   initial pass). Remaining Phase 5 items are carried in §13.

---

## 3. Working method used this session

- **Plan, decide, then implement.** Each feature started with a
  specification listing open questions (U1–U8, Q1–Q30/F1–F6, BC1–BC21,
  saturation options A–E). The user answered them before any code.
- **Line-level schedules.** For the underlines, CrumpBit, the bus compressor,
  `xfd` and the AutoSave fix, the assistant wrote schedules that cite every
  change by file, line and action, with full comment blocks.
  - An implementing agent applied the underline, CrumpBit, bus compressor
    and `xfd` schedules; the user applied the AutoSave fix.
  - The assistant reviewed each diff against its schedule, rebuilt, and
    appended findings to the document.
- **Direct edits on request.** The compressor tuning revision and the
  saturator were written straight into `BusCompressor.c`, with adjacent
  comment blocks and working notes, on the user's instruction.
- **User rules in force:**
  - "do not change code files yourself this turn" (`xfd` schedule, AutoSave
    schedule);
  - no user error screen when AutoSave recovers by itself; log it in the
    trace only;
  - "ram expansions approved" (saturator, +8 B DTCM);
  - hardware checks and commits are the user's.
- **No new tools.** Sound claims were checked with Python models in the
  scratchpad and the ARM toolchain only.

---

## 4. Session start and the S074 startup decisions

### 4.1 State at start (`S074_EFFECT_BUGS_BUFFER_USE.md`)

| Item | Value |
|---|---|
| HEAD | `692abf8` ("dsp refactor"); S073 closeout committed later as `f3a3105` |
| Link | `text=486,688`, `data=416`, `bss=426,336` |
| Flash | 487,104 / 753,664 B (266,560 B free) |
| ITCM / DTCM statics / FX arena | 4,168 / 16,384 B; 4,448 B; 126,624 B at `0x20001160` (3,744 B above the 120 KiB minimum) |
| Config | `DEV_MODE_DIAGNOSTIC 0`, `DEV_MODE_LOGGING 1`, `MEMTEST_ENABLED 1`, `DEV_FXBUF_FORCE_VOICE_UNITS 0u`, `DEV_EFFECT_FORCE_TYPE 0u` |

### 4.2 Goals (user, 2026-09-29)

1. **Bug:** Effect-page parameter names were not underlined when automated
   in the current Pattern. They should be underlined for Pattern automation
   **and** for FX-sequence locks ("it should be both").
2. **Feature:** the first Effect type that uses the shared DTCM buffer
   (Phase 5 item A8, "buffer-using template type").

### 4.3 Underline decisions (U1–U8), as resolved

| # | Question | Resolution |
|---|---|---|
| U1 | Which tracks to scan | **All 7 tracks** of the viewed Pattern: Effect targets are Scene-wide and can be written on any track |
| U2 | Which Pattern | `menu_shownPattern` (VOICE parity; the STEP `fx` editor edits the same) |
| U3 | FX locks within `len` only? | **All 16 steps.** User correction: any stored automation counts whether or not it plays. The same rule applies to Pattern automation: the current type's AUTOMATABLE flags, track length, mute, trigger bit and probability do not filter it. The SEQ LEDs still light only steps within `len`. |
| U4 | Underline `mrp` | Yes, for `fxm` Pattern automation and lane-0 locks ("same as for voice") |
| U5 | Full view too | Yes ("both 4-parameter and 1-parameter view") |
| U6 | Held locked value vs name marker | The held step's locked value owns its cell (VOICE rule) |
| U7 | RAM | **Reuse the VOICE search state: 0 B.** Reset coverage proven (§5.4) |
| U8 | Rescan points | Page entry, Pattern change, Pattern/track clear; FX locks read live |

### 4.4 Buffer-Effect decisions (B1–B9), as resolved by the CrumpBit specification

| # | Question | Resolution |
|---|---|---|
| B1 | Which Effect | CrumpBit: 8-bit data-line manipulation feeding an 8-bit tape delay (user design) |
| B2 | Names | `cbt` / `CrmBt` / `CrumpBit` |
| B3 | I/O shape | Stereo in, stereo out |
| B4 | Arena sample format | 8-bit (signed int8 in the loop) |
| B5 | Parameters | 11 rows (§7.6) |
| B6 | Time units and the buffer limit | Exponential Rate, 20 ms – 1.60 s, a fixed range that fits the **minimum** share; no buffer-dependent row |
| B7 | Tails across Scene switches | Same-type switch keeps the tail; re-entering `cbt` never adopts old content |
| B8 | Time changes | Tape style: a 150 ms one-pole glide of the delay length (no crossfade) |
| B9 | RAM | 56 B runtime inside the 76 B union: 0 B |

### 4.5 Arena gaps the first buffer type had to close

1. The same-type Scene switch did not refresh the FxBuffer handoff
   (`EFFECTS_BUS_REFERENCE.md` §13 item 1).
2. `mixer_fx_return_last_gain[]` was not reset while the Effect was `off`
   (S072 debt 8). A type that outputs on its first block would ramp from
   stale gains.
3. `fxbuf_init()` reset the handoff after the diagnostic forced-unit loop, so
   forced units carried handoff rate 0 (S072 debt 1, diagnostic only).

All three are closed (§7.9).

### 4.6 Arena capacity reference (44,108 Hz)

| Share | 16-bit mono | 16-bit stereo | 8-bit mono | 8-bit stereo |
|---|---:|---:|---:|---:|
| Whole arena (126,624 B at the start) | 1.44 s | 0.72 s | 2.87 s | 1.44 s |
| Minimum (73,632 B, all twelve voice units claimed) | 0.83 s | 0.42 s | 1.67 s | 0.83 s |

The arena is 126,592 B at the close (−32 B, the bus compressor state). The
minimum share is the arena minus twelve 4,416 B voice units (52,992 B), so it
is now **73,600 B**. CrumpBit's 70,592 B still fits, with 3,008 B to spare.

---

## 5. Effect-page automation underlines (`S074_EFFECT_P_LOCK_DISPLAY_IMPLEMENTATION.md`)

### 5.1 Root cause

| Piece | VOICE pages | Effect page (before) |
|---|---|---|
| Pattern presence search | `va_scanService()` scans the viewed Pattern's active track, 4 steps per pass | none: `menu_serviceRuntimeWidgets()` ran the scan only on VOICE pages |
| Name marker | `va_applyVoiceMarkers()` underlines the first letter of an automated name | none: `menu_applyEffectMarkers()` drew only the held-step value marker |
| FX lock presence | n/a | read only by the SEQ LEDs |
| Deferred-marker retry | yes | none: a deferred CGRAM transaction stayed missing until an unrelated repaint |

### 5.2 Behaviour after the fix

**What counts as automated, per cell:**

| Effect cell | Pattern automation (viewed Pattern: any step 0..127 of any of the 7 tracks) | FX lock (active Scene: any of 16 steps) |
|---|---|---|
| `typ`, `run`, `len`, `scl` | never | never |
| `mrp` | a Scene target `fxm` (ID 404) | lane 0 (Effect Morph) |
| `out` and other PARAM rows without a lane | block-7 ID `448 + index` | never |
| PARAM rows with a lane (`flt`: `frq res drv typ vol pan`) | as above | the row's registry lane (1..15) |

- Nothing that decides whether automation *plays* filters the underline. An
  entry left over from an earlier type still underlines the row with that
  local index.
- The only entry ignored is `PAT_AUTOMATION_TARGET_OFF` (`0x1FF`), which
  decodes as Effect local 63. A STEP-page "Add" left at `off` is a valid stored
  entry, so without the guard it would set bit 63.

**Which marker a cell shows (at most one):**

1. SEQ steps held, and the shown step locks this cell's lane: the held value
   on row 1 with its rightmost glyph underlined (the S072 behaviour).
2. Otherwise, the cell is automated: the name on row 0, first non-space
   character underlined. Compact view: the 3-character short name at
   columns `4i..4i+2`. Full view: the 8-character long name at columns 8..15
   (`mrp` shows `Effect  Morph`, so its `M` is underlined).
3. Otherwise no marker. A held but unlocked cell shows the held value **and**
   the name marker.

**Timing:** FX-lock underlines are read live on every repaint. Pattern
underlines appear when the search completes: 7 × 128 steps at 4 step reads
per foreground pass = **224 passes** (a VOICE page needs 32). Completion
triggers one repaint.

### 5.3 Design

- **Sources of truth:**
  - `pat_readStepAutomations(menu_shownPattern, track, step, …)`;
  - `effectTarget_isEffectId()` / `effectTarget_local()`, rejecting local 63;
  - `sceneModTarget_descriptor(t)->kind == SCENE_MOD_TARGET_KIND_EFFECT_MORPH`
    for `fxm`;
  - FX locks: `scene_effectConst(active)->steps[s].lock_mask`, bit `L`.
- **Type independence.** The search records raw locals and the underline
  applies no type filter, so a `typ` change needs no rescan (a type change
  clears every FX step anyway).
- **Why one scan function.** `va_scanService()` keeps its single 252-byte
  entry buffer (`pat_automation_entry_t` 4 B × 63). A second function would
  carry its own array, and after LTO inlining the stack sharing would be up to
  the compiler. One function makes zero stack growth structural.

### 5.4 Shared search state (13 B, reused)

The Effect page reuses `va_searchTrack`, `va_searchPattern`,
`va_searchCursor`, `va_searchComplete`, `va_searchTargetMask[8]` and
`va_searchSceneMask`, plus the 5 CGRAM bytes shared since S072.

- **Why reuse is safe:** the pages are mutually exclusive, and **every entry
  to either page restarts the search**. The Effect entry uses the new C13;
  the VOICE entry from any non-VOICE page uses the existing restart.
- **Roles on the Effect page:**
  - `va_searchTrack` is the 0..6 track cursor;
  - `va_searchTargetMask[]` holds Effect locals 0..62;
  - `va_searchSceneMask` gains `VA_SEARCH_SCENE_EFFECT_MORPH_BIT` (`0x08`)
    for `fxm`.
- `va_searchRestart()` chooses the geometry from `menu_activePage`, so
  callers must set the page first.

### 5.5 Restart and refresh points

| Event on the Effect page | Handling |
|---|---|
| Page entry | restart (C13; not on a repeated SHIFT+PERF screen toggle) |
| Pattern change (PERF/MIDI/follow, Bank realign) | restart and repaint (C15); the Pattern check in the scan is the backstop |
| SHIFT+COPY Pattern/track clear | restart and repaint (C9; the clear is not page-gated) |
| SHIFT+TRACK (active track change) | **no** restart (C14): the search already covers all tracks |
| `typ` change, FX lock edit, Scene switch | none needed (live read; the page repaints anyway) |
| `len`, run mode, track length, mute, probability | no effect on the underline |
| STEP-page `fx` automation edits | made on the SEQ page; the next Effect entry restarts |
| Live erase while recording | **not handled** (as on VOICE; §5.9) |

### 5.6 Change list (C1–C17, applied highest line first)

| ID | File | Action |
|---|---|---|
| C1 | `config.h` | comment: the scan budget also bounds the Effect-page search |
| C2 | `menu.h` | contract comment: `menu_voiceAutoOverlayPatternDeleted()` covers the Effect page |
| C3–C5 | `menu.c` | sharing paragraph; per-field roles; `VA_SEARCH_SCENE_EFFECT_MORPH_BIT` |
| C6 | `menu.c` | `va_searchRestart()`: track 0 on `EFFECT_PAGE`, else `menu_activeVoice` |
| C7 | `menu.c` | new `va_searchRecordEffectTarget()` (block-7 local < `EFFECT_TARGET_PATTERN_LOCAL_LIMIT`, or `fxm`) |
| C8 | `menu.c` | `va_scanService()`: an `effect_page` flag; track check VOICE-only; Effect entries classified by C7; the cursor advances to the next track after step 127 of tracks 0..5 |
| C9 | `menu.c` | Pattern-clear restart gated by `menu_isScreenPage()` (VOICE1..7 or Effect) |
| C10 | `menu.c` | new `menu_effectCellAutomated()` |
| C11 | `menu.c` | `menu_applyEffectMarkers()`: value marker first, then the name marker |
| C12 | `menu.c` | the Effect branch calls `va_scanService()`; one shared retry test (`VA_MARKER_RETRY_BIT`, `lcd_queueFree() >= 72`) after both branches |
| C13 | `menu.c` | restart on Effect-page entry (`old_page != EFFECT_PAGE`), after `menu_activePage` is set |
| C14 | `menu.c` | `menu_setActiveVoice()`: no restart on the Effect page |
| C15 | `menu.c` | `menu_setShownPattern()`: an Effect-page branch restarts and repaints (no Pattern LED update: the SEQ row belongs to the FX sequencer there) |
| C16–C17 | `menuEffects.h/.c` | new `menuEffects_cellSeqLocked()` (any of the 16 steps; lane from `menuEffects_cellLane()`) |

`menuEffects_renderSeqLeds()` was deliberately not changed.

### 5.7 Follow-up: underline ordering on SEQ hold and release (F1–F3)

- **Symptom (hardware):** on a hold, the underlined held-value glyph flashed
  briefly in the name row before appearing in the value row; on release the
  reverse. This is the S066 "Fix 5" defect of the VOICE overlay.
- **Root cause.** A cell keeps the same CGRAM slot whether its underline is
  on the name or the value, so a transition moves the slot and redefines its
  glyph. `va_queueMarkerTransaction()` orders that safely only if
  `currentDisplayBuffer` matches the LCD. The order is:
  1. restore stale cells to plain characters;
  2. define the glyph;
  3. write the frame diff.

  The Effect page redrew every hold transition with `menu_repaintAll()`,
  which fills the shadow with `0x7F`. That skipped step 1, so the old cell
  showed the new glyph until the frame write reached it.
- **Fix:**
  - F1: a new action bit `MENU_FX_ACT_HOLD_REPAINT` (`0x08`) in
    `menuEffects.h`, with the action-bit contract documented;
  - F2: `menuEffects_service()` reports hold start, held-mask change and
    release with it;
  - F3: `menu_serviceRuntimeWidgets()` answers it with `menu_repaint()`,
    which takes precedence over `MENU_FX_ACT_REPAINT`.

  Scene and type changes keep `menu_repaintAll()`. `menu_repaintGeneric()`
  rebuilds both rows in every Effect view, so `menu_repaint()` loses
  nothing. The diff-based write also costs the LCD queue less than the
  forced 64-op frame.
- **Not changed (§11.7 of the schedule):** `menu_repaintAll()` paths that
  can still move a marker slot. These are an encoder click in or out, a
  SELECT press that changes screens, and a Scene/type change while a hold
  stays on screen. Each is inside a full redraw, none has been reported, and
  the VOICE pages share them.

### 5.8 Build and review

- C1–C17: `text=487,384`, RAM unchanged, the 45 B assert compiles.
- F1–F3: `text=487,544`, RAM unchanged. Payload +856 B over S073 for the
  whole fix.
- **Review (schedule §12):** every change matched the schedule. One cosmetic
  deviation remains: the C4 per-field comment sits after
  `va_searchTargetMask[8]` (`menu.c` about 1272–1291), not above
  `va_searchTrack`. No functional defects; the local-63 guard is present;
  hold transitions never reach `menu_repaintAll()`.
- **Hardware:** PASS (user): automated names are underlined and the
  hold/release order is correct. The §8 and §11.6 checks were not reported
  one by one.

### 5.9 Known limitations (not fixed; shared with VOICE)

1. **Live erase while recording** removes automation through
   `patSvc_enqueueErase()` (`sequencer.c` about line 902) without restarting
   the search. Removed targets stay underlined until the next restart.
2. **A deferred Pattern/track clear** (PatternStackService busy): the search
   restarts at once while the pool mutation is queued, so it can re-read a
   step before the clear and keep a stale bit.
3. **No quiet-period debounce** for the Effect held-value marker (S072
   behaviour).
4. **Search latency** (224 passes) was not measured. If it is too slow on
   hardware, raise it with the user; there is no knob.
5. An existing inaccurate comment, not S074 work: `main.c` about line 532
   says the stamped CRCs are "at the end of the load image". They are in
   sector 1, right after the vector table.

---

## 6. Image script fold (user request, same day)

- `tools/stamp_image_check.py` is deleted. `tools/build_lxrv2_img.py` now
  does both jobs: stamp the `.image_check` block, then package the image
  (usage `build_lxrv2_img.py <nm> <elf> <in.bin> <out.img>`).
- **Makefile:** the `.bin` rule is objcopy only; the `img` rule calls the
  script with `nm`, the ELF, the `.bin` and the `.img`.
- **`lxr02.bin` stays raw and unstamped.** The stamped payload exists only
  inside the `.img`. From now on, record the `.img` SHA-256.
- **Failure handling:** the layout checks are unchanged. On any failure the
  script prints the reason to stderr, deletes any previous `.img` and exits
  non-zero. The cosmetic "Image check: stamped at …" line is gone.
- **Verified:**
  - the `.img` is byte-identical to the old two-script output (SHA-256
    `0e004720767220d9ab7fc7589691314c2526e899f0cbd9a903fcffaacd0ddeea`);
  - re-stamping a stamped `.bin` gives the same image;
  - a truncated `.bin` and a missing check block both fail and leave no
    image.
- References updated at the time: comments in the linker script and
  `flashImage.c/h`, plus `README.md`, `MEMORY.md`,
  `STORAGE_SRAM_MANIFEST.md` §3.4–§3.5 and `MODULE_INTERCHANGE_SPEC.md`.

---

## 7. CrumpBit (`cbt`): the first buffer-using Effect

Sources: `S074_CRUMPBIT_EFFECT.md` (specification, draft 3, final for v1)
and `S074_CRUMPBIT_IMPLEMENTATION.md` (line-level schedule and work notes).

### 7.1 Goal and identity

- **Goal (user):** convert the stereo FX send to 8 bits, manipulate the
  bits, and feed a mono 8-bit tape-style delay in the shared DTCM arena. It
  doubles as Phase 5 A8, so it had to close the three arena gaps.
- **Identity:**
  - `token3` `cbt` (permanent `.fx` identity), `abbrev5` `CrmBt`, `full8`
    `CrumpBit`;
  - `EFFECT_TYPE_CRUMPBIT = 2`, `EFFECT_TYPE_COUNT = 3`;
  - `io_flags = EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT`.
- **Folder:** `Core/DSP/Effects/CrumpBit/`, four new files:
  - `CrumpBitParameters.c/.h`: tokens, descriptors, enum, layout, UI hooks;
  - `CrumpBitEffect.c/.h`: runtime, ops, the Rate/Sync helpers.

### 7.2 Signal path

```
send L ─► 8-bit ADC ─► data lines ─► DAC ─► AC ─► hL ───────────► (1−m)·hL + m·gL·wet ─► out L
send R ─► 8-bit ADC ─► data lines ─► DAC ─► AC ─► hR ───────────► (1−m)·hR + m·gR·wet ─► out R
                                                   │                               ▲
                                                   └─► ½(hL+hR) ─► (+) ─► 8-bit ─► tape loop ─┴─► wet
                                                                   ▲    (arena share)       │
                                                                   └──── feedback · wet ◄───┘
```

Every stage runs on every sample whatever its settings (constant CPU). The
delay runs at Mix 0, feedback is computed at 0, and the bit logic runs with
every bit normal.

**ADC (Q1, answered):** the user chose to simulate an 8-bit audio ADC, so
the code is **offset binary**: codes 0..255, 128 = silence.
- `c = clamp(round(x·128) + 128, 0, 255)`. The firmware computes
  `code = (uint32_t)(x·128 + 128.5)` after clamping `x` to ±1, then folds
  256 to 255 with `code − (code >> 8)`.
- Mid-tread, so silence sits exactly on 128. One LSB is 1/128 of full scale.
- Consequence (Q30): with every bit normal the Effect is still an 8-bit
  quantiser, never transparent. A −20 dBFS send uses about ±13 codes, which
  is the intended grit.
- Draft 1 proposed sign-magnitude; that was withdrawn (audio ADCs do not use
  it).

**Data lines (F1, answered):** one pair of masks for both channels.
- off: the line is forced to 0 (`c & ~O`);
- invert: the line is flipped (`c ^ I`, "flip whatever it otherwise would
  be");
- invert wins when both are set: `c' = (c & ~(O & ~I)) ^ I`.

The firmware builds `keep = ~(bit_off & ~bit_invert) & 0xFF` once per block
and computes `code' = (adc & keep) ^ invert` per sample. The DAC decodes
`y = (c' − 128)/128`, range −1 … +127/128.

| Bit | Off | Invert |
|---|---|---|
| 7 (MSB) | every positive sample drops by half scale: heavy DC and half-wave distortion; **silence becomes −1.0** | read as two's complement: each half-wave jumps sides at zero crossings; **silence becomes −1.0** |
| 6..0 (weight 2ⁿ) | coarser steps, about −2ⁿ⁻¹ average DC | signal-correlated ±2ⁿ pattern, near-zero DC; silence becomes +2ⁿ/128 |

**AC coupling (F2, approved):** a one-pole high-pass per channel after the
DAC, about 10 Hz:
- `h[n] = y[n] − y[n−1] + R·h[n−1]`, with `R = 0.9985755`
  (`CRUMPBIT_AC_R`, ≈ 1 − 2π·10/44,108);
- it stands in for a sampler's output coupling capacitor;
- without it the mask DC reaches the outputs, and in the delay feed it builds
  to `DC/(1 − fb)` and pins the loop at a rail;
- it cannot remove the step of a high-bit switch: that decays with τ ≈ 16 ms
  (a thump).

**Tape loop:**
- The feed is `mono = ½(hL + hR)`.
- The write is `adc(mono + fb·wet) − 128`, stored as int8. Re-quantising on
  every pass is the tape character and keeps feedback bounded. The masks are
  **not** re-applied in the loop (Q8).
- The read is at `rp = wp − delay`, wrapped by compare and subtract (the share
  is not a power of two), with linear interpolation (Q10), then × 1/128.
- **Glide (Q6/B8):** each sample, `delay += (target − delay)·k` with
  `k = 1.5113e-4` (τ = 150 ms). A change of Rate, Sync, division or tempo
  bends the pitch like a tape machine changing speed. There is no second tap.
- **Unwritten region:** after `init` the loop content is undefined (boot DTCM)
  or stale. Instead of clearing up to 126 KB in one call, a fill counter
  `valid` counts samples written since the last seat. `wet` is muted unless
  `delay + 2 ≤ valid`: one compare per sample, the same cost whatever the
  state. This is "clear unless you adopt" implemented by masking.

**Rate (Q5, accepted provisionally):**
- `t(r) = 1.60 s · 80^(−r/127)`: 1.60 s at 0, 20 ms at 127, about 20 steps
  per octave of time. Rate 64 ≈ 180 ms.
- Higher rate means faster tape, so a shorter delay.
- `CRUMPBIT_DELAY_MAX_SAMPLES` = 70,573 (1.60 s).
- `CRUMPBIT_BUFFER_BYTES` = 70,592 (1.60 s + a 2-sample guard, rounded to
  32 B), declared as both `buffer_min_bytes` and `buffer_pref_bytes`.
- The range is fixed so that it fits the **minimum** share. A Scene sounds
  the same whatever voice units future instruments claim. No row is
  `BUFFER_DEPENDENT`; the DSP still clamps the target to `length − 2`.
- The rejected alternative (up to 2.87 s with the whole arena) would have
  needed a new `effective_min` op or a row stored as time, because
  `effective_max` clamps a maximum and "higher = faster" makes the limit a
  minimum rate.

**Sync (Q7, answered):** `off`/`on`. When on, the delay is the StepScale
division nearest in log time to `t(r)` at the current tempo.
- The raw 0..127 Rate stays underneath. Pots, encoder, storage, `.fx`,
  AutoSave, FX locks, Pattern automation and LFO all use the raw value.
- Divisions: the shared StepScale table
  (`/64 32t /32 16t /16 /8t 16. /8 /4t /8. /4 /2 1br 2br`, 6..768 ticks at
  96 PPQ).
- `t_d = ticks_d/96 × 60/BPM` from `seq_getBpm()`, which follows the internal
  tempo, MIDI clock and pulse clock (`clockSync.c` and `triggerJacks.c` call
  `seq_setBpm()`). In samples that is `ticks × 27,567.5 / BPM`.
- Only divisions ≤ 1.60 s are candidates: at 120 BPM that is `/64` … `/2`.
  If none fits (below about 4 BPM), the shortest is used and clamped.
- `crumpBit_divisionFor()` walks the 14 divisions, keeps the bracketing pair,
  and picks the lower one when `target² < low·high` (nearest in log time
  without logarithms).
- **Constant cost:** `crumpBit_targetSamples()` computes both the free-running
  and the snapped length every block, then selects by `sync`. A tempo change
  therefore glides the delay.
- **Display (Effect page only):** with Sync on, `rte` shows the division
  label through the new format hook. The STEP automation page, the `.fx`
  file and everything else show the raw number. The label follows tempo
  through the existing live-refresh cadence.

**Mix and delay pan (Q9):**
- `m = mix/127`; `out = h + m·(g·wet − h)`, a linear crossfade.
- Delay pan uses the mixer's balance law, attenuate only:
  `gL = p ≤ 64 ? 1 : (127 − p)/63`, `gR = p ≥ 64 ? 1 : p/64`.
- Mix, feedback (`fb = fbk × 0.99/127`, bounded < 1) and both pan gains ramp
  linearly across the block from the previous block's value. The end-of-block
  values are stored as the exact targets, so ramp rounding never
  accumulates.
- The common `vol`/`pan` rows still scale and place the whole return in the
  mixer.

### 7.3 Arena contract (as implemented)

| Op | Behaviour |
|---|---|
| `init(rt, handoff)` | `length = 0`, `write_pos = 0`, `valid = 0`, `primed = 0`: the first block seats the loop, mutes unwritten samples, and starts the delay on its target (no glide on entry). **Never adopts** in v1 (Q19). |
| `export_handoff(rt, out)` | 1 channel, 8 bits, 44,108 Hz; the write position and integer read position in pointer slot 12 (`FXBUF_HANDOFF_EFFECT_POINTER_BASE`); `FXBUF_STATE_EFFECT_WRITTEN` when `valid > 0`. Positions are share-relative, which is arena-relative (the share starts at the arena bottom). |
| `buffer_changed(rt, share)` | `crumpBit_seat()`: length = min(share bytes, 70,592). If it changed, `write_pos = 0`, `valid = 0` (content with the wrong geometry is muted). |
| `effective_max` | NULL |
| Same-type Scene switch | The runtime stays live (no `init`), so tails ring into the new Scene's settings (F6). The handoff is now refreshed (gap 1). |

`process()` returns early if the share is missing or shorter than
`CRUMPBIT_LOOP_MIN` (4); that cannot happen with the guaranteed minimum share.

### 7.4 Runtime state (`CrumpBitRuntime`, 56 B, asserted)

`length`, `write_pos`, `valid` (u32); `delay`, `mix`, `feedback`, `gain_l`,
`gain_r`, `ac_x_l`, `ac_x_r`, `ac_y_l`, `ac_y_r` (float); `bit_off`,
`bit_invert`, `mix_raw`, `feedback_raw`, `rate`, `sync`, `pan_raw`, `primed`
(u8).

- `write_param()` stores only raw row values. Every conversion runs once per
  block in `process()`, so an LFO that rewrites a row every block costs the
  same as a static setting.
- The union stays 76 B (StereoFilter). The assert in `CrumpBitEffect.c` pins
  56 B, and EffectsManager asserts the union size.
- **Headroom warning:** only 20 B are left in the union. A second tap or a
  tone filter would grow the union and shrink the arena, which needs RAM
  approval.

### 7.5 Constants (`CrumpBitEffect.c`)

| Name | Value | Meaning |
|---|---|---|
| `CRUMPBIT_SAMPLE_RATE_HZ` | 44,108 | time conversions |
| `CRUMPBIT_LSB` | 1/128 | one 8-bit step |
| `CRUMPBIT_RATE_LN_SPAN` | 4.3820266 | ln 80 = ln(1.60/0.020) |
| `CRUMPBIT_SAMPLES_PER_TICK` | 27,567.5 | samples per 96-PPQ tick at 1 BPM |
| `CRUMPBIT_GLIDE_K` | 1.5113e-4 | 150 ms one-pole |
| `CRUMPBIT_AC_R` | 0.9985755 | 10 Hz pole |
| `CRUMPBIT_FEEDBACK_MAX` | 0.99 | feedback at row 127 |
| `CRUMPBIT_LOOP_MIN` | 4 | smallest usable loop |

### 7.6 Parameters

Flags: **M** Morphable, **L** LFO (MODULATABLE, 0..127 domain), **A**
Pattern-automatable.

| Idx | File key (permanent) | Category | Long | Short | dtype | Range | Default | Flags | Lane |
|---:|---|---|---|---|---|---|---:|---|---:|
| 0 | `effect_audio_out` | Effect | AudioOut | `out` | MENU_AUDIO_OUT | 0..5 | 0 | A | — |
| 1 | `effect_level` | Effect | Level | `vol` | 0B127 | 0..127 | 127 | M L A | 7 |
| 2 | `effect_pan` | Effect | Panning | `pan` | PM63 | 0..127 | 64 | M L A | 8 |
| 3 | `crump_bit_off` | Bits | BitOff | `bof` | 0B255 | 0..255 | 0 | — | 1 |
| 4 | `crump_bit_invert` | Bits | BitInv | `biv` | 0B255 | 0..255 | 0 | — | 2 |
| 5 | `crump_mix` | Delay | Mix | `mix` | 0B127 | 0..127 | 40 | M L A | 3 |
| 6 | `crump_feedback` | Delay | Feedback | `fbk` | 0B127 | 0..127 | 48 | M L A | 4 |
| 7 | `crump_rate` | Delay | Rate | `rte` | 0B127 (+ hook) | 0..127 | 64 | M L A | 5 |
| 8 | `crump_subtype` | CrumpBit | SubType | `sub` | 0B127 (+ hook) | 0..0 | 0 (`dly`) | — | — |
| 9 | `crump_sync` | Delay | Sync | `syn` | ON_OFF | 0..1 | 0 | A | 6 |
| 10 | `crump_dly_pan` | Delay | DlyPan | `dpn` | PM63 | 0..127 | 64 | M L A | 9 |

- **Bit masks (rows 3–4):**
  - not Morphable (interpolating a mask gives unrelated bits);
  - not LFO-modulatable;
  - not Pattern-automatable: a Pattern value is 7-bit and cannot reach bit 7
    (Q12, R8);
  - sequenceable through FX lanes 1–2, which store full 8-bit values, so
    each FX step can carry its own bit pattern.
- **No WIDE8 on the masks.** `EFFECT_PARAM_FLAG_WIDE8` only governs Pattern
  automation expansion, which the masks do not use; `max_value` 255 alone
  sets the domain.
- **Sub-type:** one entry (`dly`), `max_value` 0. All 16 `DTYPE_MENU` ids are
  in use, so its label comes from the format hook. Future sub-types add their
  own rows (F4: delay-specific rows now; backward compatibility is not yet a
  constraint).
- **Lanes:** 9 of 15 used; lane 0 is Effect Morph.
- **Pan display quirk (existing, not fixed):** the Effect pan rows store 64
  as centre (the mixer's law), but `DTYPE_PM63` displays `value − 63`, so
  centre shows `1`. The common `pan` row already behaved this way and `dpn`
  follows it. Logged in `SCOPING_TARGETS.md`.

### 7.7 Effect page

**Screen map:**

| SELECT (sub-page) | Screen | Row 0 | Cells |
|---|---|---|---|
| 1 | 0 | `typ out vol pan` | manager and common rows (unchanged) |
| 1 | 1 | `run len scl mrp` | sequence settings, Effect Morph (unchanged) |
| 2 | 0 = **overlay** (home) | `- - - - - - - ->` bit states | `mix fbk rte mrp` values (no names) |
| 2 | 1 | `mix fbk rte sub` | the same rows, then the sub-type |
| 2 | 2 | `syn dpn` | two empty cells |

- The layout is `screen_count = {0, 3, 0, …}`; `custom_row0[1] = 0x01`;
  `home_sub_page = 1`; `home_screen = 0`.
- The encoder walks SELECT 1 s0 → s1 → overlay → page 2 → page 3.
- `mrp` on the overlay is the Scene Effect Morph amount (0..255), resolved by
  the new `EFFECT_LAYOUT_CELL_MORPH` sentinel (Q28).
- **Overlay row 0:** columns 0, 2, …, 14 show bits 0..7, LSB on the left.
  `-` is normal, `0` off, `!` inverted. Column 15 is the ordinary scroll
  marker (`>`).
- On the overlay there is no cursor cue (Q15) and no name underline (Q16).
  Row 1 is the ordinary value row, so pots, encoder, full view, SHIFT and
  held values all work.

**SELECT buttons and LEDs:**

| Gesture | Behaviour |
|---|---|
| SELECT *n* (1..8) | Bit *n−1* cycles normal → off → invert → normal. A stale off+invert pair counts as invert, as the DSP plays it. Written through `menuEffects_editParam()`, which fans out to same-type masked Scenes and marks AutoSave. The page then **jumps to the overlay**: it leaves the full view, abandons an open `typ` browse without committing it (Q17), and repaints with `menu_repaint()`. |
| SELECT *n* during a SEQ hold (Q13, F3) | Writes FX locks on the held steps instead. The screen and LEDs show the **last step held**: its lane 1–2 locks where locked, otherwise the retained masks. The cycled bit produces whole `bit off` and `bit invert` values that go to **every** held step, and both mask lanes are locked on each. All held steps end up equal. |
| SHIFT+SELECT *n* (Q14) | Resets bit *n−1* to normal. |
| SELECT LEDs (Q18) | LED *n* is on when bit *n−1* is off or inverted, always while `cbt` is on the page. The source is the retained masks, or the last held step's masks during a hold. The live per-step playing value is not shown. Re-rendered on every hold transition and after each SELECT edit. |

**Hold source for the whole Effect page (interpretation, schedule §1.4):**
held values now come from the **last step held** for every cell, not only the
masks, so one screen never mixes two steps. If the last step held is released
while others stay down, the highest-numbered remaining step is shown. This
also changed `flt`'s hold seed from the lowest-numbered held step to the last
one held. The last step held lives in spare bits of the existing hold byte
(`MENU_FX_HOLD_ACTIVE 0x80`, `MENU_FX_HOLD_LAST_VALID 0x40`,
`MENU_FX_HOLD_LAST_MASK 0x0F`): 0 B new RAM.

### 7.8 Effect-page framework extensions (Q24, approved)

These are registry-driven. `flt` has NULL layout and hooks, so its behaviour
is unchanged.

1. **`effect_select_layout_t`** (`EffectsManager.h`) gained
   `custom_row0[8]` (bit per screen with a type-painted top row),
   `home_sub_page` and `home_screen`, and the cell sentinel
   `EFFECT_LAYOUT_CELL_MORPH` (`0xFD`).
2. **`effect_ui_hooks_t`** gained:
   - `paint_row0(sub_page, screen, row0)` for columns 0..14 of flagged
     screens;
   - `format_value3(index, value, out)`: nonzero when handled; Effect page
     only (compact cells, full view, held values);
   - `flags` with `EFFECT_UI_FLAG_OWNS_SELECT_LEDS` (`0x01`);
   - return codes `EFFECT_UI_HANDLED` (1) and `EFFECT_UI_SHOW_HOME` (2). Any
     nonzero SELECT return abandons an open `typ` browse.
3. **`menuEffects.c/.h`:**
   - `menuEffects_paintRow0()` and `menuEffects_screenHasCustomRow0()`;
   - the type hook in `menuEffects_formatValue3()`, plus
     `menuEffects_formatParamValue3(cell, value, dst)` for explicit values;
   - `menuEffects_renderSelectLeds(sub_page)`: calls the type's LED hook when
     it owns the SELECT LEDs, else `led_setActiveSelectButton()`;
   - `menuEffects_home()`;
   - `menuEffects_hookSelect()` returns the action and abandons a `typ`
     browse;
   - `menuEffects_editParam()`: `effects_setParameter()` with no hold,
     `effects_setSeqLaneLock()` on every held step during a hold;
   - `menuEffects_shownParam()`: the last held step's lock where locked, else
     the retained value;
   - `menuEffects_liveRefreshWanted()`: nonzero when the active type has a
     `format_value3` hook, so the Sync label follows tempo;
   - `menuEffects_highestStep()`/`menuEffects_heldStep()`, replacing
     `menuEffects_firstHeldStep()`;
   - `menuEffects_leave()` clears owned SELECT LEDs;
   - the packed `holdState` byte.
4. **`menu.c`:**
   - the compact branch paints the custom row 0 after the uppercase and
     scroll marker;
   - the PARAM full view applies the format hook;
   - `menu_applyEffectMarkers()` formats held PARAM values through the hook
     and skips name markers on custom-row-0 screens;
   - new `menu_effectShowHome()` (declared in `menu.h`);
   - `menu_sceneLiveRefreshService()` treats the Effect page as a visible
     case when `menuEffects_liveRefreshWanted()`, reusing the existing
     bounded repaint (`SCENE_LIVE_REFRESH_INTERVAL_MS`, only while the
     transport runs, never while editing or under the screensaver): no new
     state;
   - the three Effect-page `led_setActiveSelectButton()` calls (page entry,
     cursor repair, encoder move) go through
     `menuEffects_renderSelectLeds()`.
5. **`buttonHandler.c`:** the FX SELECT press and SHIFT+SELECT act on
   `EFFECT_UI_SHOW_HOME`, and the LED call goes through the owner check.

### 7.9 Arena gaps closed

1. **Gap 1, same-type handoff refresh (EffectsManager, Stage 3):** handoff
   export was factored into `effects_exportHandoff()` (M5).
   `effects_activateScene()` refreshes it on a same-type switch without
   `init` (M6).
2. **Gap 2, FX return ramp while `off` (`mixer.c`, X1):** an `else` of
   `if (fx_active)` zeroes `mixer_fx_return_last_gain[0..1]` on every `off`
   block. That costs two stores per block. A type that outputs on its first
   block (CrumpBit's AC-coupled 8-bit output is never silent) now fades its
   return in from 0. The active path is bit-identical.
3. **Gap 3, `fxbuf_init()` order (`FxBuffer.c`, B1):**
   `fxbuf_handoffResetAll()` moved directly after `fxbuf_clearOwners()`, so
   diagnostic forced units get valid unit rates. There is no production
   effect.

### 7.10 Change list and stages

- **Stage 0 (gaps 2, 3):** B1 `FxBuffer.c`; X1 `mixer.c`.
- **Stage 1 (listenable on the default page):**
  - N1–N4 (the four new files);
  - E1 (type id and count);
  - M1–M4: includes, the union member, the registry row, and the asserts
    (the union holds CrumpBit and stays 76 B);
  - MK1–MK4 (Makefile): `-ICore/DSP/Effects/CrumpBit`, `CrumpBitParameters.c`
    in `SRCS`, `CrumpBitEffect.c` in `DSP_SRCS`, and an explicit `-Ofast`
    rule. The pattern rule only covers `Core/DSPAudio/`.
- **Stage 2 (page):** E2–E3, H1–H2, P1–P15, U1, C1–C9, BH1–BH2 (§7.8).
- **Stage 3 (contract):** M5–M6 and the minimum-share test.
- The implementation applied all stages as one build. The Stage 2 layout and
  hooks were present in the same build as Stage 1.

### 7.11 Build and verification

- Normal build: `text=499,144`, `data=416`, `bss=426,336`; RAM unchanged
  (ITCM 4,168, DTCM statics 4,448, FX arena 126,624); `effects_runtime` is
  `0x4c` (76 B).
- **Diagnostic build** (`DEV_MODE_DIAGNOSTIC=1`,
  `DEV_FXBUF_FORCE_VOICE_UNITS=12`): links, passes the 76 B runtime guard and
  the minimum-share linker geometry (FXBUF 126,624 B, 3,744 B above
  122,880). Both settings were restored afterwards. The `FxBf` boot-screen
  self-check result is a hardware observation that was not reported.
- `git diff --check` clean; no S074 compiler warnings.
- **Flash was larger than estimated:** +11.6 KB, against a 3–4 KB schedule
  estimate.
  - `crumpBit_process` is 4,804 B, because `-Ofast` unrolls the 32-frame
    loop.
  - `crumpBit_syncDivision` is 3,684 B: the 14-step walk is unrolled and
    `expf` inlined.
  - This costs no RAM or CPU. If flash ever gets tight,
    `#pragma GCC unroll 1` on those loops would trim it, after a listening
    check.

### 7.12 Hardware acceptance and sector 6

- **Accepted (user, 2026-09-29)** as the v1 baseline: "pretty happy with the
  implementation for now". Additions are expected later. The §9 checklist
  items were not reported one by one.
- **The bootloader writes sector 6.** The CrumpBit image was the first to
  pass `0x08080000`: `_eflash_load` was `0x08081F68`, 8,040 B into
  sector 6. The close image reaches `0x08082C90` (11,408 B in).
  - Sector 6 now holds the tail of `.text` (6,312 B, including libm's
    `__exp2f_data`/`__log2f_data`/`__powf_log2_data` and the ITCM veneers),
    the ITCM oscillator load image (4,168 B), `.data` (416 B) and the `.dtcm`
    `squareRootLut` (512 B).
  - Every image from CrumpBit on has booted and played with no
    `Img BAD s:.....6`, so the S073 open question is answered in practice.
    The user has not reported it as a separate check.

### 7.13 Risks recorded in the specification

| # | Risk | Mitigation |
|---|---|---|
| R1 | First image past `0x08080000` | Resolved in practice (§7.12). |
| R2 | High-bit changes are loud (silence moves to a rail) | Output stays within ±1.0; the AC coupling returns it to 0 in tens of ms; test at low volume |
| R3 | Mask DC, even with no input | The AC coupling |
| R4 | Framework contract changes affect every type | `flt` has NULL hooks; regression-checked |
| R5 | Any SELECT LED writer not routed through the owner check would overwrite the bits | The Effect-page call sites are routed |
| R6 | The runtime grows past 76 B | The asserts; RAM approval |
| R7 | The Sync label depends on BPM | The live-refresh cadence while playing |
| R8 | 7-bit Pattern automation cannot reach mask bit 7 | Masks are sequenced through FX lanes only |
| R9 | File keys and the token are permanent once saved | Confirmed before Stage 1 (F4) |
| R10 | Fast LFO on `rte` gives large pitch swings | Intended; the glide time sets the wildness |

---

## 8. Master bus compressor (`cmp cam ctm csc`)

Sources: `S074_BUS_COMP.md` (specification, draft 2) and
`S074_BUS_COMP_IMPLEMENTATION.md` (schedule and work notes).

### 8.1 Goal (user)

- A master bus compressor with a trigger sidechain. It is **not an Effect
  type**: it works on one output pair (ST1 or ST2) and is configured on the
  settings menu (SHIFT+LOAD/SAVE).
- Four controls, **saved per Scene**. Their page is **always the last
  settings page**, marked with the VOICE mix page's Scene-setting cues: `^`
  instead of `>` on the first screen, and `+` on the page itself.
- Its CPU counts towards the worst-case Scene; the user accounts for it
  against the `cpu` widget. **It does no work while `cmp` is off**, the one
  user-approved exception to the constant-CPU rule.
- Character: soft-knee, RMS, optical, LA-2A-like.

### 8.2 Answers to the draft-1 questions (user, 2026-09-29)

1. RAM approved: DTCM state up to 32 B, +64 B SRAM1.
2. It runs only when on, with a one-block fade whenever the target changes.
3. Page placement option (a): a named constant, the "new pages go before it"
   rule, and a diagnostic-build check.
4. No jack fallback: `cmp St2` always processes the DAC2 buffer.
5. Sidechain depth: the trigger's step velocity is the main control
   (velocity 127 always gives the deepest duck); depth also scales with `cam`.
6. Makeup should give roughly the same output at a moderately full input
   across `cam`; the saturation is the cheapest cubic, gentle, growing a
   little with `cam`.
7. Files (`BusCompressor.c/.h`) and file keys approved; no host test file.

**Draft-1 correction:** draft 1 had the buffers reversed. **St1 (DAC1, MAIN
jacks and headphones) is the mixer's `output2`; St2 (DAC2, OUT2) is
`output`.** `mixer_moveDataToOutput()` writes `MIXER_ROUTING_DAC1_*` to
`outL2`/`outR2`.

### 8.3 Placement and routing

- **Stage position:** the end of `mixer_calcNextSampleBlock()`, after every
  voice and the FX return (or its `off` branch) are summed. It runs before the
  DMA pack. Call: `busComp_processBlock(output2, output, scene)`.
- It sees every voice routed to that pair, including the mono routes that land
  in its channels, plus the FX return when routed there.
- **No jack fallback:** with nothing in OUT2, the mixer moves St2 routes to
  DAC1, so `cmp St2` then sees only what is still routed to DAC2. Intended.
- **Stereo link (BC2):** one detector and one gain for both channels.
- **Not an Effect:** no registry row, FX send, lanes or arena use.
- **Trigger timing:** `main.c` drains the trigger queue
  (`voiceControl_processPending()`) immediately before each 32-frame block,
  so a sidechain trigger lands in its voice's own block.

### 8.4 Controls

| Short | Long | Range / display | Default | Meaning |
|---|---|---|---|---|
| `cmp` | `BusComp` | `off` `St1` `St2` | `off` | Which pair is compressed |
| `cam` | `CompAmt` | 0..127 | 48 | Amount macro: threshold, ratio, makeup, drive |
| `ctm` | `CompTime` | 0..127 | 48 | Time macro: mostly release, slightly attack |
| `csc` | `CompSC` | `off` `1`..`6` | `off` | Voice whose triggers duck the bus |

Per Scene (`scene_settings_t::bus_comp[4]`), saved in `sceneset.scg` and
AutoSave, carried by Scene/Bank load/save, changed by a Scene switch. Edits
reach every Scene in the VOICE edit mask (BC15). There is no modulation in v1
(BC16: no Pattern automation, LFO or Morph).

### 8.5 DSP model (`BusCompressor.c`)

Let `a = cam/127`, `t = ctm/127`. The block is `T_b = 32/44,108 Hz =
0.7255 ms` (1,378 Hz). Every one-pole uses the bilinear per-block coefficient
`k(τ) = T_b/(τ + T_b/2)`: one division, within 0.2 % of `1 − e^(−T_b/τ)` for
τ ≥ 5 ms. Constant arguments fold at compile time.

**Detector (feed-forward RMS, BC8):**
- Per sample, it sums `L² + R²` of the pair's **input** (before gain).
- At block end, `ms = sum/64`, normalised by full scale², is smoothed in the
  power domain: `P += k(5 ms)·(ms − P)`. That makes it a true RMS average, so
  a 32-sample window of bass is not read as a peak.
- The next block uses `level_dB = 10·log10(P + 10⁻¹²)`, computed with one
  `log2f` (`3.0103 × log2`). The ε puts the floor at −120 dBFS.
- One-block (0.73 ms) detector lag, so the stage reads and writes the buffer in
  one pass. No look-ahead, no added latency.

**Static curve (soft knee):**
- `over = level − T`, knee width `W = 12 dB`, slope `s = 1 − 1/R`;
- GR = 0 for `over ≤ −6`; `−s·(over + 6)²/24` inside the knee;
  `−s·over` for `over ≥ 6`.
- The wide knee gives the gradual LA-2A onset; the effective ratio rises with
  level.

**`cam` macro (tuning revision 1 values):**
- threshold `T = −3 − 27a` dBFS (−3 … −30);
- ratio `R = 1 + 3a + 4a³` (1:1 … 8:1);
- **makeup `M = −GR(L_ref)·(1 − 0.3a)`**, with `L_ref = −12 dBFS RMS`. It was
  `−GR(L_ref)` before the revision;
- **drive `d = 1 + 0.5a²`** (was `1 + 0.4a²`).

| `cam` | T (dBFS) | R | Static GR at −12 dBFS | Makeup now (before rev. 1) | `d` now (before) | Cubic ceiling |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | −3.0 | 1.00 | 0 | 0 dB | 1.00 | 0 dBFS |
| 48 (default) | −13.2 | 2.35 | −1.2 dB (in the knee) | +1.1 dB (+1.2) | 1.07 (1.06) | −0.6 dBFS |
| 64 | −16.6 | 3.02 | −3.1 dB | +2.7 dB (+3.1) | 1.13 (1.10) | −1.0 dBFS |
| 96 | −23.4 | 5.00 | −9.1 dB | +7.1 dB (+9.1) | 1.29 (1.23) | −2.2 dBFS |
| 127 | −30.0 | 8.00 | −15.8 dB | +11.0 dB (+15.8) | 1.50 (1.40) | −3.5 dBFS (−2.9) |

- At `cam` 0 there is no gain change (1:1, makeup 0); only the sidechain duck
  and the full-scale ceiling remain.
- After revision 1, a −18 dBFS RMS bus leaves at about −17.5 dBFS at
  `cam` 127 (it was −12.8).

**Optical cell and the `ctm` macro:**
- **Fast stage `G_f`** (dB), the main release:
  - attack when the target is below `G_f`: τ = `5 + 10t` ms (5..15; the LA-2A
    is about 10 ms);
  - release: τ = `60 + 540t²` ms (60..600; 137 ms at the default).
- **Memory stage `G_m`**, the T4 cell's second, slow release:
  - charges toward `0.5·G_f` with τ = 300 ms;
  - releases toward it with τ = `500 + 4,500t²` ms (0.5..5 s; 1.1 s at the
    default).
- **Applied reduction:** `min(G_f, G_m)`, the deeper one. Brief peaks release
  fast; after sustained compression up to half the reduction lingers for
  seconds (program dependence).
- Everything runs in the dB domain at block rate; there are no per-sample
  exponentials.

**Sidechain (`csc`, BC9–BC13):**
- **Intake:** `busComp_sidechainTrigger(track, velocity)`, called from the
  trigger funnel `voiceControl_triggerNow()` (`MidiVoiceControl.c`). The
  sequencer, rolls, MIDI and front-panel previews all pass through it.
- **Matching:** `csc` *n* matches track *n*. **Track 7 (index 6, VOICE7, the
  slot-6 alternate sound) counts as voice 6.** This is the BC11 working
  assumption, **not confirmed on hardware**; changing it is one line in
  `busComp_sidechainTrigger()`.
- **Weight:** `(v/127)³`. The largest weight since the last block wins.
  Velocity 0 and `csc off` never light the cell.
- **Depth (tuning revision 1):** `duck = −(2 + 19a)·(v/127)³` dB below the
  static target (was `9 + 9a`). That is 2 dB at `cam` 0, 9.2 dB at the
  default, and 21 dB at `cam` 127 (was 18).

  | Velocity | (v/127)³ | `cam` 0 | `cam` 48 | `cam` 127 |
  |---:|---:|---:|---:|---:|
  | 127 | 1.00 | −2.0 dB | −9.2 dB | −21.0 dB |
  | 100 | 0.49 | −1.0 | −4.5 | −10.3 |
  | 80 | 0.25 | −0.5 | −2.3 | −5.3 |
  | 64 | 0.13 | −0.3 | −1.2 | −2.7 |
  | 30 | 0.01 | −0.0 | −0.1 | −0.3 |

- **Action (BC10):** in the block after the trigger,
  `G_f = min(G_f, GR + duck)`, which then releases through the `ctm`
  ballistics as if a light pulse darkened the cell.
  - Measuring from the static target `GR`, not from `G_f`, keeps velocity 127
    a consistent depth below the program's own compression, and fast repeated
    triggers do not accumulate.
  - Sustained triggering charges the memory stage, like real optical pumping.
- The duck ramps over one block (about 0.7 ms, BC12), with no click.
- **With `cmp` off** the sidechain has no effect (BC13). Pending weight is
  discarded every off block, so enabling later never produces a stale duck.
- The `cam` depth is applied at block time, so a `cam` edit between trigger
  and block is honoured.

**Gain:** `g = 2^((min(G_f, G_m) + M)·0.1660964)`, one `exp2f` per block,
ramped linearly per sample from the previous block's `g`.

**Saturation, original (answer 6; replaced in §9):**
- `u = clamp(d·y/1.5, −1, 1)`, `out = 1.5/d·(u − u³/3)`;
- unity small-signal gain; ceiling `1/d`;
- `d/1.5` was folded into the ramped gain; `1.5/d` and the int32 scale into
  one output constant, taken from the current block for both so the
  small-signal gain stays continuous when `cam` changes.
- Gentleness at `d = 1`: −6 dBFS loses 0.33 dB and full scale 1.39 dB.

**Transitions (BC4, §4.6 of the specification):**
- **Turning on (`off` → St1/St2):** the cell is seeded as if the bus sat at
  `L_ref`: `P = L_ref` (0.0631), `G_f = GR(L_ref)`, `G_m = 0`, ramp origin
  `g = 1`, crossover state 0. It crossfades dry → wet over one block, so it
  settles from the reference steady state with no makeup-driven jump.
- **Turning off:** wet → dry over one block. From the next block, no work.
- **St1 ↔ St2:** block N fades the old pair out; block N+1 fades the new pair
  in, reseeded. Neither pair steps (this supersedes the draft-1 default of
  stepping the old pair).
- **Scene switch with the same target:** the cell continues and the gain
  ramps, so `cam`/`ctm` changes are smooth.
- **The dry path is exact:** `float(int32)` is exact up to ±2²⁴, above the
  24-bit pack range.

**Constants:** `BUS_COMP_FULL_SCALE` 8,388,352; `BUS_COMP_KNEE_DB` 12;
`BUS_COMP_REF_LEVEL_DB` −12 (`BUS_COMP_REF_POWER` 0.06309573);
`BUS_COMP_RMS_MS` 5; `BUS_COMP_CHARGE_MS` 300; `BUS_COMP_MEMORY_SHARE` 0.5;
`BUS_COMP_SAT_SPAN` 1.5; `BUS_COMP_POWER_FLOOR` 1e-12.

### 8.6 Storage and RAM

| Item | Change | RAM |
|---|---|---|
| `scene_settings_t` | `uint8_t bus_comp[4]` in `scene_bus_comp_field_t` order (MODE, AMOUNT, TIME, SIDECHAIN) | 41 → 45 B; `scene_t` 1,622 → 1,626 B: **+64 B SRAM1** |
| SceneData | `SCENE_BUS_COMP_*` constants (MODE_OFF/ST1/ST2 0..2, SIDECHAIN_OFF 0, defaults 48/48), `scene_busCompClamp()` (mode 0..2, amount/time 0..127, sidechain 0..6), change-aware setter with AutoSave mark, getter, defaults in `scene_initAll()` | — |
| AutoSave | Scene parameters 41–44 (`AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE = 41`, `AUTOSAVE_SCENE_PARAM_COUNT = 45`, `AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES 45`), getter and reader branches, group asserts. **No format-version change**: the S072 Effect Morph cell set the precedent. | 0 B; the record stays 34,768 B |
| `sceneset.scg` | `bus_comp_mode`, `bus_comp_amount`, `bus_comp_time`, `bus_comp_sidechain`, one line each (writer lines 11–14), from one shared key table (`storage_busCompKey()`); parsed values are field-clamped; missing keys keep the defaults (set in both filesystem stage-default paths and the empty-Scene path) | file only |
| Page mirrors | `PAR_BUS_COMP_MODE..SIDECHAIN` = ids **58–61** in `ParameterArray.h`; `NUM_PARAMS` stays 384 | 0 B |
| Runtime | `bus_comp_state_t`: power, fast/memory GR, previous gain, pending sidechain weight, active pair: 24 B; 32 B after the saturator | DTCM `.dtcmz`; the arena shrank 32 B to 126,592 B |
| Typed load stage | `fs_stage_workspace` assert sum 2,005 → 2,009 of 2,048 B | 0 B |

**Old AutoSave records (R8):** the four cells were reserved zeros, so the
first restore after upgrading reads `off`, `cam 0`, `ctm 0`, `csc off`, not
the 48/48 defaults. That is safe (`cmp` stays off); set the values once and
they persist.

### 8.7 Settings menu

- **Placement (BC18 = a):** sub-page 2 of `MENU_MIDI_PAGE`, first half
  `cmp cam ctm csc`, second half empty. It follows the page ending with the
  Pattern allocation indicator (`pts`). `MENU_GLOBAL_SCENE_SUBPAGE = 2u`
  (`menu.h`, asserted `< NUM_SUB_PAGES`) marks it. **Rule: new global pages
  go before it, and the constant moves with them.**
- **Diagnostic check (BC18):** under `DEV_MODE_DIAGNOSTIC`, a boot check
  verifies that the page is the compressor page, its second half is empty, and
  no later global sub-page is populated. It is silent on success. On failure
  it shows `not last (BC18)` for 1.5 s. Production builds compile it out.
- **Navigation:** unchanged. The encoder reaches the page after `pts` and
  stops at `csc`; SELECT 3 opens it; SELECT again loops back.
- **Cues (`checkScrollSign()`):**

  | Screen | Before | After |
  |---|---|---|
  | Sub-page 0 first half | `>` | `^` (a Scene page follows at the end) |
  | Middle screens | `*` | `*` |
  | Sub-page 1 second half (`… pts`) | `<` | `*` (automatic: sub-page 2 is populated) |
  | Compressor page | — | `+` (Scene-owned, never `<`) |

- **Commit path:** a dedicated branch in `menu_cellCommitValue()`. It clamps,
  sends the value to every edit-masked Scene through the Preset setter (which
  writes SceneData with AutoSave marking), then refreshes the mirror.
  - **It never uses `menu_parseGlobalParam()` or the settings.cfg dirty
    mark.** The Global bulk apply (`menu_sendAllGlobals()`,
    `menu_tickGlobalApply()`) replays every id from `PAR_BEGINNING_OF_GLOBALS`
    after a Settings Load. The legacy binary and stale-globals paths
    (`filesystem_resetGlobalsToDefaults()`) zero those bytes first.
  - So in `menu_parseGlobalParam()` the four ids **only refresh the mirrors
    from the active Scene**, and a Settings Load can never write the Scenes
    (E10).
- **Value text:** `cmp` (`off St1 St2`) and `csc` (`off 1..6`) are special
  cases in `menu_formatCellValue3()` and the full view, because all 16
  `DTYPE_MENU` ids and all 16 dtype codes are taken. `cam`/`ctm` are plain
  0..127. The full view shows category `Scene` and the long names.
  `NUM_NAMES` is 99 after the new name rows.
- **Scene switch while on the page:** `preset_applySceneSettings()` refreshes
  the mirrors and the page repaints.
- No meter or gain-reduction display (not requested).

### 8.8 Files and change list (schedule IDs)

- New: `Core/DSPAudio/BusCompressor.c/.h` (N1–N2), `-Ofast` through
  `DSP_SRCS` (K1).
- DSP wiring: `mixer.c` include and call (X1–X2); `MidiVoiceControl.c`
  include and intake (V1–V2).
- Scene data: `SceneData.h/.c` (S1–S5).
- AutoSave: `Autosave.h/.c` (A1–A7).
- Storage: `storageTypes.h/.c` (F1–F4), `filesystem.c` (F5–F7).
- Preset: `presetManager.h/.c` (P1–P3: setter with mirror, mirror sync,
  Scene-apply refresh, order assert); `ParameterArray.h` (M1).
- Menu: `menu.h` (constant; text, category, long and short ids), `MenuText.h`
  (labels), `menuPages.h` (the sub-page 2 row), `menu.c` (E1–E13: dtype
  entries, `valueNames` rows, prototypes, commit branch, compact text, clamp,
  cues, full-view text, helpers, bulk-apply guard, BC18 check, mirror
  defaults, check call).

### 8.9 Build and arithmetic checks

- `text=501,920`, `bss=426,384`, DTCM statics 4,472 B, FXBUF 126,592 B at
  `0x20001180` (3,712 B margin); `busComp` `0x18` bytes in DTCM;
  `effects_runtime` still 76 B.
- The diagnostic build links with BC18 compiled.
- Arithmetic at the time (before revision 1):
  - makeup at `cam` 48/64/96/127 = +1.24/+3.14/+9.13/+15.75 dB;
  - default duck at velocity 127 = −12.40 dB;
  - `k(5 ms)` = 0.13528, `k(60 ms)` = 0.01202;
  - saturator at `d = 1`: −6 dBFS → −0.33 dB, 0 dBFS → −1.39 dB; at
    `d = 1.4` the ceiling was −2.92 dBFS.

### 8.10 Hardware result and tuning revision 1 (user, 2026-09-29)

- "Both the compressor and the `xfd` fader mode changes are in and check out
  ok, both work."
- **Requested changes** (made directly in `BusCompressor.c` with adjacent
  comment blocks):
  - the sidechain cut-in slightly greater at maximum `cam` and velocity;
  - *just slightly* more saturation at maximum `cam`;
  - *less* volume increase at maximum `cam`.
  - Everything smooth across the `cam` range, from extremely mild at low
    values to fairly extreme at the top, with volume tapered down more.
- **Implemented:** depth `9 + 9a` → `2 + 19a`; drive `0.4a²` → `0.5a²`;
  makeup × `(1 − 0.3a)` (§8.5 tables). The tuning is in `2ec72d6`.
- Not reported individually: the H1–H19 matrix of the schedule. The notable
  unconfirmed items are H12 (track 7 as voice 6, BC11) and H18 (the `cpu`
  widget with `cmp` on against off).

### 8.11 Decisions (BC1–BC21)

| # | Decision |
|---|---|
| BC1 | No jack fallback |
| BC2 | One linked detector and gain |
| BC3 | No work while `off`; the cost counts toward the worst-case Scene |
| BC4 | One-block fades; St1 ↔ St2 fades out, then in |
| BC5 | `cam` curves: threshold to −30 dBFS, makeup at the −12 dBFS reference; the ceiling stays at `cam` 0 |
| BC6 | `ctm` curves as §8.5 |
| BC7 | Cubic saturator, the cheapest (later band-split, §9) |
| BC8 | Detector `½(L² + R²)`, 5 ms smoothing |
| BC9 | Sidechain depth `(v/127)³ × (9 + 9a)` dB, later `(2 + 19a)` |
| BC10 | The trigger lights the same cell and releases with `ctm` |
| BC11 | Track 7 counts as voice 6: **working assumption, unconfirmed** |
| BC12 | One-block duck ramp |
| BC13 | `cmp off`: sidechain has no effect |
| BC14 | RAM: +64 B SRAM1, DTCM up to 32 B |
| BC15 | Fan-out to every Scene in the VOICE edit mask |
| BC16 | No modulation in v1 |
| BC17 | Labels `cmp cam ctm csc`; `BusComp CompAmt CompTime CompSC`; category `Scene` |
| BC18 | "Always last": the constant, the rule, the diagnostic check |
| BC19 | Defaults `off`, 48, 48, `off` |
| BC20 | New files `BusCompressor.c/.h` only |
| BC21 | File keys `bus_comp_mode/amount/time/sidechain` |

### 8.12 Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Makeup drives the pair into the pack's hard clip | The saturation ceiling (now the full-scale knee, §9) |
| R2 | Pumping on sustained low end | Intended; the 5 ms RMS smoothing removes per-block ripple. A sidechain high-pass is a possible later option |
| R3 | Up to +11 dB makeup lifts quiet material at high `cam` | Nature of heavy levelling |
| R4 | A duck on St2 with nothing in OUT2 has no audible effect | User decision (no fallback) |
| R6 | "Always last" breaks when a global page is added | The constant, rule and check |
| R7 | CPU while on | Counted against the worst-case Scene |
| R8 | First restore after the upgrade reads `cam 0`, `ctm 0` | Safe; set once |

### 8.13 Observation O1 (existing behaviour, not changed)

- **The Global bulk apply replays PERF Scene mirrors into edit-masked
  Scenes.** `menu_tickGlobalApply()` and `menu_sendAllGlobals()` call
  `menu_parseGlobalParam()` for every id from `PAR_BEGINNING_OF_GLOBALS` up,
  after a Settings Load (`PRESET_OP_GLOBALS_LOAD`) or a legacy `.all` load.
- That range includes `PAR_VOICE1_MORPH..PAR_VOICE6_MORPH` and
  `PAR_VOICE_DECIMATION_ALL`, whose cases send the mirrored active-Scene
  value to every Scene in the VOICE edit mask, marking AutoSave.
- **Effect:** a Settings Load equalises per-voice Morph and `srt` across the
  mask. On the legacy paths the mirrors are zeroed first, so `srt` 0 would be
  written.
- The compressor avoids this by design (E10). A fix for the existing ids
  could use the same refresh-only pattern. Not verified on hardware; logged in
  `SCOPING_TARGETS.md`.

---

## 9. Bus compressor saturation update (`S074_COMP_SAT_UPDATE.md`)

### 9.1 Request and diagnosis

- **Request (user, 2026-09-30):** at high `cam` the saturation sounds harsh
  on high-frequency content such as hi-hats. The amount is not necessarily
  the problem. Could a softer algorithm be used? "A *teensy tiny* bit more
  CPU" is acceptable.
- **Cause: aliasing and intermodulation, not the amount.**
  - The cubic runs at 44.1 kHz with no oversampling and produces a 3rd
    harmonic. For content above `fs/6 ≈ 7.35 kHz` that harmonic is above
    Nyquist and **folds back inharmonically**. A 10 kHz hat partial folds to
    14.1 kHz, a 12 kHz partial to 8.1 kHz.
  - Hats also ride on the kick's waveform through the same curve, so the kick
    modulates them (intermodulation sidebands in the treble).
  - High `cam` makes this worse: more drive, and makeup lifts quiet hats
    between kicks.
  - Low-frequency content (kick, bass, snare body) makes harmonics below
    Nyquist: the wanted warmth.

### 9.2 Measurements (Python models of the firmware formula, fs 44,108)

**Single tones at `cam` 127 (`d = 1.5`):**

| Tone, level | Folds to | Cubic | ADAA-1 cubic | Split, 35 % at 2.5 kHz | **Split, 25 % at 2.0 kHz** |
|---|---|---|---|---|---|
| 10 kHz, −2 dBFS | 14.1 kHz | −24.2 dBc (fund. −1.5 dB) | −41.4 dBc (fund. −3.5 dB) | −43.6 dBc (fund. −0.2 dB) | **−49.9 dBc** (fund. −0.1 dB) |
| 12 kHz, −4.4 dBFS | 8.1 kHz | −29.6 dBc | −47.1 dBc (fund. −4.2 dB) | −49.3 dBc | **−56.0 dBc** |
| 4 kHz, −2 dBFS | 12 kHz (true harmonic) | −24.2 dBc | −25.8 dBc | −36.5 dBc | −41.5 dBc |
| 100 Hz, −2 dBFS | 300 Hz (true) | −24.2 dBc | −24.2 dBc | −24.2 dBc (unchanged) | −24.2 dBc (unchanged) |

**Mixes** (kick 60 Hz; hats four partials 7.1–13.9 kHz). The measure is all
distortion, alias and IMD energy in 2–20 kHz relative to hat energy, with the
fundamentals excluded. The split columns include the knee.

| Case | Cubic | Split 35 % / 2.5 kHz | **Split 25 % / 2.0 kHz** | Hat level change | Output peak (cubic → split 25 %) |
|---|---|---|---|---|---|
| `cam` 127, kick −7 dBFS + hats | −19.9 dB | −27.9 dB | **−30.4 dB** | +0.7 dB | 0.66 → 0.82 FS |
| `cam` 127, hat-heavy | −22.0 dB | −32.7 dB | **−35.5 dB** | +0.8 dB | 0.67 → 0.94 FS |
| `cam` 48, kick + hats | −26.4 dB | −34.0 dB | **−36.4 dB** | +0.4 dB | 0.78 → 0.85 FS |

The split removes most harsh products and leaves low-end saturation exactly
as it was. Hats come out 0.4–0.8 dB louder because their peaks are no longer
rounded; that is the audible face of "less harsh". Peaks rise towards full
scale because the bypassed highs skip the `1/d` ceiling, hence the knee.
Without it the hat-heavy case peaks at 0.97 FS.

### 9.3 Options considered

| Option | Result | Verdict |
|---|---|---|
| **A. Band-split saturation + full-scale knee** | −10 to −14 dB on mixes; −26 dB on the folded 3rd harmonic; hats 0.4–0.8 dB brighter; +8 B | **Chosen; implemented** |
| B. First-order ADAA on the cubic | −17 dB on the folded harmonic, but a two-tap average dulls the whole bus (−2.4 dB at 10 kHz, −6 dB at 15 kHz), half-sample delay, an ill-conditioning branch, two divisions | Rejected: dulls the hats |
| C. Pre/de-emphasis around the cubic | −9 to −18 dB, but the post-boost re-amplifies high distortion; needs a knee; +16 B (over the 32 B approval) | Worse than A |
| D. 2× oversampling (polyphase half-band) | Removes aliasing properly; +150–250 cycles/frame, +100–200 B, latency, band-edge ripple | Rejected: far beyond "teensy" |
| E. A smoother curve alone (C2 quintic, `tanh`) | Below the clamp the cubic already makes only a 3rd harmonic; any smooth curve still makes it | Does not address the cause |

### 9.4 As implemented (per channel, per sample)

```
L   = LP(x)                 one-pole, fc 2.0 kHz, k = 1 − exp(−2π·2000/fs) = 0.2479 (compile-time)
H   = x − L                 exact complement: L + H = x
u   = clamp((L + α·H)·gk, −1, 1)                α = 0.25
wet = u·(c_out + c_out3·u²) + H·g_hf            g_hf = gk·c_out·(1 − α) = (1 − α)·g in int units
wet = knee(wet)             C1 quadratic, identity below 0.75 FS, full scale at 1.25 FS input
out = x + w·(wet − x)       unchanged one-block fades
```

- **Transparent when clean:** where the cubic is linear,
  `wet = (L + αH + (1 − α)H)·g = x·g`.
- **Unchanged:** the detector (still on the input `x`), cell, makeup,
  sidechain and `cam`/`ctm` curves.
- **Knee (`busComp_ceilingKnee()`)**, with `T0 = 0.75·FS`, `C = FS`:
  `a = |v|`, `over = min(max(a − T0, 0), 2(C − T0))`,
  `a = min(a − over²/(4(C − T0)), C)`, then restore the sign with `copysignf`.
  It is linear up to −2.5 dBFS, rounds to full scale with zero slope at
  1.25 FS input, and holds full scale above.
  - It is branchless, per the constant-CPU rule: `copysignf` compiles to
    `vabs` plus a predicated `vneglt`.
  - It restores the "never reaches the pack's hard clip" guarantee, now at
    full scale; the low band is still held by the cubic's `1/d` ceiling.
- **Constants:** `BUS_COMP_SPLIT_HZ` 2,000, `BUS_COMP_SPLIT_COEF` (folded),
  `BUS_COMP_HF_SAT_SHARE` 0.25, `BUS_COMP_CEIL_START` 0.75. They are distinct
  from `BUS_COMP_KNEE_DB`, the compressor's static-curve knee.
- **State:** `float split_lp[2]` in `bus_comp_state_t` (placed before
  `active`): +8 B, 24 → **32 B DTCM**, the approved ceiling (the
  `_Static_assert(<= 32)` holds). The fade-in seed zeroes it; the low-pass
  settles in about 4 samples, inside the dry → wet fade. `_edtcmz` moved
  `0x20001178` → `0x20001180`, which is already 32-byte aligned, so the arena
  stayed 126,592 B.
- `BusCompressor.h` contract comment updated (comment only).

### 9.5 Build, codegen and host replica (2026-09-30)

- No warnings from `BusCompressor.c`. `busComp.lto_priv.0` is `0x20` (32 B).
  DTCM statics 4,472 → 4,480 B; `bss` 426,384 → 426,392. Standalone
  `BusCompressor.o` `.text` grew 1,104 → 1,332 B.
- Per-block library calls are still only `log2f` and `exp2f`; no `expf`
  (checked in the disassembly).
- **Per-sample loop 34 → 71 instructions per stereo frame** (63 floating
  point). The only branch in the loop is its back-edge.
- **CPU estimate revised:** +37 instructions × 44,108 frames/s ≈ 1.6 M
  instructions/s, **about +0.6–0.8 %** of 216 MHz. The planning estimate was
  +0.45 %; the excess comes from the knee's sign handling and register
  copies around the FMAs. The compressor is now about 1.4–1.8 % while on and
  0 while off, constant.
- **Host replica** (`scratchpad/fwreplica.py`, exact firmware formulas in
  int32 units):
  - a clean −40 dBFS tone at 100 Hz, 2 kHz and 10 kHz with `g = 1`, `d = 1`
    matches `g·x` within −97 to −121 dB;
  - with `g = 3.55` (+11 dB) and `d = 1.5` it is within −68 to −92 dB (the
    cubic's normal low-level curvature on the low band);
  - an overdriven 9 kHz peak at 3 FS plus 60 Hz at 1 FS reaches
    max |out| = **1.000 FS**: the knee holds.
- If the cost ever matters: the knee could start at 0.85 FS at the same cost,
  or be dropped at the price of the guarantee. Neither is recommended; read
  the `cpu` widget first.

### 9.6 Acceptance (user, 2026-09-30)

"Saturator seems better; let's leave it like that for now." The constants
stay at their starting values (2 kHz, α 0.25, knee from 0.75 FS). Not done:
an A/B of α = 0.35 (a one-line change; tested in simulation at −8 to −11 dB,
it keeps more snare and cymbal bite) and a `cpu` widget reading.

---

## 10. Fourth fader mode `xfd` (`S074_FADER_XFD.md`)

### 10.1 Request (user)

"A fourth fader mode in addition to `pre`, `pst` and `fx`, labelled `xfd`":

- **Fader at the bottom:** the voice goes to the FX bus as `fx` mode does
  with its fader at maximum (`send × 1`), and nothing goes to the normal
  output, whatever the volume.
- **Moving up:** an ordinary volume fader for the dry output. At the top the
  voice reaches its output at normal volume, and nothing goes to the FX bus.
- The send uses the inverted log scale: moving the fader down in `xfd` raises
  the send exactly as moving it up does in `fx`.

The user also said, mid-turn: "do not change code files yourself this turn,
just write the full implementation schedule". An implementing agent applied
the schedule afterwards.

### 10.2 Rule in the mixer's terms

| Mode | `F_mix` (dry = vol × F_mix) | `F_send` (send = send × F_send) |
|---|---|---|
| `pre` (0) | fader | fader |
| `pst` (1) | fader | 1 |
| `fx` (2) | 1 | fader |
| **`xfd` (3)** | **fader** | **fader′ = taper(1 − x)** |

`fader` = `slider_vol[slot]` = `taper(x)`, where `x` is the deadzone-normalised
position and `taper()` is the 30 dB curve (`SLIDER_LOG_TAPER_DB`) baked into
the slider LUT.

### 10.3 The mirrored gain without a new table or RAM

`slider_raw_to_float()` builds `g = (R − m)/(1 − m)` with
`R = 10^((x − 1)·D/20)` and `m = 10^(−D/20)`. Solving for the other end:

```
P  = m + g·(1 − m)          (= R)
g′ = m·(1 − P) / (P·(1 − m))
```

- One division, no logarithm. `powf(10, −D/20)` folds to a constant at `-O2`
  and `-Ofast`; the linked `adc_sliderGainMirrored` has one `vdiv.f32` and no
  `powf` call.
- It matches `taper(1 − x)` to 5.6 × 10⁻¹⁶ over the whole range. A linear
  taper (`D ≤ 0`) gives `1 − g`.
- Guards make the endpoints exact: `g ≤ 0 → 1`, `g ≥ 1 → 0`.
- It lives in `adcPots.c/.h`, beside the taper, so it follows any change to
  `SLIDER_LOG_TAPER_DB`.

| Fader position | Dry (× volume) | Send (× FX send) |
|---:|---:|---:|
| 0 (bottom) | 0 | 1.0 (0 dB) |
| 0.25 | −27.0 dB | −7.9 dB |
| 0.5 | −16.4 dB | −16.4 dB |
| 0.75 | −7.9 dB | −27.0 dB |
| 1 (top) | 1.0 (0 dB) | 0 |

**The middle dips:** both taps are at −16.4 dB at centre, because each
follows the log taper as specified. A constant-power crossfade would put both
at about −3 dB; that would be a one-line change of the `xfd` branch if ever
wanted.

### 10.4 Unchanged

- The send tap stays pre-volume and the dry tap post-volume.
- The FX-send step override still applies (`preset_getEffectiveFxSendAmount()`).
- Mode changes and fader moves ramp through the existing per-block gain ramps.
- Fader modes are not automatable.
- **Storage:** the same byte `fader_setting[slot]`, now 0..3. AutoSave needs
  no change: its reader goes through the clamping setter. `sceneset.scg`
  keeps the key. **Older firmware rejects a Scene with `3`**: a value above
  the domain rejects the file (downgrade only).
- **CPU:** one division per `xfd` slot per block; otherwise it costs the same
  as `fx`. With an Effect active, the one-pass dry + send path runs at every
  position except the very top, where the send is exactly 0. The worst case
  is unchanged. RAM 0 B.

### 10.5 Changes

| ID | File | What |
|---|---|---|
| D1–D2 | `adcPots.h/.c` | `adc_sliderGainMirrored(g)` declaration and definition |
| X1 | `mixer.h` | fader-mode comment; `MIXER_FADER_XFD 3u` |
| X2 | `mixer.c` | `mixer_faderGains()` `xfd` branch; `_Static_assert` tying `MIXER_FADER_XFD` to `SCENE_FADER_SETTING_MAX` |
| S1–S4 | `SceneData.h/.c` | `SCENE_FADER_SETTING_MAX 3u`; setter and getter clamp 0..3 |
| P1–P2 | `presetManager.c/.h` | setter clamp; a stale comment corrected |
| F1 | `storageTypes.c` | `fader_setting` parse domain 0..`SCENE_FADER_SETTING_MAX` |
| M1–M2 | `menu.c` | clamp 0..3; the `xfd` label in `menu_sceneSettingFaderName()` |

Unchanged and checked: the AutoSave getter and reader, the Scene writer
(writes the stored byte), the menu dtype (`DTYPE_0B15` with the M1 clamp),
`tools/verify_bank_autosave.py`, `tools/dsp_test` (and its `frozen/` copy).

### 10.6 Build and hardware

- `text=502,072`, RAM unchanged (`bss=426,384`, DTCM statics 4,472, FX arena
  126,592); image SHA-256 `d6c8538822ae…465d`.
- A final source audit found no remaining 0..2 clamp; every path uses
  `SCENE_FADER_SETTING_MAX`.
- **Hardware (user):** "works". The H1–H9 matrix was not reported
  individually.

---

## 11. AutoSave: torn record stops publication (investigation and fix)

Sources: `S074_AUTOSAVE_BOOT_BUG.md` (investigation, no code) and
`S074_ATS_BUG_IMPLEMENTATION.md` (fix schedule, implementation notes,
hardware result).

### 11.1 Report (user, 2026-09-29)

- AutoSave did not take the Effect type change into account, especially with
  the new Effect types.
- After a Bank save there seemed to be no AutoSave record, and boot loaded
  directly from the Bank.
- One boot failed to finish in time (boot timeout).
- The card was copied to `SD_CARD_ATS_BOOT_BUG/`.

### 11.2 Evidence (card copy)

| Item | Finding |
|---|---|
| `.hcprms2` (B) | 34,768 B, `HCPR` v3, commit `0xA5`, **generation 100**, CRC `0x1ACD34EB` = computed. Valid. Bank slot **25** `NoBankMd`. |
| `.hcprms1` (A) | **65,536 B** (2 × 32 KiB clusters). Header v3, commit **0x00**, generation **101**, CRC 0 (placeholder). Bytes 34,768..65,535 are 30,768 B of stale cluster data (15,845 nonzero, starting `ffffffff ffffff0f ffffff0f …`). A torn, uncommitted publication. |
| `settings.cfg` | `active_bank=27` (`/Bank/027 NoBankOk`) |
| `.pat06b` | 32,768 B instead of 10,656 B: the same signature; peer `.pat06a` intact; harmless |
| Other files | none with a cluster-multiple size |
| `asavetrc.bin` | 461,512 B = 57,689 records over 20 boot sessions S00..S19; the ring dropped 13,000–28,000 records per session (`G` records) |
| `/bootlog.bin` | absent |

**Trace timeline:**

| Sessions | Drains admitted / published | Boot `V` | Boot reader | Notes |
|---|---|---|---|---|
| S00–S08 | 101 / 97 (a drain with nothing new completes without publishing); generations 2 → **100** | winner A or B, `0x01`/`0x03` | no reloads, about 1.2 s | healthy; gen 100 → B at `#023512` (S08, t = 42.8 s) |
| S09 | 0 / 0 (writer armed, deadline 8.2 s) | B gen 100 | no reloads | short session, 5–8 s |
| S10–S16 | 642 / 0 | B gen 100, `0x03` (Bank match) | no reloads, 1.5–1.7 s | every drain stalls in phase 3; Effect type changes to Scene 13 (S14, S15) marked dirty but never published; S16 Save:[Bank] to slot 27 |
| S17–S19 | 21 / 0 | B gen 100, **`0x07`** (Bank mismatch: B is slot 25, `active_bank` 27) | **Case 2: all 16 Scenes, 144 rows, reloaded from Bank 27**; done at 4.6–4.8 s | still stalling |

A failing drain always looked like: `A` admitted → `X` PHASE_STALL site 2
(runtime drain), phase 3, slot 0 → `E` OPERATION_ERROR (op_phase 41) → `T`
ERROR, rearmed about 5 s later. The stall came 2.6–5.9 s after admission, at
about 30,000 polls. No `V` record ever appeared: A was never fully validated.
While the scalar drain spun, the Pattern drain was starved (`H` class=pattern
denied 2,384 and 4,406 in 5 s windows). From S10: **663 drains admitted, 662
aborted, 1 cut by power-off, 0 published**, all silent.

**Where the tear happened (most likely):** in S08 the user edited Scene 13's
`fdr0` until t = 42.1 s. Generation 100 was published at 42.8 s with the mask
still dirty. That armed the 250 ms continuation drain, which skips validation
(the cached winner) and writes generation 101 straight into A. The next trace
record (`#023514`) is already a new boot, so the continuation drain's records
were still in the RAM ring when power went, and A was left open for writing.
The short S09 session's post-boot drain is the other candidate; the trace
cannot tell them apart. Either way it is an ordinary event (power removed, a
reset or a firmware update within a second or two of an edit), and the design
must survive it.

### 11.3 Root cause chain

1. **AsyncFATFS keeps the allocated size while a file is open for write.**
   `afatfs_saveDirectoryEntry()` in `AFATFS_SAVE_DIRECTORY_NORMAL` mode
   (`asyncfatfs.c` about 1875–1881) stores `physicalSize`, which is
   cluster-rounded (32 KiB clusters). The source comment says "We exaggerate
   the length of the written file so that if power is lost, the end of the
   file will still be readable". Only `AFATFS_SAVE_DIRECTORY_FOR_CLOSE`
   writes the true size.
   - The drain publishes by removing the inactive record, creating it with
     `"w"` (phases 11/24) and streaming 34,768 B (phase 13). It then closes,
     syncs, reopens `"r+"`, and writes the CRC and finally the commit byte
     (phases 17–22).
   - A power loss in phase 13 or before that close leaves the target at the
     rounded size: 65,536 B for an HCPR record, 32,768 B for PAT4.
   - **That alone is not the bug.** The torn record correctly fails
     validation (commit 0) and the peer stays authoritative.
2. **The defect:** both AutoSave validators (the drain's phase 3 and the boot
   validator's phase 3, identical code) read past the record **one byte per
   poll until EOF**. The first byte past `AUTOSAVE_RECORD_BYTES` already
   invalidates the candidate (`autosave_streamValidationUpdate()`), so the
   other 30,767 reads proved nothing.
   - Cost for A: 272 polls of 128 B (`AUTOSAVE_CRC_BYTES_PER_TICK`) plus
     30,768 single-byte polls = **31,040 polls in one phase**.
   - The Pattern validator (`filesystem_patternAutosaveCandidateValid()`)
     already did it right: one byte, then EOF.
3. **The DEV stall observer counted polls in a phase, not polls without
   progress** (`filesystem_pollPhaseStall()`, threshold 30,000, named code
   `DrSt`). Phase 3 legitimately loops once per poll while streaming, so the
   drain aborted about 1,040 polls short of EOF, every time. With
   `DEV_STALL_DETECTION 0` the drain would have taken about 6 s once, rejected
   A and repaired it. The development build turned a slow path into a
   permanent failure; the slow path was the real defect. The Bank-save and
   delete-slot observers use the same poll-count rule.
4. **No escape:**
   - An error discards the continuation shortcut (`fs_autosave_winner_cached`),
     so every retry (`filesystem_autosaveWriterCompleted()`, after 5 s)
     re-validated A first and aborted at the same place.
   - The repair step (phase 11 remove, phase 24 `"w"` recreate) was never
     reached.
   - Nothing told the user.
5. **The symptoms follow:**
   - The Effect type change was marked dirty correctly (`D` records at
     `#027919`, `#029990`) but never published, so every boot restored
     generation 100. No Effect-specific fault was found: the token is
     projected from the registry, and `cbt` restores through
     `effects_typeFromToken()` like any type.
   - The S16 Bank save went to **slot 27**. The only valid record belonged to
     slot 25, and boot validation checks the slot only, so from S17 every boot
     saw a Bank mismatch (`V 0x07`) and ran the canonical Bank 27 load with a
     Case-2 reload of all 16 Scenes (+3.2 s). A working drain would have
     re-identified the record with Bank 27 on its first run
     (`autosave_markResidentBankDirty()`).
   - Work lost: every edit after generation 100 not in the S16 Bank save.
     Pattern edits were possibly lost too (starved drain).

### 11.4 Secondary findings

1. **Trace-ring overflow:** whole-Scene dirty bursts (2,000+ `D` records) push
   out lifecycle records (13,000–28,000 dropped per session). That is why the
   tear's own records are missing. F4 (not done): collapse whole-object `D`
   bursts into their `L` summary, or flush lifecycle stages first.
2. **`tools/verify_bank_autosave.py` is stale:** it expects 129 HCNAMES rows
   (now 161) and reported "neither AutoSave record is valid" although B was
   valid.
3. **`tools/decode_devlogs.py`** misreads `pattrace.bin` as the AutoSave
   format and labels boot-reader `Q` records "unknown producer" (cosmetic).
4. **`.pat06b`** was torn the same way but was harmless: the Pattern validator
   rejects it on its first extra byte, and the next Pattern generation for
   Scene 6 recreates it with `"w"`.

### 11.5 The boot timeout (not proven)

- The trace's last flush is S19 at t = 31.0 s (`#057688`). The timed-out boot
  was never written; there is no `/bootlog.bin`.
- The cooperative timeout path (`main.c` about 1223–1260) makes one
  best-effort write after a remount. Either that failed, or the boot hung
  where the deadline cannot see. `DEV_LOGGING_IWDG` is 0, so a hard hang
  leaves nothing.
- `BOOT_FILESYSTEM_TIMEOUT_MS` = 20,000 ms per armed boot operation
  (`config.h`).
- The measurable costs this bug added to boot were small: validation of the
  overlong A +0.35–0.45 s (`V` at 1.39–1.52 s against 1.04–1.15 s), and the
  Case-2 reload +3.2 s. Both are far inside 20 s, and S17–S19 booted from
  essentially this card state.
- **Conclusion:** no link to the timeout was confirmed. To capture it, copy
  the card image back (without `.Spotlight-V100`, `.fseventsd`) and boot with
  `DEV_MODE_DIAGNOSTIC 1` (the stuck stage shows on the LCD; `/bootlog.bin`
  should appear). If the screen freezes with no timeout, repeat once with
  `DEV_LOGGING_IWDG 1`. If it does not reproduce, keep `/bootlog.bin` whenever
  a timeout recurs.
- **Still open.** The fix removed both measured boot costs (§11.9).

### 11.6 Fix plan (report §7) and the user's policy

| Item | Outcome |
|---|---|
| F1: reject an overlong candidate on its first extra byte | **Implemented** (shared helper, both validators) |
| F2: progress-aware drain stall observer | **Implemented** (0 B net RAM) |
| F3: visible warning after repeated failures | **Replaced by trace-only logging.** User policy (2026-09-30): "If the fix makes autosave recoverable after a failure without user interaction then do NOT provide unnecessary user error screens; just log the error in the trace." |
| F4: trace-ring priorities | Not included (diagnostics only) |

"What not to do" (recorded): do not skip or trust-by-default a candidate after
repeated errors (it could be the newer valid record); do not pre-truncate or
size-check records at boot (the publication already deletes and recreates the
inactive record). Workaround on the old firmware was to delete `.hcprms1` on a
computer.

### 11.7 Implementation (user-applied; `50610dd`)

| ID | File | Change |
|---|---|---|
| T2 | `AutosaveTrace.h` | `V` flags layout and `AUTOSAVE_TRACE_VALIDATED_*` defines: bit 0 WINNER, bit 1 winner index (0 = A, 1 = B), bit 2 BANK_MISMATCH (runtime: slot and name; boot: settings slot only), bit 3 CACHED (continuation path), **bit 4 A_OVERLONG, bit 5 B_OVERLONG** (new), bits 6..7 reserved zero |
| T1 | `AutosaveTrace.h` | `X` layout comment: site 2 is progress-aware since S074; its extra field carries `stream_offset / 16` |
| W1 | `filesystem.c` | `uint8_t overlong_mask` in `filesystem_autosave_writer_state_t` (inside the 2,048 B `fs_stage_workspace` union; zeroed with the workspace at phase 0 of both validators) |
| H1 | `filesystem.c` | `FS_AUTOSAVE_CANDIDATE_PENDING/DECIDED` and **`filesystem_autosaveValidateCandidateStep()`**: stream the record in 128 B chunks; a short file is decided by `Finish()`; once the record is consumed, read exactly one byte. Data means overlong: invalid, set the candidate's bit. EOF means `autosave_streamValidationFinish()` decides. At most 273 reads per candidate whatever the file size. Not DEV-gated. |
| S1 | `filesystem.c` | `FS_AUTOSAVE_DRAIN_STALL_POLLS 30000u` (asserted < `UINT16_MAX`); observer statics `op_autosave_drain_last_phase` (u8), `op_autosave_drain_stall_ticks` (u16), `op_autosave_drain_last_progress` (u16): 5 B as before; `filesystem_autosaveDrainProgress()` = modulo-65,536 sum of `op_bytes_done`, `op_item_offset`, `stream_offset`, `chunk_written`, `mask_bytes_read`, `payload_scan_offset`, `patch_count`; `filesystem_autosaveDrainStalled()` resets on any phase or progress change and fires once on poll 30,001 of no change |
| S2 | `filesystem.c` | the drain's first statement calls `filesystem_autosaveDrainStalled()`; the rest (X record, named code `DrSt<phase>`, error close-down) unchanged |
| D1, B1 | `filesystem.c` | drain phase 3 and boot validator phase 3 call the shared step |
| D2, B2 | `filesystem.c` | both `V` emitters OR `overlong_mask << 4` into the flags |
| P1 | `decode_devlogs.py` | all 10 stall sites named, plus a per-site behaviour table |
| P2 | `decode_devlogs.py` | `V` decodes bits 2..5 (`bank_mismatch=`, `cached=`, "overlong candidate rejected: …") |
| P3 | `decode_devlogs.py` | `X` decodes the 4-bit site and bit-4 native delete (it had read the pre-S057 3-bit layout) |

**Beyond the schedule (implementation notes):**
- An **idempotence guard:** if the asynchronous `afatfs_fclose()` refuses the
  first close after the one-byte probe, phase 3 repeats but keeps the
  already-decided invalid verdict, instead of probing EOF again and letting
  `Finish()` overwrite it.
- The drain-admission rearm block clears the new progress snapshot as well as
  the counter.

**Not changed, verified:** the error close-down (phases 40–43) already closes
the candidate and restores the captured dirty offsets on retry; phases 11/24
already repair a torn target; the Pattern validator already probes one byte;
the ensure step leaves existing files alone; `Autosave.c`'s streaming
validator already invalidates past the record.

**Resources:** RAM 0 B (the observer is 5 B before and after, DEV-only);
flash about +150 B; overlong validation falls from about 31,040 reads to 273;
exact-size candidates cost 272 reads plus one EOF probe; the observer adds
seven additions per drain poll in DEV builds. The card format is unchanged.

**Build:** `make all` passed at `text=502,152`, `bss=426,384`, headroom
251,096 B; `.img` 502,584 B. A `DEV_STALL_DETECTION=0` compile of
`filesystem.c` also passed (the override was removed after). The decoder
decoded the S074 trace and a synthetic `V 0x17` correctly.

### 11.8 How the captured card repairs itself (expected)

1. Boot validation rejects A after 273 reads: `V 0x17` (winner B, Bank
   mismatch, A overlong), about 1.05 s instead of 1.45 s.
2. Canonical Bank 27 load, unchanged.
3. First runtime drain: `V 0x17`, `M`, `B`, `C`, then `P` "newly active target
   A, generation 101", then `T DONE`. Phase 11 removes the 65,536 B file;
   phase 24 recreates it and it closes at 34,768 B.
4. Next boot: `V 0x01`-class (winner with Bank match); AutoSave restores
   normally with no mass reload.

### 11.9 Hardware result (2026-09-30, `SD_CARD_ATS_CORRECTION_OUTPUT/`)

The user flashed the fixed image (502,944 B, 07:33) and worked on a Scene:
"the autosave fix is in and tested; seems ok".

**Card after the fix:**

| File | Before | After |
|---|---|---|
| `.hcprms1` (A) | 65,536 B, torn gen 101 (commit 0, CRC 0) | **34,768 B**, v3, commit `0xA5`, **gen 143**, CRC valid, Bank 27 `NoBankOk` |
| `.hcprms2` (B) | 34,768 B, gen 100, Bank 25 | **34,768 B**, `0xA5`, **gen 144**, valid, Bank 27 |
| `.pat06b` | 32,768 B (torn) | **10,656 B** (rewritten by a Pattern generation) |
| Files with a cluster-rounded size | 2 | **none** |
| `settings.cfg` | `active_bank=27`, `autosave=1` | unchanged |

`asavetrc.bin` grew 461,512 → 607,912 B (+18,300 records). Its first
461,512 B are byte-identical to the old trace, so the new records continue it.

**Sessions:**

| Session | Firmware | Boot `V` | Boot reader done | Drains | Published | Stalls |
|---|---|---|---|---|---|---|
| S20 (`#057834`–`#067464`) | old | `0x07` at 1,397 ms (the old firmware cannot set the overlong bit) | 4,574 ms, Case-2 reload of all 16 | 163 | **0** | **163**, all phase 3 |
| S21 (`#067465`–`#075970`) | **fixed** | **`0x17` at 1,227 ms**: winner B gen 100, Bank mismatch, A overlong | 4,391 ms, Case 2 (expected: Bank still mismatched at boot) | 43 admitted (+1 whose `A` was dropped) | **44: gens 101 → 144** | **0** |
| S22 (`#075971`–`#075988`) | **fixed** | `0x03` at 1,236 ms: winner B **gen 144, Bank match**, no overlong bits | **1,383 ms, no reload** (restored from AutoSave) | 1 | 0 (mask clean, `M dirty=0`; finished read-only) | 0 |

**Checks:**

| Check | Result |
|---|---|
| Boot validation of the torn A (`V 0x17`) | **Pass**, 1,227 ms (170 ms faster than the old firmware on the same card; the absolute time includes other boot stages) |
| Boot path (canonical Bank 27, Case 2 `Q 0x80` mask `0xffff`) | **Pass** (expected while the record still belonged to Bank 25) |
| First drain: `M` (`#071646`, full on-card mask read), `B`, `C` (1,536 patches), **`P` "newly active target A, generation 101"** (`#071652`, t = 26.2 s), `T DONE`, no `X`/`E` | **Pass.** That drain's `A` and `V` were lost to ring overflow; the `M` record proves the full-validation path. |
| Torn file repaired (34,768 B, `0xA5`) | **Pass** |
| Catch-up: generations 101–144 in S21, all `T DONE`; alternating A/B through the continuation cache (33 × `V 0x09`/`0x0b`) plus **10** full validations (`0x01`/`0x03`), with the first drain's `V` dropped (43 recorded + 1 = 44); no overlong bit | **Pass** |
| Next boot: winner with Bank match, no mass reload, reader about 1.5 s | **Pass**: 1,383 ms, about 3 s faster than S17–S21 |
| Edits survive a power cycle | **Pass** (trace level: the last S21 drain captured the final edit, `C` patch_count 1, gen 144; S22 found nothing to write) |
| No user-visible error | **Pass** (no UI path exists) |
| Pattern AutoSave not starved | **Pass**: S21 `class=pattern` denials (824–1,842 per 5 s report) only during catch-up gens 101–113, none after; S22 0; S20 (old) up to 2,786 |

**Observations:**
1. **Trace-ring overflow** hid the first drain's `A`/`V`: the Case-2 reload
   marks about 15,000 `D` records at once; `dropped=28368` before the first
   S21 drain and `dropped=41708` after it. F4 is still not done. The fix's own
   evidence survived (boot `V 0x17`, the drain's `P`).
2. **The first full validation after boot is still slow:** 2–8.5 s from `A` to
   `V` (S22: `A` 7,913 → `V` 16,405 ms). The healthy pre-bug sessions show the
   same (S08: 7.2 s), because CRC validation is budget-paced. It is not a
   regression and is nowhere near the stall limit.
3. **Not run as separate tests:** torn-write reproduction (power off during
   the 0.3–1.5 s after an edit; expect a `V` with bit 4 or 5 and a silent
   repair) and a card pull mid-drain (expect `X` site 2, `E`, `T` ERROR within
   30,000 no-progress polls, retries every 5 s, success after reinsertion).
   The captured card was a real torn-write case and repaired itself; no `X`
   fired on the fixed firmware.

**Correction made during the session:** an early draft of the investigation
said the drain stalled "768 polls short of EOF". The correct numbers are
31,040 total polls (272 + 30,768) against the 30,000 limit, about 1,040 short.
The per-session drain counts (S00–S08 101/97, S10–S16 642, S17–S19 21) and the
boot `V` range 1.04–1.15 s were corrected at the same time. Two schedule line
citations (L7855 memset, L7894–7899) were also corrected before
implementation.

---

## 12. Documentation changes (closeout)

| File | Change |
|---|---|
| `dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` | Registry now `off/flt/cbt`; hook and layout contract extensions; the underline rule and precedence; last-step-held hold rule; CrumpBit page; §13 item 1 closed; tutorial extended (layout, hooks, buffer contract); `xfd` fader row; the feature plan's deletion noted |
| `dsp_instruments_effects/EFFECTS_MIXER_DSP_REFERENCE.md` | CrumpBit DSP (§4.4); arena gaps closed; the master bus compressor stage (new §5A); `xfd` in the fader modes; costs; S074 history |
| `dsp_instruments_effects/CPU_USE_DSP_AUDIT.md` | Items 27–31 (CrumpBit, compressor, saturator, `xfd`, the mixer `off` branch) and the S074 note in the header |
| `dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | The trigger funnel's sidechain tap; status line |
| `AUTOSAVE.md` | Torn-record section, validator one-byte probe, Scene params 0..44, `cbt` token, `V` bits, progress-aware drain, S074 validation result |
| `DEV_MODES.md` | Drain stall rule; `V` flag layout; decoder updates; BC18 check; S074 status |
| `ASYNCFATFS_REFERENCE.md` | The open-file size exaggeration and the validator rule |
| `STORAGE_SRAM_MANIFEST.md` | S074 ledger (scenes, DTCM, arena, bus compressor, sector 6 content) |
| `FILESYSTEM_SPEC.md` | `bus_comp_*` keys; `fader_setting` 0..3; S074 status |
| `MODULE_INTERCHANGE_SPEC.md` | BusCompressor, menuEffects extensions, `menu_effectShowHome()`, `adc_sliderGainMirrored()`, `MIXER_FADER_XFD`, CrumpBit in the registry |
| `BANK_PRESET_ARCHITECTURE.md` | Scene contents (bus compressor, `xfd`), corrected Scene sizes, the O1 hazard |
| `PATTERN_DYNAMIC_STACK.md`, `OSC_INTERP_AUDIT.md` | Status lines; a stale `scenes[16]` size corrected |
| `MEMORY.md`, `README.md`, `SCOPING_TARGETS.md`, `000_SESSION_INDEX.md`, volatile notes | Session 074 context; the moved DSP document paths |
| `S075_PH6_COPY_CLEAR.md` (root, new) | Phase 6 startup: copy/clear overview and decisions |

---

## 13. Open items and carried debt

### 13.1 From Session 074

- **Boot timeout** (§11.5): unexplained; needs a bench reproduction.
- **`cpu` widget** on the worst-case Scene with `cmp` on against off: not
  recorded. Expected 1.4–1.8 % while on.
- **BC11:** track 7 as sidechain voice 6, unconfirmed.
- **O1:** Settings Load bulk apply equalises per-voice Morph and `srt` across
  the VOICE edit mask (§8.13).
- **`DTYPE_PM63` pan display quirk:** centre (64) shows `1` on Effect pan
  rows.
- **F4 trace-ring priorities:** lifecycle records are lost under dirty bursts.
- **`tools/verify_bank_autosave.py`** expects 129 HCNAMES rows (still 161 in
  the format).
- **`decode_devlogs.py`** misreads `pattrace.bin` and labels `Q` "unknown
  producer" (cosmetic).
- **Saturator α 0.35** not A/B tested.
- **CrumpBit:** the minimum-share hardware run and the `FxBf` self-check
  reading were not reported; the user plans additions.
- **Cosmetic:** the C4 comment placement in `menu.c`; stale comments in
  `BusCompressor.h` (24 B) and the `BusCompressor.c` loop comment (+0.45 %);
  `main.c` about 532 ("CRCs at the end of the load image").
- **Underline limitations** (§5.9): live erase while recording; a deferred
  clear race; no quiet-period debounce; latency not measured.
- **Repository hygiene:** the `SD_CARD_*` card copies (with macOS metadata)
  are committed; `EFFECTS_BUS_FEATURE_PLAN.md` was deleted in `ca77891`
  (retrieve with `git show f3a3105:EFFECTS_BUS_FEATURE_PLAN.md`).

### 13.2 Still open from earlier sessions

- Slow Load type switching (deferred with the trace logger;
  `knowledge_files/drafts/MENU_LOAD_SPEEDUP_SMOOTHNESS.md`).
- D-C1 (keep the boot image check): undecided; kept.
- Phase 5 hardware acceptance matrices for S072 Steps 6–10: not reported.
- LFO noise range −1..1 (suspected, unverified).
- Deferred Phase 5 features: `/Effect/` browser and Load/Save item (A35); FX
  lock removal (A15); Scene copy/clear of the Effect; MIDI mapping of Effect
  parameters (A20); live record of FX moves (A22); track step-scale/shuffle
  playback (A10).
- Phase 4.5 copy operations: `pat_copyTrack`, `pat_copyPattern`,
  `pat_copyBar` are still deliberate no-ops (Phase 6 start, S075).

### 13.3 S072 carried debt, status at S074 close

| # | Item | Status |
|---|---|---|
| 1 | `fxbuf_init()` handoff-reset order | **Resolved (S074)** |
| 2 | Makefile echoed link-budget comments | Open |
| 3 | Bare `make` stops at `build/main.o` | Open; use `make all` |
| 6 | Duplicated comment line in `mixer_calcNextSampleBlock()` | Open |
| 7 | Mixed indentation in `presetManager.c` comment blocks | Open |
| 8 | FX return ramp not reset while `off` | **Resolved (S074)** |
| 9 | `tools/verify_bank_autosave.py` expects 129 rows | Open |
| 10 | Unreachable Scene Save phases 33–36 | Open |
| 11 | Blank/space-only name stems | Open |
| 13 | AutoSave tracking state at the re-validation hook | Open |
| — | S073 duplicated line in the `ResonantFilter.c` guard comment | Open |

---

## 14. Files changed (`692abf8` → `50610dd`, code only)

| Area | Files |
|---|---|
| Menu | `Core/Menu/menu.c/h`, `menuEffects.c/h`, `menuPages.h`, `MenuText.h` |
| Effects | `Core/DSP/Effects/EffectsManager.c/h`, `FxBuffer.c`, `CrumpBit/CrumpBitEffect.c/h` (new), `CrumpBit/CrumpBitParameters.c/h` (new) |
| DSP | `Core/DSPAudio/BusCompressor.c/h` (new), `mixer.c/h` |
| Front panel | `Core/Hardware/frontPanel/buttonHandler.c`, `IO/adcPots.c/h` |
| Scene data and persistence | `Core/Bank/Scene/SceneData.c/h`, `Autosave.c/h`, `AutosaveTrace.h`, `Preset/presetManager.c/h`, `Preset/ParameterArray.h`, `Core/Hardware/SD/storageTypes.c/h`, `Core/Hardware/SD/filesystem.c` |
| MIDI | `Core/MIDI/MidiVoiceControl.c` |
| Build and image | `Makefile`, `STM32F765VIHx_FLASH.ld` (comments), `Core/Hardware/flashImage.c/h` (comments), `config.h` (comments), `tools/build_lxrv2_img.py`, `tools/stamp_image_check.py` (deleted), `tools/decode_devlogs.py` |
| Committed images | `build/LXRV2_lxr02.img` (tracked; updated in several commits) |

---

## 15. Architectural invariants introduced

- **Effect-page name underline:** any stored automation counts (Pattern on
  any track or step, FX lock on any step); nothing that decides playback
  filters it. Local 63 / `0x1FF` is never automation.
- **One automation-presence search** shared by VOICE and Effect pages; every
  entry to either page restarts it; `va_searchRestart()` reads
  `menu_activePage`, so set the page first.
- **CGRAM marker moves need `menu_repaint()`**, never `menu_repaintAll()`,
  whenever a marker slot can change cell.
- **Effect-page hold source = the last step held**, for every cell.
- **Type UI stays in the registry:** layout (`custom_row0`, home screen, the
  `mrp` sentinel) and hooks (`paint_row0`, `format_value3`, LED ownership,
  `EFFECT_UI_SHOW_HOME`); no type-specific branches in `menu.c`.
- **SELECT LEDs on the Effect page go through
  `menuEffects_renderSelectLeds()`.**
- **Buffer-using Effects:** clear-by-masking (`valid` counter) is an accepted
  way to meet "clear unless you adopt"; a same-type Scene switch refreshes the
  handoff without `init`; the FX return ramp origin is zero while `off`.
- **CrumpBit's delay range is fixed to fit the minimum share**, so a Scene
  sounds the same whatever voice units are claimed.
- **Bus compressor:** St1 = `output2`, St2 = `output`; the stage runs last in
  the mixer, before the pack; no work while `cmp` is off (the only approved
  constant-CPU exception); the four settings never commit through
  `menu_parseGlobalParam()`; the settings page is always last
  (`MENU_GLOBAL_SCENE_SUBPAGE`); the output never exceeds full scale (knee).
- **Fader modes:** `SCENE_FADER_SETTING_MAX` = `MIXER_FADER_XFD` = 3 (asserted
  in `mixer.c`); the mirrored gain lives beside the taper in `adcPots.c`.
- **AutoSave validators prove exact length with a one-byte EOF probe** in one
  shared helper; never stream an overlong tail. The runtime drain's stall
  observer measures progress, not phase changes. A torn record is logged in
  `V` bits 4..5 and repaired silently by the next publication.
- **Images:** the `.bin` is unstamped; `tools/build_lxrv2_img.py` is the only
  image script; record the `.img` hash.

---

## 16. Next session

`S075_PH6_COPY_CLEAR.md` (root). Phase 6 begins with copy and clear
operations for step, bar, track, automation, Instrument, Scene and other
Scene components. The document surveys what exists (the copy/clear gestures
and the Pattern no-op stubs), proposes the component list and an operation
model, and lists the decisions to take before any schedule.

---

## 17. Disposable documents

Superseded by this log and the specification updates; the user will delete
them:

- `S074_EFFECT_BUGS_BUFFER_USE.md`
- `S074_EFFECT_P_LOCK_DISPLAY_IMPLEMENTATION.md`
- `S074_CRUMPBIT_EFFECT.md`
- `S074_CRUMPBIT_IMPLEMENTATION.md`
- `S074_BUS_COMP.md`
- `S074_BUS_COMP_IMPLEMENTATION.md`
- `S074_FADER_XFD.md`
- `S074_AUTOSAVE_BOOT_BUG.md`
- `S074_ATS_BUG_IMPLEMENTATION.md`
- `S074_COMP_SAT_UPDATE.md`

The card copies `SD_CARD_ATS_BOOT_BUG/` and `SD_CARD_ATS_CORRECTION_OUTPUT/`
are the evidence for §11. They are committed; keep or remove them as the user
decides.

---

## 18. Facts carried from the S074 startup document

- **Files a new Effect type touches:**
  - `Core/DSP/Effects/<Type>/<Type>Parameters.c/.h` and `<Type>Effect.c/.h`;
  - `EffectsManager.c` (union member, `_Static_assert`, registry row) and
    `EffectsManager.h` (`EFFECT_TYPE_<NAME>`, `EFFECT_TYPE_COUNT`);
  - the Makefile: the Effect file in `DSP_SRCS` **and** an explicit `-Ofast`
    rule (the pattern rule covers only `Core/DSPAudio/`), the parameters file
    in the normal sources, and the include path;
  - the docs: `EFFECTS_BUS_REFERENCE.md`, `EFFECTS_MIXER_DSP_REFERENCE.md`,
    and `STORAGE_SRAM_MANIFEST.md` if the union grows.
- **DSP rules for Effect types:**
  - constant CPU (the per-sample work never depends on parameter values);
  - float in place, `io->frames` = 32, respect `io->channels` and a NULL
    `io->r`, never truncate to int16, keep feedback bounded;
  - wrap indices with compare-and-subtract;
  - ramp or crossfade parameter changes (they arrive once per block, every
    block under an LFO);
  - foreground only, no allocation;
  - host-test first (`DSP_TEST.md` §6.3). An Effect runner in
    `tools/dsp_test/` would be a new file: ask first. None was requested in
    S074.
- **Testing the minimum share:** `DEV_MODE_DIAGNOSTIC 1` with
  `DEV_FXBUF_FORCE_VOICE_UNITS 12` (valid since the S074 `fxbuf_init()` order
  fix).
