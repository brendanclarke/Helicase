# S106 P1 — Generators: implementation schedule

**Date:** 2026-11-28. **Branch:** `dev-ph7-alkaline`, clean at `1d2e42ca`.
**Status:** schedule. No source was changed. The design is
`S106_P1_GENERATORS_PLAN.md` (revision 5, every question answered).

**Decisions in force:** the plan's G1–G43, its §14 decision table (FQ1–FQ12), and
RAM approved at ≈ 5,483 B (Q13). §0.4 lists the schedule-level decisions this dive had
to make where the plan was silent or the code forced a choice. **Please confirm them**,
the RAM delta (§0.3) above all.

---

## 0. How to use this schedule

### 0.1 Conventions

- **Line numbers** are for the clean tree at `1d2e42ca`. Every entry also quotes an
  anchor (a line to search for), because earlier edits in the same file shift the
  numbers. **Within one file, apply entries bottom-up** (highest line first), or find
  each anchor by text.
- **Action:** **Add** (new code at the place named), **Modify** (replace the quoted
  lines with the new form shown), **Remove** (delete the quoted range), **New file**.
- **Comment blocks:** every change carries one. The block is paste-ready documentation
  for the code beside it. Following the S084 rule, a comment never cites a schedule
  entry ID (T-3, M-12 …); it may cite the session (`S106 P1`) and plan rules (G-numbers
  are plan section labels, so comments cite the plan as "S106 P1 plan §x").
- **Code** is complete where it is new. For a Modify, the entry shows the new form of
  the lines it replaces.
- **No entry edits `MEMORY.md` or `knowledge_files/`.** §12 lists the statements made out
  of date.

### 0.2 Order of work (each step: build, host tests, gates; then the next)

| Step | Entries | Why this order |
|---|---|---|
| 0 | B-1 | Baseline before any edit |
| 1 | F-1 .. F-6 | 64-bit Scene automation mask; the new targets depend on it |
| 2 | T-1 .. T-12 | Scene target table: shuffle rows out, 28 lane rows in |
| 3 | S-1 .. S-17 | Scene settings, setters, AutoSave v4, `sceneset.scg`, identity, copy |
| 4 | G-1 .. G-12 | `GeneratorEngine` with `off` only; hooks; service; switch-time build |
| 5 | R-1 .. R-10 | Sequencer read routing and the `ato` gate |
| 6 | M-1 .. M-30 | Menu, buttons, LEDs, MIDI Learn fields |
| 7 | K-1 .. K-6 | Roll values `leg` / `sus` / `l+s` (encoding only) |
| 8 | A-1 .. A-9 | The Arpeggiator, `MNM_ARP` and the live arp |
| 9 | L-1 .. L-14 | Colloid legato/sustain; the live-record `leg` rule |
| 10 | C-1 .. C-6 | The commit job |

**After each step:**
- `make all && make img`;
- `make -C tools/dsp_test -k all`: only the known failures may remain (`voice_control`
  14, `alkaline` AL-OD0, and whatever B-1 records);
- the step's own host laws (§11);
- `python3 tools/dsp_test/elf_hotloops.py` and the inlined-set diff against the B-1
  baseline. Steps 5, 9 and 10 touch TIM3 or the render-loop trigger drain, so the ISR
  bodies are diffed too (§11.2).

### 0.3 RAM

The approved table (plan §8) is reproduced with the rows this dive measured or added.
**The bold rows are new beyond the approved ≈ 5,483 B and need your sign-off (+30 B).**

| Row | Owner | Region | Bytes | Object |
|---|---|---|---:|---|
| S | `GeneratorEngine.c` | SRAM1 | 112 | `gen_trig[7][16]` |
| S | `GeneratorEngine.c` | SRAM1 | 3,584 | `gen_spec[7][128][4]` |
| R | `GeneratorEngine.c` | SRAM1 | 163 | runtime state, measured design (§4, G-2): masks 6, request 1, built type 7, built lanes 28, built length 7, overrides 28 + 4, Scene-dirty 4, live arp 7 × 11 = 77, RNG 4 (plan: 228; the live arp is computed, not tabled, −63 B) |
| C | `SceneData.c` | SRAM1 | 1,547 | 91 B × 17 `scene_t` (padding measured at S-1) |
| F1 | `sequencer.c` | SRAM1 | +4 | `seq_scene_automation_dirty` 32 → 64 bit |
| M | `menuGenerator.c` | SRAM1 | 6 | browse candidate, edit flag, Morph view, commit state, LED cache valid, last type |
| **L** | **`InstrumentManager.c`** | **SRAM1** | **+6** | **`instrumentManager_gateHold[6]`, the sustain hold request (L-4)** |
| **L** | **`MidiNoteModes.c`** | **SRAM1** | **+14** | **`midiNoteMode_held[7]` (keys held per track, normal mode), `midiNoteMode_legPend[7]` (L-10)** |
| **L** | **`LiveRecord.c`** | **SRAM1** | **+7** | **`liveRec_lastHitStep[7]`, the step a hit was placed on (L-12)** |
| **M** | **`MidiLearn.c`** | **SRAM1** | **+1** | **`midiLearn_targetField`, the field of a waiting generator Learn (M-28)** |
| **A** | **`GeneratorEngine.c`** | **SRAM1** | **+2** | **`gen_freezeMask`, `gen_uiChanged` (C-4, M-24)** |
| | | **Total** | **≈ 5,446 B** | approved ≈ 5,483; net −37 B; new rows +30 B |

The total is below the approved figure, because the live arp shrank. The bold rows are
new objects with new owners, so they are listed for sign-off all the same.

### 0.4 Schedule decisions needing your confirmation

