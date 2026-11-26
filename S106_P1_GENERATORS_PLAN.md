# S106 P1 — Generators (plan, revision 1)

**Date:** 2026-11-26. **Branch:** `dev-ph7-alkaline` (clean at `1d2e42ca`).
**Status:** plan. No source was changed.

**Your requests (2026-11-26):**

| § | Item |
|---|---|
| 3–5 | The Generator page (`knowledge_files/drafts/GENERATOR_PAGE_SPEC_DRAFT.md`) as the SHIFT+MODE STEP menu, re-based on your generator rules (G1–G14 below) |
| 6 | The first generator type (not named in the request; Euklid is proposed, Q1) |
| 7 | Roll special values `sus` and `l+s` beside `leg` |

**Labels:**
- **Code:** read from the tree, with a file and line (clean tree at `1d2e42ca`).
- **Measured:** a number taken from the tree (the method is given).
- **Est.:** an estimate with its basis. The gate named beside it measures it.

**Binding rules that apply** (`MEMORY.md`):
- every new or enlarged RAM allocation needs sign-off (§8);
- no backward compatibility (the AutoSave layout and the Scene target IDs move, §4.6, §4.7);
- TIM3 order and the step-reset rule (S081 §5.8) do not change; the sequencer reads only
  `seq_effectiveTrack*[]` for timing and never writes the Pattern region from Morph or
  automation (S078);
- automation priority (S075/S081 R12/S083 Q1): step auto → menu edit; a MIDI write is a
  menu edit;
- Scene switch = per-track identity (S098 P1): the keep mask must cover everything that
  can make two tracks sound different (§4.8);
- no 1 ms blocking in the main loop; generation is bounded foreground work (§4.4);
- the CPU ceiling (S089 P2): no per-sample DSP cost changes. This plan adds TIM3 and
  foreground work only (§9);
- no step in this plan edits `MEMORY.md` or `knowledge_files/`. §11 lists the statements
  made out of date.

---

## 0. Summary

- **Where:** SHIFT+MODE STEP opens a new **Generator page** (select mode `0x06`, page id
  `SOM_PAGE` renamed `GENERATOR_PAGE`). The press does nothing today.
- **What it edits:** one generator per track, a Scene parameter. Row 0 is `gen`, `off` by
  default, with no other rows. Clicking into `gen` opens a browse view that shows the
  long and short names and commits on click-out, as the Effect `typ` does. A type other
  than `off` adds its rows:
  - up to four automatable/morphable parameters;
  - up to four fixed parameters;
  - `ato`, which every type has.
- **How it plays:** a track whose generator is not `off` reads its trigger bits and four of
  its specials from one device-wide **generator store** (3,696 B), not from its Pattern.
  The store is rebuilt in the foreground when:
  - the track's played Scene changes;
  - the track's Pattern changes (edit or record);
  - a generator parameter's effective value changes.

  A track that has not been rebuilt since a switch does not fire. A track rebuilt after a
  parameter change may play stale until the rebuild lands.
- **Automation:** 28 new Scene targets (4 per track) replace the 7 shuffle targets in the
  64-ID Scene block, as you said (42 − 7 + 28 = 63). They are step-automation and Morph
  only, with no LFO and no velocity flag.
- **Draft superseded:** the draft's Off/Overdub/Replace output mode, `PAR_GEN_TARGET`,
  `PAR_GEN_MIX`, `PAR_GEN_SEED` and the six-type roster are replaced by your rules. A type
  other than `off` is always "replace"; there is no overdub. SOM is archived (S097), and
  Euklid is compiled out (`config.h:294`). §1.2 lists the other draft statements that no
  longer hold.
- **First type:** Euklid (proposed, Q1). It reuses the bucket algorithm already in
  `EuklidGenerator.c:256–297`, retargeted from the Pattern to the generator store.
- **Roll:** `leg` does **not** exist in the tree (§1.6). Adding `leg`, `sus` and `l+s` needs
  rules that were never written down (Q14). This is planned as a separate, last step.
- **Defect found (F1, §10):** `seq_scene_automation_dirty` is `uint32_t`, but the Scene
  target indices already reach 41. Bits 32..41 are never set (tracks 5–7 scale, all
  shuffle), so those overlays are not restored at a transport reset. The new targets
  (to index 62) need the 64-bit fix anyway.
- **RAM:** ≈ 5,330 B SRAM1 (§8): 3,696 B store + ≈ 80 B runtime + 1,547 B Scene settings
  (91 B × 17 records) + 4 B mask. **Sign-off needed.**

---

## 1. What exists (code)

### 1.1 The SHIFT+MODE STEP press and select mode 0x06

- `buttonHandler.h:18–25`: modes VOICE 0, PERF 1, STEP 2, LOAD_SAVE 3, FX 5, MENU 7.
  Code 4 is not named. `:24` says "0x06 (SHIFT+MODE STEP) was the SOM generator; unused
  since S097".
- `buttonHandler.c:2799–2808`: "SHIFT+MODE STEP has no action (S097)". The press returns
  before any mode, page, LED or latched-surface change. Without that return, the shifted
  arithmetic `(mode + 4) & 7` at `:2815`/`:2867` would give 6.
- The mode switch at `buttonHandler.c:2874` has no `case 0x06`.
- `ledHandler.c:1243`: `case 6: break;` lights no MODE LED. The pattern is set by mode 5
  (SHIFT+PERF), which blinks MODE2 (`:1242`).
- `menu.h:87`, `:94`: `EUKLID_PAGE` and `SOM_PAGE` are still in `enum PageNames`.
  `SOM_PAGE` is kept only so `EFFECT_PAGE` keeps its value ("Unused since S097").
- `config.h:285–294`: `ENABLE_EUKLID_PAGE 0`. The Euklid page and its SHIFT paths are
  compiled out, and setting it to 1 is "not supported".

### 1.2 Generators today, and how the draft has aged

