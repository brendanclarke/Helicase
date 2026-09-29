# S074 Session Startup: Effect-page underline bug and the first buffer-using Effect

Read this before starting Session 074. It collects what the two goals need
from the code and from earlier sessions. Nothing here is implemented.

**Session goals (user, 2026-09-29), in order:**

1. **Bug:** on the Effect page, parameter names are not underlined when the
   parameter is automated in the current Pattern. They should be underlined
   for Pattern automation **and** for FX-sequence locks ("it should be
   both").
2. **Feature:** the first Effect type that uses the shared DTCM buffer
   (Phase 5 item A8, "buffer-using template type").

**Read first:** `MEMORY.md`; `knowledge_files/log_archive/073_SESSION_HANDOFF_LOG.md`
(End of session block and §9); `EFFECTS_BUS_REFERENCE.md` §4, §7, §8, §13,
§14; `EFFECTS_MIXER_DSP_REFERENCE.md` §3.3, §4, §5, §7.1.

---

## 1. State at the start of Session 074

| Item | Value |
|---|---|
| Branch | `dev-ph5-effects` |
| HEAD | `692abf8` ("dsp refactor"). The S073 closeout edits are uncommitted (post-review corrections, drill-knob removal, the `tools/dsp_golden` → `tools/dsp_test` rename staged by `git mv`, all documentation). The user manages commits. |
| Link | `text=486,688`, `data=416`, `bss=426,336` |
| Flash | 487,104 / 753,664 B; **266,560 B free** |
| ITCM / DTCM statics / FX arena | 4,168 / 16,384 B; 4,448 B; 126,624 B at `0x20001160` (3,744 B above the 120 KiB minimum) |
| `lxr02.bin` SHA-256 | `1bd8be5201eecf0222d3cdc6c36bf272ce38a33aee535ca5fcfd0a47c155fc82` |
| Config | `DEV_MODE_DIAGNOSTIC 0`, `DEV_MODE_LOGGING 1`, `MEMTEST_ENABLED 1`, `DEV_FXBUF_FORCE_VOICE_UNITS 0u`, `DEV_EFFECT_FORCE_TYPE 0u` |

**Build:** `make all && make img` (bare `make` can stop at `build/main.o`).
Measure with `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.
Never run two builds at once (an `lto1` bus error can leave an empty ELF).

---

## 2. Goal 1 — Effect-page automation underlines

### 2.1 How the VOICE pages do it today (`Core/Menu/menu.c`)

| Piece | Where (S073 line numbers) | What it does |
|---|---|---|
| Search state | around line 1262 | `va_searchTargetMask[8]` (one bit per descriptor local 0..63), `va_searchSceneMask` (voice Morph, audio out, FX send bits), `va_searchTrack`, `va_searchPattern`, `va_searchCursor`, `va_searchComplete` |
| `va_searchRestart()` | 1843 | Records the viewed Pattern (`menu_shownPattern`) and track (`menu_activeVoice`), clears the masks, restarts at step 0 |
| `va_scanService()` | 1905 | Reads `VOICE_AUTOMATION_SCAN_STEPS_PER_PASS` (4, `config.h:420`) step automation lists per foreground pass with `pat_readStepAutomations()`; sets a bit for each entry whose target is the viewed slot's descriptor, or a Scene target owned by that slot; after step 127 marks the search complete and repaints |
| `va_applyVoiceMarkers()` | 2381 | After the frame is formatted: in the compact view, underlines the first non-space character of each automated cell's 3-character name (row 0); in the full view, the first character of the long name (row 0, columns 8..15). A held-step value marker (row 1) takes precedence for its cell. |
| `va_queueMarkerTransaction()` | 2199 | The shared CGRAM transaction: four marker slots (CGRAM 2..5, one per visible cell), diffed against the LCD, with a retry bit (`VA_MARKER_RETRY_BIT`) |
| `menu_serviceRuntimeWidgets()` | around 11183 | Runs `va_updateHeldState()`, `va_scanService()` and `va_underlineService()` **only when `menu_isVoicePage()`** |
| Restart points | 2141 (Pattern clear), 9287/9292, 12482, 13121, 13237 | Page, track and Pattern changes; destructive Pattern edits |

### 2.2 What the Effect page does today

- `menu_applyEffectMarkers()` (`menu.c:2558`), called after the Effect frame
  is formed (compact view at about line 9833; full view at about line 9611),
  handles only **held FX-lane values**: while SEQ buttons are held it shows
  the first held step's value and, if that lane is locked, underlines the
  value's last character (row 1). It uses the same CGRAM transaction.
- There is **no presence scan**: nothing looks for Pattern automation of
  Effect targets or for locks elsewhere in the FX sequence, so parameter
  names are never underlined.
- Effect cells (`menuEffects.h`): `menuEffects_cell_t {kind, index,
  descriptor}`, with kinds `TYPE`, `PARAM` (descriptor index 0..63), `RUN`,
  `LENGTH`, `SCALE`, `MORPH_AMOUNT` (`mrp`). `menuEffects_cellLane()` maps a
  cell to its FX-sequence lane.

### 2.3 The data to look for

- **Pattern automation:** step entries (`pat_readStepAutomations()`, 7 tracks
  × 128 steps per Pattern, `PatternData.h`) whose 9-bit target is:
  - an Effect parameter: IDs 448..510 = Effect local 0..62
    (`effectTarget_isEffectId()`, `effectTarget_local()`); local 63 is never
    automatable;
  - Effect Morph `fxm`: Scene target 404 (`SCENE_MOD_TARGET_KIND_EFFECT_MORPH`),
    shown on the `mrp` cell.
  Effect targets can be written on **any** of the 7 tracks (the `fx`
  automation category), unlike voice targets, which belong to one track.
- **FX-sequence locks:** the active Scene's `effect_record_t.steps[s].lock_mask`
  bit *L* means lane *L* is locked on step *s*. Lane 0 is Effect Morph (`mrp`);
  lanes 1..15 map to descriptor indices through the registry row's `lanes[]`
  (for `flt`: freq, reso, drive, type, vol, pan). The SEQ LEDs light a step
  when it has any lock and lies within the sequence length
  (`menuEffects_renderSeqLeds()`).

### 2.4 Decisions for the user

| # | Question | Recommendation |
|---|---|---|
| U1 | Which Pattern tracks to scan for Effect automation: all 7 tracks of the shown Pattern, or only the active track as on the VOICE pages? | All 7: Effect targets are Scene-wide and can be on any track. The scan stays bounded per pass (7 × 128 = 896 step reads in slices). |
| U2 | Which Pattern: `menu_shownPattern` (as on the VOICE pages)? | Yes. |
| U3 | FX-sequence locks: count all 16 steps, or only steps within `len`? | Within `len`, to match the SEQ LEDs. |
| U4 | Should `mrp` be underlined for `fxm` automation and Morph-lane locks? | Yes, both. |
| U5 | Underline in the full (edit) view too, as the VOICE pages do? | Yes. |
| U6 | When a SEQ hold shows a locked value (row 1), keep the value marker's precedence over the name marker for that cell? | Yes, as on the VOICE pages. |
| U7 | RAM: reuse the VOICE search state (the pages are exclusive and the state resets at page changes: 0 B), or add Effect-specific state (about 12 B SRAM1: a 64-bit mask, an `fxm` flag, cursor/track/Pattern/complete bytes)? | Reuse if the reset points cover every Effect-page entry and exit; otherwise add and get approval (byte count, region, lifetime, owner). |
| U8 | When to rescan: Pattern/Scene/type change, Pattern edits and record, FX lock edits, page entry? | All of these; FX locks can be re-read directly from the record each repaint (16 × 2 bytes) with no scan. |

### 2.5 Constraints

- **Foreground only**; the scan is UI work, not DSP. Keep it bounded per
  pass like `va_scanService()`.
- **The LCD queue budget:** marker changes go through
  `va_queueMarkerTransaction()`; do not bypass it.
- **Scope:** Menu only (`menu.c`, `menuEffects.c/.h`). No change to
  EffectsManager, PatternData or the sequencer should be needed.

### 2.6 Hardware checks for the fix

- Pattern automation of `frq` on one track: the `frq` name is underlined on
  the Effect page; removing it clears the underline after the scan.
- Automation of `vol`/`pan`/`out` (common rows) and of `fxm` (`mrp`).
- Automation on a track other than the active one (U1).
- An FX-sequence lock on `res`: underline; a lock beyond `len` (U3).
- Both sources on the same parameter.
- Held SEQ steps still show locked values with the value underline.
- Scene switch, Pattern change, `typ` change: no stale underlines.
- VOICE-page underlines unchanged (regression).

---

## 3. Goal 2 — the first buffer-using Effect type

### 3.1 What exists

- **Arena** (`Core/DSP/Effects/FxBuffer.c/.h`): 126,624 B of DTCM, 32-byte
  aligned, never cleared by the system. With no buffer-using instruments the
  Effect share is the whole arena; with all twelve voice units claimed it is
  73,632 B. `fxbuf_effectShare()` gives `{base, offset, bytes}`; `io->share`
  carries it into `process()`.
- **Contract** (`EFFECTS_MIXER_DSP_REFERENCE.md` §5.2): `buffer_min_bytes`,
  `buffer_pref_bytes`, `init(rt, handoff)` (clear unless you adopt),
  `export_handoff`, `buffer_changed`, `effective_max` for
  `EFFECT_PARAM_FLAG_BUFFER_DEPENDENT` rows.
- **Capacity** at 44,108 Hz: whole arena 1.44 s of 16-bit mono (0.72 s
  stereo; 2.87 s / 1.44 s at 8 bit); minimum share 0.83 s of 16-bit mono
  (0.42 s stereo; 1.67 s / 0.83 s at 8 bit).
- **Testing the minimum share:** `DEV_MODE_DIAGNOSTIC 1` with
  `DEV_FXBUF_FORCE_VOICE_UNITS 12` claims all units at boot (fix debt item 3
  in §3.3 first).
- **Registry and ops tutorial:** `EFFECTS_BUS_REFERENCE.md` §14. Storage,
  AutoSave, Menu, automation and LFO are registry-driven.

### 3.2 Decisions for the user

| # | Question | Notes |
|---|---|---|
| B1 | Which Effect? | The template's job (SCOPING §5.4/§5.6) is to prove routing, clipping, Scene switching, and both minimum and maximum shares. Candidates: a stereo or ping-pong delay (time, feedback, tone); a chorus/flanger (short modulated delay); an 8-bit BBD-style delay (a preview of the Phase 7.5 stack). A phaser uses no buffer, so it would not test the arena. |
| B2 | Name tokens | A unique 3-character file token, a 5-character abbreviation and an 8-character full name. Tokens are permanent once saved. |
| B3 | I/O shape | Stereo in/stereo out, or mono in/stereo out (the mixer zeroes the right channel for the Effect to fill). |
| B4 | Sample format in the arena | 16-bit (clean) or 8-bit (twice the time, gritty; needs deliberate quantisation). |
| B5 | Parameters, ranges, defaults, lanes | Which rows are Morphable, modulatable and automatable; up to 15 sequencer lanes; `WIDE8` rows need `expand7`; local 63 never automatable. |
| B6 | Delay time units and the buffer limit | Milliseconds, samples or tempo divisions; how the maximum follows the share (`effective_max`). |
| B7 | Tails across Scene switches | The design (F6) lets buffers persist: a same-type Scene switch keeps ringing. Adopt valid content on re-entry, or always clear? |
| B8 | Time changes | Crossfade two taps, or tape-style (pitch glide); both must cost the same every block. |
| B9 | RAM | The runtime struct lives in the DTCM union (76 B today): anything larger shrinks the arena by the difference and needs approval. Audio goes in the share. |

### 3.3 Gaps the first buffer type must close

1. **Same-type Scene switch handoff refresh.** `effects_activateScene()`
   (`EffectsManager.c` about line 1202) switches the runtime only when the
   type changes (`effects_switchRuntime()`, about line 1139). With a buffer
   type a same-type switch must refresh the handoff without `init`
   (`EFFECTS_BUS_REFERENCE.md` §13 item 1).
2. **FX return ramp while `off`** (S072 debt 8): `mixer_fx_return_last_gain[]`
   is not reset while the Effect is `off`, so a type that outputs on its first
   block starts its return ramp from stale gains.
3. **`fxbuf_init()` order** (S072 debt 1, diagnostic only):
   `fxbuf_handoffResetAll()` runs after the forced-unit loop, so forced units
   carry handoff rate 0. Move the reset directly after
   `fxbuf_clearOwners()`.

### 3.4 Files a new type touches

- New `Core/DSP/Effects/<Type>/<Type>Parameters.c/.h` and
  `<Type>Effect.c/.h`.
- `EffectsManager.c`: the runtime union member and `_Static_assert`, the
  registry row. `EffectsManager.h`: `EFFECT_TYPE_<NAME>`, `EFFECT_TYPE_COUNT`.
- `Makefile`: `<Type>Effect.c` in `DSP_SRCS` **and** an explicit `-Ofast`
  rule (the pattern rule covers only `Core/DSPAudio/`); `<Type>Parameters.c`
  in the normal sources; `-ICore/DSP/Effects/<Type>`.
- Docs: `EFFECTS_BUS_REFERENCE.md` (registry), `EFFECTS_MIXER_DSP_REFERENCE.md`
  (the type's DSP and cost), `STORAGE_SRAM_MANIFEST.md` (if the union grows).

### 3.5 DSP rules for the type

- **Constant CPU:** the per-sample work must not depend on parameter values
  (delay time moves the read position; feedback, filtering and interpolation
  always run). Budget it with every send open and every modulated parameter
  moving.
- Float in place, `io->frames` = 32, respect `io->channels` and a NULL
  `io->r`; never truncate to int16; keep feedback bounded.
- Wrap buffer indices with compare-and-subtract (the share is not a power of
  two).
- Parameter changes arrive once per block (and every block under an LFO):
  ramp or crossfade.
- Foreground only; no allocation; nothing from an ISR.
- Host-test before flashing (`tools/dsp_test/DSP_TEST.md` §6.3). An Effect
  runner or WAV writer in `tools/dsp_test/` would be a new file: ask first.

### 3.6 Hardware checks for the type

- Select it with `typ`; the page layout; `.fx` save and reload; AutoSave
  restore after a reboot.
- Sends in all three fader modes; stereo panning of sends; the return's
  level, pan and routing (jack fallback); six full sends without crackle.
- Same-type Scene switch (tails, B7); switch to another type and to `off`
  (clean, no stale ramp).
- Minimum and maximum shares (`DEV_FXBUF_FORCE_VOICE_UNITS`).
- Automation, FX-sequence locks and LFO on each flagged row, including the
  buffer-dependent row at the limit.
- The worst-case Scene with the type active: underrun count and the `cpu`
  widget.

---

## 4. Working rules (from `MEMORY.md` and the user)

- **Constant CPU:** never save CPU by skipping work because something is
  inactive, silent or at zero.
- **RAM approval** for any new or larger allocation, including ITCM code and
  the Effect runtime union.
- **No new utilities**, no profiler, no extra CPU widgets.
- **Create only the files the user names**; ask before adding others.
- **No unrequested features or knobs.** Stay inside the request; log
  unrelated findings in `SCOPING_TARGETS.md` instead of fixing them.
- **Hardware checks are the user's.**
- **Commits are the user's.** Do not suggest when to commit.