| # | Decision | Why the dive made it |
|---|---|---|
| SD1 | **The Generator page uses the generic static-page geometry:** SELECT 1 = `gen in1 in2 in3` / `oct dir vel stp` (two halves), SELECT 2 = `nln ato cmt`. The plan put `nln ato cmt` on a third screen of SELECT 1. | Static pages have two four-cell halves per SELECT (`menu_switchSubPage()`, `menu.c:19697–19741`). A third screen needs the Effect page's private screen machinery (`menuEffects.c`), about 30 more `menu.c` branches. |
| SD2 | **Generator settings are shown and edited for the active Scene** (`scene_getActiveIndex()`), fanned out through `bank_editTargetMask()` like every other Scene setting. A commit requires the active Scene to be the track's played Scene. The plan said "viewed Scene". | Scene-setting cells already work this way (`menu.c:5558–5620`, Kit/Scene cells). The viewed Pattern is a Pattern concept. |
| SD3 | **The STEP `shu` cell loses MIDI Learn and NRPN** as well as step automation. | Learn rows and NRPN address Scene target IDs (`MidiLearn.c:112–116`); shuffle no longer has one. |
| SD4 | **`MNM_ARP` records each note-on as a plain hit into the Pattern** (the arp's input) and does not trigger the voice directly. | G30: live record writes the Pattern only. The recorded input is what the arp replays. |
| SD5 | **Live erase on a generator track erases the Pattern step only.** The generated trigger on that step still plays until the rebuild. | The plan's "acts on the Pattern, gated by the Pattern's own bit" (§4.5); the generated output follows at the next rebuild. |
| SD6 | **A generator lane is never MIDI-writable as a raw byte:** `menu_commitTargetEdit()` refuses `GEN_LANE` IDs, and field Learn rows are the only MIDI route. The file token is `scn,<lane>,<type>:<field>` (for example `scn,1g1,arp:in1`). | FQ11. A raw NRPN write would land on two fields at once. |
| SD7 | **Legato skips only the three envelope stage restarts.** The LFO retrigger, velocity nodes, per-note random draws and oscillator retargets run as for any trigger. | G37 names only attack-decay. |
| SD8 | **The sustain hold renews the Colloid gate to the step length plus 64 samples (two blocks)**, so the release lands after the step boundary, not on the block before it. | `gateLeft` counts whole 32-sample blocks (`ColloidVoice.h:439–481`); a renewal of exactly one step can expire a block early. |
| SD9 | **`clear track … all` does not reset the generator.** | Not in the plan; flagged in case you want it. |
| SD10 | **A track's rebuilds are frozen while its commit job runs.** | The commit's own Pattern writes would otherwise request rebuilds that change the store it is reading. |
| SD11 | **Tracks 6 and 7 share one held-key group** (slot 5) in `MidiNoteModes`, as paraphonic Colloid already does. | The note-mode pools are keyed by slot (`MidiNoteModes.c:122–126`, `:205`). Two arp tracks on slot 5 fed on one channel see one held set. |

### 0.5 Corrections to the plan found in the dive

| Plan | Code | Schedule |
|---|---|---|
| F1 lists four sites | Five: `seq_clearSceneAutomationBit()` refuses `index >= 32u` (`sequencer.c:1693`) | F-4 |
| `seq_stepFires()` + `seq_stepSpecials()` (§4.5) | The live arp must advance exactly once per step event | One reader `seq_stepRead(track, step, advanceLive, &sp)` called once per step event and reused (R-1, R-4) |
| The recording fired bit routed | G30: live record works on the Pattern only | `rec_fired` stays Pattern-based on generator tracks, evaluated with the side-effect-free `seq_conditionPassesPure()` (R-4) |
| `ato` gate at several sites | `seq_fireDeferred()` also queues automation (`sequencer.c:6739`) | The gate sits inside `seq_queueStepAutomations()` (R-7) and a counted wrapper (R-8, R-9) |
| Live arp `sequence[16]` (§3.3) | `u-d` with 3 intervals and +3 octaves is 18 entries | The note is computed from `{root, intervals, R, dir, pos}` (A-2 `arp_noteAt()`) |
| Keep-mask fields added to `PRESET_TRACK_SETTINGS_FIELDS(X)` | The macro indexes `f[track]` (`presetManager.c:3672–3674`); `gen_lane[7][4]` is 2-D | A row macro with `memcmp`/`memcpy` (S-14 .. S-16) |
| `Colloid_triggerVoice()` gains `legato` | 64 host-test calls (`tools/dsp_test/modules/colloid.c`) | A twin `Colloid_triggerVoiceLegato()` over a shared static body (L-6) |
| The legato rule detected at note-on | The release usually arrives after the new note-on; `MNM_NORMAL` keeps no held state (`MidiNoteModes.c:654–664`) | Held state per track in `MidiNoteModes`, decided at the release, applied by `liveRec_markLegato()` (L-10 .. L-13) |
| `gen_builtScene[7]` | Rebuild requests use the played Scene; nothing reads the built Scene | Dropped (−7 B) |
| Scene targets "1 spare" | 42 − 7 + 28 = 63 | Unchanged; the spare stays free (FQ12) |

---

## B. Baseline

### B-1 — record the baseline — **no code**

```
make all && make img            # link_budget.py runs at the end (Makefile:278)
make -C tools/dsp_test elf_p4_snapshot   P4BASE=baselines/s106p1/p4base
make -C tools/dsp_test elf_s099_snapshot S099BASE=baselines/s106p1/engines
python3 tools/dsp_test/elf_symdump.py --elf build/lxr02.elf \
    --sym TIM3_IRQHandler --sym audio_check_and_render \
    --sym voiceControl_processPending --sym mixer_calcNextSampleBlock \
    --out tools/dsp_test/baselines/s106p1/isr.txt
make -C tools/dsp_test -k all > tools/dsp_test/baselines/s106p1/host_suite.txt 2>&1
```

- Copy the `link_budget.py` lines (flash, ITCM, SRAM1 `.bss`) into
  `tools/dsp_test/baselines/s106p1/link_budget.txt`.
- The RAM rows of §0.3 are checked against the after-step-3 and after-step-10 figures.
- **The gates after each step:**
  - `make -C tools/dsp_test elf_p4_check P4BASE=baselines/s106p1/p4base`
    (oscillator and mixer loops exact);
  - `elf_s099_check S099BASE=baselines/s106p1/engines` (engine loops exact);
  - `elf_symdump.py --compare` of the four bodies above.
- The symbol list is the S099/S104 practice. A body that LTO splits into `.part`/
  `.constprop` clones is matched by base name (`elf_symdump.py` header).

---

## 1. The 64-bit Scene automation mask (F1)

Every entry is in `Core/Sequencer/sequencer.c`. Apply bottom-up.

### F-6 — `sequencer.c:4183` — **Modify** (the set in `seq_applySceneAutomation()`)

Anchor: `seq_scene_automation_dirty |= (1u << index);`

```c
	/*
	 * S106 P1: 64-bit restore bitmap. The Scene target table has up to 64
	 * rows (SceneModTargets.c static assert), so a 32-bit shift by an index of
	 * 32..63 was undefined; on Cortex-M7 the register LSL gave 0 and the
	 * targets at indices 32..41 (tracks 5-7 scale, all shuffle before S106)
	 * never set their bit. Affiliates: seq_restoreAllSceneAutomation().
	 */
	seq_scene_automation_dirty |= (1ull << index);
```

### F-5 — `sequencer.c:1773`, `:1804` — **Modify** (`seq_restoreAllSceneAutomation()`)

Anchor: `uint32_t mask = seq_scene_automation_dirty;` (inside `seq_restoreAllSceneAutomation`)

```c
    uint64_t mask = seq_scene_automation_dirty;   /* S106 P1: 64-bit */
```

and inside the `while (mask)` loop (anchor
`uint8_t index = (uint8_t)__builtin_ctz(mask);`, `:1804`):

```c
        uint8_t index = (uint8_t)__builtin_ctzll(mask);   /* S106 P1: 64-bit */
```

The loop's last statement `mask &= (mask - 1u);` (`:1841`) is unchanged: the arithmetic is
done in the 64-bit type of `mask`.

### F-4 — `sequencer.c:1691–1696` — **Modify** (`seq_clearSceneAutomationBit()`)

```c
void seq_clearSceneAutomationBit(uint8_t index)
{
    /*
     * S106 P1: the bitmap covers the whole 64-row Scene target block
     * (SceneModTargets.c asserts SCENE_MOD_TARGET_COUNT <= 64). The former
     * `index >= 32` guard silently refused the R12 clear for indices 32..41.
     */
    if (index >= 64u)
        return;
    seq_scene_automation_dirty &= ~(1ull << index);
}
```

### F-3 — `sequencer.c:1674` — **Modify** (in `seq_clearSceneAutomationDirtyFor()`)

Anchor: `seq_scene_automation_dirty &= (uint32_t)~(1u << index);`

```c
            seq_scene_automation_dirty &= ~(1ull << index);   /* S106 P1 */
```

### F-2 — `sequencer.c:1634`, `:1640` — **Modify** (the same function)

```c
    uint64_t mask = seq_scene_automation_dirty;   /* S106 P1: 64-bit */
```

```c
        uint8_t index = (uint8_t)__builtin_ctzll(mask);
```

`mask &= mask - 1u;` at `:1645` is unchanged.

### F-1 — `sequencer.c:1552–1563` — **Modify** (the declaration and its comment)

```c
/*
 * Scene-target step-automation restore bitmap (8 B normal SRAM1; S106 P1).
 *
 * What: one bit per Scene target-table entry, indexed by
 * sceneModTarget_indexFromId(). Why: Scene-target step values are runtime
 * overlays, so transport restore must know which retained values need to be
 * re-applied without serializing or dirtying them. S106 P1: 64 bits, the
 * full Scene block (the table holds up to 64 rows, SceneModTargets.c); the
 * 32-bit form lost indices 32..41. Lifetime: static until the next
 * restore/clear. Owner: Sequencer. Context: set and cleared in the
 * foreground drain and menu paths; no TIM3 access. Affiliates:
 * seq_applySceneAutomation(), seq_restoreAllSceneAutomation(),
 * seq_clearSceneAutomationDirtyFor(), seq_clearSceneAutomationBit(), and the
 * kind-specific Preset/InstrumentManager/GeneratorEngine runtime overlays.
 */
static uint64_t seq_scene_automation_dirty;
```

The zero assignments at `:1578` and `:1600` need no change.

---

## 2. Scene targets: shuffle out, 28 generator lanes in (G5, G7)

### T-1 — `Core/Bank/Scene/SceneModTargets.h:63` — **Modify** (append a kind)

Anchor: `    SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE` (the last enumerator)

```c
    SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE,
    /*
     * Generator lane (S106 P1).
     *
     * What: one 7-bit automatable lane of the generator on track
     * `voice_slot` (0..6); `param` is the lane 0..3. A lane is a packed byte:
     * the track's generator type decides which fields its bits hold
     * (GeneratorEngine.h gen_field_desc_t). Why: generators have four
     * step-automatable, morphable parameters per track (S106 P1 plan G5);
     * the Scene target namespace is Scene-wide, so a lane may be automated
     * from any track's steps, exactly as `3ln` names track 3.
     * Uses: USE_AUTOMATION only (no velocity, no LFO). TRACK_SHUFFLE stays in
     * the enum after shuffle lost its rows, so no enumerator value moves (the
     * value is never stored).
     * Affiliates: SceneModTargets.c rows 35..62, seq_applySceneAutomation(),
     * gen_setStepOverride(), menuGenerator.c.
     */
    SCENE_MOD_TARGET_KIND_GEN_LANE
```

### T-2 — `SceneModTargets.h` — **Modify** (the descriptor struct: append `param`)

Anchor: `    const char *short_name;` followed by `} scene_mod_target_descriptor_t;`

```c
    const char *short_name;
    /*
     * Sub-index within the kind (S106 P1): the lane 0..3 of a
     * SCENE_MOD_TARGET_KIND_GEN_LANE row; 0 for every other kind. Appended
     * last so the positional initialisers of the existing rows (which stop at
     * short_name) leave it zero.
     */
    uint8_t param;
} scene_mod_target_descriptor_t;
```

### T-3 — `SceneModTargets.h` after the `sceneModTarget_slot6DecayId()` prototype (`:148`) — **Add**

```c
/*
 * Resolve the Scene target ID of one generator lane (S106 P1).
 *
 * Inputs: track 0..6, lane 0..3. Output: the canonical ID (1g1 = 419 ..
 * 7g4 = 446 today) found by kind/track/lane, so callers do not depend on
 * table order; INSTRUMENT_PARAM_INVALID for an invalid track or lane.
 * Clients: menuGenerator.c (held-step writes, underline search, field
 * Learn), the commit job, MIDI Learn.
 */
uint16_t sceneModTarget_genLaneId(uint8_t track, uint8_t lane);
```

### T-4 — `Core/Bank/Scene/SceneModTargets.c:115–163` — **Modify** (shuffle rows out, lane rows in)

Replace the comment at `:115–121` (anchor `S078 §5.5: per-track step-automation targets`)
with the block below. Keep rows `:122–149` (IDs 21..34, length and scale). Remove the
seven shuffle rows `:150–163` (from
`{ SCENE_MOD_TARGET_ID(35u), SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE, 0u,` to the
`"7 Shuf", "7sh" },` row) and put the lane rows in their place.

```c
    /*
     * S078 §5.5: per-track step-automation targets for the morphable timing
     * parameters. IDs 405..411 are length and 412..418 scale; voice_slot
     * carries the track index for seq_applySceneAutomation(). Only the
     * automation flag is offered: these are pattern-timing values, not
     * velocity/LFO modulation destinations.
     * S106 P1: shuffle is no longer step-automatable (its Morph endpoint and
     * menu edit stay); its seven rows (IDs 419..425) are removed to make room
     * for the generator lanes below. No migration: a stored entry with one of
     * those IDs now addresses the lane that took the ID.
     */
```

The lane rows, after the `"7 Scale", "7sc" },` row:

```c
    /*
     * Generator lanes (S106 P1; IDs 419..446, indices 35..62).
     *
     * What: four 7-bit lanes per track, grouped by track (1g1..1g4, 2g1 ..
     * 7g4). voice_slot = the track whose generator the lane drives; param =
     * the lane. Range 0..127: the type packs its fields into the byte, so the
     * full range is meaningful. Only USE_AUTOMATION is set (plan G5: step
     * automation and Morph, no LFO, no velocity). Why grouped by track: the
     * automation picker walks this table in order, so one track's four lanes
     * sit together. 63 rows leave one ID of the 64-ID block free (FQ12).
     * Affiliates: sceneModTarget_genLaneId(), SCENE_MOD_TARGET_KIND_GEN_LANE.
     */
#define SCENE_GEN_LANE_ROW(index_, track_, lane_, long_, short_) \
    { SCENE_MOD_TARGET_ID(index_), SCENE_MOD_TARGET_KIND_GEN_LANE, (track_), \
      0u, 127u, SCENE_MOD_TARGET_USE_AUTOMATION, "Track", long_, short_, \
      (lane_) }
    SCENE_GEN_LANE_ROW(35u, 0u, 0u, "1 Gen 1", "1g1"),
    SCENE_GEN_LANE_ROW(36u, 0u, 1u, "1 Gen 2", "1g2"),
    SCENE_GEN_LANE_ROW(37u, 0u, 2u, "1 Gen 3", "1g3"),
    SCENE_GEN_LANE_ROW(38u, 0u, 3u, "1 Gen 4", "1g4"),
    SCENE_GEN_LANE_ROW(39u, 1u, 0u, "2 Gen 1", "2g1"),
    SCENE_GEN_LANE_ROW(40u, 1u, 1u, "2 Gen 2", "2g2"),
    SCENE_GEN_LANE_ROW(41u, 1u, 2u, "2 Gen 3", "2g3"),
    SCENE_GEN_LANE_ROW(42u, 1u, 3u, "2 Gen 4", "2g4"),
    SCENE_GEN_LANE_ROW(43u, 2u, 0u, "3 Gen 1", "3g1"),
    SCENE_GEN_LANE_ROW(44u, 2u, 1u, "3 Gen 2", "3g2"),
    SCENE_GEN_LANE_ROW(45u, 2u, 2u, "3 Gen 3", "3g3"),
    SCENE_GEN_LANE_ROW(46u, 2u, 3u, "3 Gen 4", "3g4"),
    SCENE_GEN_LANE_ROW(47u, 3u, 0u, "4 Gen 1", "4g1"),
    SCENE_GEN_LANE_ROW(48u, 3u, 1u, "4 Gen 2", "4g2"),
    SCENE_GEN_LANE_ROW(49u, 3u, 2u, "4 Gen 3", "4g3"),
    SCENE_GEN_LANE_ROW(50u, 3u, 3u, "4 Gen 4", "4g4"),
    SCENE_GEN_LANE_ROW(51u, 4u, 0u, "5 Gen 1", "5g1"),
    SCENE_GEN_LANE_ROW(52u, 4u, 1u, "5 Gen 2", "5g2"),
    SCENE_GEN_LANE_ROW(53u, 4u, 2u, "5 Gen 3", "5g3"),
    SCENE_GEN_LANE_ROW(54u, 4u, 3u, "5 Gen 4", "5g4"),
    SCENE_GEN_LANE_ROW(55u, 5u, 0u, "6 Gen 1", "6g1"),
    SCENE_GEN_LANE_ROW(56u, 5u, 1u, "6 Gen 2", "6g2"),
    SCENE_GEN_LANE_ROW(57u, 5u, 2u, "6 Gen 3", "6g3"),
    SCENE_GEN_LANE_ROW(58u, 5u, 3u, "6 Gen 4", "6g4"),
    SCENE_GEN_LANE_ROW(59u, 6u, 0u, "7 Gen 1", "7g1"),
    SCENE_GEN_LANE_ROW(60u, 6u, 1u, "7 Gen 2", "7g2"),
    SCENE_GEN_LANE_ROW(61u, 6u, 2u, "7 Gen 3", "7g3"),
    SCENE_GEN_LANE_ROW(62u, 6u, 3u, "7 Gen 4", "7g4"),
#undef SCENE_GEN_LANE_ROW
```

The `:167` assert (`<= 64u`) is unchanged and now holds at 63. Add one assert after it:

```c
/* S106 P1: 42 rows - 7 shuffle + 28 generator lanes. */
_Static_assert(SCENE_MOD_TARGET_COUNT == 63u,
               "Scene target table: 35 base rows + 28 generator lanes");
```

### T-5 — `SceneModTargets.c` after `sceneModTarget_slot6DecayId()` (ends `:298`) — **Add**

```c
/*
 * Resolve one generator lane's Scene target ID (S106 P1).
 *
 * What: finds the GEN_LANE row whose voice_slot is `track` and param is
 * `lane`. Why: callers (menuGenerator, the commit job, MIDI Learn) must not
 * hard-code row numbers. Inputs: track 0..6, lane 0..3. Output: the
 * canonical ID or INSTRUMENT_PARAM_INVALID. Cost: one walk of 63 rows;
 * foreground only.
 */
uint16_t sceneModTarget_genLaneId(uint8_t track, uint8_t lane)
{
    uint8_t i;

    for (i = 0u; i < SCENE_MOD_TARGET_COUNT; i++)
        if (scene_mod_targets[i].kind == SCENE_MOD_TARGET_KIND_GEN_LANE &&
            scene_mod_targets[i].voice_slot == track &&
            scene_mod_targets[i].param == lane)
            return scene_mod_targets[i].id;
    return INSTRUMENT_PARAM_INVALID;
}
```

### T-6 — `Core/Sequencer/sequencer.c:4134–4176` — **Modify** (`seq_applySceneAutomation()`: shuffle out, lane in)

Replace the three-kind `TRACK_LENGTH/SCALE/SHUFFLE` case with:

```c
	case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
	case SCENE_MOD_TARGET_KIND_TRACK_SCALE: {
		/*
		 * Per-track timing step automation (S078 §5.5; S106 P1: length and
		 * scale only - shuffle lost its step targets).
		 *
		 * What: routes the 7-bit Pattern value to a runtime-only overlay for
		 * the target track's length or scale. Why: the overlay is consumed by
		 * the effective track getters, so step automation changes playback
		 * without overwriting the retained Pattern Normal values or raising a
		 * Pattern dirty bit. Inputs: descriptor->voice_slot carries the track
		 * index. Outputs: presetMorph overlay set; the effective cache is
		 * written so the next DDA tick plays it (S078 §5.10).
		 * Affiliates: presetMorph_setTrackParamStepOverride(),
		 * seq_restoreAllSceneAutomation() (the transport clear).
		 */
		const presetMorph_trackParam_t param =
			(descriptor->kind == SCENE_MOD_TARGET_KIND_TRACK_LENGTH)
			? PRESETMORPH_TRACK_LENGTH : PRESETMORPH_TRACK_SCALE;

		if (descriptor->voice_slot >= NUM_TRACKS)
			return 0u;
		presetMorph_setTrackParamStepOverride(descriptor->voice_slot,
		                                      param, value);
		if (param == PRESETMORPH_TRACK_LENGTH)
			seq_effectiveTrackLength[descriptor->voice_slot] = value;
		else
			seq_effectiveTrackScale[descriptor->voice_slot] = value;
		break; }
	case SCENE_MOD_TARGET_KIND_GEN_LANE:
		/*
		 * Generator lane step automation (S106 P1).
		 *
		 * What: hands the 7-bit packed lane to GeneratorEngine as the lane's
		 * step overlay. Why: the overlay takes precedence over Normal/Morph
		 * for that lane until the transport or a Pattern restore clears it,
		 * the same contract as the track timing overlays above; the engine's
		 * service sees the new effective lane and rebuilds the store (plan
		 * §4.3; it may play stale until then, G11). Inputs:
		 * descriptor->voice_slot (track), descriptor->param (lane 0..3).
		 * Outputs: gen_setStepOverride(); the restore bit set below.
		 * Context: foreground drain (seq_drainPendingAutomationTo()).
		 * Affiliates: gen_clearAllStepOverrides() in
		 * seq_restoreAllSceneAutomation().
		 */
		if (descriptor->voice_slot >= NUM_TRACKS || descriptor->param > 3u)
			return 0u;
		gen_setStepOverride(descriptor->voice_slot, descriptor->param, value);
		break;
```

Add `#include "GeneratorEngine.h"` to the include block, after `#include "SceneModTargets.h"`
(`:63`).

### T-7 — `sequencer.c:1659–1665` — **Modify** (`seq_clearSceneAutomationDirtyFor()`)

```c
            case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
            case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
            /* S106 P1: a generator lane belongs to its track like the timing
             * targets: a kept track keeps its lane restore duty. */
            case SCENE_MOD_TARGET_KIND_GEN_LANE:
                if (descriptor->voice_slot < NUM_TRACKS)
                    drop = (uint8_t)((track_mask >> descriptor->voice_slot) &
                                     1u);
                break;
```

### T-8 — `sequencer.c:1796` and `:1830–1834` — **Modify** (`seq_restoreAllSceneAutomation()`)

After `presetMorph_clearAllTrackParamStepOverrides();` (`:1796`) **Add**:

```c
    /*
     * S106 P1: drop every generator lane overlay before the retained pass,
     * for the same reason as the track timing overlays: the engine reads the
     * overlays directly when it computes effective lanes. Each cleared lane
     * requests a rebuild of its track (gen_clearAllStepOverrides()).
     */
    gen_clearAllStepOverrides();
```

In the `switch`, replace the timing cases (`:1832–1834`):

```c
            /* S078 §5.5 / S106 P1: track timing and generator lane overlays
             * are cleared above. */
            case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
            case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
            case SCENE_MOD_TARGET_KIND_GEN_LANE:
```

### T-9 — `Core/Menu/menu.c` — **Modify** (the shuffle sites)

**(a) `:2559–2568` `va_seqTrackSearchBit()`**: remove the shuffle case.

```c
    switch (descriptor->kind) {
    case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
        return VA_SEARCH_SCENE_TRACK_BIT(0u);
    case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
        return VA_SEARCH_SCENE_TRACK_BIT(1u);
    /* S106 P1: shuffle (cell 2) has no step target any more. */
    default:
        return 0u;
    }
```

**(b) `:4353–4370` `menu_seqCellToTrackTarget()`**: cell 2 is no longer automatable.

```c
    /*
     * S106 P1: only length (cell 0) and scale (cell 1) have step targets;
     * shuffle's seven Scene targets were removed to make room for the
     * generator lanes (plan G7). Cell 2 therefore resolves to invalid, which
     * every caller (held-step writes, search, LEDs, pot clear, MIDI target)
     * treats as "not automatable".
     */
    switch (cell_position) {
    case 0u: base = INSTRUMENT_VOICE_ID_COUNT + 21u; break; /* length */
    case 1u: base = INSTRUMENT_VOICE_ID_COUNT + 28u; break; /* scale  */
    default: return INSTRUMENT_PARAM_INVALID;
    }
    id = (instrument_param_id_t)(base + track);
    descriptor = sceneModTarget_descriptor(id);
    if (!descriptor || descriptor->voice_slot != track)
        return INSTRUMENT_PARAM_INVALID;
    if ((cell_position == 0u &&
         descriptor->kind != SCENE_MOD_TARGET_KIND_TRACK_LENGTH) ||
        (cell_position == 1u &&
         descriptor->kind != SCENE_MOD_TARGET_KIND_TRACK_SCALE))
        return INSTRUMENT_PARAM_INVALID;
    return id;
```

**(c) `:5964–5996` `menu_commitTrackTimingTarget()`**: shuffle is no longer reached.
Header comment:

```c
/*
 * Track timing target from MIDI (S083 C2; plan 3.5 Scene targets; S106 P1:
 * length and scale only).
 *
 * What: writes length or scale of track `d->voice_slot` in the active Scene
 * (Pattern data: never fanned out, edit-mask rule), clears that track's step
 * override and Scene automation bit like a STEP menu edit (S081 5.2), and
 * refreshes the STEP mirrors when that track and Scene are on screen. Inputs:
 * descriptor (LENGTH or SCALE kind), 7-bit value. Output: 1.
 */
```

The `switch (d->kind)` at `:5983–5996` becomes:

```c
    if (d->kind == SCENE_MOD_TARGET_KIND_TRACK_LENGTH) {
        pat_setTrackLength(scene, track, value);
        menu_clearTrackTimingOverrideFor(track, 0u, PRESETMORPH_TRACK_LENGTH);
    } else {
        pat_setTrackScale(scene, track, value);
        menu_clearTrackTimingOverrideFor(track, 1u, PRESETMORPH_TRACK_SCALE);
    }
```

**(d) `:6099–6103` `menu_commitTargetEdit()`**: remove shuffle and refuse a raw lane.

```c
        case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
        case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
            (void)menu_commitTrackTimingTarget(d, value);
            break;
        /*
         * S106 P1: a generator lane is a packed byte of several fields, so a
         * raw MIDI write would move two fields at once. MIDI reaches a
         * generator only through field Learn rows (MidiLearn.c; plan FQ11),
         * never through the lane ID.
         */
        case SCENE_MOD_TARGET_KIND_GEN_LANE:
            return 0u;
```

Update the header line `:6019–6020` to "track len/scl through
menu_commitTrackTimingTarget() (S106 P1: shuffle has no target; a generator lane is
refused)".

**(e) `:17787–17795` `menu_cellMidiTarget()`**: comment only. The shuffle cell resolves
to invalid through (b).

```c
            /* STEP len/scl of the active track (cell positions 0/1). S106
             * P1: shuffle (cell 2) resolves to invalid, so it has no MIDI
             * address (no Learn, NRPN or TX). */
```

### T-10 — `Core/MIDI/MidiControl.c:134–145`, `:256–271` — **Modify**

`midiControl_targetOnChannel()`:

```c
		case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
		case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
		/* S106 P1: a generator lane is per track like the timing targets;
		 * this decides whether a `v` field Learn row on a voice channel
		 * moves the lane's track (F50). */
		case SCENE_MOD_TARGET_KIND_GEN_LANE: {
```

`midiControl_targetValue7()`:

```c
		case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
		case SCENE_MOD_TARGET_KIND_TRACK_SCALE: {
			const pat_scene_region_t *r = pat_sceneRegion(scene_index);

			if (!r || slot >= NUM_TRACKS)
				return 0u;
			return (d->kind == SCENE_MOD_TARGET_KIND_TRACK_LENGTH)
			       ? (uint8_t)((r->track_length[slot] > 127u)
			                   ? 127u : r->track_length[slot])
			       : r->track_scale[slot];
		}
		/* S106 P1: a lane's retained Normal byte. */
		case SCENE_MOD_TARGET_KIND_GEN_LANE:
			return scene_getGenLane(scene_index, slot, d->param);
```

### T-11 — `Core/Sequencer/LiveRecord.c:666–674` — **Modify** (`liveRec_targetValue7()`)

```c
        case SCENE_MOD_TARGET_KIND_TRACK_SCALE: {
            const pat_scene_region_t *r = pat_sceneRegion(a);

            if (!r || slot >= NUM_TRACKS)
                return 0u;
            return r->track_scale[slot];
        }
        /*
         * S106 P1: a generator lane recorded in LATCH takes the origin Scene's
         * retained Normal lane byte, as every Scene target takes its retained
         * value (shuffle has no target any more).
         */
        case SCENE_MOD_TARGET_KIND_GEN_LANE:
            return scene_getGenLane(a, slot, d->param);
```

### T-12 — `Core/MIDI/MidiLearn.c:638–656` — **Modify** (refuse raw lane rows on load)

In the `scn` branch, after `if (i >= n) return 0u;` **Add**:

```c
		/*
		 * S106 P1: a generator lane is never Learnable as a raw byte (plan
		 * FQ11). A 4-field `scn` row naming a lane is refused; the 5-field
		 * form with a `<type>:<field>` token is the field row (below).
		 */
		if (sceneModTarget_descriptor(sceneModTarget_idFromIndex(i))->kind ==
		        SCENE_MOD_TARGET_KIND_GEN_LANE)
			return 0u;
```

The field-row form is M-28.

**Not changed, verified:**
- `presetMorphEngine.c/.h`: `PRESETMORPH_TRACK_SHUFFLE` keeps its overlay slot. Nothing
  sets it any more, and the Morph endpoint path (`presetMorph_getTrackEffectiveShuffle()`,
  `presetMorphEngine.c:1164`) is unchanged.
- `copyOps.c:399` and `menu.c:15068` iterate the table by count and pick the rows up.
- `InstrumentManager.c:1740` and `MidiLearn.c:115` use the index API and need no change.

---

## 3. Scene settings, persistence, identity and copy

### S-1 — `Core/Bank/Scene/SceneData.h:284` — **Add** (four fields at the end of `scene_settings_t`, after `chain_next[]`)

Anchor: `    uint8_t chain_next[SCENE_CHAIN_SLOT_COUNT];`

```c
    /*
     * Per-track generators (S106 P1; plan §3.1).
     *
     * What: one generator per track, all Scene parameters:
     *  - gen_type[t]: bits 0..6 the type id (GEN_TYPE_OFF 0, GEN_TYPE_ARP 1,
     *    GeneratorEngine.h), bit 7 the type-independent `ato` switch (read
     *    the track's Pattern automation while the type is not off; plan G3);
     *  - gen_lane[t][4]: the four 7-bit automatable lanes, Normal. The type
     *    packs its fields into them (gen_field_desc_t);
     *  - gen_lane_morph[t][4]: their Morph endpoints, interpolated field by
     *    field at the slot's resolved Morph amount (track 7 on slot 6);
     *  - gen_fixed[t][4]: non-automatable parameters (the arp uses none).
     * Why: the type and its parameters are Scene parameters (plan G4); the
     * device-wide generator store is rebuilt from them and from the track's
     * Pattern (GeneratorEngine.c).
     * Writers: the scene_setGen*() setters only (AutoSave extension rule
     * below), except validated whole-Scene staging and Scene 17's copy.
     * Storage: sceneset.scg keys gen_type / gen_lane / gen_lane_morph /
     * gen_fixed; AutoSave Scene cells 76..166 (Autosave.h).
     * RAM: 91 B per Scene, 1,547 B across the 17 records (approved, S106 P1).
     * Affiliates: GeneratorEngine.c, menuGenerator.c, presetManager.c
     * identity lists (PRESET_TRACK_SETTINGS_*), copyClearService.c track paste.
     */
    uint8_t gen_type[NUM_TRACKS];
    uint8_t gen_lane[NUM_TRACKS][4];
    uint8_t gen_lane_morph[NUM_TRACKS][4];
    uint8_t gen_fixed[NUM_TRACKS][4];
```

The trailing "Autosave extension rule" comment stays after the new fields.

### S-2 — `SceneData.h:625` — **Add** (prototypes after `scene_getTrackMorphShuffle()`)

```c
/*
 * Per-track generator settings (S106 P1).
 *
 * What: store or read one track's generator type, its `ato` switch, its
 * four lanes (Normal and Morph) and its four fixed parameters. Setters clamp
 * (type to GEN_TYPE_COUNT - 1, ato to 0/1, lanes and fixed bytes to 0..127),
 * commit through scene_storeParameterByte() (one AutoSave cell + the
 * card-clean bit each) and then tell GeneratorEngine
 * (gen_sceneSettingChanged()), which requests a rebuild when the Scene is the
 * track's played Scene. scene_setGenType() keeps the ato bit; it does not
 * reset the lanes (menuGenerator.c does that on a type change, plan Q6).
 * Getters return 0 (off, lane 0) for an invalid Scene/track/lane and are
 * single-byte reads, safe in TIM3.
 * Inputs: Scene 0..16, track 0..6, lane/index 0..3, value. Clients:
 * menuGenerator.c, the AutoSave restore, scene_commitSettings(), the commit
 * job, copyClearService.c track paste.
 */
void scene_setGenType(uint8_t scene_index, uint8_t track, uint8_t type);
uint8_t scene_getGenType(uint8_t scene_index, uint8_t track);
void scene_setGenAto(uint8_t scene_index, uint8_t track, uint8_t on);
uint8_t scene_getGenAto(uint8_t scene_index, uint8_t track);
void scene_setGenLane(uint8_t scene_index, uint8_t track, uint8_t lane,
                      uint8_t value);
uint8_t scene_getGenLane(uint8_t scene_index, uint8_t track, uint8_t lane);
void scene_setGenLaneMorph(uint8_t scene_index, uint8_t track, uint8_t lane,
                           uint8_t value);
uint8_t scene_getGenLaneMorph(uint8_t scene_index, uint8_t track,
                              uint8_t lane);
void scene_setGenFixed(uint8_t scene_index, uint8_t track, uint8_t index,
                       uint8_t value);
uint8_t scene_getGenFixed(uint8_t scene_index, uint8_t track, uint8_t index);
```

### S-3 — `Core/Bank/Scene/SceneData.c:1109` — **Add** (after `scene_getTrackMorphShuffle()`)

Add `#include "GeneratorEngine.h"` to the include block.

```c
/*
 * Per-track generator settings (S106 P1; contract in SceneData.h).
 *
 * What: change-aware setters and plain getters for gen_type (with the ato
 * bit), gen_lane, gen_lane_morph and gen_fixed. Each setter commits one
 * byte through scene_storeParameterByte() with its AutoSave cell
 * (AUTOSAVE_SCENE_PARAM_GEN_*_BASE + index) and then calls
 * gen_sceneSettingChanged(scene, track): the engine refreshes its TIM3 masks
 * and requests a rebuild only when `scene` is the track's played Scene.
 * Why: the AutoSave extension rule (one named index, one getter branch, one
 * change-aware setter per retained byte) and the rebuild trigger of plan
 * G9 ("a generator parameter change") from one place.
 * Context: foreground. The getters are single-byte reads.
 */
static uint8_t scene_genTrackValid(uint8_t track, uint8_t index)
{
    return (uint8_t)(track < NUM_TRACKS && index < 4u);
}

void scene_setGenType(uint8_t scene_index, uint8_t track, uint8_t type)
{
    scene_t *scene = scene_get(scene_index);
    uint8_t value;

    if (!scene || track >= NUM_TRACKS)
        return;
    if ((type & GEN_TYPE_MASK) >= GEN_TYPE_COUNT)
        type = GEN_TYPE_OFF;
    value = (uint8_t)((scene->settings.gen_type[track] & GEN_ATO_BIT) |
                      (type & GEN_TYPE_MASK));
    scene_storeParameterByte(
        scene_index, &scene->settings.gen_type[track],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_GEN_TYPE_BASE + track), value);
    gen_sceneSettingChanged(scene_index, track);
}

uint8_t scene_getGenType(uint8_t scene_index, uint8_t track)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t type;

    if (!scene || track >= NUM_TRACKS)
        return GEN_TYPE_OFF;
    type = (uint8_t)(scene->settings.gen_type[track] & GEN_TYPE_MASK);
    return (type < GEN_TYPE_COUNT) ? type : (uint8_t)GEN_TYPE_OFF;
}

void scene_setGenAto(uint8_t scene_index, uint8_t track, uint8_t on)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || track >= NUM_TRACKS)
        return;
    scene_storeParameterByte(
        scene_index, &scene->settings.gen_type[track],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_GEN_TYPE_BASE + track),
        (uint8_t)((scene->settings.gen_type[track] & GEN_TYPE_MASK) |
                  (on ? GEN_ATO_BIT : 0u)));
    gen_sceneSettingChanged(scene_index, track);
}

uint8_t scene_getGenAto(uint8_t scene_index, uint8_t track)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || track >= NUM_TRACKS)
        return 0u;
    return (uint8_t)((scene->settings.gen_type[track] & GEN_ATO_BIT) != 0u);
}

void scene_setGenLane(uint8_t scene_index, uint8_t track, uint8_t lane,
                      uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || !scene_genTrackValid(track, lane))
        return;
    scene_storeParameterByte(
        scene_index, &scene->settings.gen_lane[track][lane],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_GEN_LANE_BASE + track * 4u + lane),
        (uint8_t)(value & 0x7Fu));
    gen_sceneSettingChanged(scene_index, track);
}

uint8_t scene_getGenLane(uint8_t scene_index, uint8_t track, uint8_t lane)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || !scene_genTrackValid(track, lane))
        return 0u;
    return scene->settings.gen_lane[track][lane];
}

void scene_setGenLaneMorph(uint8_t scene_index, uint8_t track, uint8_t lane,
                           uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || !scene_genTrackValid(track, lane))
        return;
    scene_storeParameterByte(
        scene_index, &scene->settings.gen_lane_morph[track][lane],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_GEN_LANE_MORPH_BASE + track * 4u + lane),
        (uint8_t)(value & 0x7Fu));
    gen_sceneSettingChanged(scene_index, track);
}

uint8_t scene_getGenLaneMorph(uint8_t scene_index, uint8_t track,
                              uint8_t lane)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || !scene_genTrackValid(track, lane))
        return 0u;
    return scene->settings.gen_lane_morph[track][lane];
}

void scene_setGenFixed(uint8_t scene_index, uint8_t track, uint8_t index,
                       uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || !scene_genTrackValid(track, index))
        return;
    scene_storeParameterByte(
        scene_index, &scene->settings.gen_fixed[track][index],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_GEN_FIXED_BASE + track * 4u + index),
        (uint8_t)(value & 0x7Fu));
    gen_sceneSettingChanged(scene_index, track);
}

uint8_t scene_getGenFixed(uint8_t scene_index, uint8_t track, uint8_t index)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || !scene_genTrackValid(track, index))
        return 0u;
    return scene->settings.gen_fixed[track][index];
}
```

### S-4 — `SceneData.c:1196–1211` — **Modify** (`scene_settingsDefaults()`: comment only)

After the track-Morph default block (inside the `for` loop), add:

```c
        /*
         * S106 P1: the generator fields stay zero from the memset above:
         * type off, ato off, lanes/fixed 0. `off` has no fields, so zero is
         * its whole default; a type's own defaults are applied when the type
         * is chosen (menuGenerator.c, gen_typeDefaults()).
         */
```

### S-5 — `SceneData.c:1237–1245` — **Modify** (`scene_commitSettings()`: carry the generator fields)

Inside the per-track loop, after the three `scene_setTrackMorph*()` calls:

```c
        /*
         * S106 P1: copy/clear carry the track's generator (plan Q10a): the
         * type and ato byte, the four lanes, their Morph endpoints and the
         * fixed bytes, each through its change-aware setter.
         */
        {
            uint8_t k;

            scene_setGenType(scene_index, i,
                             (uint8_t)(image.gen_type[i] & GEN_TYPE_MASK));
            scene_setGenAto(scene_index, i,
                            (uint8_t)((image.gen_type[i] & GEN_ATO_BIT) != 0u));
            for (k = 0u; k < 4u; k++) {
                scene_setGenLane(scene_index, i, k, image.gen_lane[i][k]);
                scene_setGenLaneMorph(scene_index, i, k,
                                      image.gen_lane_morph[i][k]);
                scene_setGenFixed(scene_index, i, k, image.gen_fixed[i][k]);
            }
        }
```

### S-6 — `Core/Bank/Scene/Autosave.h:48` — **Modify** (format version)

```c
/*
 * S106 P1: 3 -> 4. The Scene/Effect boundary inside each Scene section moved
 * (AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES 118 -> 182, AUTOSAVE_EFFECT_OFFSET
 * 128 -> 192) to fit the 91 generator cells. A version-3 record would be
 * misread, so the reader rejects it and builds a fresh baseline (no
 * migration, binding). The record and payload sizes are unchanged.
 */
#define AUTOSAVE_HEADER_FORMAT_VERSION        4u
```

### S-7 — `Autosave.h:64` — **Modify** (Effect section size)

```c
/* S106 P1: 512 -> 448; the Scene parameter allocation took 64 bytes. The
 * Effect's live bytes end at relative 432 (419 parameters + source). */
#define AUTOSAVE_EFFECT_SECTION_BYTES       448u
```

### S-8 — `Autosave.h:176–197` — **Modify** (the Scene geometry block)

The comment block at `:176–187` becomes:

```c
/*
 * One Scene's relative regions and explicit parameter allocation.
 *
 * Scene source occupies bytes 8..9; parameters occupy bytes 10..191 (S106 P1:
 * was 10..127), of which indices 0..166 exist (40 = Effect Morph amount,
 * Session 072; 41..44 = the bus compressor's cmp/cam/ctm/csc, S074; 76..166
 * the generators, S106 P1). The Effect region (192..639; was 128..639) holds a
 * 3-byte type token, an 8-byte name, and 419 live parameter bytes from
 * Effect-relative offset 11, then the 2-byte source at 430..431. Kit begins
 * at 640 (unchanged): source at 8..9, parameters at 10..127, then six
 * 192-byte Instruments, ending at 1,920.
 */
```

and the constants:

```c
#define AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES  182u  /* S106 P1: was 118 */
#define AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES   167u  /* S106 P1: +91 generator cells (76..166) */
#define AUTOSAVE_EFFECT_OFFSET                192u  /* S106 P1: was 128 */
```

and (`:197`):

```c
#define AUTOSAVE_EFFECT_PARAMETER_ALLOC_BYTES  437u  /* S106 P1: was 501 */
```

`AUTOSAVE_KIT_OFFSET 640u` is unchanged. The asserts at `:361–379` then hold:
- 10 + 182 = 192;
- 192 + 448 = 640;
- 11 + 437 = 448;
- 419 ≤ 437;
- source at 430;
- 432 ≤ 448.

### S-9 — `Autosave.h:275–277` — **Modify** (enum: the generator groups)

```c
    AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE = 72,
    AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE = 74,
    /*
     * S106 P1: per-track generators, appended after the chain cells:
     * gen_type (with the ato bit) 76..82, gen_lane 83..110 (track * 4 +
     * lane), gen_lane_morph 111..138, gen_fixed 139..166. They need the
     * enlarged 182-byte Scene allocation and AutoSave format version 4.
     * Owner: scene_setGen*() (SceneData.c).
     */
    AUTOSAVE_SCENE_PARAM_GEN_TYPE_BASE = 76,
    AUTOSAVE_SCENE_PARAM_GEN_LANE_BASE = 83,
    AUTOSAVE_SCENE_PARAM_GEN_LANE_MORPH_BASE = 111,
    AUTOSAVE_SCENE_PARAM_GEN_FIXED_BASE = 139,
    AUTOSAVE_SCENE_PARAM_COUNT = 167
```

### S-10 — `Core/Bank/Scene/Autosave.c:121–124` — **Modify** (the closing group asserts)

```c
_Static_assert(AUTOSAVE_SCENE_PARAM_GEN_TYPE_BASE -
                   AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE ==
                   SCENE_CHAIN_SLOT_COUNT,
               "Scene chain next group must cover both slots");
/* S106 P1: the generator groups close the live cells. */
_Static_assert(AUTOSAVE_SCENE_PARAM_GEN_LANE_BASE -
                   AUTOSAVE_SCENE_PARAM_GEN_TYPE_BASE == NUM_TRACKS,
               "Scene generator type group must cover every track");
_Static_assert(AUTOSAVE_SCENE_PARAM_GEN_LANE_MORPH_BASE -
                   AUTOSAVE_SCENE_PARAM_GEN_LANE_BASE == NUM_TRACKS * 4u,
               "Scene generator lane group must cover 7 x 4 lanes");
_Static_assert(AUTOSAVE_SCENE_PARAM_GEN_FIXED_BASE -
                   AUTOSAVE_SCENE_PARAM_GEN_LANE_MORPH_BASE == NUM_TRACKS * 4u,
               "Scene generator lane Morph group must cover 7 x 4 lanes");
_Static_assert(AUTOSAVE_SCENE_PARAM_COUNT -
                   AUTOSAVE_SCENE_PARAM_GEN_FIXED_BASE == NUM_TRACKS * 4u,
               "Scene generator fixed group must cover 7 x 4 bytes");
```

### S-11 — `Autosave.c:1081–1089` — **Modify** (`autosave_getSceneParameter()`: the tail)

```c
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE) {
        /* Indices 72..73 are the S080 Scene chain conditions. */
        *value = scene->settings.chain_condition[
            parameter_index - AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE];
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_GEN_TYPE_BASE) {
        /* Indices 74..75 are the S080 Scene chain next-scene values. */
        *value = scene->settings.chain_next[
            parameter_index - AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE];
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_GEN_LANE_BASE) {
        /* S106 P1: indices 76..82, the generator type + ato byte. */
        *value = scene->settings.gen_type[
            parameter_index - AUTOSAVE_SCENE_PARAM_GEN_TYPE_BASE];
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_GEN_LANE_MORPH_BASE) {
        /* S106 P1: indices 83..110, the lanes (track * 4 + lane). */
        const uint8_t k = (uint8_t)(parameter_index -
                                    AUTOSAVE_SCENE_PARAM_GEN_LANE_BASE);
        *value = scene->settings.gen_lane[k >> 2u][k & 3u];
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_GEN_FIXED_BASE) {
        /* S106 P1: indices 111..138, the lane Morph endpoints. */
        const uint8_t k = (uint8_t)(parameter_index -
                                    AUTOSAVE_SCENE_PARAM_GEN_LANE_MORPH_BASE);
        *value = scene->settings.gen_lane_morph[k >> 2u][k & 3u];
    } else {
        /* S106 P1: indices 139..166, the fixed parameters. */
        const uint8_t k = (uint8_t)(parameter_index -
                                    AUTOSAVE_SCENE_PARAM_GEN_FIXED_BASE);
        *value = scene->settings.gen_fixed[k >> 2u][k & 3u];
    }
    return 1u;
```

The guard at the top of the function (`Autosave.c:1026–1028`,
`parameter_index >= AUTOSAVE_SCENE_PARAM_COUNT` returns 0) bounds the last branch.
`autosave_applyScenePayload()` loops to the same constant (`:1514–1515`).

### S-12 — `Autosave.c:1632–1639` — **Modify** (`autosave_applyScenePayload()`: the tail)

```c
        } else if (parameter_index < AUTOSAVE_SCENE_PARAM_GEN_TYPE_BASE) {
            /* Indices 74..75 restore the S080 chain next-scene values. */
            scene_setChainNext(
                scene_index,
                (uint8_t)(parameter_index -
                          AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE),
                value);
        } else if (parameter_index < AUTOSAVE_SCENE_PARAM_GEN_LANE_BASE) {
            /*
             * S106 P1: indices 76..82 restore the generator type + ato byte
             * through the clamping setters (an unknown type restores off).
             */
            const uint8_t t = (uint8_t)(parameter_index -
                                        AUTOSAVE_SCENE_PARAM_GEN_TYPE_BASE);

            scene_setGenType(scene_index, t, (uint8_t)(value & GEN_TYPE_MASK));
            scene_setGenAto(scene_index, t,
                            (uint8_t)((value & GEN_ATO_BIT) != 0u));
        } else if (parameter_index < AUTOSAVE_SCENE_PARAM_GEN_LANE_MORPH_BASE) {
            const uint8_t k = (uint8_t)(parameter_index -
                                        AUTOSAVE_SCENE_PARAM_GEN_LANE_BASE);

            scene_setGenLane(scene_index, (uint8_t)(k >> 2u),
                             (uint8_t)(k & 3u), value);
        } else if (parameter_index < AUTOSAVE_SCENE_PARAM_GEN_FIXED_BASE) {
            const uint8_t k = (uint8_t)(parameter_index -
                                        AUTOSAVE_SCENE_PARAM_GEN_LANE_MORPH_BASE);

            scene_setGenLaneMorph(scene_index, (uint8_t)(k >> 2u),
                                  (uint8_t)(k & 3u), value);
        } else {
            const uint8_t k = (uint8_t)(parameter_index -
                                        AUTOSAVE_SCENE_PARAM_GEN_FIXED_BASE);

            scene_setGenFixed(scene_index, (uint8_t)(k >> 2u),
                              (uint8_t)(k & 3u), value);
        }
```

Add `#include "GeneratorEngine.h"` (for `GEN_TYPE_MASK`/`GEN_ATO_BIT`) to `Autosave.c`.

### S-13 — `Core/Hardware/SD/filesystem.c:19159–19164` — **Modify** (`filesystem_nextScenesetLine()`: four lines)

```c
    case 19u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "chain_next", scene->settings.chain_next,
            SCENE_CHAIN_SLOT_COUNT);
    /*
     * S106 P1: the generator lines, appended after the chain so no earlier
     * writer line moves: gen_type (7 values, bit 7 = ato), gen_lane and
     * gen_lane_morph (28 values each, track-major), gen_fixed (28). The
     * longest line is 127 characters, under FS_TEXT_LINE_MAX (160). Parser:
     * storageTypes.c, the gen_* keys.
     */
    case 20u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "gen_type", scene->settings.gen_type, NUM_TRACKS);
    case 21u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "gen_lane", &scene->settings.gen_lane[0][0],
            (uint8_t)(NUM_TRACKS * 4u));
    case 22u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "gen_lane_morph", &scene->settings.gen_lane_morph[0][0],
            (uint8_t)(NUM_TRACKS * 4u));
    case 23u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "gen_fixed", &scene->settings.gen_fixed[0][0],
            (uint8_t)(NUM_TRACKS * 4u));
    default:
        return 0u;
```

### S-14 — `filesystem.c:18490–18494` — **Modify** (`filesystem_initSceneStage()`: comment only)

After the track-Morph default loop:

```c
    /*
     * S106 P1: the staged generator fields stay zero from the memset at the
     * top (type off, ato off, lanes/fixed 0), so a sceneset.scg without the
     * gen_* keys loads every track with no generator.
     */
```

### S-15 — `Core/Hardware/SD/storageTypes.c:782` — **Add** (parser keys, before the `fader_setting` branch)

Anchor: `    } else if (storage_streq(key, "fader_setting")) {`

Add `#include "GeneratorEngine.h"` to the include block.

```c
    } else if (storage_streq(key, "gen_type")) {
        /*
         * Parse the per-track generator type bytes (S106 P1).
         *
         * Inputs: seven comma-separated 0..255 values; bits 0..6 the type id,
         * bit 7 ato. Output: staged gen_type[]. An unknown type id loads as
         * off with its ato bit kept, so a file from a later firmware with
         * more types does not reject the Scene. A missing key keeps the
         * zeroed stage (no generator). Writer: filesystem_nextScenesetLine()
         * line 20.
         */
        storage_status_t st;
        uint8_t t;

        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseCsvU8(value, target_settings->gen_type, NUM_TRACKS,
                                255u);
        if (st != STORAGE_STATUS_OK)
            return st;
        for (t = 0u; t < NUM_TRACKS; t++)
            if ((target_settings->gen_type[t] & GEN_TYPE_MASK) >= GEN_TYPE_COUNT)
                target_settings->gen_type[t] &= GEN_ATO_BIT;
        return STORAGE_STATUS_OK;
    } else if (storage_streq(key, "gen_lane")) {
        /* S106 P1: 28 lane bytes 0..127, track-major; writer line 21. */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        return storage_parseCsvU8(value, &target_settings->gen_lane[0][0],
                                  (uint8_t)(NUM_TRACKS * 4u), 127u);
    } else if (storage_streq(key, "gen_lane_morph")) {
        /* S106 P1: 28 lane Morph endpoints; writer line 22. */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        return storage_parseCsvU8(value, &target_settings->gen_lane_morph[0][0],
                                  (uint8_t)(NUM_TRACKS * 4u), 127u);
    } else if (storage_streq(key, "gen_fixed")) {
        /* S106 P1: 28 fixed bytes; writer line 23. */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        return storage_parseCsvU8(value, &target_settings->gen_fixed[0][0],
                                  (uint8_t)(NUM_TRACKS * 4u), 127u);
```

`storage_parseCsvU8()`'s token buffer is `char token[4]` (`storageTypes.c:295`): three
digits plus the terminator, enough for 0..255.

### S-16 — `Core/Bank/Scene/Preset/presetManager.c:3554–3566` — **Modify** (identity lists and size assert)

```c
#define PRESET_TRACK_SETTINGS_FIELDS(X) \
    X(midi_channel) X(midi_note) \
    X(track_morph_length) X(track_morph_scale) X(track_morph_shuffle) \
    X(gen_type)
/*
 * S106 P1: per-track settings that are a row of bytes per track (the
 * generator lanes, Morph endpoints and fixed parameters,
 * scene_settings_t.f[track][4]). They are compared with memcmp() and copied
 * with memcpy() by the PM_*_TRACK_ROW macros below, because the scalar
 * X(f) form compares f[track] as a value. A generator track is identical
 * only when its type, ato, lanes, endpoints and fixed bytes are all equal,
 * so a kept track's generator store stays valid across the switch (plan
 * §4.8).
 */
#define PRESET_TRACK_SETTINGS_ROWS(X) \
    X(gen_lane) X(gen_lane_morph) X(gen_fixed)
```

and the assert:

```c
_Static_assert(sizeof(scene_settings_t) == 166u,
               "scene_settings_t changed: classify the new field in the S098 "
               "field lists (presetManager.c), then update 166 (S106 P1: "
               "75 + 91 generator bytes)");
```

If S-1's measured padding makes `sizeof` differ from 166, the assert stops the build.
Record the measured size, and the RAM row of §0.3 with it.

### S-17 — `presetManager.c:3672–3675` and `:4064–4067` — **Modify** (match and Scene 17 copy)

In `preset_trackMatches()`, after `PRESET_TRACK_SETTINGS_FIELDS(PM_MATCH_TRACK_SETTING)`
and its `#undef`:

```c
#define PM_MATCH_TRACK_ROW(f) \
    if (memcmp(sa->settings.f[track], sb->settings.f[track], \
               sizeof(sa->settings.f[track])) != 0) return 0u;
    PRESET_TRACK_SETTINGS_ROWS(PM_MATCH_TRACK_ROW)
#undef PM_MATCH_TRACK_ROW
```

In `preset_copyBackgroundTrack()`, after `PRESET_TRACK_SETTINGS_FIELDS(PM_COPY_TRACK_SETTING)`
and its `#undef`:

```c
    /* S106 P1: the generator rows travel with the track into Scene 17. */
#define PM_COPY_TRACK_ROW(f) \
    memcpy(dst_scene->settings.f[track], src_scene_data->settings.f[track], \
           sizeof(dst_scene->settings.f[track]));
    PRESET_TRACK_SETTINGS_ROWS(PM_COPY_TRACK_ROW)
#undef PM_COPY_TRACK_ROW
```

`presetManager.c` already includes `<string.h>` (it uses `memcmp` at `:3613`).

### S-18 — `Core/Menu/CopyClear/copyClearService.c:878–881` — **Add** (track paste FINISH: carry the generator)

After the `pat_setTrackAutoSmooth(...)` call inside `if (s) {`:

```c
                /*
                 * S106 P1 (plan Q10a): a track copy carries the source
                 * track's generator settings onto the destination track of
                 * this Scene: type, ato, lanes, Morph endpoints and fixed
                 * bytes. They are Scene settings, written through the
                 * change-aware setters (AutoSave marks; the engine requests
                 * a rebuild when this Scene is the track's played Scene).
                 * The Pattern rows above are the generator's input, so the
                 * copied track plays the same generated output.
                 */
                {
                    uint8_t k;
                    const uint8_t st = src->track;

                    scene_setGenType(job->scene, job->track,
                                     scene_getGenType(src->scene, st));
                    scene_setGenAto(job->scene, job->track,
                                    scene_getGenAto(src->scene, st));
                    for (k = 0u; k < 4u; k++) {
                        scene_setGenLane(job->scene, job->track, k,
                                         scene_getGenLane(src->scene, st, k));
                        scene_setGenLaneMorph(job->scene, job->track, k,
                            scene_getGenLaneMorph(src->scene, st, k));
                        scene_setGenFixed(job->scene, job->track, k,
                                          scene_getGenFixed(src->scene, st, k));
                    }
                }
```

Scene copy (`ccCopy_runScene()` / `ccCopy_runSceneSettings()`) reaches
`scene_commitSettings()` and is covered by S-5.

---

## 4. GeneratorEngine: the store, the masks, the service, the hooks

Step 4 builds with type `off` only. The registry has one entry until step 8 (A-1)
appends the arp. The live arp branch of `gen_readStep()` arrives at step 8 (A-5), and
the commit functions at step 10 (C-3).

### G-1 — `Core/Bank/Scene/Pattern/GeneratorEngine.h` — **New file**

```c
/*
 * Core/Bank/Scene/Pattern/GeneratorEngine.h
 *
 *  Created on: S106 P1 (2026-11-28)
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * GeneratorEngine - per-track pattern generators (S106 P1).
 *
 * What: one generator per track, chosen per Scene (scene_settings_t.gen_*).
 * A track whose generator is not `off` plays its trigger bits and four
 * specials from one device-wide store (gen_trig/gen_spec, 3,696 B) instead of
 * its Pattern. The store is rebuilt in the foreground when the track's
 * played Scene changes, its Pattern changes, one of its effective lanes
 * changes, or (for types that wrap) its effective length changes.
 * Why: plan G1-G11. The store exists once for the device, so a rebuild is a
 * bounded foreground job, never a TIM3 one; TIM3 only reads it.
 * Ownership: this module owns the store and the masks; it reads Patterns only
 * through pat_* and writes them only through the copy/clear commit job
 * (copyClearService.c). SceneData owns the settings; the setters tell this
 * module (gen_sceneSettingChanged()).
 * Contexts: TIM3 reads gen_readStep() and the masks; everything else is
 * foreground. gen_tracksSwitched() and gen_patternChanged() are also safe
 * inside PRIMASK sections and from TIM3.
 * Affiliates: GeneratorArp.c (the first type), menuGenerator.c (the page),
 * sequencer.c (seq_stepRead(), the ato gate, the switch hooks),
 * PatternData.c (pat_markSceneDirty() hook), SceneModTargets.c (lanes).
 */
#ifndef GENERATORENGINE_H_
#define GENERATORENGINE_H_

#include <stdint.h>
#include "PatternData.h"

#define GEN_LANES          4u   /* automatable 7-bit lanes per track */
#define GEN_FIXED_PARAMS   4u   /* non-automatable bytes per track */
#define GEN_SPECIAL_SLOTS  4u   /* specials stored per generated step */
#define GEN_FIELDS_MAX     8u   /* logical fields packed into the lanes */
#define GEN_TYPE_MASK      0x7Fu
#define GEN_ATO_BIT        0x80u

/* Stored type ids: the registry order is the storage schema. Append only. */
enum {
    GEN_TYPE_OFF = 0u,
    GEN_TYPE_ARP = 1u,      /* GeneratorArp.c; registered at step 8 */
    GEN_TYPE_COUNT
};

/* Field flags (gen_field_desc_t.flags). */
#define GEN_FIELD_ORDERED   0x01u  /* Morph steps by index; else switches at 50 % */
#define GEN_FIELD_ZERO_OFF  0x02u  /* index 0 is `off`: Morph to/from it switches */

/* One part of a field: `bits` bits at `shift` in lane `lane` (0..3). */
typedef struct {
    uint8_t lane;
    uint8_t shift;
    uint8_t bits;
} gen_field_part_t;

/*
 * One logical field (a cell on the Generator page).
 *
 * What: its 3-letter and 8-letter names, its value count (1..16) and default
 * index, its Morph rule, its value names or display numbers, and where its
 * bits live. A split field has two parts: part[0] holds the low bits,
 * part[1] the high bits (plan §6.3: in3 and vel). Values are indices
 * 0..count-1; an index above count-1 read from a lane reads as count-1.
 */
typedef struct {
    const char *name3;
    const char *long8;
    uint8_t count;
    uint8_t def;
    uint8_t flags;
    const char (*names)[4];     /* count names, or NULL */
    const uint8_t *numbers;     /* count display numbers, or NULL (index) */
    uint8_t parts;              /* 1 or 2 */
    gen_field_part_t part[2];
} gen_field_desc_t;

/*
 * One generator type (the registry entry).
 *
 * passes(): nonzero when these effective lanes make the type fall through to
 * the track's own steps (plan G22). build(): writes the 128 steps of `track`
 * into `spec` (the store row, written in place) and the trigger image into
 * `trig` (published by the engine afterwards); bounded, foreground.
 * commitStep(): may rewrite one built step's specials before a commit writes
 * it (NULL: write as built). passthrough_bits: specials a commit keeps from
 * the Pattern step (plan G43). wraps_at_length: the build depends on the
 * effective track length, so a length change rebuilds.
 */
typedef struct {
    const char *short3;
    const char *full8;
    uint8_t field_count;
    const gen_field_desc_t *fields;
    uint8_t fixed_count;
    const gen_field_desc_t *fixed;
    uint8_t special_bit[GEN_SPECIAL_SLOTS];
    uint8_t passthrough_bits;
    uint8_t wraps_at_length;
    uint8_t (*passes)(const uint8_t lane[GEN_LANES]);
    void (*build)(uint8_t track, uint8_t scene,
                  const uint8_t lane[GEN_LANES],
                  const uint8_t fixed[GEN_FIXED_PARAMS], uint8_t len,
                  uint8_t trig[PATTERN_TRACK_BYTES],
                  uint8_t spec[NUM_STEPS][GEN_SPECIAL_SLOTS]);
    void (*commitStep)(uint8_t step, uint8_t spec[GEN_SPECIAL_SLOTS]);
} gen_type_desc_t;

/*
 * TIM3-visible masks (bit t = track t). Written in the foreground with
 * single byte stores (or under PRIMASK in the switch paths); read by TIM3.
 *  gen_activeMask  the played Scene's type for t is not off;
 *  gen_readyMask   the store row of t is valid for its played Scene (G10);
 *  gen_passMask    the built lanes fall through to the Pattern (G22);
 *  gen_atoOffMask  active and `ato` off: none of t's automation is read (G3);
 *  gen_liveMask    a MIDI hold drives t's live arp (step 8).
 */
extern volatile uint8_t gen_activeMask;
extern volatile uint8_t gen_readyMask;
extern volatile uint8_t gen_passMask;
extern volatile uint8_t gen_atoOffMask;
extern volatile uint8_t gen_liveMask;

/* Lifecycle and service (foreground). */
void gen_init(void);
void gen_service(void);
void gen_buildNow(uint8_t trackMask);

/* Change notifications. */
void gen_tracksSwitched(uint8_t trackMask);          /* any context */
void gen_sceneSettingChanged(uint8_t scene_index, uint8_t track);
void gen_patternChanged(uint8_t scene_index);        /* any context */

/* Step-automation lane overlays (foreground drain / restore / R12). */
void gen_setStepOverride(uint8_t track, uint8_t lane, uint8_t value);
void gen_clearStepOverride(uint8_t track, uint8_t lane);
void gen_clearAllStepOverrides(void);

/* Registry and fields (foreground). */
const gen_type_desc_t *gen_typeDesc(uint8_t type);
uint8_t gen_fieldGet(const gen_field_desc_t *f, const uint8_t lane[GEN_LANES]);
uint8_t gen_fieldSet(const gen_field_desc_t *f, uint8_t value,
                     uint8_t lane[GEN_LANES]);
void gen_typeDefaults(uint8_t type, uint8_t lane[GEN_LANES],
                      uint8_t fixed[GEN_FIXED_PARAMS]);
void gen_effectiveLanes(uint8_t track, uint8_t scene,
                        uint8_t out[GEN_LANES]);

/* Readers. gen_readStep(): TIM3 (and foreground); gen_outputBit(): LEDs. */
uint8_t gen_readStep(uint8_t track, uint8_t step, uint8_t advanceLive,
                     pat_step_specials_t *sp);
uint8_t gen_outputBit(uint8_t track, uint8_t step);
uint8_t gen_takeUiChanged(void);

#endif /* GENERATORENGINE_H_ */
```

### G-2 — `Core/Bank/Scene/Pattern/GeneratorEngine.c` — **New file**

```c
/*
 * Core/Bank/Scene/Pattern/GeneratorEngine.c - per-track generators (S106 P1).
 * Contract: GeneratorEngine.h.
 */
#include "stm32f4xx.h"
#include "GeneratorEngine.h"
#include "PatternData.h"
#include "SceneData.h"
#include "sequencer.h"
#include "presetMorphEngine.h"
#include "config.h"
#include <string.h>

/*
 * The generator store (S106 P1; plan §3.2; RAM approved 2026-11-28).
 *
 * What: gen_trig[t] holds the 128 generated trigger bits of track t (bit s
 * of byte s >> 3); gen_spec[t][s] the four special bytes of step s in the
 * built type's slot order (gen_type_desc_t.special_bit). Specials outside
 * the slot map read as the pat_readStepSpecials() defaults.
 * Why: one store for the device (plan G8): a Scene switch rebuilds the rows
 * of the tracks that changed. Tearing: a build writes the specials first and
 * publishes the trigger bytes after, so TIM3 reads at most one torn step
 * (old trigger, new specials), which G11 accepts.
 * RAM: 112 + 3,584 B SRAM1. Writer: gen_rebuildTrack(). Readers:
 * gen_readStep() (TIM3), gen_outputBit(), the commit job.
 */
static uint8_t gen_trig[NUM_TRACKS][PATTERN_TRACK_BYTES];
static uint8_t gen_spec[NUM_TRACKS][NUM_STEPS][GEN_SPECIAL_SLOTS];

volatile uint8_t gen_activeMask;
volatile uint8_t gen_readyMask;
volatile uint8_t gen_passMask;
volatile uint8_t gen_atoOffMask;
volatile uint8_t gen_liveMask;

/*
 * Foreground state (S106 P1; plan §3.3).
 *
 * gen_requestMask: tracks owed a rebuild. gen_builtType[t]: the type the row
 * was built with (TIM3 reads it to map the special slots). gen_builtLane /
 * gen_builtLen: the effective lanes and length it was built from (the
 * change test). gen_stepOverride / gen_overrideMask: step-automation lane
 * overlays (bit t * 4 + lane). gen_patSceneDirty: Scenes whose Pattern
 * changed since the last service pass (bit s, 0..16). gen_freezeMask:
 * tracks whose commit job is running (no rebuild, SD10). gen_uiChanged:
 * tracks rebuilt since the menu last took the mask (Generator page LEDs).
 * gen_checkCursor: the round-robin track of the effective-lane test.
 */
static volatile uint8_t gen_requestMask;
static uint8_t gen_builtType[NUM_TRACKS];
static uint8_t gen_builtLane[NUM_TRACKS][GEN_LANES];
static uint8_t gen_builtLen[NUM_TRACKS];
static uint8_t gen_stepOverride[NUM_TRACKS][GEN_LANES];
static uint32_t gen_overrideMask;
static volatile uint32_t gen_patSceneDirty;
static volatile uint8_t gen_freezeMask;
static volatile uint8_t gen_uiChanged;
static uint8_t gen_checkCursor;

/*
 * The type registry (S106 P1). Index = stored type id. `off` has no fields
 * and no build; the arp joins at GEN_TYPE_ARP in step 8 (GeneratorArp.c).
 */
static const gen_type_desc_t gen_typeOff = {
    "off", "Off     ", 0u, 0, 0u, 0, { 0u, 0u, 0u, 0u }, 0u, 0u, 0, 0, 0
};
static const gen_type_desc_t *const gen_registry[GEN_TYPE_COUNT] = {
    &gen_typeOff,
    0,              /* GEN_TYPE_ARP: &gen_typeArp from step 8 */
};

static uint8_t gen_slotOf(uint8_t track)
{
    return (uint8_t)((track >= 6u) ? 5u : track);
}

const gen_type_desc_t *gen_typeDesc(uint8_t type)
{
    type = (uint8_t)(type & GEN_TYPE_MASK);
    if (type >= GEN_TYPE_COUNT || !gen_registry[type])
        return &gen_typeOff;
    return gen_registry[type];
}

/*
 * Read or write one logical field in a lane image (S106 P1; plan §6.3).
 *
 * What: gen_fieldGet() assembles the field's index from its one or two
 * parts (part[0] low bits, part[1] high bits) and clamps it to count - 1;
 * gen_fieldSet() writes `value` (clamped) back into those bits only, leaving
 * every other bit of the lanes as it was, and returns a mask of the lanes it
 * touched (bit l). Why: the menu shows fields and automation writes lanes;
 * this is the single packing rule both use.
 */
uint8_t gen_fieldGet(const gen_field_desc_t *f, const uint8_t lane[GEN_LANES])
{
    uint8_t v = 0u;
    uint8_t p;
    uint8_t at = 0u;

    if (!f)
        return 0u;
    for (p = 0u; p < f->parts && p < 2u; p++) {
        const gen_field_part_t *pp = &f->part[p];
        const uint8_t m = (uint8_t)((1u << pp->bits) - 1u);

        v = (uint8_t)(v | (uint8_t)(((lane[pp->lane] >> pp->shift) & m) << at));
        at = (uint8_t)(at + pp->bits);
    }
    return (v >= f->count) ? (uint8_t)(f->count - 1u) : v;
}

uint8_t gen_fieldSet(const gen_field_desc_t *f, uint8_t value,
                     uint8_t lane[GEN_LANES])
{
    uint8_t touched = 0u;
    uint8_t p;

    if (!f || f->count == 0u)
        return 0u;
    if (value >= f->count)
        value = (uint8_t)(f->count - 1u);
    for (p = 0u; p < f->parts && p < 2u; p++) {
        const gen_field_part_t *pp = &f->part[p];
        const uint8_t m = (uint8_t)((1u << pp->bits) - 1u);

        lane[pp->lane] = (uint8_t)((lane[pp->lane] & (uint8_t)~(m << pp->shift)) |
                                   (uint8_t)((value & m) << pp->shift));
        lane[pp->lane] &= 0x7Fu;
        touched = (uint8_t)(touched | (uint8_t)(1u << pp->lane));
        value = (uint8_t)(value >> pp->bits);
    }
    return touched;
}

void gen_typeDefaults(uint8_t type, uint8_t lane[GEN_LANES],
                      uint8_t fixed[GEN_FIXED_PARAMS])
{
    const gen_type_desc_t *d = gen_typeDesc(type);
    uint8_t i;

    memset(lane, 0, GEN_LANES);
    memset(fixed, 0, GEN_FIXED_PARAMS);
    for (i = 0u; i < d->field_count; i++)
        (void)gen_fieldSet(&d->fields[i], d->fields[i].def, lane);
    for (i = 0u; i < d->fixed_count && i < GEN_FIXED_PARAMS; i++)
        fixed[i] = d->fixed[i].def;
}

/*
 * Field-wise Morph of two lane images (S106 P1; plan §4.3, Q5).
 *
 * What: out starts as Normal; each field is interpolated at amount 0..255:
 * ordered fields step by index with rounding; a GEN_FIELD_ZERO_OFF field
 * whose either side is 0 (`off`) switches at 50 %, as does every unordered
 * field; equal sides keep their value. Why: halfway between two packed
 * bytes is not halfway between their fields.
 */
static void gen_morphLanes(const gen_type_desc_t *d, const uint8_t n[GEN_LANES],
                           const uint8_t m[GEN_LANES], uint8_t amount,
                           uint8_t out[GEN_LANES])
{
    uint8_t i;

    memcpy(out, n, GEN_LANES);
    if (amount == 0u)
        return;
    for (i = 0u; i < d->field_count; i++) {
        const gen_field_desc_t *f = &d->fields[i];
        const uint8_t a = gen_fieldGet(f, n);
        const uint8_t b = gen_fieldGet(f, m);
        uint8_t v = a;

        if (a == b)
            continue;
        if ((f->flags & GEN_FIELD_ORDERED) != 0u &&
            !((f->flags & GEN_FIELD_ZERO_OFF) != 0u && (a == 0u || b == 0u))) {
            const int16_t d16 = (int16_t)((int16_t)b - (int16_t)a);

            v = (uint8_t)((int16_t)a +
                (int16_t)(((int32_t)d16 * (int32_t)amount +
                           ((d16 >= 0) ? 127 : -127)) / 255));
        } else if (amount >= 128u) {
            v = b;
        }
        (void)gen_fieldSet(f, v, out);
    }
}

/*
 * The lanes a track plays now (S106 P1; plan §4.3).
 *
 * What: each lane is its step overlay when one is active, else the
 * field-wise Morph of the Scene's Normal and Morph-endpoint lanes at the
 * slot's resolved Morph amount (track 7 on slot 6). Inputs: track, the
 * track's played Scene. Output: out[4]. Context: foreground (reads the Morph
 * worker's resolved amount, presetMorph_getResolvedVoiceAmount()).
 */
void gen_effectiveLanes(uint8_t track, uint8_t scene, uint8_t out[GEN_LANES])
{
    uint8_t n[GEN_LANES];
    uint8_t m[GEN_LANES];
    uint8_t l;

    for (l = 0u; l < GEN_LANES; l++) {
        n[l] = scene_getGenLane(scene, track, l);
        m[l] = scene_getGenLaneMorph(scene, track, l);
    }
    gen_morphLanes(gen_typeDesc(scene_getGenType(scene, track)), n, m,
                   presetMorph_getResolvedVoiceAmount(scene, gen_slotOf(track)),
                   out);
    for (l = 0u; l < GEN_LANES; l++)
        if ((gen_overrideMask >> (track * GEN_LANES + l)) & 1u)
            out[l] = gen_stepOverride[track][l];
}

/*
 * Refresh one track's TIM3 masks from its played Scene (S106 P1).
 *
 * What: active = the type is not off; atoOff = active and ato off. A track
 * that became off loses ready/pass/live. Called with TIM3 able to interrupt
 * (single byte read-modify-writes are made under PRIMASK by the callers that
 * need it; see gen_tracksSwitched()).
 */
static void gen_refreshMasks(uint8_t track)
{
    const uint8_t scene = seq_getTrackPlayedScene(track);
    const uint8_t bit = (uint8_t)(1u << track);
    const uint8_t type = scene_getGenType(scene, track);
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if (type != GEN_TYPE_OFF) {
        gen_activeMask |= bit;
        if (scene_getGenAto(scene, track))
            gen_atoOffMask &= (uint8_t)~bit;
        else
            gen_atoOffMask |= bit;
    } else {
        gen_activeMask &= (uint8_t)~bit;
        gen_atoOffMask &= (uint8_t)~bit;
        gen_readyMask &= (uint8_t)~bit;
        gen_passMask &= (uint8_t)~bit;
    }
    __set_PRIMASK(primask);
}

/*
 * Rebuild one track's store row (S106 P1; plan §4.4).
 *
 * What: for a non-off type, resolves the effective lanes and length, asks
 * the type whether it passes through (G22) and otherwise builds the 128
 * steps: specials straight into gen_spec[t], triggers into a local image,
 * published byte by byte after. Then records what it was built from, sets
 * ready, and marks the track for the Generator page's LED refresh.
 * Why: the single build path for the service, the switch-time build and
 * the effective-value test. Inputs: track. Cost: the type's build (the arp:
 * one scan of 128 input steps and 128 writes, Est. < 60 µs). Context:
 * foreground; never TIM3. A frozen track (commit running) is skipped.
 */
static void gen_rebuildTrack(uint8_t track)
{
    const uint8_t bit = (uint8_t)(1u << track);
    const uint8_t scene = seq_getTrackPlayedScene(track);
    const uint8_t type = scene_getGenType(scene, track);
    const gen_type_desc_t *d = gen_typeDesc(type);
    uint8_t lane[GEN_LANES];
    uint8_t fixed[GEN_FIXED_PARAMS];
    uint8_t trig[PATTERN_TRACK_BYTES];
    uint8_t len;
    uint8_t i;

    gen_requestMask &= (uint8_t)~bit;
    if ((gen_freezeMask & bit) != 0u)
        return;
    gen_refreshMasks(track);
    if (type == GEN_TYPE_OFF || !d->build)
        return;
    gen_effectiveLanes(track, scene, lane);
    for (i = 0u; i < GEN_FIXED_PARAMS; i++)
        fixed[i] = scene_getGenFixed(scene, track, i);
    len = seq_getEffectiveTrackLength(track);
    if (len < 1u || len > NUM_STEPS)
        len = NUM_STEPS_PER_BAR;
    memcpy(gen_builtLane[track], lane, GEN_LANES);
    gen_builtLen[track] = len;
    gen_builtType[track] = type;
    if (d->passes && d->passes(lane)) {
        gen_passMask |= bit;
        gen_readyMask |= bit;
        gen_uiChanged |= bit;
        return;
    }
    d->build(track, scene, lane, fixed, len, trig, gen_spec[track]);
    for (i = 0u; i < PATTERN_TRACK_BYTES; i++)
        gen_trig[track][i] = trig[i];
    gen_passMask &= (uint8_t)~bit;
    gen_readyMask |= bit;
    gen_uiChanged |= bit;
}

void gen_init(void)
{
    memset(gen_trig, 0, sizeof(gen_trig));
    memset(gen_spec, 0, sizeof(gen_spec));
    gen_activeMask = 0u;
    gen_readyMask = 0u;
    gen_passMask = 0u;
    gen_atoOffMask = 0u;
    gen_liveMask = 0u;
    gen_requestMask = 0x7Fu;      /* the first service passes build the boot Scene */
    gen_overrideMask = 0u;
    gen_patSceneDirty = 0u;
    gen_freezeMask = 0u;
    gen_uiChanged = 0u;
    gen_checkCursor = 0u;
}

/*
 * Tracks moved to another played Scene (S106 P1; plan §4.2, G10).
 *
 * What: for each track in the mask, refreshes its masks from the (already
 * updated) played Scene, clears its ready bit and requests a rebuild. Why: a
 * switched track must not play the old Scene's store; until the rebuild it
 * does not fire (G10). The whole-Scene path calls gen_buildNow() right
 * after, so normally no step is lost (Q12).
 * Context: any; called inside seq_selectActivePattern()'s PRIMASK section,
 * so it uses only byte stores and SceneData byte reads.
 */
void gen_tracksSwitched(uint8_t trackMask)
{
    uint8_t t;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    for (t = 0u; t < NUM_TRACKS; t++) {
        const uint8_t bit = (uint8_t)(1u << t);

        if ((trackMask & bit) == 0u)
            continue;
        gen_readyMask &= (uint8_t)~bit;
        gen_requestMask |= bit;
        gen_refreshMasks(t);
    }
    __set_PRIMASK(primask);
}

/*
 * A generator setting of (scene, track) changed (S106 P1; plan G9).
 *
 * What: when `scene` is the track's played Scene, refreshes its masks and
 * requests a rebuild; a type change also clears ready (the old row belongs
 * to the old type). Other Scenes' edits change nothing playing. Context:
 * foreground (the SceneData setters, AutoSave restore, loads, copy).
 */
void gen_sceneSettingChanged(uint8_t scene_index, uint8_t track)
{
    const uint8_t bit = (uint8_t)(1u << track);

    if (track >= NUM_TRACKS || seq_getTrackPlayedScene(track) != scene_index)
        return;
    gen_refreshMasks(track);
    if (scene_getGenType(scene_index, track) != gen_builtType[track])
        gen_readyMask &= (uint8_t)~bit;
    gen_requestMask |= bit;
}

/*
 * A Scene's Pattern changed (S106 P1; plan §4.2, G9/G11).
 *
 * What: marks the Scene in gen_patSceneDirty; the next service pass requests
 * every generator track that plays it. Ready is kept (a Pattern edit may
 * play stale until the rebuild, G11). Context: any - it is called from
 * pat_markSceneDirty(), which TIM3 reaches through live erase.
 */
void gen_patternChanged(uint8_t scene_index)
{
    uint32_t primask;

    if (scene_index >= 32u)
        return;
    primask = __get_PRIMASK();
    __disable_irq();
    gen_patSceneDirty |= (1ul << scene_index);
    __set_PRIMASK(primask);
}

void gen_setStepOverride(uint8_t track, uint8_t lane, uint8_t value)
{
    if (track >= NUM_TRACKS || lane >= GEN_LANES)
        return;
    gen_stepOverride[track][lane] = (uint8_t)(value & 0x7Fu);
    gen_overrideMask |= (1ul << (track * GEN_LANES + lane));
    gen_requestMask |= (uint8_t)(1u << track);
}

void gen_clearStepOverride(uint8_t track, uint8_t lane)
{
    const uint32_t bit = 1ul << (track * GEN_LANES + lane);

    if (track >= NUM_TRACKS || lane >= GEN_LANES ||
        (gen_overrideMask & bit) == 0u)
        return;
    gen_overrideMask &= ~bit;
    gen_requestMask |= (uint8_t)(1u << track);
}

void gen_clearAllStepOverrides(void)
{
    uint8_t t;

    for (t = 0u; t < NUM_TRACKS; t++)
        if (((gen_overrideMask >> (t * GEN_LANES)) & 0x0Fu) != 0u)
            gen_requestMask |= (uint8_t)(1u << t);
    gen_overrideMask = 0u;
}

/*
 * One bounded foreground pass (S106 P1; plan §4.4).
 *
 * What, in order:
 *  1. Scenes whose Pattern changed request their generator tracks;
 *  2. one track (round robin) is tested: its effective lanes, or for a
 *     wrapping type its effective length, against what it was built from;
 *  3. at most one requested track is rebuilt.
 * Why: a rebuild is bounded and the service runs at the 500 Hz front-panel
 * cadence (timebase_serviceFrontPanel()), so seven tracks rebuild within
 * 14 ms and no pass blocks the main loop (MEMORY.md: no 1 ms blocking).
 * Context: foreground only.
 */
void gen_service(void)
{
    uint32_t dirty;
    uint32_t primask;
    uint8_t t;

    primask = __get_PRIMASK();
    __disable_irq();
    dirty = gen_patSceneDirty;
    gen_patSceneDirty = 0u;
    __set_PRIMASK(primask);
    if (dirty != 0u)
        for (t = 0u; t < NUM_TRACKS; t++)
            if ((gen_activeMask >> t) & 1u &&
                ((dirty >> seq_getTrackPlayedScene(t)) & 1u))
                gen_requestMask |= (uint8_t)(1u << t);

    t = gen_checkCursor;
    gen_checkCursor = (uint8_t)((gen_checkCursor + 1u) % NUM_TRACKS);
    if (((gen_activeMask >> t) & 1u) && ((gen_requestMask >> t) & 1u) == 0u) {
        const uint8_t scene = seq_getTrackPlayedScene(t);
        const gen_type_desc_t *d = gen_typeDesc(scene_getGenType(scene, t));
        uint8_t lane[GEN_LANES];

        gen_effectiveLanes(t, scene, lane);
        if (memcmp(lane, gen_builtLane[t], GEN_LANES) != 0 ||
            (d->wraps_at_length &&
             seq_getEffectiveTrackLength(t) != gen_builtLen[t]))
            gen_requestMask |= (uint8_t)(1u << t);
    }

    for (t = 0u; t < NUM_TRACKS; t++) {
        if ((gen_requestMask >> t) & 1u) {
            gen_rebuildTrack(t);
            break;
        }
    }
}

/*
 * Rebuild now (S106 P1; plan §4.4, Q12).
 *
 * What: rebuilds every requested track in the mask synchronously. Why: the
 * Scene change executor and the per-track assignment call it right after
 * the switch, so the first step in the new Scene is normally ready.
 * Cost: the type's build x tracks (arp x 7 Est. < 420 µs, under the 1 ms
 * rule). Context: foreground.
 */
void gen_buildNow(uint8_t trackMask)
{
    uint8_t t;

    for (t = 0u; t < NUM_TRACKS; t++)
        if (((trackMask & gen_requestMask) >> t) & 1u)
            gen_rebuildTrack(t);
}

/* Write one stored special byte into the decoded record. */
static void gen_applySpecial(pat_step_specials_t *sp, uint8_t which,
                             uint8_t value)
{
    switch (which) {
    case PAT_SPECIAL_NOTE_BIT:   sp->note = value;      break;
    case PAT_SPECIAL_VEL_BIT:    sp->velocity = value;  break;
    case PAT_SPECIAL_COND_BIT:   sp->condition = value; break;
    case PAT_SPECIAL_ROLL_BIT:   sp->roll = value;      break;
    case PAT_SPECIAL_TIMING_BIT: sp->timing = value;    break;
    case PAT_SPECIAL_RHYTHM_BIT: sp->rhythm = value;    break;
    case PAT_SPECIAL_FILL_BIT:   sp->fill = value;      break;
    default: return;
    }
    sp->flags |= which;
}

/*
 * Read one generated step (S106 P1; plan §3.2, §4.5).
 *
 * What: *sp starts as the pat_readStepSpecials() defaults (note 63,
 * velocity 100, fill 100, everything else 0) and takes the built type's four
 * slots; the return is the generated trigger. A track not ready returns 0
 * (G10) with defaulted specials, so condition counters stay sane.
 * advanceLive is used by the live arp branch (step 8); without a hold it is
 * ignored. Inputs: track 0..6, step 0..127. Context: TIM3 and foreground;
 * byte reads only. Accessors: seq_stepRead() (sequencer.c), the commit job.
 */
uint8_t gen_readStep(uint8_t track, uint8_t step, uint8_t advanceLive,
                     pat_step_specials_t *sp)
{
    const uint8_t bit = (uint8_t)(1u << track);
    const gen_type_desc_t *d;
    uint8_t k;

    (void)advanceLive;
    sp->note = PAT_DEFAULT_NOTE;
    sp->velocity = PAT_DEFAULT_VELOCITY;
    sp->condition = 0u;
    sp->roll = 0u;
    sp->timing = 0u;
    sp->rhythm = 0u;
    sp->fill = 100u;
    sp->flags = 0u;
    if (track >= NUM_TRACKS || step >= NUM_STEPS ||
        (gen_readyMask & bit) == 0u)
        return 0u;
    d = gen_typeDesc(gen_builtType[track]);
    for (k = 0u; k < GEN_SPECIAL_SLOTS; k++)
        gen_applySpecial(sp, d->special_bit[k], gen_spec[track][step][k]);
    return (uint8_t)((gen_trig[track][step >> 3u] >> (step & 7u)) & 1u);
}

/*
 * The trigger the Generator page shows for one step (S106 P1; plan G29).
 * Output: the generated bit for a ready, non-pass track; the Pattern bit for
 * a pass or off track (what plays). Context: foreground.
 */
uint8_t gen_outputBit(uint8_t track, uint8_t step)
{
    const uint8_t bit = (uint8_t)(1u << track);

    if (track >= NUM_TRACKS || step >= NUM_STEPS)
        return 0u;
    if ((gen_activeMask & bit) == 0u || (gen_passMask & bit) != 0u)
        return pat_isStepActive(track, step, seq_getTrackPlayedScene(track));
    if ((gen_readyMask & bit) == 0u)
        return 0u;
    return (uint8_t)((gen_trig[track][step >> 3u] >> (step & 7u)) & 1u);
}

uint8_t gen_takeUiChanged(void)
{
    const uint8_t m = gen_uiChanged;

    gen_uiChanged = 0u;
    return m;
}
```

**Note:** `gen_typeOff`'s positional initialiser follows the struct order of G-1.
`seq_getEffectiveTrackLength()` is G-5.

### G-3 — `Makefile:146` — **Add** (the new source)

```make
  Core/Bank/Scene/Pattern/EuklidGenerator.c \
  Core/Bank/Scene/Pattern/GeneratorEngine.c \
```

`GeneratorArp.c` joins at step 8 (A-8) and `menuGenerator.c` at step 6 (M-30).

### G-4 — `main.c:667` — **Add** (init after `euklid_init();`)

```c
    euklid_init();
    /*
     * S106 P1: the generator store and masks start empty with every track
     * requested, so the first service passes build the boot Scene's
     * generators once the Scene data is resident (scene_initAll() and the
     * boot load run before the main loop's first front-panel service).
     */
    gen_init();
```

Add `#include "GeneratorEngine.h"` to `main.c`'s include block.

### G-5 — `Core/Sequencer/sequencer.h:186` and `sequencer.c:2388` — **Add** (the effective length getter)

`sequencer.h`, after `uint8_t seq_getTrackPlayedScene(uint8_t track);`:

```c
/*
 * The effective (morphed / automated) loop length of a track (S106 P1).
 *
 * What: seq_effectiveTrackLength[track], the value the DDA wraps at
 * (1..NUM_STEPS). Why: generators that wrap their output at the track
 * length (the arp, plan Q1) rebuild when it changes; the cache is static to
 * sequencer.c. Output: 1..128, or NUM_STEPS_PER_BAR for an invalid track.
 * Context: any (one byte read). Accessor: GeneratorEngine.c.
 */
uint8_t seq_getEffectiveTrackLength(uint8_t track);
```

`sequencer.c`, after `seq_getTrackPlayedScene()` (ends `:2387`):

```c
uint8_t seq_getEffectiveTrackLength(uint8_t track)
{
    if (track >= NUM_TRACKS || seq_effectiveTrackLength[track] < 1u)
        return NUM_STEPS_PER_BAR;
    return seq_effectiveTrackLength[track];
}
```

### G-6 — `Core/Hardware/timebase.c:277` — **Add** (the service after `ccSvc_tick();`)

```c
    ccSvc_tick();

    /*
     * S106 P1: one bounded generator pass after copy/clear (GeneratorEngine.c
     * gen_service()): Pattern-dirty requests, one effective-lane test, at
     * most one rebuild. Same 500 Hz foreground cadence, never TIM3.
     */
    gen_service();
```

Add `#include "GeneratorEngine.h"` to `timebase.c`.

### G-7 — `Core/Bank/Scene/Pattern/PatternData.c:99–103` — **Modify** (`pat_markSceneDirty()`: the rebuild hook)

```c
static void pat_markSceneDirty(uint8_t scene_index)
{
    bank_invalidateSdCleanScene(scene_index);
    autosave_markPatternDirty(scene_index);
    /*
     * S106 P1: the single Pattern mutation funnel also tells the generators
     * (plan G9: "the track's pattern data is updated via edit or record").
     * gen_patternChanged() is one PRIMASK-guarded OR, safe from TIM3 (live
     * erase reaches this funnel through pat_setStepActive()).
     */
    gen_patternChanged(scene_index);
}
```

Add `#include "GeneratorEngine.h"` to `PatternData.c`, and update the function's header
comment `Affiliates:` line with "gen_patternChanged() (S106 P1)".

### G-8 — `sequencer.c:2689–2697` — **Add** (`seq_selectActivePattern()`: inside the PRIMASK section)

After the `for (track ...)` loop that writes `seq_perTrackPattern[track] = pattern;` and
before `seq_perTrackActive = 0u;`:

```c
	/*
	 * S106 P1: the changed tracks' generator rows belong to the outgoing
	 * Scene; clear their ready bits and request rebuilds while TIM3 is
	 * masked, so no step of a changed track plays the old row (plan G10).
	 * A kept track is identical in both Scenes, generator settings included
	 * (presetManager.c identity lists), so its row stays valid.
	 */
	gen_tracksSwitched(change);
```

### G-9 — `Core/Menu/menu.c:10675` — **Add** (`menu_perfSceneChangeExecute()`: the switch-time build)

After `seq_selectActivePattern(scene_index, keep, effect_same);`:

```c
    /*
     * S106 P1 (plan Q12): rebuild the changed generator tracks now, in the
     * foreground and before the next TIM3 step can reach them, so the first
     * step in the new Scene is normally ready. Kept tracks were not
     * requested and are skipped.
     */
    gen_buildNow((uint8_t)(~keep & 0x7Fu));
```

Add `#include "GeneratorEngine.h"` to `menu.c`.

### G-10 — `sequencer.c:2269–2290` — **Add** (`seq_setTrackPlayedScene()`: mark)

At the end of the function, after `seq_trackPlayState[track] = 0u;`:

```c
    /*
     * S106 P1: the reassigned track(s) play another Scene's generator
     * settings and Pattern; drop the old rows (plan G10). Tracks 5 and 6
     * moved together above, so both are marked.
     */
    gen_tracksSwitched((uint8_t)((track >= 5u) ? 0x60u : (1u << track)));
```

### G-11 — `sequencer.c:2409–2435` — **Add** (`seq_assignTracksToScene()`: build)

After the `for (track ...)` loop:

```c
	/*
	 * S106 P1 (plan Q12): the per-track assignment builds its generator rows
	 * now, as the whole-Scene change does (menu_perfSceneChangeExecute()).
	 * Foreground: the panel gesture and MIDI PC on a voice channel.
	 */
	gen_buildNow(trackMask);
```

### G-12 — `sequencer.c` four switch paths — **Add** (mark)

Each path below moves `seq_perTrackPattern[]`. Each gets one `gen_tracksSwitched()` call
after its loop, inside the same PRIMASK section where the function has one:

| Function | Anchor (the loop) | Mask passed |
|---|---|---|
| `seq_alignActivePatternToScene()` (`:2811`) | `seq_perTrackPattern[track] = scene_index;` (`:2848`) | `0x7Fu` |
| `seq_clearPerTrackOverrides()` (`:2354`) | `seq_perTrackPattern[track] = seq_activePattern;` (`:2362`) | `changed` (already computed at `:2360`) |
| `seq_switchToBackgroundScene()` (`:2924`) | `seq_perTrackPattern[track] = SCENE_BACKGROUND_INDEX;` (`:2937`) | `0x7Fu` |
| `seq_moveTracksToBackgroundScene()` (`:2975`) | `seq_perTrackPattern[track] = SCENE_BACKGROUND_INDEX;` (`:2986`) | `track_mask` |

The line to add, with the mask from the table:

```c
	/*
	 * S106 P1: these tracks now play another Scene's generator settings and
	 * Pattern; drop their store rows and rebuild in the service (plan G10:
	 * a switched track does not fire until rebuilt).
	 */
	gen_tracksSwitched(mask);
```

These paths do not do a switch-time build:
- the loads that call `seq_alignActivePatternToScene()` (`filesystem.c:15633`, `:15744`,
  `:15955`, `:32133`, `:33454`) realign the transport;
- the Scene 17 moves come from `preset_tickBackgroundBankLoad()` (`presetManager.c:4654`).

The service rebuilds within one to seven passes (2–14 ms), and G10 keeps those tracks silent
in between.

---

## 5. Sequencer read routing and the `ato` gate (`Core/Sequencer/sequencer.c`)

With every generator `off`, each routed read takes its first branch and calls exactly
what the line called before. Gate (§11.2): the TIM3 body changes only at these sites.

### R-1 — `sequencer.c:3504` — **Add** (three helpers before `static void seq_advanceTrackStep(uint8_t track)`)

```c
/*
 * Is the track's playback routed through its generator? (S106 P1)
 *
 * What: 1 when the track's generator is on and either builds output (not
 * passing through) or a MIDI hold drives its live arp; 0 when the track
 * plays its Pattern (no generator, or all intervals off, plan G22). Inputs:
 * the GeneratorEngine TIM3 masks. Context: TIM3 and foreground (byte reads).
 */
static uint8_t seq_trackRouted(uint8_t track)
{
	const uint8_t bit = (uint8_t)(1u << track);

	return (uint8_t)((gen_activeMask & bit) != 0u &&
	                 ((gen_passMask & bit) == 0u ||
	                  (gen_liveMask & bit) != 0u));
}

/*
 * One step of a track as it plays (S106 P1; plan §4.5).
 *
 * What: the trigger bit and specials of step `step` of `track`: from the
 * played Scene's Pattern when the track is not routed, else from the
 * generator store or the live arp (gen_readStep()). advanceLive is 1 only
 * at the track's step event, so the live arp moves on once per step; every
 * re-read of the same step (deferred fire, ramp prediction, muted probe)
 * passes 0. Why: one reader instead of separate trigger and specials calls,
 * so a step is read once per event and the live arp cannot advance twice.
 * Inputs: track 0..6, step 0..127, advanceLive. Output: trigger 0/1, *sp.
 * Context: TIM3 (seq_advanceTrackStep(), seq_fireDeferred(), the ramp
 * prediction).
 */
static uint8_t seq_stepRead(uint8_t track, uint8_t step, uint8_t advanceLive,
                            pat_step_specials_t *sp)
{
	const uint8_t scene = seq_perTrackPattern[track];

	if (!seq_trackRouted(track)) {
		*sp = pat_readStepSpecials(scene, track, step);
		return pat_isStepActive(track, step, scene);
	}
	return gen_readStep(track, step, advanceLive, sp);
}

/*
 * The automation gate (S106 P1; plan G3, FQ5).
 *
 * What: 0 when the track's generator is on (pass-through included) and its
 * `ato` is off: none of the track's step automation is queued, counted or
 * paired. Otherwise the Pattern count as before. Why: "ato = on means read
 * the automation, ato = off means don't" whenever the type is not off. Lanes
 * stored on other tracks' steps still apply (the targets are Scene-wide).
 */
static uint8_t seq_trackReadsAutomation(uint8_t track)
{
	return (uint8_t)((gen_atoOffMask & (uint8_t)(1u << track)) == 0u);
}

static uint8_t seq_stepAutomationCount(uint8_t track, uint8_t step)
{
	if (!seq_trackReadsAutomation(track))
		return 0u;
	return pat_stepAutomationCount(seq_perTrackPattern[track], track, step);
}
```

### R-2 — `sequencer.c:3664–3690` — **Modify** (the muted self-mute probe)

Replace the `if (track_muted) { ... }` block:

```c
		if (track_muted) {
			/*
			 * S106 P1: the probe reads the step as it plays (seq_stepRead(),
			 * advanceLive 0), so a generated condition can bypass the mute
			 * like a Pattern one. A Pattern step without a block reads the
			 * default condition 0, which is not a self-mute condition, so
			 * the result equals the former address-bit pre-test.
			 */
			pat_step_specials_t msp;
			uint8_t cond;
			uint8_t self_mute = 0u;

			(void)seq_stepRead(track, (uint8_t)seq_stepIndex[track], 0u, &msp);
			cond = msp.condition;
			/*
			 * Self-mute conditions (S080 B4; S104 P1 shifts them +1
			 * behind !1-): 28..34 "this voice muted", 35..41 "this
			 * voice not muted", voice = cond - 28 or cond - 35.
			 */
			if (cond >= 28u && cond <= 34u &&
			    (uint8_t)(cond - 28u) == track)
				self_mute = 1u;
			else if (cond >= 35u && cond <= 41u &&
			    (uint8_t)(cond - 35u) == track)
				self_mute = 1u;
			if (self_mute &&
			    seq_evaluateStepCondition(&msp, track))
				process_step = 1u;
		}
```

### R-3 — `sequencer.c:3714–3718` — **No change** (the muted recording track's fired bit)

It keeps reading the Pattern: live record works on the Pattern only (plan G30).

### R-4 — `sequencer.c:3724–3741` — **Modify** (the step read, the fired bit)

Replace from `pat_step_specials_t sp = pat_readStepSpecials(` through the
`rec_fired = (uint8_t)(step_allowed && ... );` statement:

```c
		/*
		 * S106 P1: the step as it plays - Pattern, generator store or live
		 * arp - read once for the whole step event (seq_stepRead(),
		 * advanceLive 1). step_on replaces the three later trigger-bit reads.
		 * The condition is evaluated on the played specials, so a generated
		 * `lst` chain follows its base step (plan G18).
		 */
		pat_step_specials_t sp;
		const uint8_t step_on = seq_stepRead(track,
		    (uint8_t)seq_stepIndex[track], 1u, &sp);
		uint8_t step_allowed = seq_evaluateStepCondition(&sp, track);
		/* Set when this step's own trigger is deferred (A1-3b); the step-allowed
		 * automation block below is then skipped so the entries queue at the
		 * deferred fire instead. */
		uint8_t deferred_this_step = 0u;
		/* Set when this step's stored trigger is suppressed (C-9c). */
		uint8_t trigger_suppressed = 0u;
		/* S106 P1: this step's played trigger fires (roll carry, below). */
		const uint8_t step_fires = (uint8_t)(step_allowed && step_on);

		/*
		 * Fired bit: bit 15 set and the condition passed, known at the step start
		 * whether or not the trigger is deferred (S081 §9.1a, C-9a).
		 * S106 P1: on a routed track the ring reports the Pattern step (live
		 * record works on the Pattern only, plan G30), evaluated without
		 * side effects so the played condition's counters are not touched
		 * twice (seq_conditionPassesPure()).
		 */
		if (seq_trackRouted(track)) {
			pat_step_specials_t psp = pat_readStepSpecials(
			    seq_perTrackPattern[track], track, (uint8_t)seq_stepIndex[track]);

			rec_fired = (uint8_t)(pat_isStepActive(track,
			                          (uint8_t)seq_stepIndex[track],
			                          seq_perTrackPattern[track]) &&
			                      seq_conditionPassesPure(&psp, track));
		} else {
			rec_fired = step_fires;
		}
```

### R-5 — `sequencer.c:3764–3770` — **Modify** (the roll cut)

```c
		if (seq_rollActive[track] && sp.roll == 0u && step_on &&
		    step_allowed) {
			seq_rollActive[track] = 0u;
			seq_rollWindow[track] = 0u;
		}
```

(K-3 changes `sp.roll == 0u` to the rate test at step 7.)

### R-6 — `sequencer.c:3823–3828` — **Modify** (the carry test reads the played trigger)

```c
		if ((seq_rollActive[track] & SEQ_ROLL_CARRY) != 0u &&
		    !(step_fires && !trigger_suppressed &&
		      ((seq_eraseFullMask | seq_eraseSpecialsMask) &
		       (uint8_t)(1u << track)) == 0u &&
		      seq_rollCarries(track, &sp)))
			seq_rollActive[track] &= (uint8_t)~SEQ_ROLL_CARRY;
```

Add to the block comment above it (`:3800–3822`): "S106 P1: `step_fires` (the played
trigger) replaces `rec_fired`, which on a generator track reports the Pattern."