| Draft statement | Tree today |
|---|---|
| SOM: `SomGenerator.c`, `SELECT_MODE_SOM_GEN 0x06`, dead `seq_SomModeActive` | Archived in S097 (`archive/som/`). No `SomGenerator.c` in `Core/`. Mode 0x06 is free. |
| Euklid: `EUKLID_PAGE` on select mode 5 | Mode 5 is the Effect page (S072). Euklid is compiled out (`config.h:294`). `EuklidGenerator.c` (298 lines) is still built (`Makefile:146`). |
| Euklid "bakes" into the Pattern | Still true: `euklid_transferPattern()` (`EuklidGenerator.c:256`) calls `patSvc_clearTrack()` then `pat_setStepActive()` for each hit, a Bresenham-style bucket over `len`. Its only callers are the compiled-out Menu cases (`menu.c:20688–20719`) and `buttonHandler.c:839–841`. |
| "No free select-mode slot" | Codes 4 and 6 are free. 6 is SHIFT+MODE STEP. |
| `CAT_GENERATOR` unused | `menu.c:1441–1444` uses it for the archived SOM labels (`x`, `y`, `flx`, `frq`). |
| Six const sub-tables, one per type | The Effect page has shown since S072 that a registry-driven page owner (`menuEffects.c`, 1,271 lines) works beside the static tables. That is the better model (§5). |

### 1.3 Pattern storage and the specials

- `PatternData.h:16–20`: 7 tracks, 128 steps, 8 bars × 16. `PATTERN_TRACK_BYTES` = 16.
- `PatternData.h:56–63`: **seven** specials, bits 0..6: note, velocity, condition, roll,
  timing, rhythm, fill. Bit 7 is reserved. Your rule says "4 of the 8 specials"
  (ambiguity A1).
- `PatternData.c:1239–1246`: `pat_readStepSpecials()` defaults are note
  `PAT_DEFAULT_NOTE`, velocity `PAT_DEFAULT_VELOCITY` (100), condition 0, roll 0, timing 0,
  rhythm 0, fill 100.
  - **Code:** playback never tests the presence flags. The only flag readers are
    `copyOps.c:581–583` and the automation byte count (`sequencer.c:3044–3058`).
  - So a generator can store four plain bytes per step, and a special it does not store
    takes the default. No presence bits are needed.
- `PatternData.c:99–103`: `pat_markSceneDirty()` is the single Pattern mutation funnel
  (Bank clean invalidation + AutoSave Pattern bit). **Code:** it is called by every writer,
  including the raw pool paths:
  - `pat_setStepActive`, `pat_toggleStep`, `pat_eraseStep`, `pat_clearTrack`;
  - `pat_writeDynamic`, `pat_tryAppendAutomation`;
  - `pat_rawPlace`, `pat_rawPlaceViaSwap`, `pat_rawSwapReturn`, `pat_rawPublishEmpty`;
  - `pat_rawRegionPublishSteps`, `pat_rawRegionPublishRewritten`, `pat_rawRegionReset`;
  - `pat_releaseStepDynamic`;
  - the track setters (length, scale, shuffle, play mode, loopable, auto smooth);
  - `pat_setPatternChangeBar`, `pat_setPatternNext`.

  It takes a Scene, not a track. `pat_setStepActive()` is also called from TIM3
  (`sequencer.c:3845`, `:3851`, live erase).

### 1.4 The sequencer's reads

`seq_advanceTrackStep()` (`sequencer.c:3504`) reads each step through PatternData on the
track's played Scene (`seq_perTrackPattern[track]`):

| Line | Read | Purpose |
|---|---|---|
| 3666–3672 | `region->address[...]`, `pat_readStepSpecials()` | muted self-mute condition |
| 3715–3717 | `pat_readStepSpecials()`, `pat_isStepActive()` | muted recording track's fired bit |
| 3729 | `pat_readStepSpecials()` | the step's specials |
| 3744, 3765, 3830 | `pat_isStepActive()` | fired bit, roll cut, trigger |
| 3845, 3851 | `pat_setStepActive(..., 0)` | live erase (TIM3 write) |
| 4012 | `pat_stepAutomationCount()` | automation present |
| 3030 (`seq_queueStepAutomations`, `:3004`) | `region->address`/`pool` | queue the step's automation |
| 4734–4749 | count / specials / active of step b | ramp pairing (`seq_rampNextStep` family) |
| 6748, 6754 | `pat_readStepSpecials()` | deferred fire (`seq_fireDeferred`) |

### 1.5 Scene targets (step automation, Morph, LFO)

- `SceneModTargets.c:19–165`: 42 rows at IDs 384 + index:
  - 0..5 voice Morph;
  - 6 retired `srt`;
  - 7 `7dc`;
  - 8..13 audio out;
  - 14..19 FX send;
  - 20 `fxm`;
  - 21..27 track length `1ln..7ln`;
  - 28..34 scale `1sc..7sc`;
  - 35..41 shuffle `1sh..7sh` (`:150–162`).
- `:167`: `_Static_assert(SCENE_MOD_TARGET_COUNT <= 64u)`, because the Scene block is 64 IDs
  before Effect block 7. 0x1FF (511) is the Pattern off sentinel (`PatternData.h:285`).
- Track targets carry only `SCENE_MOD_TARGET_USE_AUTOMATION`.
  `seq_applySceneAutomation()` (`sequencer.c:4090`) routes them to
  `presetMorph_setTrackParamStepOverride()` and writes `seq_effectiveTrack*[]` directly
  (`:4134–4176`).
- Per-track Morph endpoints: `scene_settings_t.track_morph_length/scale/shuffle[7]`
  (`SceneData.h`). They are interpolated by the slot's resolved Morph amount (track 7 uses
  slot 6's) in `presetMorph_getTrackEffective*()`
  (`presetMorphEngine.h:206–228`). SHIFT on the STEP page shows the endpoints
  (`buttonHandler.c`, `menu_setPatternTrackMorphEndpoint()`).
- Other users of the track kinds, to be updated when shuffle loses its targets:
  - `menu.c:2564`, `:4368`, `:6101`;
  - `MidiControl.c:134–136`, `:256–264`;
  - `LiveRecord.c:667`.
- Users of the target index:
  - `copyOps.c:399`;
  - `menu.c:4398`, `:5605`, `:5823`, `:15068`;
  - `MidiLearn.c:115`, `:639`;
  - `InstrumentManager.c:1740`;
  - `sequencer.c:4100`.

### 1.6 Roll specials

- `MenuText.h:562–565`: `stepRollRateNames` has 10 entries: `off 1/8 8tr 16 16t 32 32t
  64 64t 128`.
- `sequencer.c:3293–3301`: `seq_rollPpq()` table[10]. It returns 0 for rate > 9.
- `menu.c:7083`: the STEP roll clamp is 0..9.
- **`leg` does not exist.** S099 P2 §11 proposed it, and the handoff
  (`099_SESSION_HANDOFF_LOG.md:627`) says it "wait[s] for answers to the eight questions
  asked in the session". Those questions are not in the tree. The deleted
  `S099_P2_ADV_FIXES.md` at `a0ebcf08` names them but does not list them.
