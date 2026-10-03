# S075 — Phase 6 Copy and Clear — Follow-up F2 (hardware notes, 2026-10-03)

Plan for the second round of hardware notes. **Plan only; no code is changed
by this document.** Baseline: the working tree after the F1 + trace
implementation (`S075_PH6_COPYCLEAR_F1_AND_TRACE_IMPLEMENTATION.md`). Line
numbers were read on 2026-10-03 and are given with a function anchor.

## Contents

1. Notes and root causes
2. F2-A — default voice output St1 for every voice
3. F2-B — bus compressor defaults off / 0 / 0 / off
4. F2-C — copy/clear header text `Copy` / `Clear`
5. F2-D — Effect page full views: long name only
6. F2-E — Effect pan shows 0 at centre; CrumpBit defaults; `snc`
7. F2-F — SHIFT+TRACK on the Effect page shows that voice's `+` mix screen
8. F2-G — VOICE mode: hold SHIFT to show and edit Morph endpoints
9. F2-H — morphable FX send (recommendation)
10. Resources
11. Verification
12. Follow-up decisions

---

## 1. Notes and root causes

| # | Note (user) | Root cause found | Change |
|---|---|---|---|
| 1 | After `clear scene` / `clear scene settings` some voices come up L1 (voice 1) and St2 (voice 6); all should be St1. | `scene_defaultVoiceAudioOut()` (`SceneData.c` line 28) returns route 2 (L1) for slot 0 and route 1 (St2) for slot 5 — the old "Slak" convention. `filesystem_defaultVoiceAudioOut()` (`filesystem.c` line 16745) copies it. Route ids (`MenuText.h` line 110): 0 St1, 1 St2, 2 L1, 3 R1, 4 L2, 5 R2. | F2-A |
| 2 | The menu header should read `Clear` / `Copy`, not `CLR` / `COPY`. | `copyClear_formatMenu()` writes `"CLR"` / `"COPY"`. | F2-C |
| 3 | Clearing a Scene or its settings should reset the compressor to cmp off, cam 0, ctm 0, csc off. | `SCENE_BUS_COMP_DEFAULT_AMOUNT` and `_TIME` are 48 (`SceneData.h` lines 146–147), used by `scene_busCompDefaults()` for every default path. | F2-B |
| 4 | Effect page `run` full view shows the long name on the left and the short name on the right. | `menuEffects_paintEditView()` (`menuEffects.c` line 550) writes the long name at column 0 and `menuEffects_formatValue3()` at column 13 for `run`, `typ` and `scl`. | F2-D |
| 5 | `typ` full view: `Off 1 Mo     off`, `StFilter   * flt`, `CrumpBit   * cbt`; should be just the long name. | Same as 4, plus the `*` "changed" mark at column 11, plus a real bug: `menuEffects_copyField()` (line 541) tests `src[i]` for every column, so after a short string's terminator it keeps copying whatever follows it in flash ("Off" + "\0" + "1 Mo…"). | F2-D |
| 6 | Effect output pan should reset to 0 (shows 1). CrumpBit defaults: mix 0, fbk 64, rte 64, dpn 0 (dpn also shows 1). Rename `syn` → `snc`. | Pan rows use `DTYPE_PM63` (display = value − 63) but the default and the DSP centre are 64 (`EffectTypes.h` line 66; mixer balance law `mixer.c` lines 1030–1036 and CrumpBit `CrumpBitEffect.c` lines 329–332 are both centred on 64). CrumpBit rows: `CrumpBitParameters.c` lines 58–69. | F2-E |
| 7 | Effect page: SHIFT+TRACK shows the `+` mix screen of that voice while TRACK is held; release restores the last Effect page. | Today SHIFT+TRACK in EFFECTS goes to the type's TRACK hook, then the mute/select path (`buttonHandler.c` `handleVoiceButton()` lines 1114–1122). | F2-F |
| 8 | VOICE mode: holding SHIFT shows/edits Morph endpoints like the Effect page. | VOICE Morph view is today a latch (SHIFT+MODE VOICE, `buttonHandler.c` line 272 `menu_setVoiceModeShowMorph()`); a plain SHIFT press in VOICE does nothing (`processPress()` `case BUT_SHIFT`). | F2-G |
| 9 | FX send should be morphable, both endpoints stored in Scene settings and following the voice's Morph amount. | FX send is one Scene byte per voice (`scene_settings_t.fx_send_amount[6]`). | F2-H |