### R-7 — `sequencer.c:3830–3855` — **Modify** (erase and trigger decisions)

Replace the opening `if (pat_isStepActive(...)) {` and the two erase branches, down to
the `} else if (step_allowed && !trigger_suppressed) {` that opens the trigger block,
with:

```c
		/*
		 * Erase and fire decisions (S106 P1, schedule SD5).
		 *
		 * What: on a track that plays its Pattern, nothing changes: an active
		 * step is erased while an erase gesture holds the track, else it fires.
		 * On a routed (generator) track the two are independent: an erase
		 * acts on the Pattern step (the generator's input, gated by the
		 * Pattern's own bit), and the generated trigger still fires; the
		 * output follows at the next rebuild (pat_setStepActive() reaches
		 * pat_markSceneDirty(), which requests it). Why: plan §4.5 - live
		 * erase acts on the Pattern.
		 */
		{
		const uint8_t bit = (uint8_t)(1u << track);
		const uint8_t erase_bits = (uint8_t)((seq_eraseFullMask |
		                                      seq_eraseSpecialsMask) & bit);
		uint8_t erase_now;
		uint8_t fire_now;

		if (seq_trackRouted(track)) {
			erase_now = (uint8_t)(erase_bits != 0u &&
			    pat_isStepActive(track, (uint8_t)seq_stepIndex[track],
			                     seq_perTrackPattern[track]));
			fire_now = step_on;
		} else {
			erase_now = (uint8_t)(erase_bits != 0u && step_on);
			fire_now = (uint8_t)(erase_bits == 0u && step_on);
		}
		if (erase_now) {
			if ((seq_eraseFullMask & bit) != 0u) {
				/* (the existing "Live erase (full)" comment and two calls) */
				pat_setStepActive(seq_perTrackPattern[track], track,
				                  (uint8_t)seq_stepIndex[track], 0u);
				patSvc_enqueueErase(seq_perTrackPattern[track], track,
				                    (uint8_t)seq_stepIndex[track]);
			} else {
				/* S081 G-5: specials erase keeps automation; TIM3 clears bit 15. */
				pat_setStepActive(seq_perTrackPattern[track], track,
				                  (uint8_t)seq_stepIndex[track], 0u);
				patSvc_enqueueEraseSpecials(seq_perTrackPattern[track], track,
				                            (uint8_t)seq_stepIndex[track]);
			}
		}
		if (fire_now && step_allowed && !trigger_suppressed) {
```