- **Hazard (code):** a stored roll value ≥ 10 reaches `seq_rollBegin()` through
  `sp.roll > 0u` (`sequencer.c:3963`). There `ppq == 0` returns without firing
  (`:3442–3446`), so the step would be silent.
- The sequencer has no note gate. The engine is told the step length
  (`seq_trackStepSamples()`, `sequencer.h:341`), and a plain step's fill < 100 opens a
  release window (`seq_lenStart()`, `sequencer.c:3397`, S096 P3). Alkaline has slide and a
  7/12 gate (`AlkalineVoice.h:260–263`). Colloid has a real note gate (S096 C1). The drum
  family has no gate.

### 1.7 Scene switch paths (who must ask for a rebuild)

| Path | Context | Code |
|---|---|---|
| Whole-Scene change (direct, deferred, queued, chain) | foreground: `menu_perfSceneChangeExecute()` (`menu.c:10635`) → `seq_selectActivePattern()` (`sequencer.c:2616`). Deferred/queued fire flags are raised by TIM3 and executed by `menu_serviceDeferredSceneChange()` (`menu.c:10805`) | per-track keep mask `preset_sceneSwitchKeepMask()` (`presetManager.c:3776`) |
| Per-track Scene assignment (PERF hold VOICE + SEQ) | foreground: `seq_setTrackPlayedScene()` (`sequencer.c:2269`) | tracks 5/6 move together |
| Align to Scene / clear overrides | `seq_alignActivePatternToScene()` (`:2811`), `seq_clearPerTrackOverrides()` (`:2354`) | |
| Scene 17 background switch | `seq_switchToBackgroundScene()` (`:2924`), return at `:2986` | |
| Loads, copy/clear, Bank Load | rewrite a resident Pattern region, so they reach `pat_markSceneDirty()` (raw paths) | |

### 1.8 Persistence

- `scene_settings_t` is 75 B (`presetManager.c:3564`), × 17 (`scenes[16]` + `scene_background`,
  `SceneData.c:27`, `:46`).
- AutoSave Scene parameters: 118 cells allocated, 76 live (`Autosave.h:190–191`; cells
  51..71 are track Morph, 72..75 chain). **42 free.**
- Effect region: 512 B at 128 (`:64`, `:192`), 501 allocated, 419 live; the source is at
  430..431. The Scene section is 1,920 B (`:63`) = 128 + 512 + 1,280 (Kit).
  `AUTOSAVE_HEADER_FORMAT_VERSION 3u` (`:48`).
- The `sceneset.scg` keys are text (`storageTypes.c:717` `track_morph_length` pattern), so
  new keys are additive.
- Keep mask / identity field lists: `PRESET_TRACK_SETTINGS_FIELDS(X)`
  (`presetManager.c:3554–3556`).

### 1.9 The Effect page as the model

- `menuEffects.c` owns the cells; menu.c delegates through `MENU_CELL_EFFECT`
  (`menu.c:2197–2207`). There are 23 `MENU_CELL_EFFECT` references and about 30
  `EFFECT_PAGE` branches in `menu.c`.
- `typ` transaction:
  - `menuEffects_editModeChanged()` (`:719`) opens a candidate on click-in and commits
    with `effects_changeType()` on click-out, only if the type changed;
  - `menuEffects_typeBrowse()` (`:749`) steps without wrapping;
  - `menuEffects_paintEditView()` (`:644`) shows row 0 "Effect" / "Type" and row 1 the
    8-character `full8` name. The compact cell shows the 3-character name.

---

## 2. Behaviour (your rules, numbered for reference)

- **G1** One generator type per track. The default is `off`, which does nothing.
- **G2** A track whose type is not `off` reads its trigger bits and specials from the
  generator store, not its Pattern.
- **G3** `ato` (Read Automation) on/off on every type. When on, the track's Pattern
  automation is also applied (A6, Q3).
- **G4** The type and its parameters are Scene parameters. The type is editable only from
  the menu: no automation, Morph, MIDI or Learn.
- **G5** Up to four automatable parameters per track, possibly bit-packed. Each is
  step-automatable and morphable, not LFO-modulatable.
- **G6** Up to four non-automatable parameters per track.
- **G7** Track shuffle becomes non-automatable to make room (its Morph endpoint stays).
- **G8** The store holds 4 of the specials, chosen per type: 7 × 16 B trigger bits (112 B)
  + 7 × 128 × 4 B specials (3,584 B). It exists once for the device.
- **G9** Rebuild triggers:
  - a Scene or track switch;
  - an edit or record of the track's Pattern;
  - a generator parameter change.
- **G10** After a switch, a non-`off` track does not fire until rebuilt.
- **G11** After a parameter change, a track may play stale until rebuilt.
- **G12** The page is SHIFT+MODE STEP. Row 0 is `gen`, showing `off` with no other rows
  by default.
- **G13** Clicking into `gen` changes it, showing the long and short names, as the Effect
  type does.
- **G14** Roll gains `sus` and `l+s` beside `leg`.

---

## 3. Data model

### 3.1 Scene settings (per track, in `scene_settings_t`)

| Field | Bytes | Meaning |
|---|---:|---|
| `gen_type[7]` | 7 | bits 0..6 type id (0 = `off`); **bit 7 = `ato`** |
| `gen_auto[7][4]` | 28 | automatable parameters, Normal, 0..127 each (bit-packing is per type) |
| `gen_auto_morph[7][4]` | 28 | their Morph endpoints |
| `gen_fixed[7][4]` | 28 | non-automatable parameters |
| **per Scene** | **91** | `scene_settings_t` 75 → 166 B |

Why bit 7 for `ato`: it keeps `ato` out of the four fixed slots that G6 gives the type.
The type id has 127 values. Q5 covers whether a type change resets the eight bytes.

Writers follow the "Autosave extension rule" comment at `SceneData.h:285–298`: a named
AutoSave index, a live getter branch, and a SceneData setter through the change-aware store
helper. The setters are `scene_setGenType()`, `scene_setGenAto()`, `scene_setGenAuto()`,
`scene_setGenAutoMorph()` and `scene_setGenFixed()`. Each one also calls
`gen_requestRebuild(track)` when the Scene is a played Scene of that track.

### 3.2 The generator store (device-wide, `GeneratorEngine.c`)

```c
/* bit s of gen_trig[t][s >> 3] = step s of track t fires (G8). */
static uint8_t gen_trig[NUM_TRACKS][PATTERN_TRACK_BYTES];        /* 112 B  */
/* the type's four special bytes per step, in its slot order (G8). */
static uint8_t gen_spec[NUM_TRACKS][NUM_STEPS][GEN_SPECIAL_SLOTS]; /* 3,584 B */
```