---

## 2. F2-A — default voice output St1 for every voice

**Change.** The default Scene route of every voice is 0 (St1).

| File | Line | Change |
|---|---|---|
| `Core/Bank/Scene/SceneData.c` | 28–44 `scene_defaultVoiceAudioOut()` | return 0 for every slot; block: "Default Scene route of a voice: St1 (route 0) for every slot (user F2, 2026-10-03; replaces the old L1/St2 convention). Used by scene_initAll(), scene_settingsDefaults() (clear scene / clear scene settings), and the invalid-value fallbacks of the route setter/getter." |
| `Core/Hardware/SD/filesystem.c` | 16745 `filesystem_defaultVoiceAudioOut()` | same; used by `filesystem_initSceneStage()` (missing `audio_out` key) and the legacy kitset import fallback (line 12693) — scope: §12 F2-Q1 |
| `Core/DSPAudio/mixer.c` | `mixer_init()` routing defaults (if not already 0) | match |
| `tools/populate_scene_directory.py` | `audio_out` default list | all 0 |
| `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`, `BANK_PRESET_ARCHITECTURE.md` | default route text | St1 for all voices |

Existing Scenes keep their stored routes; only defaults change.

---

## 3. F2-B — bus compressor defaults off / 0 / 0 / off

| File | Line | Change |
|---|---|---|
| `Core/Bank/Scene/SceneData.h` | 146–147 | `SCENE_BUS_COMP_DEFAULT_AMOUNT 0u`, `SCENE_BUS_COMP_DEFAULT_TIME 0u`; block: "Defaults off / 0 / 0 / off (user F2): a cleared or fresh Scene has no compression and neutral values." |
| `SceneData.c` `scene_busCompDefault[]` (line ~849) | uses the macros | none |
| `SceneData.h` line 507 block, `filesystem_initSceneStage()` comment | "off, 48, 48, off" → "off, 0, 0, off" | text |
| `AUTOSAVE.md`, `FILESYSTEM_SPEC.md`, `BANK_PRESET_ARCHITECTURE.md` | default values | text |

`scene_busCompDefaults()` already feeds every default path (`clear scene`,
`clear scene settings`, fresh Scenes, missing `bus_comp_*` keys). Scope:
F2-Q2.

---

## 4. F2-C — copy/clear header text `Copy` / `Clear`

`Core/Menu/CopyClear/copyClearSession.c` `copyClear_formatMenu()`:

```c
    cc_put(row0, &pos, (cc_state.phase == CC_OP_CLEAR) ? "Clear" : "Copy");
    /* F1-D: the source indicator always starts at the 9th character. */
    pos = 8u;
```

`copyClearSession.h` `copyClear_menuVisible()` block: "row 0 `Copy` / `Clear`
at column 0 and the source indicator from column 8 (the 9th character)". Spec
§8.1 example: `Copy    03T2s005`, `Clear   S03T2`.

---

## 5. F2-D — Effect page full views: long name only

**Rule (user):** a full (clicked-in) view never shows both a long and a short
form of the same value; it shows the long name only.

`Core/Menu/menuEffects.c`:

1. **Bug fix, `menuEffects_copyField()` (line 541):** stop at the first
   terminator and pad the rest:

```c
/*
 * Copy a bounded/padded LCD field (F2-D fix).
 *
 * Copies src up to its terminator or `width` characters, then pads with
 * spaces. The former loop tested src[i] for every column and read past a
 * short string's terminator into whatever followed it in flash, which put
 * "Off 1 Mo" on the `typ` view. Callers: menuEffects_paintEditView().
 */
static void menuEffects_copyField(char *dst, const char *src, uint8_t width)
{
    uint8_t i = 0u;

    if (src)
        for (; i < width && src[i] != '\0'; i++)
            dst[i] = src[i];
    for (; i < width; i++)
        dst[i] = ' ';
}
```