The trigger block body (`:3856–3969`) is unchanged, and so is the brace count:
- `:3970`, which closed `} else if (step_allowed && !trigger_suppressed) {`, now closes
  `if (fire_now && ...) {`;
- `:3971`, which closed `if (pat_isStepActive(...)) {`, now closes the new `{` scope
  opened above `const uint8_t bit`.

### R-8 — `sequencer.c:3004` — **Add** (the gate at the top of `seq_queueStepAutomations()`)

After the local declarations:

```c
    /*
     * S106 P1 (plan G3): a generator track with `ato` off reads none of its
     * Pattern automation. Gating here covers every caller (the step event
     * and seq_fireDeferred()).
     */
    if (!seq_trackReadsAutomation(track))
        return;
```

`seq_trackReadsAutomation()` is defined after this function (R-1). Add its prototype to
the forward declarations at `:1849`:

```c
static uint8_t seq_trackReadsAutomation(uint8_t track);   /* S106 P1 */
```

### R-9 — `sequencer.c:4011–4014` — **Modify** (the early-flush count)

```c
            if (seq_trackShufflePending[track] && !deferred_this_step &&
                seq_stepAutomationCount(track,
                                        (uint8_t)seq_stepIndex[track]) != 0u)
                seq_fireDeferred(track);
```