The type declares a slot map: `uint8_t special_bit[4]`, each a `PAT_SPECIAL_*_BIT` or 0
for an unused slot. The TIM3 read (§4.5) builds a `pat_step_specials_t`: it starts from the
`pat_readStepSpecials()` defaults (§1.3) and overwrites only the mapped fields. Specials
outside the map take the defaults.

### 3.3 Runtime state (`GeneratorEngine.c`)

| Object | Bytes | Use |
|---|---:|---|
| `gen_readyMask` | 1 | bit t: the store for track t is valid for its played Scene (TIM3 reads it, G10) |
| `gen_requestMask` | 1 | bit t: track t owes a rebuild (foreground) |
| `gen_builtScene[7]` | 7 | the Scene the store was built from |
| `gen_builtType[7]` | 7 | the type it was built with |
| `gen_builtEff[7][4]` | 28 | the effective automatable values it was built from (change detect, §4.3) |
| `gen_stepOverride[7][4]` | 28 | step-automation overlay values (§4.6) |
| `gen_overrideMask` | 4 | 28 bits: which overlays are active |
| `gen_patSceneDirty` | 4 | bit s: Scene s's Pattern changed (set in `pat_markSceneDirty()`; 17 bits) |
| **total** | **80** | |

---

## 4. Architecture

### 4.1 Module and registry

`Core/Bank/Scene/Pattern/GeneratorEngine.c/.h` is a peer of `EuklidGenerator.c`, outside
the Sequencer. It follows the ownership rules (`MODULE_INTERCHANGE_SPEC.md`): it reads
Patterns only through `pat_*` and never writes a Pattern.

```c
typedef struct {
    const char *short3;            /* "euk" */
    const char *full8;             /* "Euklid  " */
    uint8_t auto_count;            /* 0..4 */
    uint8_t fixed_count;           /* 0..4 */
    const gen_param_desc_t *autos; /* name3, long8, min, max, default, dtype */
    const gen_param_desc_t *fixed;
    uint8_t special_bit[4];        /* slot map (§3.2) */
    /* Build 128 steps of one track into the store; bounded (§4.4). */
    void (*build)(uint8_t track, uint8_t scene, const uint8_t eff[4],
                  const uint8_t fixed[4], uint8_t trig[16],
                  uint8_t spec[NUM_STEPS][4]);
} gen_type_desc_t;
```

Type 0 is `off`: no rows, no build. Ids are stored, so the registry order is the storage
schema (as for the instrument and Effect types). Append only.

### 4.2 Rebuild requests (G9)

| Trigger | Where it is raised |
|---|---|
| Whole-Scene change | `seq_selectActivePattern()` (`sequencer.c:2616`): clear `gen_readyMask` bits of the **changed** tracks inside the existing PRIMASK section, and set their request bits. A kept track (identical, §4.8) keeps its store and its ready bit. |
| Per-track assignment | `seq_setTrackPlayedScene()` (`:2269`; tracks 5/6 together) |
| Align / override clear / Scene 17 | `seq_alignActivePatternToScene()` (`:2811`), `seq_clearPerTrackOverrides()` (`:2354`), `seq_switchToBackgroundScene()` (`:2924`, `:2986`): clear ready, request |
| Pattern edit or record | `pat_markSceneDirty()` (`PatternData.c:99`) sets bit s of `gen_patSceneDirty`. The service requests every non-`off` track whose played Scene is s. It does **not** clear ready (G11: stale is allowed). The funnel is also reached from TIM3 (live erase), so the store is a single 32-bit OR under PRIMASK, like the AutoSave bit beside it. |
| Generator parameter edit | the SceneData setters (§3.1) |
| Effective value change (Morph sweep, step overlay) | the service compares `gen_builtEff` against the recomputed effective values (§4.3) |
| Type change | the click-out commit (§5.2): clear ready, request |

### 4.3 Effective automatable values

For each automatable slot, the effective value = the step overlay (if active), else Normal
interpolated against the Morph endpoint at the slot's resolved Morph amount
(`presetMorph_getResolvedVoiceAmount()`; track 7 uses slot 6). This is the same law as
`presetMorph_getTrackEffective*()`. It is computed in the foreground service and never in
TIM3. A rebuild is requested only when the **quantised** value differs from
`gen_builtEff`. That bounds the rebuild rate under a Morph sweep or an LFO on voice Morph
(§12 R6).

### 4.4 The service and the switch-time build

- **`gen_service()`** runs in the foreground main loop beside the other bounded services.
  It rebuilds **at most one track per pass**, in request order:
  1. resolve the effective values;
  2. call `build()`;
  3. record `gen_built*`;
  4. set the ready bit.

  Every type's `build()` must have a worst case well under 1 ms. Euklid is 128 steps of
  integer adds (Est. < 20 µs at 216 MHz, measured by the host test's cycle count and a DEV
  trace stage).
- **Switch-time build:** `menu_perfSceneChangeExecute()` and the per-track assignment call
  `gen_buildNow(changed_mask)` right after `seq_selectActivePattern()` returns. That
  rebuilds the changed non-`off` tracks synchronously, so the first step after a switch is
  normally ready (G10 still holds for the gap). Each type's worst case × 7 must stay under
  the 1 ms rule. Euklid × 7 is Est. < 150 µs. Q12 covers the alternative.
- **Tearing:** the store has one copy. TIM3 may read a step while the foreground rewrites
  it. The build writes each step's specials before its trigger bit, so a torn step is
  either the old trigger with new specials or the new step. G11 accepts this.

### 4.5 Sequencer read routing (TIM3)

Two inline readers in `sequencer.c` replace the direct calls listed in §1.4 on the
playback paths (`:3670`, `:3715–3717`, `:3729`, `:3744`, `:3765`, `:3830`, `:4734–4749`,
`:6748`, `:6754`):

```c
/* S106 P1: the generator store for a non-off track, else the Pattern. */
static inline uint8_t seq_stepFires(uint8_t track, uint8_t step);
static inline pat_step_specials_t seq_stepSpecials(uint8_t track, uint8_t step);
```

- The type is read from the played Scene's `gen_type[track]` (one byte).
- If the type is non-`off` and the ready bit is clear, `seq_stepFires()` returns 0 (G10).
  Specials are still defaulted, so condition counters stay sane.
- The muted self-mute probe (`:3666`) reads the address entry directly today. It switches
  to `seq_stepSpecials()` so a generated condition can bypass the mute.