2. **`menuEffects_paintEditView()` (lines 550–600):**
   - `MENU_FX_CELL_TYPE`: keep `entry->full8` at column 0; REMOVE the `*`
     mark (line ~567) and the `menuEffects_formatValue3()` call (line ~568).
   - `MENU_FX_CELL_RUN`: REMOVE the `menuEffects_formatValue3()` call
     (line ~577).
   - `MENU_FX_CELL_SCALE`: REMOVE the `menuEffects_formatValue3()` call
     (line ~587) — same rule (long name `stepScale_longName()` stays).
   - `LENGTH` and `MORPH_AMOUNT` show only a number at the right and are
     unchanged.
   - Block of the function: "Full views show the long name of a value at
     column 0 and nothing else on that row; numeric rows show the number at
     column 13 (F2-D)."

3. The `typ` browse still commits only on click-out; without the `*` mark the
   candidate is visible from its name changing. See F2-Q3.

Other pages: the generic full view in `menu.c` prints only the short value at
column 13 (no long name on the bottom row), so it already follows the rule.

---

## 6. F2-E — Effect pan shows 0 at centre; CrumpBit defaults; `snc`

### 6.1 Pan centre

> **Decided (F2-Q4, 2026-10-03): neither A nor B as written.** Every pan in
> the product operates and displays the same way: stored 0 is fully left and
> shows `-63`, stored 127 is fully right and shows `64`, and the default is
> the absolute centre, stored **63**, which shows `0`. `DTYPE_PM63` stays
> (no `DTYPE_PM64`). The Effect pan defaults (`pan`, CrumpBit `dpn`) move to
> 63, and the stereo balance law of the Effect return and of the CrumpBit
> delay moves its centre to 63 so that 63 is unity on both sides. The mono
> laws (instrument pan, mono Effect return) keep their `squareRootLut` maths;
> stereo and mono maths may differ but display and default the same.
> Instrument defaults are not touched. Option A below is withdrawn.
> Implementation: `S075_PH6_COPYCLEAR_F2_IMPLEMENTATION.md` Stage E.

The Effect pan rows (`effect_pan`, CrumpBit `crump_dly_pan`) store 0..127 with
the DSP centre at **64** (mixer balance law and CrumpBit both give unity
gain on both sides at 64). Their dtype `DTYPE_PM63` displays value − 63, so
centre shows `+1`. Two ways to fix (F2-Q4):

- **A (recommended): display-only.** ADD `DTYPE_PM64` (display value − 64,
  range −64..+63) and use it for the two Effect pan rows. Stored values and
  sound are unchanged; centre (64) shows 0; every saved Effect still sounds
  the same.
- **B: move the default to 63.** Default 63 shows 0 but is not the DSP
  centre (right gain 63/64, about −0.14 dB), unless the Effect DSP centre is
  moved to 63 too, which changes the sound of saved Effects.

Plan for A:

| File | Change |
|---|---|
| `Core/DSP/Instruments/InstrumentManager.h` (dtype enum) | ADD `DTYPE_PM64` with block: "Signed display centred at 64: stored 0..127 shows −64..+63; used where the DSP centre is 64 (Effect pans, F2-E)." |
| `Core/Menu/menu.c` lines ~2583, ~4483, ~4643 (dtype switches) | `case DTYPE_PM64: numtostrps(out, (int8_t)(value - 64));` and the same numeric edit path as PM63 |
| `Core/DSP/Effects/EffectParamRows.h` line 61 | `effect_pan` → `DTYPE_PM64` |
| `CrumpBitParameters.c` line 68 | `crump_dly_pan` → `DTYPE_PM64` |
| storage | none (byte values unchanged) |

Defaults stay 64 (`EFFECT_COMMON_DEFAULT_PAN`), which now shows 0.

### 6.2 CrumpBit defaults and `snc` (`CrumpBitParameters.c` lines 58–69)

| Row | Default now | New default |
|---|---|---|
| `crump_mix` (`mix`) | 40 | **0** |
| `crump_feedback` (`fbk`) | 48 | **64** |
| `crump_rate` (`rte`) | 64 | 64 (unchanged) |
| `crump_dly_pan` (`dpn`) | 64 (shows +1) | **63**, shows 0 (F2-Q4 decision) |
| `crump_sync` short name | `syn` | **`snc`** |

Defaults apply when the type is chosen or reset (`effects_recordDefaultsForType()`,
`clear fx`); saved Effects keep their values. The table's block (lines
30–49) gains "Defaults (user F2): mix 0, fbk 64, rte 64, dpn centre". The
`.fx` key `crump_sync` is unchanged (only the 3-letter label changes).
`EFFECTS_BUS_REFERENCE.md` CrumpBit table updated.