### R-10 — `sequencer.c:4734–4757` and `:6747–6759` — **Modify** (ramp prediction; deferred fire)

`seq_rampPredict()`:

```c
    if (seq_stepAutomationCount(track, (uint8_t)seq_stepIndex[track]) == 0u)
        return;
    if ((seq_mutedTracks & bit) || (seq_eraseFullMask & bit))
        return;
    b = seq_rampNextStep(track, region, &repeatInc);
    if (b < 0 || seq_stepAutomationCount(track, (uint8_t)b) == 0u)
        return;
    {
        /* S106 P1: B as it will play (generator store or Pattern). */
        const uint8_t b_on = seq_stepRead(track, (uint8_t)b, 0u, &spb);

        if (!seq_rampConditionPredict(&spb, track, repeatInc))
            return;
        interval = stepScale_ticksQ8(seq_effectiveTrackScale[track]);
        r = seq_trackAccumulator[track];
        ticksToB = (interval > r) ? (uint16_t)((interval - r + 255u) >> 8u) : 1u;
        /* B's automation is deferred with B's trigger (shuffle + micro-timing). */
        if (b_on && (seq_eraseSpecialsMask & bit) == 0u) {
```

The rest of the function body is unchanged. Close the added `{` before
`ctx->endQ4 = ...`. The local `uint8_t scene` becomes unused; remove it.