- **Live erase (`:3845`, `:3851`)** keeps acting on the Pattern (the input). On a generator
  track it is gated by the Pattern's own bit, not the generated one (A7, Q7).
- **Automation (`:3004`, `:4012`, ramps):** on a non-`off` track, queueing happens only when
  `ato` is on (G3). What `ato` off gates is open (Q3).
- **Cost (Est.):** one extra byte load and compare per read per step per track in TIM3. No
  render cost (§9).

### 4.6 Scene targets (G5, G7)

- **Remove** the seven shuffle rows (`SceneModTargets.c:150–162`). **Append** 28 rows,
  kind `SCENE_MOD_TARGET_KIND_GEN_PARAM`: `voice_slot` = track, plus a new `param` field
  0..3. Flags are `USE_AUTOMATION` only. Order 1g1 1g2 1g3 1g4 2g1 … 7g4. That gives 63
  rows, IDs 384..446.
  - Labels: generic `"Track"`, `"1 Gen 1"`, `"1g1"`. A labels hook names them by the
    track's current type (`1hi` for Euklid hits on track 1, Q15).
- **ID reuse:** shuffle IDs 419..425 become 1g1..2g3. Stored shuffle automation in existing
  Patterns would replay as those generator parameters (no migration; §12 R3).
- `seq_applySceneAutomation()` (`sequencer.c:4090`) gets a `GEN_PARAM` case. It calls
  `gen_setStepOverride(track, param, value)` and sets its dirty bit (§10).
  `seq_restoreAllSceneAutomation()` (`:1771`) clears every generator overlay and requests a
  rebuild of the tracks that had one.
- The shuffle case is removed from:
  - `seq_applySceneAutomation()`;
  - `menu.c:2564`, `:4368`, `:6101` (the STEP `shu` cell no longer offers held-step
    automation);
  - `MidiControl.c:136`, `:256–264`;
  - `LiveRecord.c:667`.

  The shuffle Morph endpoint, the menu edit and MIDI Learn of the retained value stay.
  `PRESETMORPH_TRACK_SHUFFLE` stays for Morph; only its step overlay goes.
- MIDI Learn (`MidiLearn.c:115`, `:639`) and copy/clear (`copyOps.c:399`) iterate the
  table, so they pick the rows up. Q17 covers whether MIDI Learn should offer them.

### 4.7 Persistence

- **`sceneset.scg`:** new keys `gen_type`, `gen_auto`, `gen_auto_morph`, `gen_fixed`, 7 or
  28 values each. They follow the `track_morph_length` reader/writer pair
  (`storageTypes.c:717`, `filesystem.c:19139`). A missing key loads `off`, with defaults.
- **AutoSave:** 91 new Scene cells (76 → 167) exceed the 118 allocated. Proposal: move the
  Scene/Effect boundary inside the fixed 1,920 B Scene section:

  | Constant (`Autosave.h`) | Now | New |
  |---|---:|---:|
  | `AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES` (`:190`) | 118 | 182 |
  | `AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES` (`:191`) | 76 | 167 |
  | `AUTOSAVE_EFFECT_OFFSET` (`:192`) | 128 | 192 |
  | `AUTOSAVE_EFFECT_SECTION_BYTES` (`:64`) | 512 | 448 |
  | `AUTOSAVE_EFFECT_PARAMETER_ALLOC_BYTES` | 501 | 437 (≥ 419 live; source ends at 432) |
  | `AUTOSAVE_KIT_OFFSET` (`:201`) | 640 | 640 (unchanged) |
  | `AUTOSAVE_HEADER_FORMAT_VERSION` (`:48`) | 3 | 4, so the old record is rejected, not misread |

  The record and payload sizes (`:350–353`, 30,848 / 34,768) do not change, so the dirty
  mask RAM does not change. New enum bases: `AUTOSAVE_SCENE_PARAM_GEN_TYPE_BASE = 76`,
  `GEN_AUTO_BASE = 83`, `GEN_AUTO_MORPH_BASE = 111`, `GEN_FIXED_BASE = 139`, `COUNT = 167`.
- **Pattern (PAT4):** no change. The generator writes nothing to a Pattern.

### 4.8 Identity, edit mask, copy/clear

- **Keep mask (S098 P1):** add `gen_type`, `gen_auto`, `gen_auto_morph` and `gen_fixed` to
  `PRESET_TRACK_SETTINGS_FIELDS(X)` (`presetManager.c:3554`). A kept track then has
  identical generator settings and an identical Pattern, so its store and ready bit can stay
  as they are. The `_Static_assert(sizeof(scene_settings_t) == 75u)` at `:3564` becomes 166.
- **Edit-mask fan-out (S075):** proposed: generator edits fan out like other per-track
  Scene settings (Q9).
- **Copy/clear:** proposed: a track copy carries the track's generator settings, and a
  Scene copy carries all of them (Q10).

---

## 5. Menu: the Generator page

### 5.1 Entry and LEDs

- `buttonHandler.c:2799–2808`: replace the early return with entry to mode 0x06
  (`SELECT_MODE_GEN`), through the existing shifted arithmetic.
- `case SELECT_MODE_GEN:` in the switch at `:2874`: clear sequencer and select LEDs, then
  `menu_switchPage(GENERATOR_PAGE)`.
- `ledHandler.c:1243`: `case 6:` blinks MODE3 (STEP), as mode 5 blinks MODE2.
- `menu.h:94`: rename `SOM_PAGE` to `GENERATOR_PAGE`, which keeps the id and so
  `EFFECT_PAGE`'s value. `EUKLID_PAGE` stays, compiled out.
- The page edits the **active track** (`menu_getActiveVoice()`). TRACK buttons already
  change it in this mode (`buttonHandler.c:3519` path). The page repaints on a track change.
- SHIFT is held when the page is entered. The first SHIFT release after entry must not
  toggle the Morph view (§5.3).

### 5.2 Cells

`MENU_CELL_GENERATOR` (new `menu_cell_kind_t`, `menu.c:2197`) delegates value, dtype,
format, clamp and commit to `menuGenerator.c`, as `MENU_CELL_EFFECT` does to
`menuEffects.c`. The page is far simpler than the Effect page: there are no FX-sequence
cells and no type UI hooks. Proposed layout (4 cells per screen, as on the voice pages):

| Screen | Cells |
|---|---|
| SELECT 1, screen 0 | `gen`, then automatable 1..3 |
| SELECT 1, screen 1 | automatable 4, `ato`, fixed 1..2 |
| SELECT 2 | fixed 3..4 |