---

## 7. F2-F — SHIFT+TRACK on the Effect page shows that voice's `+` mix screen

**Behaviour.** In EFFECTS mode, SHIFT+TRACK *n* switches the display to VOICE
*n*'s mix sub-page (SELECT 8, `MENU_VOICE_MIX_SUBPAGE` = 7), on its Scene
settings screen (`+`: output, FX send, fader, voice Morph), for as long as
TRACK *n* is held. Pots and the encoder edit those cells (with the normal
edit-mask fan-out). On TRACK release the Effect page returns exactly as it
was: sub-page, remembered screen, cursor and edit state. Releasing SHIFT
first does not end the overlay; TRACK does.

Plan:

| File | Change |
|---|---|
| `Core/Menu/menu.c` / `menu.h` | ADD `menu_fxVoiceMixOverlayBegin(uint8_t track)` / `menu_fxVoiceMixOverlayEnd(void)` with block: "Momentary per-voice Scene-settings screen on the Effect page (F2-F). Begin saves the Effect page position (menuIndex, sub-page, screen, edit mode) in a 4-byte overlay record, switches to VOICE *track*'s mix sub-page on its first Scene-setting screen (`menu_voiceInstrumentScreenCount(7)`), and repaints; End restores the saved position, the endless-pot mapping and the Effect LEDs. Track 7 shows slot 6 (track 7 shares slot 6's mix settings). Callers: buttonHandler.c." |
| `Core/Hardware/frontPanel/buttonHandler.c` `handleVoiceButton()` lines 1114–1122 | in `SELECT_MODE_FX` with SHIFT held: call `menu_fxVoiceMixOverlayBegin(voiceNr)`, record the button in a press mask (like `buttonHandler_voiceSceneSeqPressedMask`), return before the type hook and the mute path |
| `buttonHandler.c` `processRelease()` (TRACK release) | when the mask holds the button: clear it, `menu_fxVoiceMixOverlayEnd()`, consume the release |
| `buttonHandler_processEvents()` overflow block | clear the mask and end the overlay |
| `copyClearSession.c` | no change: copy/clear routes TRACK before this handler |

RAM: 4 B (saved Effect position) + 1 B (press mask) — approved (F2-Q5).

> **Decided (F2-Q6, 2026-10-03).** (a) SHIFT+TRACK still changes the active
> track, as today. (b) If the Effect type assigns an action to SHIFT+TRACK,
> that type assignment takes priority and no overlay opens. (c) While SHIFT
> is held the overlay shows the voice Morph view (F2-G); when SHIFT is
> released the Morph view goes away and the overlay stays while TRACK alone
> is held. (d) The Effect page LEDs stay. Implementation:
> `S075_PH6_COPYCLEAR_F2_IMPLEMENTATION.md` Stage I.

---

## 8. F2-G — VOICE mode: hold SHIFT to show and edit Morph endpoints

**Behaviour.** In VOICE mode, holding SHIFT shows the Morph endpoints of the
current VOICE page (as SHIFT does on the Effect page); pots and the encoder
edit the Morph endpoint. Releasing SHIFT returns to the Normal view.
Non-morphable cells show and edit their single value, as in the latched
Morph view today.

Plan:

| File | Change |
|---|---|
| `buttonHandler.c` `processPress()` `case BUT_SHIFT`, `SELECT_MODE_VOICE` branch (currently a no-op, lines ~1384–1395) | `menu_setVoiceModeShowMorph(1u)` |
| `buttonHandler.c` `processRelease()` `case BUT_SHIFT`, `SELECT_MODE_VOICE` branch (line ~1543) | `menu_setVoiceModeShowMorph(buttonHandler_morphVoiceModeActive)` (back to the latch state) |
| `menu.c` `menu_setVoiceModeShowMorph()` | already remaps pots and repaints; block gains "also driven momentarily by SHIFT in VOICE mode (F2-G)" |
| Comments in `case BUT_SHIFT` (VOICE) | replace "Holding SHIFT in VOICE mode no longer enters a temporary STEP overlay … only the physical SHIFT LED changes" with the new rule |

Interactions: SHIFT+TRACK (mute in VOICE), SHIFT+MODE (mode modifiers) and
SHIFT+copy/clear (clear operation) keep working; the page shows Morph values
while SHIFT is held during them. The SHIFT+MODE VOICE latch: F2-Q7.