`seq_fireDeferred()`, the two re-reads:

```c
	if (roll) {
		/* S106 P1: the deferred step as it plays (advanceLive 0: the live
		 * arp already moved on at the step event). */
		pat_step_specials_t sp;

		(void)seq_stepRead(track, step, 0u, &sp);
		seq_rollBegin(track, &sp);
	} else {
		/* S096 P3: a plain deferred fire starts the step's length window
		 * (re-reads the specials: no stored fill, 0 B). */
		pat_step_specials_t sp;

		(void)seq_stepRead(track, step, 0u, &sp);
		seq_triggerVoice(track, seq_trackShuffleVel[track],
		                 seq_trackShuffleNote[track], 1);
		seq_lenStart(track, sp.fill);
	}
```

Step 9 (L-8) changes this plain branch again for `leg`/`sus`.

---

## 6. The Generator page (menu, buttons, LEDs, MIDI Learn)

### M-1 — `Core/Hardware/frontPanel/buttonHandler.h:24` — **Modify** (name mode 0x06)

```c
/*
 * SHIFT+MODE STEP selects the Generator page (S106 P1). Mode 6 was the SOM
 * generator until S097 and unused until S106. The shifted mode arithmetic
 * (mode + 4) & 7 of handleModeButtons() lands here.
 */
#define SELECT_MODE_GEN         0x06
```

### M-2 — `Core/Hardware/frontPanel/buttonHandler.c:2797–2808` — **Remove** (the S097 early return)

Remove the block comment "SHIFT+MODE STEP has no action (S097)" and the two lines
`if (buttonHandler_getShift() && mode == SELECT_MODE_STEP) return;`. The press then
falls through the latched-surface close (`:2809–2819`) and the shifted assignment at
`:2867` sets `SELECT_MODE_GEN`.

### M-3 — `buttonHandler.c:2925` — **Add** (the mode case, after `case SELECT_MODE_FX: ... break;`)

```c
    case SELECT_MODE_GEN:
        /*
         * SHIFT+MODE STEP: the Generator page (S106 P1; plan §5.1).
         *
         * What: clears the step/SELECT rows and opens GENERATOR_PAGE for the
         * active track; the page paints the generated triggers on the SEQ row
         * (menu_switchPage() -> menuGenerator_renderSeqLeds()). Why: plan G12.
         * SHIFT is still held from the chord; the Morph view turns on only on
         * a later SHIFT press (the press handler), so this entry shows Normal
         * values. Affiliates: menuGenerator.c, menu_switchPage().
         */
        led_clearSequencerLeds();
        led_clearSelectLeds();
        menu_switchPage(GENERATOR_PAGE);
        break;
```

### M-4 — `buttonHandler.c:2954` `handleSelectButton()` — **Add** (two cases)

In the SHIFT switch, after `case SELECT_MODE_VOICE: buttonHandler_selectBar(selectNr); break;`:

```c
        case SELECT_MODE_GEN:
            /* S106 P1: SHIFT+SELECT chooses the viewed bar of the generated
             * output (the SEQ row), as in VOICE mode. */
            buttonHandler_selectBar(selectNr);
            menu_generatorRefreshLeds();
            break;
```

In the plain switch, after the `case SELECT_MODE_VOICE:` block:

```c
    case SELECT_MODE_GEN:
        /* S106 P1: SELECT 1/2 choose the page halves (static-page geometry,
         * schedule SD1). */
        menu_switchSubPage(selectNr);
        led_setActiveSelectButton(menu_getSubPage());
        menu_repaintAll();
        break;
```

### M-5 — `buttonHandler.c:2528`, `:2641` — **Add** (SEQ press and release in mode 6)

`buttonHandler_seqButtonPressed()`, plain switch, before `default:`:

```c
        case SELECT_MODE_GEN:
            /*
             * S106 P1 (plan Q16): SEQ buttons select steps for held-step lane
             * automation. A press arms the shared hold timer; at the hold
             * threshold menu_genOverlayHoldExpired() opens the overlay. The
             * press never toggles a step: the Pattern is edited in STEP mode.
             */
            if (menu_voiceAutoOverlayActive())
                buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
            else
                buttonHandler_setTimeraction(
                    buttonHandler_visibleStep(seqButtonPressed));
            break;
```

`buttonHandler_seqButtonReleased()`, plain switch, before `default:`:

```c
    case SELECT_MODE_GEN:
        /* S106 P1: an overlay-owned release is consumed; a short tap only
         * clears its timer (no step toggle). */
        if (menu_voiceAutoOverlayActive()) {
            buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
            return;
        }
        (void)buttonHandler_TimerActionOccured();
        break;
```

### M-6 — `buttonHandler.c:885–902` — **Add** (`buttonHandler_armTimerActionStep()`: the hold threshold)

After the `SELECT_MODE_FX` branch:

```c
    if (bh_state.selectButtonMode == SELECT_MODE_GEN) {
        /* S106 P1: the hold opens the Generator page's held-step overlay. */
        menu_genOverlayHoldExpired();
        return;
    }
```

### M-7 — `buttonHandler.c:4101` (SHIFT press) and `:4531` (SHIFT release) — **Add**

Press switch, before `case SELECT_MODE_STEP:`:

```c
        case SELECT_MODE_GEN:
            /* S106 P1 (plan §5.3): hold SHIFT on the Generator page to show and
             * edit the lanes' Morph endpoints, field by field. */
            menu_setGenShowMorph(1u);
            break;
```

Release switch, before `case SELECT_MODE_STEP:`:

```c
        case SELECT_MODE_GEN:
            /* S106 P1: SHIFT release returns to the Normal lanes. */
            menu_setGenShowMorph(0u);
            break;
```

### M-8 — `Core/Hardware/frontPanel/ledHandler.c:1243`, `:1416–1418` — **Modify**

```c
    case 6: led_setBlinkLed(LED_MODE3, 1); break;  /* S106 P1: Generator (SHIFT+MODE STEP) */
```

and the chase condition:

```c
        /* S106 P1: the Generator page shows the chase over the generated
         * output (plan G29). */
        if ((menu_activePage < MENU_MIDI_PAGE) ||
            (menu_activePage == SEQ_PAGE) ||
            (menu_activePage == EUKLID_PAGE) ||
            (menu_activePage == GENERATOR_PAGE)) {
```

### M-9 — `Core/Menu/menu.h:89–94` and the prototype block — **Modify / Add**

The page enum:

```c
    /*
     * The Generator page (S106 P1; SHIFT+MODE STEP, select mode 6). It reuses
     * the id of the S097-archived SOM page, so EFFECT_PAGE keeps its value.
     * Cells are resolved by menuGenerator.c, not by the static page table.
     */
    GENERATOR_PAGE,
```

After `uint8_t menu_voiceAutoOverlayActive(void);`:

```c
/*
 * Generator page hooks (S106 P1).
 * menu_genOverlayHoldExpired(): the SEQ hold threshold in mode 6 opens the
 *   held-step lane overlay (ButtonHandler).
 * menu_setGenShowMorph(): SHIFT press/release in mode 6 (the Morph view).
 * menu_generatorRefreshLeds(): repaint the SEQ row with the generated
 *   output of the active track (bar change, track change, rebuild).
 */
void menu_genOverlayHoldExpired(void);
void menu_setGenShowMorph(uint8_t on);
void menu_generatorRefreshLeds(void);
```

`menu.h:94` is the only code use of `SOM_PAGE` (grep of `Core/`); `menuPages.h:28–30`
names it in a comment (M-29).

### M-10 — `Core/Menu/menuGenerator.h` — **New file**

```c
/*
 * Core/Menu/menuGenerator.h - the Generator page owner (S106 P1).
 *
 * What: resolves the Generator page's cells for the active track (the
 * `gen` type cell, the type's field cells, its fixed cells, `ato` and the
 * `cmt` commit cell), their values, names, clamps and commits, the `gen`
 * browse transaction, the SHIFT Morph view, the commit action and the SEQ
 * row (the generated output, plan G29).
 * Why a module: as menuEffects.c, so menu.c keeps only the generic renderer,
 * encoder and pot plumbing and delegates through MENU_CELL_GENERATOR.
 * Layout (schedule SD1): linear cell index L = sub_page * 8 + position;
 * L 0 = gen, 1..F = fields, then fixed, then ato, then cmt. For the arp:
 * SELECT 1 = gen in1 in2 in3 | oct dir vel stp, SELECT 2 = nln ato cmt.
 * Type off shows `gen` only (plan G12).
 * Scene: the active Scene (scene_getActiveIndex()); edits fan out through
 * bank_editTargetMask() to the Scenes whose track has the same type (Q9,
 * schedule SD2). Track: menu_getActiveVoice().
 * Context: foreground only. Affiliates: menu.c (GENERATOR_PAGE branches),
 * GeneratorEngine.c, buttonHandler.c (mode 6), MidiLearn.c (field rows).
 */
#ifndef MENU_GENERATOR_H_
#define MENU_GENERATOR_H_

#include <stdint.h>

typedef enum {
    MENU_GEN_CELL_NONE = 0,
    MENU_GEN_CELL_TYPE,      /* `gen`: browse on click-in, commit on click-out */
    MENU_GEN_CELL_FIELD,     /* index = field 0..field_count-1 */
    MENU_GEN_CELL_FIXED,     /* index = fixed 0..fixed_count-1 */
    MENU_GEN_CELL_ATO,       /* `ato` */
    MENU_GEN_CELL_COMMIT     /* `cmt`: click starts the commit job */
} menuGenerator_cell_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t index;
} menuGenerator_cell_t;

/* menuGenerator_editModeChanged() / _service() action bits. */
#define MENU_GEN_ACT_REPAIR     0x01u   /* layout changed: reset the cursor */
#define MENU_GEN_ACT_EXIT_EDIT  0x02u   /* leave the clicked-in view */
#define MENU_GEN_ACT_REPAINT    0x04u   /* repaint the page */
#define MENU_GEN_ACT_LEDS       0x08u   /* repaint the SEQ row */

void menuGenerator_enter(void);
void menuGenerator_leave(void);
uint8_t menuGenerator_resolveCell(uint8_t sub_page, uint8_t position,
                                  menuGenerator_cell_t *out);
uint16_t menuGenerator_cellValue(const menuGenerator_cell_t *cell);
void menuGenerator_clampValue(const menuGenerator_cell_t *cell, uint16_t *value);
uint8_t menuGenerator_cellCommit(const menuGenerator_cell_t *cell,
                                 uint16_t value, uint8_t panel);
uint8_t menuGenerator_formatValue3(const menuGenerator_cell_t *cell,
                                   uint8_t value, char *dst);
void menuGenerator_shortName(const menuGenerator_cell_t *cell, char *dst);
uint8_t menuGenerator_paintEditView(const menuGenerator_cell_t *cell);
uint8_t menuGenerator_editModeChanged(uint8_t edit_active,
                                      const menuGenerator_cell_t *cell);
void menuGenerator_typeBrowse(int8_t inc);
void menuGenerator_setShowMorph(uint8_t on);
uint8_t menuGenerator_showMorph(void);
uint8_t menuGenerator_service(void);
void menuGenerator_renderSeqLeds(void);
void menuGenerator_onTrackChanged(void);

/*
 * Field lanes and held-step edits (menu.c's gn_* overlay family).
 * menuGenerator_fieldLanes(): the Scene target IDs of a field's one or two
 *   lanes (ids[0] = part 0); returns the count (0 for non-field cells).
 * menuGenerator_heldEdit(): applies `delta` to the field in the seeded lane
 *   image `lanes` (the held step's stored lane bytes, or the retained lanes)
 *   and returns the mask of the lanes it changed.
 */
uint8_t menuGenerator_fieldLanes(const menuGenerator_cell_t *cell,
                                 uint16_t ids[2]);
uint8_t menuGenerator_heldEdit(const menuGenerator_cell_t *cell,
                               uint8_t lanes[4], int16_t delta);

/*
 * MIDI Learn of a field (plan G33, FQ11; schedule SD6).
 * menuGenerator_learnTag(): 0x80 | type << 3 | field for a field cell, else
 *   0. MidiLearn stores it in a Scene row's type byte.
 * menuGenerator_commitFieldFromMidi(): a CC on a field row: the 7-bit value
 *   scaled onto the field's range, committed as a menu edit (fan-out, R12,
 *   recording) on `track`; refused when the track's type is not `type`.
 */
uint8_t menuGenerator_learnTag(const menuGenerator_cell_t *cell);
uint8_t menuGenerator_commitFieldFromMidi(uint8_t track, uint8_t type,
                                          uint8_t field, uint8_t value7);

#endif /* MENU_GENERATOR_H_ */
```

### M-11 — `Core/Menu/menuGenerator.c` — **New file**