Only the cells the type declares are shown. Type `off` shows `gen` alone (G12).

- **`gen` transaction (G13):** click-in opens a browse view. Row 0 shows "Generatr" /
  "Type"; row 1 shows the `full8` name and the `short3` name at column 13. Turning steps
  through the registry without wrapping. Click-out commits only when the type changed,
  through `scene_setGenType()`. That resets the eight parameter bytes to the type's
  defaults (Q5), clears ready and requests a rebuild. This mirrors
  `menuEffects_editModeChanged()`/`menuEffects_typeBrowse()` (`menuEffects.c:719`, `:749`).
  `gen` has no automation target, no Morph and no MIDI (G4).
- **Automatable cells:** a menu edit is R12 (it suppresses the step overlay until the next
  trigger, transport or Scene change), as track timing cells do (`menu.c:4390–4399`
  `menu_clearTrackTimingOverrideFor()`). A held-step edit writes step automation to the
  cell's `Ng#` target on the held steps of the active track, as voice cells do (Q16).
- **Automation underline:** an automatable cell is underlined when its target has stored
  automation, using the existing search.

### 5.3 SHIFT Morph view

Holding SHIFT on the page shows and edits `gen_auto_morph` for the four automatable cells,
as `menu_setPatternTrackMorphEndpoint()` does for the STEP page (`buttonHandler.c`, S078
§5.6).

### 5.4 STEP LEDs on a generator track

Proposed: STEP mode keeps showing the Pattern (the input you edit). The Generator page's
SEQ row shows the generated triggers of the active track for the viewed bar, with the
playhead chase. The draw goes through `led_updatePatternTrack()` with a generator source
(Q6).

---

## 6. The first type: Euklid (proposed, Q1)

| Kind | Cell | Range | Default | Meaning |
|---|---|---|---|---|
| auto 1 | `len` | 1..128 (stored 0..127) | 16 | cycle length |
| auto 2 | `hit` | 0..128 (clamped to `len`) | 4 | hits per cycle |
| auto 3 | `rot` | 0..127 (mod `len`) | 0 | rotation |
| auto 4 | `vel` | 1..127 | 100 | velocity of generated hits |
| fixed 1 | `src` | `off` / `stp` / `ord` | `off` | specials source (below) |
| — | `ato` | off/on | off | G3 |

- **Build:** run the bucket from `euklid_transferPattern()` (`EuklidGenerator.c:276–292`)
  over `len`, and repeat the cycle across all 128 steps. The track's own length, scale and
  play mode then act on the result: `len` 5 on a 16-step track is a polymeter.
  Length/scale/play-mode changes need no rebuild.
- **Slot map (Q4):** note, velocity, condition, roll.
  - `src off`: note = `PAT_DEFAULT_NOTE`, velocity = `vel`, condition 0, roll 0.
  - `src stp`: a hit at step s takes note, condition and roll from the Pattern step s,
    and velocity from that step if it has one, else `vel`.
  - `src ord`: the k-th hit takes them from the k-th active Pattern step (k wraps). This
    is "rhythm from Euklid, notes from the Pattern". It is the type's use of the Pattern as
    input, and why a Pattern edit requests a rebuild.
- **Retire the old code:** `EuklidGenerator.c`'s Pattern-writing paths
  (`euklid_transferPattern()`, the setters) and the compiled-out callers
  (`menu.c:20688–20719`, `buttonHandler.c:31`, `:828–841`, `:3585`, `:3628`, `:4074`,
  `ledHandler.c:1418`, `ENABLE_EUKLID_PAGE`) are removed. The bucket moves into the new
  type. `EUKLID_PAGE` and `PAR_EUKLID_*` are removed if nothing else references them;
  the implementation schedule will list each reference.

---

## 7. Roll special values `leg`, `sus`, `l+s` (G14)

**Code:** none of the three exists (§1.6).

**Proposed encoding:** roll indices 10 `leg`, 11 `sus`, 12 `l+s`. Changes:
- `stepRollRateNames` count 10 → 13;
- the STEP roll clamp (`menu.c:7083`) 9 → 12;
- `seq_rollPpq()` unchanged (0 for ≥ 10).

The trigger branch at `sequencer.c:3963` becomes `sp.roll > 0u && sp.roll < 10u`, so these
values fire a plain step (avoiding the silent-step hazard, §1.6). They then pass a
gate-mode flag to the trigger. PERF overlay types (`perfBtnTypeNames`, values 6..14 = roll
1..9) are unchanged unless you want them there.

**Proposed meaning (needs your rules, Q14):**

| Value | Gate | Envelope / pitch on the next trigger |
|---|---|---|
| `leg` | the engine's one step (as now) | the next note is legato: no envelope retrigger; Alkaline glides as its slide does; engines without legato ignore it |
| `sus` | held to the next trigger on this track (not one step) | normal retrigger |
| `l+s` | held to the next trigger | legato |

Engines with a gate are Alkaline, Colloid and Katran (to confirm in the schedule). On the
drum family, `sus` and `l+s` would need the plain-step length window extended past one step
(`seq_lenStart()`, `sequencer.c:3397`), which today only shortens. This is a separate
step (§9, step 8). It does not block the generator work.

---

## 8. RAM (sign-off needed)

| Row | Owner | Region | Bytes | Object |
|---|---|---|---:|---|
| S | `GeneratorEngine.c` | SRAM1 | 112 | `gen_trig[7][16]` |
| S | `GeneratorEngine.c` | SRAM1 | 3,584 | `gen_spec[7][128][4]` |
| R | `GeneratorEngine.c` | SRAM1 | 80 | runtime state (§3.3) |
| C | `SceneData.c` | SRAM1 | 1,547 | 91 B × 17 `scene_t` (`scenes[16]` + `scene_background`); padding to be measured |
| F1 | `sequencer.c` | SRAM1 | +4 | `seq_scene_automation_dirty` 32 → 64 bit |
| M | `menuGenerator.c` | SRAM1 | ≈ 8 | browse candidate, edit flag, screen memory |
| | | **Total** | **≈ 5,335 B** | |

- The free SRAM1 figure at `1d2e42ca` is not recorded: S105 did not record one, and there
  is no ELF in `build/`. S104 recorded `.bss` 422,704 B (`S104_P1_IMPLEMENTATION.md:4236`).
  Step 0 measures it with `link_budget.py` before any edit.
- `MEMORY.md` reserves free SRAM1 for future Pattern data. The store is Pattern-like data,
  but it is a new owner, so it is called out here.