---

## 9. F2-H — morphable FX send (recommendation)

**Recommendation.** Store a second FX-send endpoint per voice in the Scene
settings and resolve the effective send from the voice's own Morph amount
each audio block, exactly where the effective send is already pulled.

| Part | Proposal |
|---|---|
| Data | `scene_settings_t.fx_send_morph[INSTRUMENT_SLOT_COUNT]` (0..127) beside `fx_send_amount[]` (Normal endpoint). +6 B per Scene, **+96 B SRAM1** for 16 Scenes (approval, F2-Q8). |
| Effective value | `preset_getEffectiveFxSendAmount(scene, slot)` (the function the mixer calls every block): step override first (unchanged), else `lerp(normal, morph, voice_morph_amount[slot] / 255)`; then the LFO overlay as today. Six integer lerps per block, no Morph-worker change. Track 7 uses slot 6. |
| Editing | VOICE mix `+` screen: the FX send cell edits the Normal endpoint normally and the Morph endpoint while SHIFT is held (F2-G) or the Morph latch is on; fan-out through the edit mask like the Normal endpoint (`preset_setVoiceFxSendMorph()` beside `preset_setVoiceFxSendAmount()`). Display shows the endpoint being edited. |
| SceneData | `scene_setVoiceFxSendMorph()` / `scene_getVoiceFxSendMorph()` change-aware setter/getter with its own AutoSave Scene parameter cells (sole writer rule). |
| AutoSave | six new Scene parameter cells in the reserved range (45..50 of the 118-byte Scene parameter area); record layout and format version unchanged; older firmware ignores reserved cells. `AUTOSAVE_SCENE_PARAM_COUNT` 45 → 51. |
| `sceneset.scg` | new key `fx_send_morph` (six values), written after the bus compressor lines. A file without it loads Morph 0 (no backward compatibility, user 2026-10-03). |
| Defaults | fresh / cleared Scene: both endpoints 0. |
| Copy/clear | `copy scene settings` / `copy scene` carry both (through `scene_commitSettings()`); `clear send` sets both endpoints to 0 (F2-Q9). |
| Automation / LFO | step automation `Nfx` stays an absolute override of the effective send while its step plays. FX send has no LFO target of its own (`Nfx` is step-only, `SceneModTargets.c`); an LFO on the voice's Morph amount moves the send through the resolved Morph amount. Morph lane of `Nfx` not needed. |
| Docs | `FILESYSTEM_SPEC.md` (`sceneset.scg` table), `AUTOSAVE.md` (Scene cells 45..50), `BANK_PRESET_ARCHITECTURE.md` (Scene contents, Morph), `STORAGE_SRAM_MANIFEST.md`, `tools/verify_bank_autosave.py` (cells 45..50). |

Alternative (not recommended): a per-voice Morph "send offset" byte reusing
the same storage; same RAM, harder to read and to edit.

---

## 10. Resources

| Item | SRAM1 | Approval |
|---|---:|---|
| F2-F overlay record + TRACK press mask | +5 B | F2-Q5 |
| F2-H `fx_send_morph[6]` × 16 Scenes | +96 B | F2-Q8 |
| F2-A … F2-E, F2-G | 0 | — |

Flash: small (dtype case, overlay, setter/getter, parser key).

---

## 11. Verification (hardware)

| Case | Expected |
|---|---|
| `clear scene` (another Scene), `clear scene settings` | all six voices route St1; compressor off / 0 / 0 / off |
| Copy/clear menus | top-left `Copy` / `Clear`; indicator from column 9 |
| Effect `run`, `scl`, `typ` full views | long name only; `typ` shows `Off`, `StFilter`, `CrumpBit` with nothing else on the row |
| New Effect / `clear fx` | pan shows 0 |
| CrumpBit chosen | mix 0, fbk 64, rte 64, dpn 0; sync label `snc` |
| Effect page, SHIFT+TRACK 3 held | VOICE 3 `+` screen; pots edit output/send/fader/voice Morph; release returns to the same Effect screen and cursor |
| VOICE page, SHIFT held | Morph endpoints shown and edited; release returns to Normal (or the latch state) |
| FX send Morph | set Normal 0, Morph 127 on a voice; sweeping that voice's Morph moves the send; save/load and power cycle keep both endpoints |