```c
/*
 * Core/Menu/menuGenerator.c - the Generator page owner (S106 P1).
 * Contract: menuGenerator.h.
 */
#include "menuGenerator.h"
#include "GeneratorEngine.h"
#include "SceneData.h"
#include "SceneModTargets.h"
#include "BankData.h"
#include "LiveRecord.h"
#include "sequencer.h"
#include "ledHandler.h"
#include "buttonHandler.h"
#include "menu.h"
#include "MenuText.h"
#include <string.h>

/*
 * Page state (S106 P1; RAM 6 B SRAM1, approved row M).
 *
 * menuGenerator_typeEdit / _typeCandidate: the open `gen` browse (as the
 * Effect `typ`): the candidate is shown, and committed on click-out only
 * (plan G13). menuGenerator_showMorphView: SHIFT held (plan §5.3).
 * menuGenerator_commitState: 0 idle, 1 running ("..."), 2 refused ("--")
 * until the next cursor move. menuGenerator_lastType: the track's type at
 * the last paint (a type change elsewhere repairs the cursor).
 */
static uint8_t menuGenerator_typeEdit;
static uint8_t menuGenerator_typeCandidate;
static uint8_t menuGenerator_showMorphView;
static uint8_t menuGenerator_commitState;
static uint8_t menuGenerator_lastType;
static uint8_t menuGenerator_ledValid;

static uint8_t mg_track(void)  { return menu_getActiveVoice(); }
static uint8_t mg_scene(void)  { return scene_getActiveIndex(); }
static uint8_t mg_type(void)   { return scene_getGenType(mg_scene(), mg_track()); }
static const gen_type_desc_t *mg_desc(void) { return gen_typeDesc(mg_type()); }

static void mg_copyField(char *dst, const char *src, uint8_t width)
{
    uint8_t i = 0u;

    while (src && i < width && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < width)
        dst[i++] = ' ';
}

/* The lane image shown (Normal, or Morph endpoints while SHIFT is held). */
static void mg_lanes(uint8_t scene, uint8_t track, uint8_t morph,
                     uint8_t out[GEN_LANES])
{
    uint8_t l;

    for (l = 0u; l < GEN_LANES; l++)
        out[l] = morph ? scene_getGenLaneMorph(scene, track, l)
                       : scene_getGenLane(scene, track, l);
}

void menuGenerator_enter(void)
{
    menuGenerator_typeEdit = 0u;
    menuGenerator_showMorphView = 0u;
    menuGenerator_commitState = 0u;
    menuGenerator_lastType = mg_type();
    menuGenerator_ledValid = 0u;
}

void menuGenerator_leave(void)
{
    /* Leaving abandons an open browse and the Morph view (as menuEffects). */
    menuGenerator_typeEdit = 0u;
    menuGenerator_showMorphView = 0u;
}

uint8_t menuGenerator_resolveCell(uint8_t sub_page, uint8_t position,
                                  menuGenerator_cell_t *out)
{
    const gen_type_desc_t *d = mg_desc();
    const uint8_t type = menuGenerator_typeEdit ? menuGenerator_typeCandidate
                                                : mg_type();
    uint16_t at = (uint16_t)(sub_page * 8u + position);

    out->kind = MENU_GEN_CELL_NONE;
    out->index = 0u;
    if (at == 0u) {
        out->kind = MENU_GEN_CELL_TYPE;
        return 1u;
    }
    /* While browsing, or for type off, only `gen` exists (plan G12). */
    if (menuGenerator_typeEdit || type == GEN_TYPE_OFF)
        return 0u;
    at--;
    if (at < d->field_count) {
        out->kind = MENU_GEN_CELL_FIELD;
        out->index = (uint8_t)at;
        return 1u;
    }
    at = (uint16_t)(at - d->field_count);
    if (at < d->fixed_count) {
        out->kind = MENU_GEN_CELL_FIXED;
        out->index = (uint8_t)at;
        return 1u;
    }
    at = (uint16_t)(at - d->fixed_count);
    if (at == 0u) {
        out->kind = MENU_GEN_CELL_ATO;
        return 1u;
    }
    if (at == 1u) {
        out->kind = MENU_GEN_CELL_COMMIT;
        return 1u;
    }
    return 0u;
}

uint16_t menuGenerator_cellValue(const menuGenerator_cell_t *cell)
{
    uint8_t lanes[GEN_LANES];

    switch (cell->kind) {
    case MENU_GEN_CELL_TYPE:
        return menuGenerator_typeEdit ? menuGenerator_typeCandidate : mg_type();
    case MENU_GEN_CELL_FIELD:
        mg_lanes(mg_scene(), mg_track(), menuGenerator_showMorphView, lanes);
        return gen_fieldGet(&mg_desc()->fields[cell->index], lanes);
    case MENU_GEN_CELL_FIXED:
        return scene_getGenFixed(mg_scene(), mg_track(), cell->index);
    case MENU_GEN_CELL_ATO:
        return scene_getGenAto(mg_scene(), mg_track());
    default:
        return 0u;
    }
}

void menuGenerator_clampValue(const menuGenerator_cell_t *cell, uint16_t *value)
{
    uint16_t max = 0u;

    switch (cell->kind) {
    case MENU_GEN_CELL_FIELD:
        max = (uint16_t)(mg_desc()->fields[cell->index].count - 1u);
        break;
    case MENU_GEN_CELL_FIXED:
        max = (uint16_t)(mg_desc()->fixed[cell->index].count - 1u);
        break;
    case MENU_GEN_CELL_ATO:
        max = 1u;
        break;
    case MENU_GEN_CELL_TYPE:
        max = (uint16_t)(GEN_TYPE_COUNT - 1u);
        break;
    default:
        break;
    }
    if (*value > max)
        *value = max;
}

/*
 * Commit a field value (S106 P1; plan §5.2, Q9, R12, G30).
 *
 * What: for every Scene in bank_editTargetMask() whose track has the active
 * Scene's type, rewrites the field's bits in that Scene's Normal lanes (or
 * Morph lanes in the Morph view) through scene_setGenLane*(). Then, for the
 * active Scene's lanes it touched: the R12 clear (the step overlay and its
 * restore bit), and in LATCH/OVERDUB the live-record report of each lane
 * (liveRec_paramIn(), panel source 0 or MIDI), which records to rtk as every
 * menu edit does. Morph-view edits are not recorded (as menu.c's
 * menu_liveRecRecordCell()). Returns nonzero when a byte changed.
 */
static uint8_t mg_commitField(uint8_t track, uint8_t type, uint8_t field,
                              uint8_t value, uint8_t panel)
{
    const gen_type_desc_t *d = gen_typeDesc(type);
    const uint8_t morph = (uint8_t)(panel && menuGenerator_showMorphView);
    const uint32_t mask = bank_editTargetMask();
    uint8_t touchedActive = 0u;
    uint8_t changed = 0u;
    uint8_t m;
    uint8_t l;

    if (field >= d->field_count)
        return 0u;
    for (m = 0u; m < SCENE_SLOT_TOTAL; m++) {
        uint8_t lanes[GEN_LANES];
        uint8_t before[GEN_LANES];
        uint8_t touched;

        if (((mask >> m) & 1u) == 0u || scene_getGenType(m, track) != type)
            continue;
        mg_lanes(m, track, morph, lanes);
        memcpy(before, lanes, GEN_LANES);
        touched = gen_fieldSet(&d->fields[field], value, lanes);
        for (l = 0u; l < GEN_LANES; l++) {
            if (((touched >> l) & 1u) == 0u || lanes[l] == before[l])
                continue;
            if (morph)
                scene_setGenLaneMorph(m, track, l, lanes[l]);
            else
                scene_setGenLane(m, track, l, lanes[l]);
            changed = 1u;
        }
        if (m == mg_scene())
            touchedActive = touched;
    }
    for (l = 0u; l < GEN_LANES; l++) {
        const uint16_t id = sceneModTarget_genLaneId(track, l);
        uint8_t index;

        if (((touchedActive >> l) & 1u) == 0u)
            continue;
        if (!morph) {
            gen_clearStepOverride(track, l);
            if (sceneModTarget_indexFromId(id, &index))
                seq_clearSceneAutomationBit(index);
            liveRec_paramIn(id, panel ? 0u : LIVEREC_SRC_MIDI);
        }
    }
    return changed;
}

uint8_t menuGenerator_cellCommit(const menuGenerator_cell_t *cell,
                                 uint16_t value, uint8_t panel)
{
    const uint8_t track = mg_track();
    const uint32_t mask = bank_editTargetMask();
    uint8_t m;

    switch (cell->kind) {
    case MENU_GEN_CELL_FIELD:
        return mg_commitField(track, mg_type(), cell->index, (uint8_t)value,
                              panel);
    case MENU_GEN_CELL_FIXED:
        for (m = 0u; m < SCENE_SLOT_TOTAL; m++)
            if (((mask >> m) & 1u) && scene_getGenType(m, track) == mg_type())
                scene_setGenFixed(m, track, cell->index, (uint8_t)value);
        return 1u;
    case MENU_GEN_CELL_ATO:
        for (m = 0u; m < SCENE_SLOT_TOTAL; m++)
            if ((mask >> m) & 1u)
                scene_setGenAto(m, track, (uint8_t)(value != 0u));
        return 1u;
    default:
        return 0u;   /* `gen` commits on click-out; `cmt` acts on click */
    }
}

uint8_t menuGenerator_formatValue3(const menuGenerator_cell_t *cell,
                                   uint8_t value, char *dst)
{
    const gen_type_desc_t *d = mg_desc();
    const gen_field_desc_t *f = 0;

    switch (cell->kind) {
    case MENU_GEN_CELL_TYPE:
        mg_copyField(dst, gen_typeDesc(value)->short3, 3u);
        return 1u;
    case MENU_GEN_CELL_ATO:
        mg_copyField(dst, value ? "on" : menuText_off, 3u);
        return 1u;
    case MENU_GEN_CELL_COMMIT:
        mg_copyField(dst, (menuGenerator_commitState == 1u) ? "..." :
                          (menuGenerator_commitState == 2u) ? " --" :
                          menuText_ok, 3u);
        return 1u;
    case MENU_GEN_CELL_FIELD:
        f = &d->fields[cell->index];
        break;
    case MENU_GEN_CELL_FIXED:
        f = &d->fixed[cell->index];
        break;
    default:
        return 0u;
    }
    if (value >= f->count)
        value = (uint8_t)(f->count - 1u);
    if (f->names) {
        mg_copyField(dst, f->names[value], 3u);
    } else {
        numtostrpu(dst, f->numbers ? f->numbers[value] : value, ' ');
    }
    return 1u;
}

void menuGenerator_shortName(const menuGenerator_cell_t *cell, char *dst)
{
    const gen_type_desc_t *d = mg_desc();

    switch (cell->kind) {
    case MENU_GEN_CELL_TYPE:   mg_copyField(dst, "gen", 3u); break;
    case MENU_GEN_CELL_FIELD:  mg_copyField(dst, d->fields[cell->index].name3, 3u); break;
    case MENU_GEN_CELL_FIXED:  mg_copyField(dst, d->fixed[cell->index].name3, 3u); break;
    case MENU_GEN_CELL_ATO:    mg_copyField(dst, "ato", 3u); break;
    case MENU_GEN_CELL_COMMIT: mg_copyField(dst, "cmt", 3u); break;
    default:                   mg_copyField(dst, "", 3u); break;
    }
}

/*
 * The clicked-in view (S106 P1; plan §5.2, G13).
 *
 * `gen`: row 0 "Generatr" / "Type", row 1 the candidate's 8-letter name
 * and its 3-letter name at column 13. A field: row 0 the type's name /
 * the field's long name, row 1 the value text at column 13. `ato`, `cmt`:
 * row 0 "Generatr" / "ReadAuto" or "Commit"; `cmt` shows the Load/Save
 * arrow and `ok` (menu.c:14358-14364 idiom). Returns 1 when painted.
 */
uint8_t menuGenerator_paintEditView(const menuGenerator_cell_t *cell)
{
    const gen_type_desc_t *d = mg_desc();
    char v3[3];

    memset(&editDisplayBuffer[0][0], ' ', 16u);
    memset(&editDisplayBuffer[1][0], ' ', 16u);
    switch (cell->kind) {
    case MENU_GEN_CELL_TYPE: {
        const gen_type_desc_t *c = gen_typeDesc(
            (uint8_t)menuGenerator_cellValue(cell));

        mg_copyField(&editDisplayBuffer[0][0], "Generatr", 8u);
        mg_copyField(&editDisplayBuffer[0][8], "Type", 8u);
        mg_copyField(&editDisplayBuffer[1][0], c->full8, 8u);
        mg_copyField(&editDisplayBuffer[1][13], c->short3, 3u);
        return 1u; }
    case MENU_GEN_CELL_FIELD:
        mg_copyField(&editDisplayBuffer[0][0], d->full8, 8u);
        mg_copyField(&editDisplayBuffer[0][8], d->fields[cell->index].long8, 8u);
        break;
    case MENU_GEN_CELL_FIXED:
        mg_copyField(&editDisplayBuffer[0][0], d->full8, 8u);
        mg_copyField(&editDisplayBuffer[0][8], d->fixed[cell->index].long8, 8u);
        break;
    case MENU_GEN_CELL_ATO:
        mg_copyField(&editDisplayBuffer[0][0], "Generatr", 8u);
        mg_copyField(&editDisplayBuffer[0][8], "ReadAuto", 8u);
        break;
    case MENU_GEN_CELL_COMMIT:
        mg_copyField(&editDisplayBuffer[0][0], "Generatr", 8u);
        mg_copyField(&editDisplayBuffer[0][8], "Commit", 8u);
        editDisplayBuffer[1][13] = ARROW_SIGN;
        mg_copyField(&editDisplayBuffer[1][14], menuText_ok, 2u);
        return 1u;
    default:
        return 0u;
    }
    (void)menuGenerator_formatValue3(cell,
                                     (uint8_t)menuGenerator_cellValue(cell), v3);
    memcpy(&editDisplayBuffer[1][13], v3, 3u);
    return 1u;
}

/*
 * The `gen` transaction and the `cmt` action (S106 P1; plan §5.2, §5.5).
 *
 * `gen`: click-in opens a browse on the current type; turning steps the
 * candidate without wrapping (menuGenerator_typeBrowse()); click-out commits
 * only a changed type. For every Scene in the edit mask it then writes the
 * type (scene_setGenType(); ato kept) and the new type's default lanes,
 * Morph lanes and fixed bytes (plan Q6), and returns REPAIR (the cell list
 * changed). `cmt`: a click-in requests the commit job (gen_requestCommit(),
 * step 10) and returns EXIT_EDIT, so the click is one action; a refusal
 * shows " --" (FQ2/FQ4).
 */
uint8_t menuGenerator_editModeChanged(uint8_t edit_active,
                                      const menuGenerator_cell_t *cell)
{
    if (edit_active) {
        if (cell && cell->kind == MENU_GEN_CELL_TYPE) {
            menuGenerator_typeEdit = 1u;
            menuGenerator_typeCandidate = mg_type();
        } else if (cell && cell->kind == MENU_GEN_CELL_COMMIT) {
            menuGenerator_commitState =
                gen_requestCommit(mg_track(), mg_scene()) ? 1u : 2u;
            return (uint8_t)(MENU_GEN_ACT_EXIT_EDIT | MENU_GEN_ACT_REPAINT);
        }
        return 0u;
    }
    if (!menuGenerator_typeEdit)
        return 0u;
    menuGenerator_typeEdit = 0u;
    if (menuGenerator_typeCandidate == mg_type())
        return MENU_GEN_ACT_REPAIR;
    {
        const uint8_t track = mg_track();
        const uint8_t type = menuGenerator_typeCandidate;
        const uint32_t mask = bank_editTargetMask();
        uint8_t lanes[GEN_LANES];
        uint8_t fixed[GEN_FIXED_PARAMS];
        uint8_t m, l;

        gen_typeDefaults(type, lanes, fixed);
        for (m = 0u; m < SCENE_SLOT_TOTAL; m++) {
            if (((mask >> m) & 1u) == 0u)
                continue;
            scene_setGenType(m, track, type);
            for (l = 0u; l < GEN_LANES; l++) {
                scene_setGenLane(m, track, l, lanes[l]);
                scene_setGenLaneMorph(m, track, l, lanes[l]);
            }
            for (l = 0u; l < GEN_FIXED_PARAMS; l++)
                scene_setGenFixed(m, track, l, fixed[l]);
        }
        menuGenerator_lastType = type;
    }
    return (uint8_t)(MENU_GEN_ACT_REPAIR | MENU_GEN_ACT_LEDS);
}

void menuGenerator_typeBrowse(int8_t inc)
{
    if (!menuGenerator_typeEdit)
        return;
    if (inc > 0 && menuGenerator_typeCandidate + 1u < GEN_TYPE_COUNT)
        menuGenerator_typeCandidate++;
    else if (inc < 0 && menuGenerator_typeCandidate > 0u)
        menuGenerator_typeCandidate--;
}

void menuGenerator_setShowMorph(uint8_t on)  { menuGenerator_showMorphView = (uint8_t)(on != 0u); }
uint8_t menuGenerator_showMorph(void)        { return menuGenerator_showMorphView; }

/*
 * Per-pass page service (S106 P1).
 *
 * What: a rebuild of the active track repaints the SEQ row; a type change
 * from elsewhere (AutoSave restore, MIDI-driven copy, Scene switch) repairs
 * the cursor; a finished commit (gen_commitState(), step 10) clears the
 * "..." state. Output: MENU_GEN_ACT_* bits for menu.c.
 */
uint8_t menuGenerator_service(void)
{
    uint8_t act = 0u;
    const uint8_t type = mg_type();

    if ((gen_takeUiChanged() >> mg_track()) & 1u || !menuGenerator_ledValid)
        act |= MENU_GEN_ACT_LEDS;
    if (type != menuGenerator_lastType && !menuGenerator_typeEdit) {
        menuGenerator_lastType = type;
        act |= (uint8_t)(MENU_GEN_ACT_REPAIR | MENU_GEN_ACT_REPAINT);
    }
    if (menuGenerator_commitState == 1u && !gen_commitRunning()) {
        menuGenerator_commitState = 0u;
        act |= MENU_GEN_ACT_REPAINT;
    }
    return act;
}

/*
 * The SEQ row: the generated output of the active track for the viewed bar
 * (S106 P1; plan G29, §5.4). Every other mode shows the Pattern (unchanged).
 * The chase is drawn by the ledHandler chase layer (M-8).
 */
void menuGenerator_renderSeqLeds(void)
{
    const uint8_t track = mg_track();
    const uint8_t start = (uint8_t)(menu_currentBar * NUM_STEPS_PER_BAR);
    uint8_t i;

    for (i = 0u; i < NUM_STEPS_PER_BAR; i++)
        led_setValue(gen_outputBit(track, (uint8_t)(start + i)),
                     (uint8_t)(LED_STEP1 + i));
    menuGenerator_ledValid = 1u;
}

void menuGenerator_onTrackChanged(void)
{
    menuGenerator_typeEdit = 0u;
    menuGenerator_commitState = 0u;
    menuGenerator_lastType = mg_type();
    menuGenerator_ledValid = 0u;
}

uint8_t menuGenerator_fieldLanes(const menuGenerator_cell_t *cell,
                                 uint16_t ids[2])
{
    const gen_field_desc_t *f;
    uint8_t p;

    if (!cell || cell->kind != MENU_GEN_CELL_FIELD)
        return 0u;
    f = &mg_desc()->fields[cell->index];
    for (p = 0u; p < f->parts && p < 2u; p++)
        ids[p] = sceneModTarget_genLaneId(mg_track(), f->part[p].lane);
    return f->parts;
}

uint8_t menuGenerator_heldEdit(const menuGenerator_cell_t *cell,
                               uint8_t lanes[4], int16_t delta)
{
    const gen_field_desc_t *f;
    int16_t v;

    if (!cell || cell->kind != MENU_GEN_CELL_FIELD)
        return 0u;
    f = &mg_desc()->fields[cell->index];
    v = (int16_t)((int16_t)gen_fieldGet(f, lanes) + delta);
    if (v < 0)
        v = 0;
    if (v >= (int16_t)f->count)
        v = (int16_t)(f->count - 1u);
    return gen_fieldSet(f, (uint8_t)v, lanes);
}

uint8_t menuGenerator_learnTag(const menuGenerator_cell_t *cell)
{
    if (!cell || cell->kind != MENU_GEN_CELL_FIELD)
        return 0u;
    return (uint8_t)(0x80u | ((mg_type() & 0x0Fu) << 3u) | (cell->index & 7u));
}

uint8_t menuGenerator_commitFieldFromMidi(uint8_t track, uint8_t type,
                                          uint8_t field, uint8_t value7)
{
    const gen_type_desc_t *d = gen_typeDesc(type);

    if (track >= NUM_TRACKS ||
        scene_getGenType(scene_getActiveIndex(), track) != type ||
        field >= d->field_count)
        return 0u;
    /* Scale 0..127 onto 0..count-1 (the field's own range). */
    return mg_commitField(track, type, field,
        (uint8_t)(((uint16_t)value7 * d->fields[field].count) / 128u), 0u);
}
```

`gen_requestCommit()` and `gen_commitRunning()` are added at step 10 (C-3). Until then,
steps 6–9 build with two stubs at the end of `GeneratorEngine.c`. C-3 replaces them:

```c
/* Step 6 stubs, replaced by the commit job at step 10 (S106 P1). */
uint8_t gen_requestCommit(uint8_t track, uint8_t scene) { (void)track; (void)scene; return 0u; }
uint8_t gen_commitRunning(void) { return 0u; }
```

Declare both in `GeneratorEngine.h` (after `gen_takeUiChanged()`):

```c
/* The commit job (step 10): request (0 = refused) and running test. */
uint8_t gen_requestCommit(uint8_t track, uint8_t scene);
uint8_t gen_commitRunning(void);
```

### M-12 — `Core/Menu/menu.c:2197–2259` — **Modify** (cell kind and cell member)

```c
    MENU_CELL_EFFECT,
    /*
     * Generator page cell (S106 P1). Value, format, clamp and commit
     * delegate to menuGenerator.c through cell.gen (the `gen` type cell, a
     * field, a fixed byte, `ato` or `cmt`).
     */
    MENU_CELL_GENERATOR
} menu_cell_kind_t;
```

and in `menu_cell_t`, after `menuEffects_cell_t fx;`:

```c
    /* Generator page cell identity; meaningful only for MENU_CELL_GENERATOR. */
    menuGenerator_cell_t gen;
```

Add `#include "menuGenerator.h"` to `menu.c`'s include block.

### M-13 — `menu.c:4860` — **Add** (`menu_resolveCellAbsolute()`: the Generator branch)

After `if (subPage >= NUM_SUB_PAGES) return cell;`:

```c
    /*
     * S106 P1: the Generator page's cells come from menuGenerator.c. They are
     * resolved here (not in menu_resolveCell()), so has2ndPage(), the
     * populated-column helpers and the generic traversal see them through
     * the same function they use for static pages (schedule SD1).
     */
    if (menu_activePage == GENERATOR_PAGE) {
        if (position < MENU_STATIC_SUBPAGE_CELLS &&
            menuGenerator_resolveCell(subPage, position, &cell.gen))
            cell.kind = MENU_CELL_GENERATOR;
        return cell;
    }
```

### M-14 — `menu.c` five per-cell dispatchers — **Add** (one branch each, beside the `MENU_CELL_EFFECT` branch)

`menu_cellDtype()` (`:5385`):

```c
    /* S106 P1: Generator cells are plain indices; menuGenerator clamps. */
    if (cell->kind == MENU_CELL_GENERATOR)
        return DTYPE_0B127;
```

`menu_cellDisplayValue()` (`:5412`):

```c
    if (cell->kind == MENU_CELL_GENERATOR)
        return menuGenerator_cellValue(&cell->gen);
```

`menu_cellCommitValueEx()` (`:5548`):

```c
    /* S106 P1: Generator cells commit through menuGenerator (fan-out, R12,
     * recording inside); `panel` selects the Morph view and the source. */
    if (cell->kind == MENU_CELL_GENERATOR)
        return menuGenerator_cellCommit(&cell->gen, value, panel);
```

`menu_formatCellValue3()` (`:6823`):

```c
    if (cell && cell->kind == MENU_CELL_GENERATOR &&
        menuGenerator_formatValue3(&cell->gen, value, valueAsText))
        return;
```

`menu_clampCellValue()` (`:7106`):

```c
    if (cell->kind == MENU_CELL_GENERATOR) {
        menuGenerator_clampValue(&cell->gen, value);
        return;
    }
```

### M-15 — `menu.c:15620` and `:15921` — **Add** (`menu_repaintGeneric()`: full view and names)

Edit view, after the Effect `paintEditView` branch:

```c
        /* S106 P1: Generator cells paint their own full view. */
        if (cell.kind == MENU_CELL_GENERATOR &&
            menuGenerator_paintEditView(&cell.gen)) {
            gn_applyMarkers();
            return;
        }
```

Compact name loop, after the `MENU_CELL_EFFECT` short-name branch:

```c
            } else if (cell.kind == MENU_CELL_GENERATOR) {
                menuGenerator_shortName(&cell.gen,
                                        &editDisplayBuffer[0][4u * i]);
```

After `sa_applyTrackMarkers();` (`:15997`) **Add**:

```c
    /* S106 P1: the Generator page's lane underlines (gn_applyMarkers()). */
    gn_applyMarkers();
```

### M-16 — `menu.c:16227–16229` — **Modify** (`menu_moveToMenuItem()`: the cross-sub-page step)

```c
    if (menu_activePage == PERFORMANCE_PAGE ||
        menu_activePage == RECORDING_PAGE ||
        menu_activePage == MENU_MIDI_PAGE ||
        /* S106 P1: SELECT 1 -> SELECT 2 (nln ato cmt) by scrolling. */
        menu_activePage == GENERATOR_PAGE) {
```

### M-17 — `menu.c:16045–16105` — **Add** (`menu_encoderChangeParameter()`: four Generator intercepts)

After the STEP overlay block (`:16045–16051`):

```c
    /*
     * S106 P1 (plan Q16): Generator held-step overlay. A turn on a field
     * writes the field's lane(s) to every held step of the active track.
     */
    if (va_overlayActive && menu_activePage == GENERATOR_PAGE) {
        gn_writeAutomationFromKnob(activeParameter, inc);
        return;
    }
```

Inside the Learn branch (`:16057–16064`), before the generic `menu_cellMidiTargetAt()`
test:

```c
        /* S106 P1 (plan G33): a field Learns as a tagged lane row. */
        if (menu_activePage == GENERATOR_PAGE &&
            gn_learnSelect(activePage, activeParameter))
            return;
```

After the Effect `typ` browse branch (`:16101–16105`):

```c
    /* S106 P1: `gen` browses candidates in its full view; `cmt` is inert. */
    if (cell.kind == MENU_CELL_GENERATOR) {
        if (cell.gen.kind == MENU_GEN_CELL_TYPE) {
            menuGenerator_typeBrowse(inc);
            return;
        }
        if (cell.gen.kind == MENU_GEN_CELL_COMMIT)
            return;
    }
```

The TX block at `:16164–16170` needs no change: `menu_cellMidiTarget()` returns 0 for a
Generator cell (no raw lane TX, SD6).

### M-18 — `menu.c:17608–17619` — **Add** (`menu_parseEncoder()`: the click)

After the Effect click block:

```c
    /*
     * S106 P1: `gen` click-in browses, click-out commits the type; `cmt`
     * click starts the commit job and leaves the full view at once.
     */
    if (btnClicked && menu_activePage == GENERATOR_PAGE) {
        menu_cell_t gen_cell = menu_resolveCell(
            menu_getSubPage(), menuIndex & MASK_PARAMETER);
        const uint8_t act = menuGenerator_editModeChanged(
            editModeActive,
            gen_cell.kind == MENU_CELL_GENERATOR ? &gen_cell.gen : NULL);

        if (act & MENU_GEN_ACT_EXIT_EDIT)
            editModeActive = 0u;
        if (act & MENU_GEN_ACT_REPAIR) {
            menu_resetActiveParameter();
            menu_endlessPotMappingChanged();
        }
        if (act & MENU_GEN_ACT_LEDS)
            menu_generatorRefreshLeds();
        if (btnClicked && va_overlayActive) {
            va_underlineSuppressed = 0u;
            gn_refreshAutomationLeds();
        }
    }
```

### M-19 — `menu.c:18287` and `:18386–18388` — **Add** (`menu_parseKnobDelta()`)

Before the STEP overlay block (`:18287`):

```c
    /*
     * S106 P1 (plan Q16): Generator held-step overlay owns the pots: a field
     * turn writes its lane(s) to the held steps; in a copy/clear clear mode
     * it removes them from the held steps only.
     */
    if (va_overlayActive && menu_activePage == GENERATOR_PAGE &&
        knobNr < ENDLESS_POT_COUNT) {
        const uint8_t pos = (uint8_t)(knobNr +
            (((menuIndex & MASK_PARAMETER) > 3u) ? 4u : 0u));

        if (copyClear_ownsPots()) {
            if (copyClear_isClearMode() && !copyClear_menuVisible())
                gn_clearAutomationFromKnob(pos);
            return;
        }
        gn_writeAutomationFromKnob(pos, delta);
        return;
    }
```

After the Effect `typ` inert line (`:18386–18388`):

```c
    /* S106 P1: the pot over `gen` or `cmt` is inert. */
    if (cell.kind == MENU_CELL_GENERATOR &&
        (cell.gen.kind == MENU_GEN_CELL_TYPE ||
         cell.gen.kind == MENU_GEN_CELL_COMMIT))
        return;
```

Pot clear without held steps (`menu_knobClearTarget()`) returns 0 for a Generator cell
(it resolves no single Pattern target), so a SHIFT+COPY pot turn over a field clears
nothing.

### M-20 — `menu.c` `menu_switchPage()` — **Modify / Add** (`:19918–19924`, `:20086`, `:20206`)

The overlay boundary (`:19918–19920`):

```c
    if ((was_voice_page && !menu_isVoicePage(pageNr)) ||
        (menu_activePage == SEQ_PAGE && pageNr != SEQ_PAGE) ||
        /* S106 P1: leaving the Generator page ends its held-step overlay. */
        (menu_activePage == GENERATOR_PAGE && pageNr != GENERATOR_PAGE))
        va_resetOverlay();
```

After `if (menu_activePage == EFFECT_PAGE && pageNr != EFFECT_PAGE) menuEffects_leave();`:

```c
    if (menu_activePage == GENERATOR_PAGE && pageNr != GENERATOR_PAGE)
        menuGenerator_leave();
```

A new case before `case EFFECT_PAGE: {`:

```c
    case GENERATOR_PAGE:
        /*
         * The Generator page (S106 P1; plan §5.1).
         *
         * What: enters at SELECT 1 / `gen`, resets the browse and Morph view,
         * and on a fresh entry restarts the automation-presence search in
         * Generator geometry (all seven tracks, the active track's lane
         * targets; va_searchRestart() reads menu_activePage, so it is set
         * first). Affiliates: menuGenerator_enter(), gn_applyMarkers().
         */
        menu_instrumentLoadActive = 0u;
        menu_setVoiceModeShowMorph(0u);
        menuGenerator_enter();
        menu_activePage = GENERATOR_PAGE;
        if (old_page != GENERATOR_PAGE)
            va_searchRestart();
        editModeActive = 0u;
        lockPotentiometerFetch();
        menuIndex = 0u;
        break;
```