- AutoSave: no RAM change (§4.7).

---

## 9. Order and checks

| Step | Work | Gate |
|---|---|---|
| 0 | Baseline: `make all && make img`, `link_budget.py`, `elf_hotloops.py`, inlined-set list into `tools/dsp_test/baselines/s106p1/` | record SRAM1/flash/ITCM |
| 1 | F1: the 64-bit Scene automation mask (§10) | host test: dirty bit set and restored for index 41 |
| 2 | Scene targets: shuffle out, 28 `GEN_PARAM` rows in, every kind switch updated (§4.6) | `_Static_assert` ≤ 64; picker lists 63 |
| 3 | Scene settings, setters, `sceneset` keys, AutoSave relayout and version 4, keep-mask fields, copy/clear (§3.1, §4.7, §4.8) | save/load round trip; AutoSave restore |
| 4 | `GeneratorEngine` core: registry with `off` only, store, ready/request masks, the `pat_markSceneDirty()` hook, the service, switch-time build (§4.1–§4.4) | host test `gen_engine`: G9/G10/G11 laws |
| 5 | Sequencer routing and the `ato` gate (§4.5) | `off` everywhere: TIM3 behaviour identical; loop gate 86/86; ISR bodies changed only in the routed reads |
| 6 | Menu: mode 0x06, `GENERATOR_PAGE`, `MENU_CELL_GENERATOR`, `gen` transaction, Morph view, LEDs (§5) | device: entry, browse, commit, `off` shows one row |
| 7 | Euklid type and removal of the old Euklid paths (§6) | host test: Euklid output against the reference bucket for every `len`/`hit`/`rot`; `src` laws |
| 8 | Roll `leg`/`sus`/`l+s` (§7), after Q14 | per-engine host laws; device by ear |

After each step: `make -C tools/dsp_test -k all`. Only the known failures may remain:
`voice_control` 14, `alkaline` AL-OD0, and whatever S105 left (to be listed at step 0).

**CPU:** no per-sample render change. TIM3 gains one gen-type read per routed call (Est.
< 30 cycles per track per step). The foreground service is bounded per pass (§4.4).
**Flash (Est.):** +5..8 KB against 42,884 B headroom (S105 `link_budget.py`).

---

## 10. Defect found: the Scene automation dirty mask (F1)

- **Code:**
  - `sequencer.c:1563` declares `static uint32_t seq_scene_automation_dirty;`;
  - `:4183` does `seq_scene_automation_dirty |= (1u << index);`;
  - `:1674` and `:1695` clear with `(uint32_t)~(1u << index)`;
  - `:1773` copies it into a `uint32_t mask`.
- The table has 42 rows (§1.5), so `index` reaches 41.
- A shift by 32..41 is undefined in C; GCC on Cortex-M7 emits a register `LSL`, which gives
  0. The bits for `5sc`, `6sc`, `7sc` and `1sh`..`7sh` are never set.
- **Effect:** if only those targets were automated, `seq_restoreAllSceneAutomation()` sees
  `mask == 0` and returns at `:1777–1778`. It never calls
  `presetMorph_clearAllTrackParamStepOverrides()`, so the step overlay survives a
  transport reset or Pattern restart.
- **Fix (step 1):**
  - make the variable and the three local masks `uint64_t`;
  - use `(1ull << index)`;
  - use `__builtin_ctzll` at the restore loop.

  It needs no RAM sign-off (4 B, listed in §8).

---

## 11. Statements made out of date (for you to act on)

- `MEMORY.md` §Sequencer/Timing "Per-track timing (S078)" and `PATTERN_DYNAMIC_STACK.md`
  §6.4: shuffle is no longer step-automatable (Morph remains).
- `MENU_UI_SPEC.md`: SHIFT+MODE STEP has an action (mode 0x06, the Generator page);
  `SOM_PAGE` is renamed; MODE3 blinks in mode 6.
- `MODULE_INTERCHANGE_SPEC.md`: the new owner `GeneratorEngine`; the sequencer's step reads
  are routed; `EuklidGenerator.c` is retired.
- `FILESYSTEM_SPEC.md` / `STORAGE_SRAM_MANIFEST.md`: the AutoSave Scene/Effect boundary
  (192/448), format version 4, the new `sceneset` keys, the RAM rows of §8.
- `PERFORMANCE_RECORD_SPEC.md`: what record and erase do on a generator track (after Q7).
- `knowledge_files/drafts/GENERATOR_PAGE_SPEC_DRAFT.md`: superseded by this plan (§0, §1.2).
- `MEMORY.md` §Menu/UI "Step edit (S099 P2)": roll values 10..12 (after step 8).

---

## 12. Risks

- **R1 RAM.** ≈ 5.3 KB SRAM1 against a figure not yet measured at HEAD. If SRAM1 is tight,
  the 1,547 B of Scene settings is the part to shrink (Q5, Q11: nibble-packing the fixed
  parameters, or no Morph endpoints).
- **R2 AutoSave layout change.** Format version 4 rejects every existing AutoSave record at
  the first boot of the new image, so the unsaved live state since the last explicit save
  is lost once. Saved Scene/Bank files are unaffected (their keys are additive).
- **R3 Target ID reuse.** IDs 419..425 change meaning from shuffle to 1g1..2g3. Any
  existing Pattern with shuffle step automation will drive those generator parameters
  instead. While a track's generator is `off`, the overlay is set but nothing reads it, so
  the effect is only seen once a generator is turned on. With no migration (binding), the
  remedy is to clear that automation. The alternative is to leave IDs 419..425 retired, but
  that needs 70 IDs, more than the block holds.
- **R4 Tearing.** One store, rewritten while TIM3 reads it (§4.4). One step may mix old and
  new. Accepted by G11; ordered writes keep it to one step.
- **R5 First step after a switch.** If the switch-time build is not done (a type with a long
  `build()`, or Q12 decides background-only), the boundary step of every changed generator
  track is silent (G10). This is audible on the downbeat.
- **R6 Rebuild storms.** A voice Morph swept by an LFO changes the effective generator values
  every block. The quantised change test and the one-track-per-pass service bound the cost,
  but the output can change many times per bar. That is musical, but it is a new kind of
  movement.
- **R7 Interaction surface.** Each of these reads Pattern step data and must either be routed
  or be confirmed as reading the input:
  - live record (ring, fired bit);
  - live erase;
  - echo marks;
  - the PERF looper;
  - conditions that test fills or the last condition;
  - ramps (next-step pairing);
  - deferred and shuffled fires;
  - MIDI note modes;
  - copy/clear;
  - Scene 17.

  §4.5 covers the TIM3 sites. The implementation schedule must list every one.