---

## 12. Follow-up decisions

- **F2-Q1 — Scope of the St1 default.** Apply St1 to every default path:
  fresh/empty Scenes, `clear scene`, `clear scene settings`, a `sceneset.scg`
  without `audio_out`, and the legacy kitset import fallback (old Kits that
  carried routing)? Recommendation: yes for all except the legacy kitset
  import, which should keep reading the old file's own routes (it already
  does when the old file has them; the fallback only fires when a value is
  missing — then St1).
- **F2-Q2 — Scope of the compressor defaults.** Same question: off/0/0/off
  for every fresh Scene and for a `sceneset.scg` missing `bus_comp_*` keys,
  not only for clears? Recommendation: yes (one default everywhere).
- **F2-Q3 — `typ` browse feedback.** Without the `*` mark, the only sign that
  the shown type is not yet applied is that the sound has not changed until
  click-out. Acceptable, or show the candidate differently (for example the
  name blinking) while it differs from the current type?
- **F2-Q4 — Effect pan fix.** A (display-only `DTYPE_PM64`, recommended) or
  B (default 63)? Also: do the voice pans (`instrument_pan`, `DTYPE_PM63`)
  show the value you expect at their default? If any of them also shows `+1`
  at centre, the same fix applies.
- **F2-Q5 — RAM for F2-F.** +5 B SRAM1 (saved Effect position 4 B, TRACK press
  mask 1 B). Approve?
- **F2-Q6 — SHIFT+TRACK overlay details.**
  (a) Does the overlay leave the active track unchanged (recommended), or
  also select that track as SHIFT+TRACK does today?
  (b) The overlay takes precedence over an Effect type's own TRACK hook for
  SHIFT+TRACK (no current type uses SHIFT+TRACK). OK?
  (c) While the overlay is up and SHIFT is still held, F2-G applies: the FX
  send cell shows/edits its Morph endpoint (F2-H). OK?
  (d) LEDs during the overlay: keep the Effect page LEDs (recommended), or show
  the VOICE LEDs of that track?
- **F2-Q7 — Morph latch.** Keep SHIFT+MODE VOICE as a latched Morph view in
  addition to the new momentary SHIFT view (recommended; releasing SHIFT
  returns to the latch state), or retire the latch?
- **F2-Q8 — RAM for the FX send Morph endpoint.** +96 B SRAM1. Approve?
- **F2-Q9 — `clear send`.** Set both FX send endpoints (Normal and Morph) to
  0 (recommended)?

### 12.1 Decisions (user, 2026-10-03)

| # | Decision |
|---|---|
| F2-Q1 | Yes: St1 on every default path, including the legacy kitset import fallback (which only fires when the old file's route is missing or invalid). |
| F2-Q2 | Yes: compressor off / 0 / 0 / off on every default path. |
| F2-Q3 | No `*` mark; the encoder click is enough of a gate. |
| F2-Q4 | One pan UX: stored 0 = fully left = `-63`; 127 = fully right = `64`; default = absolute centre, stored 63 = `0`. Effect pan rows default 63 and the Effect stereo balance centre moves to 63; mono maths unchanged; instrument defaults untouched (§6.1). |
| F2-Q5 | +5 B approved. |
| F2-Q6 | Active track changes; a type's own SHIFT+TRACK action wins; SHIFT held = Morph view, released = Normal view while TRACK is held; Effect LEDs kept (§7). |
| F2-Q7 | Keep the SHIFT+MODE VOICE latch; SHIFT release returns to the latch state. |
| F2-Q8 | +96 B approved. |
| F2-Q9 | Yes: `clear send` sets both FX-send endpoints to 0. |

Follow-up decisions on the schedule (user, 2026-10-03):

| # | Decision |
|---|---|
| R2-1 | SHIFT+TRACK is the entry; holding TRACK keeps the Scene-settings screen. SELECT functions assigned by the Effect type keep working while TRACK is held; any menu screen change is suppressed until TRACK is released. |
| R2-2 | No backward compatibility: AutoSave records and other temporary records are deleted after the update, so no migration code (no AutoSave marker, no missing-key copy rule). |
| R2-3 | `tools/verify_bank_autosave.py` is fixed so every check matches the firmware after the implementation. |

The implementation schedule is `S075_PH6_COPYCLEAR_F2_IMPLEMENTATION.md`.