The LED branch (`:20206`), before `} else {`:

```c
    } else if (pageNr == GENERATOR_PAGE) {
        /* S106 P1: the SEQ row shows the generated output (plan G29). */
        led_setActiveVoiceLeds((uint8_t)(1 << menu_getActiveVoice()));
        led_setActiveSelectButton(menu_getSubPage());
        menuGenerator_renderSeqLeds();
```

### M-21 — `menu.c:20964–20965` — **Modify** (`menu_setActiveVoice()`)

```c
        if (menu_activePage != EFFECT_PAGE)
            va_searchRestart();
        /* S106 P1: the Generator page follows the selected track. */
        if (menu_activePage == GENERATOR_PAGE) {
            menuGenerator_onTrackChanged();
            menu_generatorRefreshLeds();
            menu_repaintAll();
        }
        return;
```

### M-22 — `menu.c:2472–2482`, `:2625–2636`, `:2749–2756`, `:2784` — **Modify** (the presence search in Generator geometry)

`va_searchRestart()`:

```c
    /* S106 P1: the Generator page scans all seven tracks, because its lane
     * targets are Scene-wide and may be stored on any track's steps. */
    va_searchTrack = (menu_activePage == EFFECT_PAGE ||
                      menu_activePage == PERFORMANCE_PAGE ||
                      menu_activePage == GENERATOR_PAGE) ? 0u : menu_activeVoice;
```

`va_searchRecordTarget()`, after the Effect branch:

```c
    /*
     * S106 P1: the Generator page classifies the active track's generator
     * lanes, stored on any track: bit l of va_searchSceneMask (the mask is
     * page-local; gn_applyMarkers() is its reader on this page).
     */
    if (menu_activePage == GENERATOR_PAGE) {
        const scene_mod_target_descriptor_t *descriptor =
            sceneModTarget_descriptor(target);

        if (descriptor && descriptor->kind == SCENE_MOD_TARGET_KIND_GEN_LANE &&
            descriptor->voice_slot == menu_activeVoice)
            va_searchSceneMask |= (uint8_t)(1u << (descriptor->param & 3u));
        return;
    }
```

`va_scanService()`: the seven-track geometry flag at `:2749–2756` and the cursor advance
at `:2784`:

```c
    uint8_t effect_page = (uint8_t)(menu_activePage == EFFECT_PAGE ||
                                    /* S106 P1: same seven-track walk */
                                    menu_activePage == GENERATOR_PAGE);
```

(The Generator page keeps the Effect page's seven-track cursor walk. The page test in
`va_searchRecordTarget()` above runs before the Effect classification, so the shared flag
only selects the walk.)

### M-23 — `menu.c:18632–18638` — **Add** (`menu_serviceRuntimeWidgets()`: Generator services)

After the SEQ_PAGE block:

```c
    /*
     * S106 P1: Generator page services: the held-step poll and debounce (as
     * SEQ_PAGE), the seven-track lane search, and menuGenerator_service()'s
     * actions (LED repaint after a rebuild, cursor repair after an external
     * type change, the end of a commit).
     */
    if (menu_activePage == GENERATOR_PAGE) {
        uint8_t act;

        if (va_overlayActive)
            va_updateHeldState();
        va_scanService();
        if (va_overlayActive)
            va_underlineService();
        act = menuGenerator_service();
        if (act & MENU_GEN_ACT_REPAIR) {
            editModeActive = 0u;
            menu_resetActiveParameter();
            menu_endlessPotMappingChanged();
        }
        if ((act & MENU_GEN_ACT_LEDS) && !va_overlayActive)
            menuGenerator_renderSeqLeds();
        if (act & (MENU_GEN_ACT_REPAINT | MENU_GEN_ACT_REPAIR))
            menu_repaintAll();
    }
```

`va_updateHeldState()` (`:2854`) refreshes LEDs through `sa_refreshAutomationLeds()` on
SEQ_PAGE and `va_refreshAutomationLeds()` otherwise (`:2908–2911`). Make it call
`gn_refreshAutomationLeds()` on GENERATOR_PAGE:

```c
        if (menu_activePage == SEQ_PAGE)
            sa_refreshAutomationLeds();
        else if (menu_activePage == GENERATOR_PAGE)
            gn_refreshAutomationLeds();       /* S106 P1 */
        else
            va_refreshAutomationLeds();
```

### M-24 — `menu.c` after `menu_enterStepTrackAutomationOverlay()` (ends `:4830`) — **Add** (the `gn_*` family)

Add the prototypes beside `sa_*` at `:2412–2415`:

```c
static void gn_writeAutomationFromKnob(uint8_t cellPos, int8_t delta);
static void gn_clearAutomationFromKnob(uint8_t cellPos);
static void gn_refreshAutomationLeds(void);
static void gn_applyMarkers(void);
static uint8_t gn_learnSelect(uint8_t subPage, uint8_t position);
```

The definitions:

```c
/*
 * Generator held-step overlay (S106 P1; plan Q16, G32).
 *
 * What: the SEQ_PAGE sa_* contract for the Generator page's fields. A field
 * is one or two lanes (Scene targets 1g1..7g4 of the active track); a
 * held-step edit writes the field into each held step's lane bytes:
 *  - seed: each lane from the newest held step that stores it
 *    (va_resolveHeldValue()), else the active Scene's retained lane;
 *  - edit: menuGenerator_heldEdit() changes only the field's bits;
 *  - write: patSvc_writeStepAutomation() of each touched lane on every held
 *    step of the active track (menu_shownPattern).
 * A split field (in3, vel) writes both lanes, so a panel edit is always
 * whole (plan §6.3). Clear removes the field's lane(s) from the held steps
 * and restarts the search. Markers: a field's name underlines when any of
 * its lanes is stored anywhere in the Scene (the seven-track search), in
 * every view and whatever `ato` is (G32); a held value underlines while a
 * held step stores it.
 * Inputs: the absolute cell position on the current sub-page (0..7), delta.
 * Context: foreground. Affiliates: menuGenerator_fieldLanes(),
 * menuGenerator_heldEdit(), va_resolveHeldValue(), va_queueMarkerTransaction().
 */
static uint8_t gn_cellAt(uint8_t cellPos, menu_cell_t *cell)
{
    *cell = menu_resolveCell((uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT),
                             cellPos);
    return (uint8_t)(cell->kind == MENU_CELL_GENERATOR &&
                     cell->gen.kind == MENU_GEN_CELL_FIELD);
}

static void gn_writeAutomationFromKnob(uint8_t cellPos, int8_t delta)
{
    menu_cell_t cell;
    uint16_t ids[2];
    uint8_t lanes[GEN_LANES];
    uint8_t n;
    uint8_t touched;
    uint8_t l, p, i;
    uint8_t wrote = 0u;

    if (!va_overlayActive || menu_activePage != GENERATOR_PAGE ||
        !gn_cellAt(cellPos, &cell))
        return;
    n = menuGenerator_fieldLanes(&cell.gen, ids);
    for (l = 0u; l < GEN_LANES; l++)
        lanes[l] = scene_getGenLane(scene_getActiveIndex(), menu_activeVoice, l);
    for (p = 0u; p < n; p++) {
        uint8_t v7;
        const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(ids[p]);

        if (d && va_resolveHeldValue((instrument_param_id_t)ids[p], &v7))
            lanes[d->param & 3u] = v7;
    }
    touched = menuGenerator_heldEdit(&cell.gen, lanes, delta);
    for (l = 0u; l < GEN_LANES; l++) {
        const uint16_t id = sceneModTarget_genLaneId(menu_activeVoice, l);

        if (((touched >> l) & 1u) == 0u)
            continue;
        for (i = 0u; i < va_heldCount; i++)
            if (patSvc_writeStepAutomation(
                    menu_shownPattern, menu_activeVoice,
                    buttonHandler_visibleStep(va_heldOrder[i]), id, lanes[l]))
                wrote = 1u;
        va_searchSceneMask |= (uint8_t)(1u << l);
    }
    if (wrote) {
        va_lastEditTick = time_sysTick;
        menu_knobs_dirty = 1u;
    }
}

static void gn_clearAutomationFromKnob(uint8_t cellPos)
{
    menu_cell_t cell;
    uint16_t ids[2];
    uint8_t n, p, i;

    if (!va_overlayActive || menu_activePage != GENERATOR_PAGE ||
        !gn_cellAt(cellPos, &cell))
        return;
    n = menuGenerator_fieldLanes(&cell.gen, ids);
    for (p = 0u; p < n; p++)
        for (i = 0u; i < va_heldCount; i++)
            (void)patSvc_removeStepAutomation(
                menu_shownPattern, menu_activeVoice,
                buttonHandler_visibleStep(va_heldOrder[i]), ids[p]);
    va_searchRestart();
    gn_refreshAutomationLeds();
    menu_repaint();
}

static void gn_refreshAutomationLeds(void)
{
    menu_cell_t cell;
    uint16_t ids[2];

    if (menu_activePage != GENERATOR_PAGE)
        return;
    if (va_overlayActive && editModeActive &&
        gn_cellAt((uint8_t)(menuIndex & MASK_PARAMETER), &cell) &&
        menuGenerator_fieldLanes(&cell.gen, ids) > 0u) {
        /* Steps that store the field's first lane, held steps forced on. */
        led_updateAutomationStepView(menu_activeVoice, menu_shownPattern,
                                     (instrument_param_id_t)ids[0], va_heldMask);
        return;
    }
    menuGenerator_renderSeqLeds();
}

/*
 * Generator lane underlines (S106 P1; plan G32). One marker per visible
 * field cell, the SEQ_PAGE rule: a held value (row 1) while a held step
 * stores one of the field's lanes, else the name (row 0) once the
 * seven-track search found one of its lanes. Runs on every frame of the
 * page, with or without held steps, so stale CGRAM marks retire.
 */
static void gn_applyMarkers(void)
{
    uint8_t glyph_probe[8];
    uint8_t desired_base[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_row[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_col[4] = { 0u, 0u, 0u, 0u };
    uint8_t desired_valid = 0u;
    const uint8_t first = (uint8_t)(editModeActive
        ? (menuIndex & MASK_PARAMETER)
        : (((menuIndex & MASK_PARAMETER) > 3u) ? 4u : 0u));
    const uint8_t count = editModeActive ? 1u : 4u;
    uint8_t i;

    if (menu_activePage != GENERATOR_PAGE)
        return;
    for (i = 0u; i < count; i++) {
        menu_cell_t cell;
        uint16_t ids[2];
        uint8_t n, p;
        uint8_t held = 0u;
        uint8_t found = 0u;
        uint8_t v7;

        if (!gn_cellAt((uint8_t)(first + i), &cell))
            continue;
        n = menuGenerator_fieldLanes(&cell.gen, ids);
        for (p = 0u; p < n; p++) {
            const scene_mod_target_descriptor_t *d =
                sceneModTarget_descriptor(ids[p]);

            if (va_overlayActive &&
                va_resolveHeldValue((instrument_param_id_t)ids[p], &v7))
                held = 1u;
            if (va_searchComplete && d &&
                (va_searchSceneMask >> (d->param & 3u)) & 1u)
                found = 1u;
        }
        if (!held && !found)
            continue;
        {
            const uint8_t row = held ? 1u : 0u;
            const uint8_t col = editModeActive
                ? (uint8_t)(held ? 13u : 8u) : (uint8_t)(4u * i);

            if (lcd_underlineGlyph((uint8_t)editDisplayBuffer[row][col],
                                   glyph_probe)) {
                desired_base[i] = (uint8_t)editDisplayBuffer[row][col];
                marker_row[i] = row;
                marker_col[i] = col;
                desired_valid |= (uint8_t)(1u << i);
            }
        }
    }
    va_queueMarkerTransaction(desired_base, desired_valid,
                              marker_row, marker_col);
}

/* S106 P1: the SEQ hold threshold in mode 6 (buttonHandler). */
void menu_genOverlayHoldExpired(void)
{
    if (menu_activePage != GENERATOR_PAGE || buttonHandler_seqHeldMask() == 0u)
        return;
    va_overlayActive = 1u;
    va_underlineSuppressed = 0u;
    gn_refreshAutomationLeds();
    menu_repaint();
}

void menu_setGenShowMorph(uint8_t on)
{
    menuGenerator_setShowMorph(on);
    if (menu_activePage == GENERATOR_PAGE)
        menu_repaintAll();
}

void menu_generatorRefreshLeds(void)
{
    if (menu_activePage == GENERATOR_PAGE && !va_overlayActive)
        menuGenerator_renderSeqLeds();
}

/* S106 P1 (plan G33): Learn on a field selects its tagged lane row. */
static uint8_t gn_learnSelect(uint8_t subPage, uint8_t position)
{
    menu_cell_t cell = menu_resolveCell(subPage, position);
    uint16_t ids[2];

    if (cell.kind != MENU_CELL_GENERATOR ||
        menuGenerator_fieldLanes(&cell.gen, ids) == 0u)
        return 0u;
    midiLearn_selectTargetField(ids[0], menuGenerator_learnTag(&cell.gen));
    return 1u;
}
```

The marker transaction (`va_queueMarkerTransaction()`) owns one call per frame. The VOICE,
Effect, PERF and SEQ passes return at once off their pages, so only one pass queues on the
Generator page (the `:15985–15997` order).

### M-25 — `menu.c:6714–6724` — **Modify** (`menu_learnName()`: LRN on a field)

```c
static uint8_t menu_learnName(const menu_cell_t *cell, char *buf)
{
    uint16_t want = midiLearn_displayTarget();
    uint16_t id;

    if (want == MIDI_TARGET_OFF)
        return 0u;
    /* S106 P1: a Generator field shows LRN/FUL while its tagged row waits. */
    if (cell && cell->kind == MENU_CELL_GENERATOR) {
        uint16_t ids[2];

        if (menuGenerator_fieldLanes(&cell->gen, ids) == 0u ||
            ids[0] != want ||
            midiLearn_displayField() != menuGenerator_learnTag(&cell->gen))
            return 0u;
    } else if (!menu_cellMidiTarget(cell, &id) || id != want) {
        return 0u;
    }
    memcpy(buf, midiLearn_showingFull() ? "FUL" : "LRN", 3);
    return 1u;
}
```

### M-26 — `Core/MIDI/MidiLearn.c:94`, `:211–225` — **Add** (the field byte and its selection)

After `static uint16_t midiLearn_target = MIDI_TARGET_OFF;`:

```c
/*
 * The field tag of a waiting Generator Learn (S106 P1; RAM +1 B SRAM1, row M).
 * 0xFF (MIDI_LEARN_TYPE_NONE) for every other target; else
 * 0x80 | type << 3 | field (menuGenerator_learnTag()). It becomes the stored
 * row's type byte, so one lane can hold rows for different fields.
 */
static uint8_t midiLearn_targetField = MIDI_LEARN_TYPE_NONE;
```

`midiLearn_selectTarget()` sets `midiLearn_targetField = MIDI_LEARN_TYPE_NONE;`. Add after
it:

```c
void midiLearn_selectTargetField(uint16_t id, uint8_t fieldTag)
{
    if (!midiLearn_selectingTarget() || id >= MIDI_TARGET_OFF)
        return;
    midiLearn_target = id;
    midiLearn_targetField = fieldTag;
    midiLearn_state = MIDI_LEARN_WAIT_CC;
    menu_learnDisplayChanged();
}

uint8_t midiLearn_displayField(void)
{
    return midiLearn_targetField;
}
```

Declare both in `MidiLearn.h` after `midiLearn_displayTarget()`:

```c
/* S106 P1: select a Generator field (lane id + tag) / the shown tag. */
void     midiLearn_selectTargetField(uint16_t id, uint8_t fieldTag);
uint8_t  midiLearn_displayField(void);
```

`midiLearn_end()` (`:167–173`) adds `midiLearn_targetField = MIDI_LEARN_TYPE_NONE;`.

### M-27 — `MidiLearn.c:251–271`, `:310` — **Modify** (capture: the tag is the row's type)

In `midiLearn_captureCc()` and `midiLearn_capturePressure()`, after the
`midiLearn_keyOf(...)` call succeeds:

```c
	/* S106 P1: a Generator field row is keyed by its field tag. */
	if (space == MIDI_LEARN_SPACE_SCENE &&
	    midiLearn_targetField != MIDI_LEARN_TYPE_NONE)
		type = midiLearn_targetField;
```

### M-28 — `MidiLearn.c:138–150`, `:391–392`, `:517–523`, `:638–656` — **Modify** (match, apply, file I/O)

`midiLearn_typeMatches()`: a tagged Scene row applies while its track has that type.

```c
	if (space == MIDI_LEARN_SPACE_EFFECT)
		return (uint8_t)((uint8_t)effects_activeType() == e->type);
	/* S106 P1: a Generator field row applies while the lane's track has the
	 * row's type in the active Scene. */
	if (space == MIDI_LEARN_SPACE_SCENE && (e->type & 0x80u) != 0u &&
	    e->type != MIDI_LEARN_TYPE_NONE) {
		const scene_mod_target_descriptor_t *d =
		    sceneModTarget_descriptor(sceneModTarget_idFromIndex(e->index));

		return (uint8_t)(d && d->kind == SCENE_MOD_TARGET_KIND_GEN_LANE &&
		                 scene_getGenType(scene_getActiveIndex(),
		                                  d->voice_slot) ==
		                     (uint8_t)((e->type >> 3u) & 0x0Fu));
	}
	return 1u;
```

`midiLearn_applySource()`: the absolute branch.

```c
		if (mode == MIDI_LEARN_MODE_ABS) {
			/* S106 P1: a Generator field row writes its field, not the lane. */
			if (midiLearn_spaceOf(e) == MIDI_LEARN_SPACE_SCENE &&
			    (e->type & 0x80u) != 0u && e->type != MIDI_LEARN_TYPE_NONE) {
				const scene_mod_target_descriptor_t *d =
				    sceneModTarget_descriptor(id);

				if (d)
					consumed |= menuGenerator_commitFieldFromMidi(
					    d->voice_slot, (uint8_t)((e->type >> 3u) & 0x0Fu),
					    (uint8_t)(e->type & 7u), value7);
			} else {
				consumed |= midiControl_writeTarget(id, value7);
			}
		} else if (instrumentParam_isVoiceParameter(id)) {
```

`midiLearn_formatFileLine()`, the Scene branch: append the field token.

```c
	} else {
		const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(
		    sceneModTarget_idFromIndex(e->index));

		space = "scn";
		key = d ? d->short_name : 0;
		/*
		 * S106 P1: a Generator field row is written
		 * "learn=<src>,<g|v>,scn,<lane>,<type>:<field>" (schedule SD6), for
		 * example `learn=021,g,scn,1g1,arp:in1`.
		 */
		if (d && (e->type & 0x80u) != 0u && e->type != MIDI_LEARN_TYPE_NONE) {
			const gen_type_desc_t *g = gen_typeDesc((e->type >> 3u) & 0x0Fu);
			const uint8_t f = (uint8_t)(e->type & 7u);

			if (f < g->field_count) {
				genToken[0] = g->short3[0]; genToken[1] = g->short3[1];
				genToken[2] = g->short3[2]; genToken[3] = ':';
				genToken[4] = g->fields[f].name3[0];
				genToken[5] = g->fields[f].name3[1];
				genToken[6] = g->fields[f].name3[2];
				genToken[7] = '\0';
				fieldText = genToken;
			}
		}
	}
```

`genToken` is `char genToken[8]` and `fieldText` is `const char *fieldText = 0;`, both
declared with the function's locals. After the `key` append (`:536`), **Add**:

```c
		if (fieldText) {
			pos = midiLearn_append(dst, cap, pos, ",", 1u);
			pos = midiLearn_append(dst, cap, pos, fieldText, 7u);
		}
```

`midiLearn_parseFileValue()`, the `scn` branch: accept `count == 4` (plain) or
`count == 5` (field).

```c
	} else if (strcmp(field[2], "scn") == 0 && (count == 4u || count == 5u)) {
		/* ... the existing name match on field[3] ... */
		if (count == 5u) {
			/*
			 * S106 P1: "<type>:<field>" names a Generator field of the
			 * lane: resolve the type by its 3-letter name and the field by
			 * its 3-letter name; anything else refuses the row.
			 */
			const scene_mod_target_descriptor_t *d =
			    sceneModTarget_descriptor(sceneModTarget_idFromIndex(i));
			uint8_t gt, gf;

			if (!d || d->kind != SCENE_MOD_TARGET_KIND_GEN_LANE ||
			    strlen(field[4]) != 7u || field[4][3] != ':')
				return 0u;
			for (gt = 1u; gt < GEN_TYPE_COUNT; gt++)
				if (strncmp(gen_typeDesc(gt)->short3, field[4], 3u) == 0)
					break;
			if (gt >= GEN_TYPE_COUNT)
				return 0u;
			for (gf = 0u; gf < gen_typeDesc(gt)->field_count; gf++)
				if (strncmp(gen_typeDesc(gt)->fields[gf].name3,
				            &field[4][4], 3u) == 0)
					break;
			if (gf >= gen_typeDesc(gt)->field_count)
				return 0u;
			type = (uint8_t)(0x80u | (gt << 3u) | gf);
		} else if (sceneModTarget_descriptor(sceneModTarget_idFromIndex(i))->kind ==
		               SCENE_MOD_TARGET_KIND_GEN_LANE) {
			return 0u;   /* T-12: never a raw lane */
		}
```

The field splitter at `:583–589` already yields at most five fields. T-12's check moves
into the `else if` above.

Add `#include "GeneratorEngine.h"`, `#include "menuGenerator.h"` and
`#include "SceneData.h"` to `MidiLearn.c` where they are not already included.

### M-29 — `Core/Menu/menuPages.h:24–36` — **Modify** (comment only)

"Every other page (VOICE1-7, LOAD, SAVE, GENERATOR, EFFECT) resolves its cells elsewhere"
(S106 P1: SOM → GENERATOR; the Generator page resolves through `menuGenerator.c`).

### M-30 — `Makefile:98`, `:260` — **Add** (`menuGenerator.c` in SRCS and COLD_LAST)

```make
  Core/Menu/menuEffects.c \
  Core/Menu/menuGenerator.c \
```

and in `COLD_LAST` after `Core/Menu/menuEffects.c \`:

```make
  Core/Menu/menuGenerator.c \
```

It is a cold UI source, linked after the DSP objects like `menuEffects.c` (`Makefile:243–253`).

---