- **R8 menu.c growth.** A third delegated cell kind adds branches to the generic renderer,
  encoder and pot paths, where `EFFECT_PAGE` already has about 30. Keeping `menuGenerator.c`
  narrow (no hooks, no sequence) limits this, but `menu.c` is 21,460 lines.
- **R9 Edit-mask semantics.** If generator edits fan out (Q9), a fan-out to a played Scene of
  another track must request that track's rebuild. Missing one gives a track that plays
  stale indefinitely, until something else requests a rebuild.
- **R10 F1 changes behaviour.** After the fix, automated `5sc`..`7sc` overlays are restored at
  a transport reset as designed. Anyone relying on the stuck overlay will hear a difference.
- **R11 Roll values 10..12.** Until step 8 lands, the clamp stays at 9. If a file holds
  10..12 from another build, the step is silent through `seq_rollBegin()` (§1.6).

---

## 13. Ambiguities in the request

- **A1 "4 of the 8 specials".** The tree has 7 (`PatternData.h:56–63`; bit 7 is reserved).
  The plan reads it as "4 of the specials", plus the trigger bit held separately.
- **A2 "the first generator to implement".** The pasted text describes the framework and
  `off`, not a concrete type. Euklid is proposed (Q1).
- **A3 `leg` "should already" exist.** It does not (§1.6), and the eight S099 questions about
  it are not in the tree.
- **A4 "when the scene or track switches".** Read as "the track's played Scene changes", by
  whole-Scene change, per-track assignment, align or Scene 17. Not read as selecting a
  different track on the panel.
- **A5 "does not fire" vs "may play stale".** Read as: a switch clears ready (silent until
  rebuilt); a Pattern or parameter change keeps ready (old output until rebuilt).
- **A6 `ato`.** Unclear whether off also blocks automation of *other* targets stored on the
  track (voice parameters) and the track's own generator-parameter automation (Q3).
- **A7 Erase and record on a generator track.** "Pattern data updated via edit or record"
  implies the Pattern stays the editable input. The plan keeps record and erase writing the
  Pattern. Erasing therefore removes nothing audible unless the type reads the Pattern
  (Euklid `src stp`/`ord`).
- **A8 "potentially bit-packed".** Read as: a type may split one 7-bit automatable byte into
  sub-fields. The Scene storage stays one byte per slot.
- **A9 Morph for generator parameters.** Read as the S078 track-timing law: a Scene endpoint
  per parameter and the slot's resolved Morph amount, with track 7 on slot 6.
- **A10 Generated length.** The plan always builds all 128 steps, so track length, scale and
  play-mode changes do not need a rebuild. If a type should instead fit its output to the
  track length, those become rebuild triggers.

---

## 14. Open architectural questions (please answer before implementation)

1. **Q1 First type.** Euklid as in §6 (recommended: existing algorithm, no new concepts), or
   another type?
2. **Q2 Target allocation.**
   - **(A)** 28 per-track targets, removing shuffle automation as you said (recommended;
     allows a generator parameter to be automated from any track's steps).
   - **(B)** 4 "own track" targets resolved by the track whose step carries them. That
     keeps shuffle automatable (46 IDs), but generator automation can then live only on
     the generator track's own steps, which ties it to Q3.
3. **Q3 What `ato` off blocks.**
   - **(a)** all Pattern automation of that track;
   - **(b)** all except the track's own generator-parameter targets (recommended, so a
     generator can be automated without also replaying the Pattern's voice automation);
   - **(c)** only voice/instrument targets.
4. **Q4 Euklid specials.** Is the slot map note/velocity/condition/roll right, and is `src`
   (`off`/`stp`/`ord`) the input model you want? Is `vel` the right fourth automatable
   parameter?
5. **Q5 Parameters across a type change.**
   - **(a)** one shared 4 + 4 byte set per track, reset to the new type's defaults when
     the type changes (recommended; as `effects_changeType()`);
   - **(b)** kept and reinterpreted;
   - **(c)** stored per type (7× the RAM).
6. **Q6 STEP-mode view of a generator track.** STEP shows the Pattern (input) and the
   Generator page shows the output (recommended), or STEP shows the output?
7. **Q7 Record and erase on a generator track.** Write the Pattern (the input) and rebuild
   (recommended), or refuse while the type is not `off`?
8. **Q8 Mute, solo and the looper** act on the generated output, read through the routed
   readers. Agreed?
9. **Q9 Edit-mask fan-out.** Do generator edits fan out to the Scenes in the VOICE edit
   mask, like the other track settings (recommended)?
10. **Q10 Copy/clear.** Track copy carries the track's generator settings and Scene copy
    carries all of them (recommended)?
11. **Q11 AutoSave.** Is moving the Scene/Effect boundary (192/448) and bumping the format
    to 4 acceptable, given the one-time loss of the unsaved live state? The alternative is
    to pack each track into 6 cells to fit the 42 free cells. That means no Morph
    endpoints and nibble-packed fixed parameters.
12. **Q12 Switch-time build.** A synchronous build of the changed tracks inside the Scene
    change executor, so the first step is ready (recommended; needs every type's
    `build()` × 7 under 1 ms), or background only, accepting R5?
13. **Q13 RAM.** Do you approve §8 (≈ 5,335 B SRAM1, owners as listed)?
14. **Q14 Roll gate values.**
    - Are the meanings in §7 right?
    - Should `sus` hold to the next trigger, or to the end of the next step?
    - On the drum family, should `sus` extend the length window or be ignored?
    - The eight S099 legato questions are not in the tree: can you restate them, or
      should §7 replace them?
    - Should the three values also appear in the PERF overlay types?
15. **Q15 Target names.** Generic `1g1`..`7g4` in the automation picker, or names from the
    track's current type (`1hi`), which change with the type?
16. **Q16 Held-step editing on the Generator page.** Should holding SEQ buttons and turning
    an automatable cell write step automation on the active track, as on the voice pages
    (recommended)? If so, the SEQ row is the step selector on this page rather than the
    output display of §5.4. The output display would then show only while no step is held.
17. **Q17 MIDI.** Should generator automatable parameters be MIDI-Learnable and CC-mappable
    (as menu edits, Q1 rule)? The type itself never is (G4).
18. **Q18 Page layout.** Is the screen layout in §5.2 acceptable: `gen` plus three
    automatable cells first, then automatable 4, `ato` and the fixed cells?
