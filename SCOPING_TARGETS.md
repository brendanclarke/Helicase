# LXR-02 Open Helicase — Feature Scoping & Implementation Roadmap

*Compiled from `putting it together`, the follow-up architecture discussion, `MEMORY.md`, `README.md`, `BURST_REDUCTION.md`, and a direct read of the current `Core/` source tree. Where a number or struct layout is stated as fact below, it was checked against the actual code (file/line references included). Where something is a genuine open question — usually "how much headroom is actually free in DTCM/ITCM" — it's flagged as such instead of guessed at, because the previous pass through this document invented specific numbers in places the code doesn't support.*

## How this document is organized

The work is grouped into six phases, ordered so that each phase either reduces risk for the next one or builds on ground the previous phase just cleared. Phase 1 is a pure refactor — no user-visible behavior changes, just moving code and freeing up headroom so the audio thread has room to breathe before anything new gets bolted on. Phase 2 rebuilds the SD/scene storage model that everything downstream (patterns, kits, FX) will be saved into. Phase 3 rebuilds the sequencer's data model on top of that storage model. Phases 4–6 are additive features that assume the new foundation is in place.

Within each phase, features are grouped by **where they live in the codebase**, per your original request, so a given implementation pass touches a small, coherent set of files.

1. **Phase 1 — Foundation Refactors** (no new features; your four listed refactor/reorg tasks, including burst reduction)
2. **Phase 2 — SD Card & Scene Architecture** (`Core/Scene/`, new file hierarchy, debounced autosave)
3. **Phase 3 — Sequencer Paradigm Shift** (`Core/Sequencer/Pattern/`, dynamic event pool)
4. **Phase 4 — Voice, MIDI & Morph Control** (`Core/MIDI/`, `Core/Scene/` morph engine, `Core/DSPAudio/lfo.c`)
5. **Phase 5 — UI & Performance Workflow** (`Core/Menu/`, `Core/Hardware/frontPanel/`)
6. **Phase 6 — DSP Expansion** (`Core/DSPAudio/` — new voices, oscillators, FX bus)

Every phase ends with **Open Engineering Questions** (things that need a decision or a measurement before/during implementation) and **Suggested Complementary Features** (ideas adjacent to what you asked for, flagged clearly as suggestions, not commitments).

---

## Phase 1 — Foundation Refactors

**Location:** `Core/MIDI/frontPanelParser.c`, `Core/Sequencer/sequencer.c` → `Core/Sequencer/Pattern/`, `Core/Preset/` → `Core/Scene/`

These are the four refactor/reorg tasks you already flagged before any of the new feature work. None of them change behavior; they change where code lives and how it's called, so that every phase after this one is being built on the layout you actually want rather than being built once and then dragged through a rename later. Do these first, in this order, each as its own compileable/testable commit.

### 1.1 Burst reduction

`BURST_REDUCTION.md` already has the full plan for this — the chunked kit-load apply burst in `menu_pollPresetStatus()`, the parameter-application state machine, and the per-tick budget. Nothing in this document changes that plan; it's called out here only so it stays first in the sequence, because every later phase (bigger scenes, bigger patterns, more parameters) makes an unbounded foreground burst worse, not better. Implement it as written there before touching anything else.

One correction to flag: the intermediate draft of this document (and the "RED TEAM" draft before it) both suggested shrinking `AUDIO_DMA_FRAMES` from 96 to 64 "to buy headroom." That's already been tried — the comment directly above the `#define` in `config.h` says 64 was tested and "reduced slack enough to freeze/glitch during pattern load testing," and 96 is documented as "the measured stable session-023 compromise." Don't re-litigate that without new data; if headroom is still tight after burst reduction lands, the documented path to more slack is enlarging the 4KB `.dma_nocache` MPU window to allow 128, not shrinking to 64.

### 1.2 Remove `frontPanelParser.c`, wire functions directly

This is a bigger job than the earlier draft made it look. The real numbers:

- `frontPanelParser.c` is 816 lines with **70 `case` labels** across three internal `switch(command)` blocks (opcode dispatch for standard front-panel messages, an LFO-target sub-switch, and a sysex path) plus the `frontPanel_sendData()` encoder itself.
- `frontPanel_sendData()` is called **105 times** across the codebase: 55 in `Core/Menu/menu.c`, 35 in `Core/Hardware/frontPanel/buttonHandler.c`, 6 in `Core/Preset/presetManager.c` (soon `Core/Scene/`), plus a handful internal to the parser file itself.

So this isn't "remove a file and add a few direct calls" — it's 105 call sites, each of which currently packs its payload into `(status, data1, data2)` MIDI-CC-shaped bytes, sends it through `frontPanel_sendData()`, and relies on the parser's `switch` to unpack and route it to the real target function (sequencer, DSP voice, LED handler, modulation node, etc.). The real target functions already exist and are already correct — the opcode encode/decode round-trip is the only thing being removed. Practically, this means:

1. For each of the 70 opcodes, find the `case` body in `frontPanelParser.c` and the function(s) it ultimately calls.
2. At each of the 105 call sites, replace the `frontPanel_sendData(OPCODE, data1, data2)` call with a direct call to that same target function, passing the already-known values instead of re-encoding them into a byte pair and decoding them back out.
3. Delete the opcode's `case` once no call site still routes through it.

This is mechanical but not risk-free — some opcodes fan out to more than one target function, and a few (the sysex path, `LED_QUERY_SEQ_TRACK`, the pattern/euklid "request params" opcodes) exist purely to trigger a *response* back through the same protocol, which won't make sense once the protocol is gone; those need to become direct reads of the relevant state rather than a send/receive round trip. Budget this as file-by-file (start with `presetManager.c`'s 6 call sites as a small dry run, then `sequencer.c`, then `buttonHandler.c`, then `menu.c` last since it's the largest).

### 1.3 Move pattern storage out of `sequencer.c` into `/Pattern/`, `pat_*` prefix

Current real layout, for reference: `sequencer.h` defines `NUM_TRACKS 7`, `NUM_PATTERN 8`, `NUM_STEPS 128`, a 7-byte `Step` struct (`volume`, `prob`, `note`, `param1Nr`, `param1Val`, `param2Nr`, `param2Val`), and the `PatternSet` struct that holds `seq_subStepPattern[NUM_PATTERN][NUM_TRACKS][NUM_STEPS]`, `seq_mainSteps[NUM_PATTERN][NUM_TRACKS]`, `seq_patternSettings[NUM_PATTERN]`, and `seq_patternLengthRotate[NUM_PATTERN][NUM_TRACKS]`. `seq_tick()` (the per-4kHz-tick step advance, `sequencer.c:775`) is called from `TIM3_IRQHandler()` in `sequencerTimer.c:91-102`.

Since Phase 4 is about to replace this entire pattern data model (8 fixed patterns of up to 128 sub-steps → up to 128 real steps per track with no sub-steps, addressed through the dynamic event pool), there's a sequencing question worth deciding explicitly: **do the mechanical move and the data-model rewrite in the same pass, or move first and rewrite in place second?**

Recommendation: move first, rewrite second, as two separate commits. Move everything pattern-storage-and-servicing-related (the structs above, the `Step`/`PatternSet` types, load/save helpers, step-advance logic, euklid/patgen generators) into `Core/Sequencer/Pattern/`, rename the moved functions with the `pat_*` prefix, and get it compiling and behaving identically to before. Then do the Phase 4 rewrite inside that new location. Two reasons: first, a pure rename-and-move is easy to verify byte-for-byte (same behavior, different file/name), so if something breaks you know it's the move, not new logic; second, `seq_tick()` and the timer wiring stay in `sequencer.c` (they're timing/scheduling, not pattern storage), so the move needs a clean line between "what moves to `Pattern/`" and "what stays in `sequencer.c` as the scheduler that calls into `Pattern/`" — deciding that boundary is easier without simultaneously redesigning the data the boundary is passing around.

### 1.4 Move `Core/Preset/` into `Core/Scene/`

`Core/Preset/` is currently `presetManager.c/.h` (562/108 lines) and `ParameterArray.c/.h` (681/448 lines) — 1,799 lines total, ~90 functions between them (kit load/save, morph, globals, drumset send, everything currently gated through `frontPanel_sendData` per 1.2 above). Same logic as 1.3: move and rename first (`Core/Scene/`, functions can stay `preset_*` for now or move to `scene_*` — your call, but pick one and do it in this pass rather than mixing prefixes), verify unchanged behavior, then build the Phase 3 BANK/SCENE/KIT hierarchy inside the new location rather than fighting the move and the redesign at the same time.

Note the include-path knock-on: `presetManager.c`/`ParameterArray.c` are referenced by `-ICore/Preset` in the `Makefile`, and by `#include "presetManager.h"` / `#include "ParameterArray.h"` from `frontPanelParser.c`, `menu.c`, `lfo.c`, and others — all of those includes and the Makefile's `-I` flag need updating in the same commit as the move, or the build breaks immediately (which is a fine sanity check that the move was mechanical and complete).

**Suggested complementary step:** since 1.2, 1.3, and 1.4 all touch `frontPanelParser.c`'s call sites in overlapping files (`presetManager.c` and `sequencer.c` both `#include "frontPanelParser.h"`), doing 1.2 *before* 1.3/1.4 means the direct-wiring pass only has to happen once, against the pre-move file layout, rather than being redone against new paths. The order above (1.1 → 1.2 → 1.3 → 1.4) reflects that.

### Open Engineering Questions

- **Opcode fan-out audit:** before starting 1.2, worth a quick pass building a table of all 70 opcodes → which function(s) each currently calls → which of the 105 call sites use it, so the "delete once unused" step in 1.2 has a checklist instead of relying on the compiler catching orphaned cases.
- **`pat_*` vs `scene_*` naming collision:** `PatternSetting` (the struct) and pattern-settings-adjacent names in `sequencer.h` may collide semantically with "pattern" as used in the *old* 8-pattern-slot sense once Phase 4 removes that concept. Worth deciding up front whether `pat_*` refers to "the per-track step data" (the new sense) or keeps meaning "one of the old 8 slots" during the transition, so Phase 4 doesn't have to rename things a second time.


## Phase 2 — SD Card & Scene Architecture

**Location:** `Core/Scene/` (post-move), `Core/Hardware/SD/filesystem.c`, `Core/Hardware/SD/asyncfatfs/`

This phase builds the BANK/SCENE/KIT/PAT/FX library structure from `putting it together` inside the `Scene/` directory Phase 1 just created, and lands the debounced autosave system. It comes before the sequencer rewrite (Phase 3) because the sequencer rewrite's whole memory design (dynamic pool split 16-ways-plus-one across scenes) only makes sense once "scene" is a real, addressable unit that the file layer understands — right now the SD layer has no concept of a scene at all.

### 2.1 Current SD layer, for grounding

`filesystem.c` is entirely flat: `filesystem_makeFilename()` builds an 8.3 name like `p000.snd` (`p` + 3-digit slot number + extension) directly into a buffer — there is no directory component anywhere in the current path-building code, and file *type* is an enum (`fs_file_type_t`: `FS_FILE_KIT`, `FS_FILE_PATTERN`, `FS_FILE_MORPH`, `FS_FILE_PERFORMANCE`, `FS_FILE_ALL`, `FS_FILE_GLOBALS`, `FS_FILE_SAMPLES`) rather than a path. So the `BANK/001 <name>/SCENE 01 <name>/KIT/drum1.drm` hierarchy from `putting it together` is a genuinely new capability, not an extension of something that half-exists.

The good news: the underlying `asyncfatfs` library (the Betaflight-derived async FAT driver already in `Core/Hardware/SD/asyncfatfs/`) already exports `afatfs_mkdir()` and `afatfs_chdir()` — they're just never called from `filesystem.c` today. So this is "teach the existing facade to walk directories," not "write a new filesystem layer."

### 2.2 BANK / SCENE / KIT / PAT / FX hierarchy

Implement the directory structure and file types exactly as specced in `putting it together`:

```
SD:/
├── BANK/001 <name>/settings.cfg, SCENE 01 <name>/{KIT/*.drm|.snr|.cym|.hat, pattern.pat, effects.fx}, … SCENE 16 <name>/
├── KIT/001 <name>/{drum1.drm, drum2.drm, drum3.drm, snare.snr, cymbal.cym, hi-hat.hat}, …
├── PAT/<name>.pat, …
├── FX/<name>.fx, …
├── SCENE/<name>.scn, …
├── SAMPLES/<name>.wav, …
└── WAVETABLES/001 <name>/<name>.wav, …
```

`fs_file_type_t` needs new members for `.snr`/`.cym`/`.hat`/`.drm` (currently there's no per-instrument-part file type — everything voice-related goes through the monolithic kit), `.fx`, `.scn`, and the directory-scoped variants of `.pat`/`.kit` (root `PAT/`/`KIT/` vs. inside a `BANK/xxx/SCENE xx/`). `filesystem_makeFilename()` becomes path-building rather than name-building, and every call site that currently only handles a flat slot number needs a bank/scene context to resolve into a path.

Root-level `KIT/`, `PAT/`, `FX/`, and `SCENE/` act as shared libraries per the spec — any file there can load into any scene slot in any bank, and any scene's parts can be exported back out to them. This is a straightforward extension of the existing "load this slot" flow once paths exist, just with more possible source/destination roots.

### 2.3 The 17th scene (background bank loading)

`putting it together` calls for one extra SRAM-resident scene slot beyond the 16 that make up a bank, used as a landing zone: when a new bank starts loading, the currently-playing scene keeps playing from its existing slot while the new bank's scenes stream into slots that aren't the active one; the 17th slot exists so there's always a "safe" landing spot even if the active slot index in the new bank happens to collide with the one currently playing. Concretely, this is 17 identical Scene-in-SRAM structures rather than 16, plus a load-target selection rule ("load into the slot that isn't currently feeding the audio thread"), plus the lockout you already specified: menu load/save/reload is barred until the background load finishes, and any edits made to the cached-but-not-yet-committed scene during the load are discarded on the next scene change (though they can be saved manually once the lock lifts, per the spec).

This also directly answers what "load a kit by MIDI bank change" becomes in the new model, per your note in `putting it together` — a Bank MSB (CC0) message triggers exactly this background-bank-load path rather than a synchronous `.kit`/`.prf` load.

### 2.4 Debounced auto-save to dot-files

Per your answer: this applies specifically to the files that live inside a loaded bank — the per-instrument part files (`.snr`, `.cym`, `.drm`, etc.), the `.fx` file, the 16 `.pat` files, and the `.cfg` file. Anything else (root-library `KIT/`/`PAT/`/`FX/`/`SCENE/` files, `.wav` samples/wavetables) is explicit load/save only, not auto-saved.

Mechanism, exactly as you specified: a parameter edit marks its owning file as stale and (re)starts a 5-second idle timer. Any further edit to a parameter in that same file resets the 5-second timer. If edits keep coming in continuously past 30 seconds without a 5-second gap, force a write anyway rather than letting the timer be reset indefinitely. Each of these files is backed by a `.<name>.<ext>` shadow copy holding the last state committed by an explicit menu **SAVE** — the debounced writes go to the live `.<name>.<ext>` working file, not the dot-shadow, and a new **RELOAD** menu page (alongside LOAD/SAVE) restores the working file from the dot-shadow.

Power-loss safety: write the new content to a `.tmp` file first, then rename it over the live file, so a mid-write power loss leaves the old file intact rather than a half-written one. This needs `afatfs`-level rename support (confirm it's exposed — the header only shows `mkdir`/`chdir` in the directory-ops group; verify a rename/replace primitive exists or needs adding as part of this work).

### 2.5 Reload from snapshot (SHIFT+PLAY)

Per your `putting it together` note and follow-up answer: this reverts the *currently playing scene* to its last-saved (dot-shadow) state, discarding live edits — and because of 2.4's architecture, this needs no separate cache or snapshot mechanism of its own. It's just "reload the working file from the dot-shadow," the same primitive the new RELOAD menu page uses, bound to a shortcut.

### Open Engineering Questions

- **SRAM budget for 17 scenes:** each scene now holds a full kit (6 instrument parts' worth of parameters), a pattern (up to 16,382 bytes — Phase 3's finalized dynamic event pool ceiling), and FX settings. The total SRAM cost of "17 scenes resident simultaneously" needs to be tallied once kit and FX sizes are also known — unlike the pattern pool's earlier draft, this ceiling is now fixed by the event pool's 14-bit address width, not just an SRAM-availability guess, so there's no "shrink it if it doesn't fit" lever on the pattern side anymore; any shortfall has to come out of the kit or FX budget instead, or out of dropping below 17 fully-resident scenes.
- **Rename/replace primitive in `asyncfatfs`:** needs confirming before 2.4's `.tmp`-then-rename safety mechanism can be built as specified.
- **Directory-walk cost on `afatfs_chdir`:** the async FAT driver services filesystem ops incrementally across ticks already (per the "flash is fast enough, it already works for samples" answer), but nested directories (`BANK/001/SCENE 01/KIT/`) mean more directory-entry lookups per file open than the current flat scheme. Probably fine given samples already stream from flash successfully, but worth a quick timing check once real directory traversal is in.

### Suggested Complementary Features

- **Scene "undo" sandbox** (from the earlier draft, still a good idea): a `SHIFT+RELOAD`-style shortcut that reverts the *active* scene to its state at the moment it was first selected this session, distinct from the dot-shadow reload — useful for "I've been messing with this scene live for the last ten minutes and want back to where I started this set," which the dot-shadow (last explicit SAVE) doesn't cover if the last save was from a previous session.
- **Bank-level "safe mode" indicator:** since background bank loads lock menu load/save/reload, a small persistent LED/LCD indicator that a background load is in progress would avoid a "why won't LOAD open" moment during a set.

## Phase 3 — Sequencer Paradigm Shift

**Location:** `Core/Sequencer/Pattern/` (post-1.3 move)

This is the biggest single architectural change in the whole roadmap: replacing the current `seq_subStepPattern[NUM_PATTERN][NUM_TRACKS][NUM_STEPS]` static array (8 fixed pattern slots × 7 tracks × 128 sub-steps × 7-byte `Step` struct) with a per-scene, per-track pointer array into a dynamic, bit-packed event pool. "Patterns" as a selectable, separate concept go away — a scene now just *has* a pattern (one per track), and scene-switching is what pattern-switching used to be.

### 3.1 Step/bar model

8 bars, 16 steps per bar, 128 steps max per track, no sub-steps. What used to be sub-step resolution is now handled by **microtiming offset** (a per-step fine-timing value) and **roll modes** at the step level, rather than by subdividing the step grid itself. In step mode, the transport through bars is serviced by the `BAR <>` control and the `SELECT` buttons — `SELECT` no longer addresses sub-steps (there aren't any), it addresses which bar of the 8 is currently shown/edited, matching how you described it.

### 3.2 The dynamic event pool

**Address array.** Every scene has 7 tracks × 128 steps = 896 possible steps, each represented by a fixed `896 × 2 bytes = 1,792 bytes` per-scene array — same total size as the first pass at this design, but now split as **2 flag bits + a 14-bit address** rather than a plain 16-bit pointer. The two flags — "has automation" and "has special (non-default) step data" — live here, in the static array, not inside the block itself. That's the key move: it's what makes room, inside the block header, for a step-ID (needed for O(1) defragmentation, below) without growing the block.

**Two reserved address values need zero pool storage**, and only ever occur when both flags are clear (a real block is never allocated for a step that's actually blank or default):
- `0x0000` — **blank.** Step is off. No trigger, no automation, no lookup.
- `0x3FFF` — **default.** Step is on, plays at the track's default note, 100 velocity, 100% probability. No lookup, no dynamic allocation.

The allocator simply never hands either offset out to a real block — a fixed exclusion rule, not an ongoing risk.

**Any other address is a real 14-bit byte offset into the pool**, addressing up to `2¹⁴ − 2 = 16,382` usable bytes. Since the flags live in the static array, nothing in the block itself needs to restate them:

- **Block header (always present, 2 bytes):** 10-bit step-ID — a back-reference to which of the 896 steps owns this block, needing 10 bits since `2⁹ = 512 < 896 ≤ 1,024 = 2¹⁰` — plus 6 bits whose meaning depends on the static array's flags for this step:
  - **Automation flag set, special flag clear:** the 6 bits are an automation count, encoded as `count − 1` so all 64 codes are useful (raw `0` = 1 automation, raw `63` = 64 automations) — a block only exists because it has *something* in it, so there's no reason to ever waste a code on "zero automations."
  - **Special flag set, automation flag clear:** the 6 bits are the special-field flags themselves — one bit per override (note, velocity, microtiming, roll mode, rand1; one bit reserved for future use). Each set bit contributes one override byte, in flag order, immediately after the 2-byte header.
  - **Both flags set:** the 6 bits are still the automation count as above, but a **third header byte** is added, holding the same special-field flags plus 2 further bits — one is **`automation hold`** (defined here only as a flag that exists; what it *means* during playback is sequencer-level logic for Phase 3's playback design, not a storage-format question), the other reserved. Override bytes follow the third header byte, then the automation entries.
- **Automation entries** are 2 bytes each, unchanged: 9-bit target parameter ID (up to 512 addressable parameters — see 3.4), 7-bit value.

**Worst case, using your own numbers:** a step using every currently-defined special field (5, plus 1 reserved and unused) and 4 automations costs `3 header bytes + 5 override bytes + 4×2 automation bytes = 16 bytes`. If every one of the 896 steps hit that simultaneously, the whole scene costs `896 × 16 = 14,336 bytes` — comfortably inside the 16,382-byte pool, with 2,046 bytes of headroom left over even in that extreme case. In the more realistic "every step has at least one automation, nothing else" case, the pool holds **7,295 total automation entries**. That's about 7% fewer than the 7,872 the earlier 18KB-pool version could claim — but that comparison wasn't apples-to-apples: the 18KB version never actually budgeted bytes for the step-ID that 3.3's defragmentation depends on. A version of the 18KB pool corrected to include one (which needs a 3rd header byte, since a 1-byte header can't fit a 10-bit ID) tops out at 6,976 — this design beats even that, from a smaller nominal pool, because moving the flags out to the static array buys back exactly the room the step-ID needs without a 3rd byte in the common case.

### 3.3 Background defragmentation & real-time-safe live recording

This has two genuinely different jobs, because there are two different ways a step's dynamic-stack block gets written, with very different timing pressure:

- **Parameter locks** (manual, menu-driven edits) are slow and human-paced — effectively unlimited time, on audio-thread timescales, between one edit and the next. These can synchronously request more room for a step, relocating its block if needed, without anyone noticing a stall.
- **Live recording** captures automation in real time while the pattern plays. It must never be made to wait on the background defragmenter — a stall here is an audible or lost value, not a UI hiccup.

That split drives the design: a fast, allocation-free path for the common case, kept ahead of by a cheap local "top-up" operation, with the existing global defragmenter demoted to separate, lower-priority housekeeping.

**Fast path — the common case touches compaction not at all.** A live-record write for a parameter that **already has** an automation entry on that step is a single in-place value-byte overwrite: no growth, no allocation, no interaction with compaction, no matter how fast values are streaming in. This covers the ordinary case of sweeping an already-automated parameter and needs nothing further.

**Reserved slack — covers the case that does grow.** A live-record write for a parameter with **no existing entry on that step yet** needs 2 new bytes. Rather than reserving the bare 2-byte minimum, reserve a full 4-byte chunk of trailing slack after every block whenever it's written or relocated — matching the free-space bitmap's own 4-byte chunk granularity below, so this costs nothing extra to track. A 4-byte chunk absorbs **two** first-time automation writes before it's exhausted, not one — worth having, because a single live pass can put more than one newly-automated parameter onto the same step (sweeping two knobs at once); a bare 2-byte reservation would force an immediate relocation on the second one.

**Micro-relocation — refilling slack, one step at a time.** When a step's trailing slack is used up, that step is queued (low priority) for a slack refill: relocate just that block to a new spot with a fresh 4-byte slack chunk appended, using the same write-new/swap-pointer/free-old mechanic as the main defragmenter, just scoped to a single step instead of a pool-wide sweep. That narrow scope is what makes it cheap enough to run reactively, a few per background tick, well inside the bounded-per-tick-work approach the rest of the project already leans on (`BURST_REDUCTION.md`). At realistic musical tempos, the gap between one new-parameter live-record event on a given step and the next is on the order of a step's duration — tens to low hundreds of milliseconds — which is enormous headroom on a 216MHz core for the background maintainer to stay ahead of.

**Safety net.** The pending-write input queue from the original design stays as the backstop for the pathological case — several new-parameter automations landing on the same step within a single tick, faster than the maintainer can refill slack. A write that finds its step's slack already exhausted queues instead of blocking, and drains within the next tick or two. This should be rare, not the normal path.

**Global defragmenter — separate, lower priority.** Micro-relocations leave small holes scattered through the pool over a long editing session, the same way any allocator does. The pool-wide defragmenter (find the most fragmented region, consolidate it) is what turns scattered holes back into large contiguous free runs. It's periodic housekeeping, not on any real-time path — the reserved-slack mechanism above is what actually guarantees live recording never stalls, independent of whether the global sweep has run recently.

**Free-space tracking.** A 1-bit-per-chunk occupancy bitmap over 4-byte chunks, covering the full `2¹⁴` nominal address space rather than just the 16,382 usable bytes — chunking the full nominal range is what gives a clean `2¹⁴ ÷ 4 = 4,096` chunks `= 4,096 bits = 512 bytes`, with the 2 reserved addresses simply always marked occupied. Both micro-relocation and the global defragmenter use this to find a new home for a block: a linear scan is cheap on this core (worst case ~128 32-bit word tests across 4,096 bits, faster still with a count-trailing-zeros instruction to skip whole free/full words at once). The bitmap doesn't need to track *whose* block occupies a chunk — that's what the block's own step-ID header is for: "whose block is this" is answered by reading the 10-bit ID, not by anything in the bitmap.

**Cross-scene safety, per your note:** mutating step data on a track is disabled if that track's *scene* isn't the one currently playing — so only one scene's pool is ever under concurrent read (playback) + write (edit, live-record, micro-relocation, defrag) pressure at a time.

**Atomicity:** unchanged — the static array entry is a 2-byte, 2-byte-aligned field, so the pointer swap that makes a relocated or newly-written block live is a single aligned `STRH` on Cortex-M7, atomic with respect to interrupts and DMA without additional locking.

**Save format:** because the pool stays defragmented — both by the periodic global sweep and continuously by the micro-relocation slack top-ups — it writes to the `.pat` file close to as-is.

### 3.4 Parameter ID space (shared with the FX sequencer)

The 9-bit target parameter ID in each automation entry addresses up to 512 distinct parameters. Your budget: roughly 32 FX parameters plus up to 80 parameters per voice × 6 voices = 480, for a 512 total. Current drum voices sit around 32 parameters, so this leaves real headroom for the new voice types in Phase 6 (which will have more parameters than the current drum/snare/cymbal/hihat set) without running out of address space. This same 9-bit ID space is reused by the FX sequencer's automation encoding (Phase 6), so it's worth fixing this parameter-ID scheme once, here, rather than each subsystem inventing its own.

### 3.5 Copy operations

Per your note, the full set: copy scene, copy instrument (single voice part), copy track sequence (one track's step data between patterns/scenes), copy FX (FX stack + its per-scene settings), copy bar, copy step. The existing "copy step, copy sub-step, copy single-voice track between patterns" flow described in `putting it together` becomes the template for all of these — hold COPY, press a source selector, press a destination selector — extended to the new selectable units (scene, instrument, FX) on top of the ones that already exist (step, track).

### 3.6 Automation always runs; trigger is a separate bit

Automation on a step plays back regardless of whether that step has a trigger — this is a change from the old velocity-0-as-automation-only-step model. Only steps with an actual trigger light their LED. Pressing a step button in step mode still toggles the trigger/LED, but does **not** touch that step's note, velocity, automation, or any other stored data — trigger-on/off and "what data lives on this step" are fully decoupled. Additionally: holding `SHIFT+COPY/CLEAR` while the menu is up, then pressing sequence and `SELECT` buttons, clears just a step or just a bar of all automation/settings (distinct from toggling a trigger off).

### 3.7 Per-track step timing scale

Per-track length (up to 128 steps) and per-track scale, accessible from the second page under the transient-voicing ("click") sub-page. Since there are no sub-steps in this paradigm, scale is expressed relative to the base step (1 step = 1/16th note): scaling a track up to ×16 means 1 step on that track = 1 bar, in `/2` increments down to `/16` (1 step = 1/128th note). Dot and triplet subdivisions are flagged by you as open — see below.

### 3.8 Roll overhaul

Roll rate becomes independent of pattern length (previously tied to it). Rolls become recordable in three modes — full (pitch + velocity), note-only, or velocity-only — configured from the `SHIFT+RECORD` menu. That same menu's existing "automation lane" selector doesn't make sense anymore (there's no separate automation lane in the dynamic-pool model — automation lives on the step it was recorded on) and is replaced by a **record-to-track** option: `slf` (default — record a voice's live automation onto its own track) or any other track, in which case live parameter automation gets recorded onto the specified track instead of the source voice's own.

### 3.9 Patgen/Euklid reset

On the Patgen/Euclidean page (`SHIFT+PERF`), pressing `SHIFT+PERF` twice reverts the pattern to its state from before entering the page; exiting normally commits. Flagged caveat from the spec: if the pattern *length* parameter was changed on the page, a revert may leave residual track-offset artifacts even after the revert, since length changes can shift step alignment in ways a simple content-revert doesn't undo — worth a specific test case once this is implemented.

### 3.10 Triplet/ternary scale mode ("notes from others")

A borrowed idea worth folding in here since it's sequencer-scale-adjacent: a pattern-level scale selector with `12a`/`12b` modes that skip specific steps of a 16-step grid on a fixed schedule (`12a` skips steps 2, 6, 10, 14; `12b` skips 3, 7, 11, 15) to convert a 16-step binary grid into a 12-step ternary (triplet) feel and back, without needing a genuinely different step count. This is a cheap way to get triplet feel without touching the 128-step/8-bar architecture — worth doing as a scale/display mode on top of Phase 3 rather than a structural change.

### Open Engineering Questions

- **Manual roll triggering:** you flagged needing "a smart way of triggering manual rolls" now that rolls are decoupled from pattern length — this needs a concrete UI proposal (which button/hold-gesture initiates a manual roll, and at what rate) before Phase 5's UI work can wire it up.
- **Dot/triplet subdivisions for per-track scale:** flagged as "maybe" in the source doc — worth a decision before 3.7 is implemented, since it affects the scale-value encoding (a plain `/2..×16` power-of-two range doesn't accommodate dotted/triplet values without extra encoding bits).
- **Live-record write semantics — confirm before 3.3 is built.** 3.3's whole slack-reservation design rests on an assumption: that live-recording a continuously-moving parameter onto a single step *updates one existing automation entry's value in place* (cheap, no growth) rather than appending a new entry on every incoming value change (which would consume slack constantly rather than only on the first touch of a new parameter). This needs confirming — is an automation entry keyed uniquely per `(step, parameter)`, with live record overwriting the value byte of the existing entry once one exists for that pair? If instead every value change during a live pass is meant to append a fresh entry, the slack-reservation sizing in 3.3 needs to be revisited from scratch, since the "one-time cost per step" framing wouldn't hold.
- **`automation hold` playback semantics:** the flag exists in the storage format (3.2) but what it does during playback — does a held value persist until the next automation for that parameter anywhere later in the pattern, and does that next automation become the new held value or a one-shot override — is undecided and belongs with `seq_tick()`'s playback logic.

### Suggested Complementary Features

- **Pool usage meter.** A percentage-used indicator in the global settings, next to the CPU meter, for the active scene's event pool — you specifically asked for this ("0-99% like there is for cpu use") and it's cheap to compute (pool bytes used ÷ pool size) and genuinely useful for knowing when you're approaching the automation ceiling on a dense pattern.
- **Per-scene pool high-water mark, not just live usage** — showing "peak used this session" alongside live usage would help catch a scene that briefly spiked into heavy automation and then got scaled back, which a live-only meter would hide.

## Phase 4 — Voice, MIDI & Morph Control

**Location:** `Core/MIDI/MidiParser.c`, `Core/Scene/` (morph engine), `Core/DSPAudio/lfo.c`

### 4.1 Per-voice MIDI, isolation

Less new work than it might look like: `midi_MidiChannels[8]` already exists (`MidiParser.c:157` / `MidiParser.h:83`) — one channel per voice (elements 0–6) plus a global channel (element 7) — and `PAR_MIDI_CHAN_1`…`PAR_MIDI_CHAN_7`/`PAR_MIDI_CHAN_GLOBAL` are already real parameters (`ParameterArray.h`). So per-voice MIDI channel *assignment* is already there; what's missing is:

- A `0` sentinel value meaning "MIDI input disabled for this voice" (or globally, for the global channel slot), checked wherever `midi_MidiChannels[voice]` currently gates channel-match logic (`MidiParser.c:1180`, `1226`, `1234`, `1246`, `1271`, `1492`, `1497`).
- Chromatic note handling on a voice's own channel (currently voices mostly respond to their channel for CC/trigger purposes — verify note-on with arbitrary pitch is already routed to the voice's oscillator pitch, or needs adding).
- CC1 (mod wheel) on a voice's individual channel driving that voice's per-voice morph (4.3), rather than only the global channel driving global morph.
- Bank-change (CC0) on a voice's individual channel loading a different scene into that voice's part slot specifically, per `putting it together`'s "Bank change can load another scene from the SD card, CC1 does the individual voice morph" — this depends on Phase 2's scene/bank plumbing being in place, and on a "swap just this voice's part from a different scene" operation existing (which is a variant of the Phase 3.5 "copy instrument" operation, applied at load time instead of copy time).

### 4.2 Mod wheel → morph

CC1 on the global channel drives global morph; CC1 on an individual voice channel drives that voice's per-voice morph (4.3). Per the spec, the incoming 7-bit MIDI value is doubled to reach the 0–255 morph range, except 127 which maps directly to 255 (so the top of the MIDI range reliably hits the true morph endpoint rather than landing at 254 and never quite reaching the stored morph-target parameters).

### 4.3 Per-voice morph

New capability: each voice gets its own morph amount, independent of the global morph value, blending between that voice's kit parameters and its morph-target parameters. Settable from the PERF menu (0–255 full range), by step automation (Phase 3.6/3.2 — per-voice morph is one of the automatable parameters), and by velocity modulation. Receiving a global morph message overwrites all per-voice values (global morph "wins" when it arrives, consistent with the existing single global morph behavior today).

Since scenes now carry their own kit/morph-target pair (Phase 2), per-voice and global morph amounts need to be saved per scene, and per your note there should be a global option controlling scene-switch behavior for morph state: reset to 0 on every scene change, retain each scene's own morph value across changes (so switching back to a scene resumes wherever its morph was left), or always keep one consistent morph value applied uniformly regardless of scene. This is a genuinely new global setting, not implied by anything that currently exists.

**Per-voice morph as an automation target** has two distinct behaviors that need to stay separate in the implementation:
- **Velocity modulation** sets the per-voice morph value once per trigger — same mechanism as step automation or a PERF-menu edit, and it updates the visible PERF-menu value.
- **LFO-to-voice-morph** is different: it modulates *between* the voice's current morph amount and the full morph-kit endpoint, continuously, and is explicitly "background" — it does not update or appear as a value on the PERF menu, unlike every other way of setting per-voice morph.

### 4.4 Morph engine implementation

The current LXR-02 morph engine (`Core/Preset/presetManager.c`, soon `Core/Scene/`) is a single global morph: `preset_morph()` sets a target and bumps a generation counter; `preset_morphTick()`, called once per main loop, advances a `morph_index` cursor across `END_OF_SOUND_PARAMETERS` and sends **exactly one** interpolated parameter per call via `frontPanel_sendData()` (soon a direct call, per Phase 1.2), skipping a short list of indices that shouldn't be morphed (index 127's encoding collision, velocity-destination slots, voice-LFO slots, LFO-target slots). This one-parameter-per-tick design is exactly what you described, and it's the mechanism Phase 4 needs to extend to per-voice + LFO-overlay morphing without breaking its real-time-safety property (bounded work per tick, no burst).

There's real prior art for this specific extension: `LXR/mainboard/LxrStm32/src/Preset/MorphEngine.c` in the original LXR-1 codebase already implements per-voice morph amounts plus an LFO-to-morph overlay, as a "background morph worker" (`preset_serviceMorphInterpolation()`) that does "exactly one interpolation unit... per call" — its own doc comment uses almost exactly your phrasing. Its approach: a `preset_morphScanParam` cursor walks parameters (like LXR-02's `morph_index` today), and a `preset_morphDrainPhase` counter interleaves an LFO-overlay check for each of up to 6 source voices *at every parameter*, before advancing to the next parameter — so its actual drain order is "param 1: base value, then check voices 1–6 for an LFO-morph overlay targeting this param; param 2: same; …" rather than "finish all params for every voice, then do LFO overlays at the end."

That's worth flagging because it's a different order than the one you described for LXR-02: *"morph is on voice 1, param 1, next loop param 2… done with voice 1, now voice 2… done with voice 6, last parameter, aha, LFO of voice x targets morph of voice y, go back and do the special LFO morph of voice y, param 1, next loop param 2…"* — i.e., LXR-02's version should fully drain the normal per-voice morph sweep first, then append LFO-morph overlay passes at the end of that same cycle, extending total cycle length only when an LFO-morph assignment is active. Structurally this is the easier of the two orderings to implement (a single cursor that walks through "phase 0: normal sweep" then "phase 1..N: one LFO-overlay pass per assigned source" and wraps back to phase 0), and it's a smaller behavioral change from LXR-02's current single-cursor `morph_index` design than porting LXR-1's interleaved version verbatim would be — so LXR-1's file is useful as a reference for *what a working implementation of this idea looks like* (the free-list of helper concepts: an "is there an LFO-morph assignment for this source voice" predicate, a drain-phase counter, one-parameter-at-a-time application through the existing live-apply path) without needing to port its interleaving order.

### 4.5 One-shot LFOs

Current `lfo.h`/`lfo.c`: 8 waveforms (`LFO_SINE`, `LFO_TRI`, `LFO_SAW_UP`, `LFO_SAW_DOWN`, `LFO_REC`, `LFO_NOISE`, `LFO_EXP_UP`, `LFO_EXP_DOWN`), a 32-bit phase accumulator (`phase`/`phaseInc`), and `lfo_calc()` already detects a phase wraparound every call via `overflow = oldPhase > lfo->phase` (used today to re-roll the noise waveform's held value on each cycle) — that same overflow flag is the natural hook for one-shot behavior: on wraparound, a one-shot waveform latches to idle instead of continuing to free-run.

New waveforms per `putting it together`: `si1`/`tr1`/`sq1`/`rmp1`/`rnd1` (one-shot sine/triangle/square/ramp/noise) and a new `xt1` ("exponential-triangle" — exponential rise immediately followed by exponential fall, effectively `LFO_EXP_UP` and `LFO_EXP_DOWN` concatenated into a single cycle rather than selected as alternatives). Behavior specifics from the spec:

- In one-shot mode, the existing `offset` control (today used generically) becomes a **pre-trigger delay**, scaled to the LFO's rate — i.e., the delay is expressed as a fraction of one cycle at the current rate, not an absolute time, so it scales sensibly as rate changes.
- One-shot noise (`rnd1`) holds a single random value for the entire one-shot cycle (re-rolled only on retrigger) — this reuses the existing `lfo->rnd`-latch-on-overflow mechanism in `lfo_calc()`'s `LFO_NOISE` case almost directly.
- One-shot rect (presumably the one-shot variant implied by `LFO_REC`) is phase-inverted relative to the free-running version, so it can go immediately on and then off within the one cycle, with the offset/pre-trigger delay controlling an initial off-portion if wanted.

This needs a small state machine per LFO instance beyond what `Lfo` currently holds (idle / delayed / running, at minimum), since a one-shot LFO needs to know it's *finished* and hold its last value (or a defined rest value) rather than wrapping back to phase 0 and re-triggering itself — `lfo_retrigger()` (`lfo.h:83`) is the existing retrigger entry point this needs to hook into.

### Open Engineering Questions

- **One-shot LFO state field:** the `Lfo` struct doesn't currently have an idle/delayed/running state — needs adding, and needs to be sized/placed consistently with the rest of the struct's DTCM-resident hot-path fields (see `LfoStruct` in `lfo.h`).
- **Per-voice morph value storage location:** LXR-02's current morph state is two flat parameter arrays (`parameter_values`/`parameters2`, base and morph-target) plus a single global morph amount — no per-voice amount array exists yet. This needs adding (a 6- or 7-element `uint8_t` array is the obvious shape, matching LXR-1's `preset_vMorphAmount[7]`) as part of 4.3, independent of the drain-order question in 4.4.
- **LFO-to-morph assignment storage:** where does "LFO of voice X targets morph of voice Y" get stored and how is it exposed in the UI (Phase 5)? LXR-1 keeps this as an automation-target selector inside its `PresetKitState`; LXR-02's equivalent needs a home once the Scene/kit data model is finalized in Phase 2.

### Suggested Complementary Features

- **Morph snapshot** (from the earlier draft, still worth keeping): a command to commit the currently-heard interpolated morph state into the base kit parameters, freeing the morph-target slot for a new destination without a save/reload round trip.
- **Per-voice morph reset-with-global option**, as a natural extension of the "how does per-voice morph behave across scene changes" global setting in 4.3 — worth exposing "reset per-voice morphs to match global on next global morph message" as an explicit sub-option, since it's a one-line addition once the main behavior exists.

## Phase 5 — UI & Performance Workflow

**Location:** `Core/Menu/menu.c`, `Core/Hardware/frontPanel/buttonHandler.c`, `Core/Hardware/frontPanel/ledHandler.c`

This phase is UI-heavy and depends on Phases 3 and 4 already existing (there's no automation view to redesign until the dynamic event pool exists to read automation *from*, and no per-voice morph display until per-voice morph exists). It's also the largest single UI redesign in the roadmap, so it's written up here in more procedural detail than the other phases, since the spec itself is procedural.

### 5.1 Automation view redesign

This replaces the current step-view automation display (parameter assignment/amount shown under step view) with two connected new views.

**View A — scrollable automation list**, reachable from step view when editing step automation:
- A non-looping, scrollable list of `parameter – voice – amount` entries, showing "end" once you scroll past the last entry rather than wrapping.
- **Knob 1:** cycle the automation parameter (shown as a 3-character short name in the leftmost column).
- **Knob 2:** cycle by voice (shown as `vo1`–`vo6` in the second column).
- **Knob 3:** change the amount (third column).
- **Knob 4:** change the "function" to apply (fourth column) — cycling through: view automation (jumps to View B below), remove just this automation, remove all automation from this step, remove all automation of this type from the track, set all automation of this type on this track to this value, and room for more to be added later.
- Clicking the encoder **executes** whichever function is showing in the fourth column — for anything mutating (remove/set), that means a confirmation screen first; for "view automation," it jumps straight into View B.

**View B — the automation editor itself**, reachable either from View A or directly from the voice page:
- On entry, the current automation for the selected parameter, across every step in the track, is copied into a temporary working array. All edits in this view happen against that working copy — nothing is committed to the real pattern data until the view is exited normally.
- Holding `SHIFT` and pressing `COPY/CLEAR` brings up a "cancel automation edit?" confirmation; confirming discards the working copy entirely, reverting to whatever was there on entry.
- Screen layout: top line shows the voice (long name) and parameter (long name) being edited. Voice LEDs show the currently selected track but **don't** function as track selectors in this view except as a copy destination — you can't switch which track you're editing automation for mid-view by pressing a voice button, only by copying to a different track. `SELECT` LEDs show the current bar (of the 8); pressing a `SELECT` button switches which bar is shown.
- Bottom row (16 characters, one per step of the visible bar): blank if the step has no automation for this parameter, otherwise a `0`–`9` digit representing the automation amount on a relative 0–127 scale.
- **Knob 1:** cycle voice. **Knob 2:** cycle parameter. Sequence buttons light up for any step that currently has automation for the selected parameter.
- **Multi-step editing:** holding any combination of the 16 sequence buttons switches the bottom-row readout to `avg:` (average value across the held steps, left side) and `mod:+0` (a running modification delta, right side). Turning the encoder while steps are held does two things at once: (1) it adds automation at the currently-shown average value to any held step that doesn't already have automation for this parameter, and (2) it increments/decrements the automation value of *every* held step by ±1 per detent, updating the displayed average live. You can keep adjusting by continuing to turn the encoder, by changing which steps are held, or by switching bar/track via the bar/track buttons — the temporary working array tracks all of it.
- **Copy:** from this view you can copy steps, bars, or copy the whole automation lane to another track.
- **No dedicated clear operation inside this view** — the only ways out are the normal exit (commits) or the `SHIFT+COPY/CLEAR` cancel (discards). Clearing automation is a View-A-and-below operation (per 5.1 View A's "remove" functions, or the step-view `SHIFT+COPY/CLEAR`+button wipe from Phase 3.6).

This is a genuinely large piece of UI state machine — four knobs with context-dependent meaning in View A, a temporary-array-with-commit-on-exit model in View B, and a multi-step "hold N buttons, turn one knob, average updates live" interaction that doesn't have a close analog elsewhere in the current menu code. Worth prototyping the state machine (what's "current view," "held steps," "working array," "dirty" state) as its own small module before wiring it into `menu.c`'s existing page-dispatch structure, rather than growing it inline.

### 5.2 Morph quick access & automation indicator

- While viewing a single parameter in the encoder click-in view, holding `SHIFT` toggles between editing that parameter's normal value and its morph-target value, avoiding a save/reload round trip just to set morph endpoints. A further "lock" mode to keep the whole voice interface showing morph-target values for every parameter (rather than needing to hold `SHIFT` per-parameter) is called out as wanted too.
- The voice page should **only** service voice and scene editing — no step-editing functions belong there (that's step view's job). On the voice page, the 16 sequence buttons become **scene toggles**: each one toggles whether that scene is included in the current voice-parameter edit, so a parameter change can be applied to all 16 scenes, a subset, or just one, depending on which are toggled on. This is a genuinely different meaning for those 16 buttons than they have anywhere else in the UI (scene *selection* elsewhere, scene *inclusion-in-edit* here) — worth a clear visual distinction (different LED color/blink pattern) so it's not confused with PERF-mode scene switching.
- The LXR-02 hardware has dedicated shift-labeled functions already printed on the sequence buttons — new shift functions should avoid piling onto those buttons where another control is reasonably available, per your explicit note.
- The LCD's underline indicator should show when the currently-displayed parameter is automated anywhere in the currently playing scene/pattern — a quick "is this being moved by something" signal without needing to open the automation view to check.

### 5.3 PERF mode: scene switching & per-track assignment

- **Instant scene switching:** a global menu option makes scene switching (via `SEQ` buttons or MIDI program change) take effect at the next step rather than waiting for the end of the bar, preserving sequencer position through the switch. This also carries whatever kit/morph/parameter changes the new scene brings, not just pattern data — it's a full scene swap, not just a pattern swap, which is a bigger behavioral change than the pattern-only version originally described in `putting it together`. Default behavior (end-of-bar) is preserved when the option is off.
- **Per-track scene assignment:** hold a voice button and press a `SEQ` (scene) button to assign that individual track to play from a different scene than the rest — same gesture as the old "per-track pattern assignment" idea, retargeted at scenes.
- **Per-voice morph in the PERF page:** each voice gets a direct morph control on the PERF page, full 0–255 range. Step automation and velocity automation update it in real time and it's visible; LFO-to-voice-morph modulation does **not** update the displayed value (per Phase 4.3 — it's background-only). Changing global morph updates every per-voice value shown here.

### 5.4 Looper

Moves to the `SELECT` buttons (confirmed correction from your round-2 answer — the original `putting it together` draft used `SEQ` 9–16, which conflicts with `SEQ` now meaning scene-select in PERF mode). The original spec describes 8 divisions from a half-bar down to 1/64th note, halving at each button, with holding one additional button ("button 9" in the original 16-button numbering) adding a dotted 50% to whichever other loop button is held, and releasing all loop buttons returning the sequencer to the position it would have reached without looping.

That division range was originally expressed in **sub-step** terms (64 sub-steps = 1/2 bar, down to 1 sub-step = 1/64th), which no longer exists as a unit post-Phase-3. This needs re-deriving in step/bar terms before it can be implemented — flagged below as an open question, since a naive re-mapping (halving from "1/2 bar" down through 8 buttons) lands on 1/256th at the bottom with no sub-steps to represent it, which doesn't match the original "down to 1/64th" intent.

### 5.5 Load/save UI rework

The original `putting it together` draft proposed a specific knob remapping for the load/save menus (knob 1 = type, knob 2 = number/cursor, knobs 3/4 = character entry with capitals/numbers/lowercase split across them). Per your note, this is superseded — "we have bigger file changes in mind" — because the whole load/save menu needs rebuilding around the new file-type set from Phase 2 (BANK/SCENE/KIT/PAT/FX/instrument-parts) rather than the old kit/pattern/morph/performance/all/globals/samples set. The specific knob assignment idea is worth keeping as a starting point for that rebuild, but the menu structure itself (what "type" even means, what "auto-load" means per type) needs designing fresh against the Phase 2 file model rather than patched onto the current one.

### 5.6 External MIDI sequencing tracks

From "notes from others" in `putting it together`: doubling the sequencer's track count (6 or 7 additional tracks) purely for sequencing external MIDI gear via program-change/CC, using the same UI and workflow as the internal voice tracks, reached through a shift function that opens a second page mirroring the internal-voice page layout. This is naturally deferred until after Phases 3–4 land, since it's most straightforward to build as "the same per-track step/automation machinery Phase 3 already built, pointed at a MIDI-out target instead of a DSP voice" rather than a parallel implementation.

### Open Engineering Questions

- **Looper division mapping without sub-steps.** Needs a concrete answer before 5.4 can be built: is 1/64th represented via a track-scale-style subdivision (Phase 3.7's `/2`..`×16` scale applied to a virtual "loop track"), or is the shortest loop division now coarser (e.g., 1/16th, one full step) given sub-steps no longer exist? This changes both the encoding and the UI.
- **Manual roll trigger gesture** (carried over from Phase 3) needs a home in this UI redesign — likely a `SHIFT`+something on the step buttons or a dedicated control, per the "try not to put shift functions on the SEQ buttons" constraint from 5.2.
- **Automation view performance:** View B's temporary working array (automation for one parameter, all steps in a track) needs to be read from and written back to the Phase 3 dynamic pool efficiently — worst case, entering the view triggers up to 128 individual pool lookups (one per step) to populate the array, and exiting triggers up to 128 pool writes. Should be fine given the pool is designed for O(1)-ish per-step access, but worth confirming against Phase 3's actual implementation once it exists.

### Suggested Complementary Features

- **Automation "eraser" mode** (from the earlier draft, still reasonable): a shortcut — e.g., holding `CLEAR` while turning a parameter's knob — that wipes all step automation for that specific parameter across the active track in one gesture, complementing but distinct from View A's per-step "remove" functions.
- **Scene-inclusion visual on the voice page (5.2):** since toggling scene inclusion for a parameter edit is a new interaction, consider a brief on-screen summary ("editing: 3/16 scenes") when a parameter is touched, so it's obvious at a glance how broad the edit's blast radius is before committing to a knob turn.

## Phase 6 — DSP Expansion

**Location:** `Core/DSPAudio/`

The heaviest phase computationally, and the one where the earlier drafts did the most guessing. This version tries to separate what's confirmed by the current code, what's confirmed by your answers, and what genuinely needs a measurement or a decision before implementation — rather than asserting specific byte counts that sound precise but aren't backed by anything.

### 6.1 Voice tiers

Currently: `DRUM`, `SNARE`, `CYMBAL`, `HIHAT` instrument types, freely swappable between any track (tracks 6 and 7 always choke each other, per the existing spec), instrument type itself is not modulatable or morphable. Per your answer, this becomes three CPU/memory tiers rather than a flat list:

- **Basic** — drum, snare (and similar low-DSP-cost voices).
- **Advanced** — cymbal, hi-hat, and other voices that need meaningfully more DSP per sample.
- **Advanced-buffer** — granular, convolution, and anything else that needs a dedicated hot data buffer: a **1/4-second, 8-bit, mono buffer in ITCM** per voice instance of this tier, used to loop lower-resolution grains/impulse data in the background without taxing the main per-sample DSP budget.

This tiering is a real architectural decision (voice types declare which tier they need, and the tier determines what resources — filter complexity budget, ITCM buffer allocation — get reserved for that voice slot), not just a naming convention, so it's worth deciding the tier-to-resource mapping explicitly before the first advanced-buffer voice (granular, 6.2) gets built against it.

**A genuine open question, not glossed over:** ITCM is 16KB total on this part, and it currently holds the oscillator hot-path *code* — `calcSineBlock`, `calcFmSineBlock`, `calcFmBlock`, `calcNextOscSampleFmBlock`, `calcNoiseBlock`, `calcNextOscSampleBlock`, `calcWavetableOscBlock`, `calcSampleOscFmBlock`, `calcUserSampleOscFmBlock`, `calcUserSampleOscBlock`, `calcSampleOscBlock`, `osc_setFreq` — about a dozen functions tagged `INITCM` (`config.h`'s `ENABLE_OSC_INITCM_CODE` switch). A 1/4-second 8-bit mono buffer at ~44.1kHz is roughly 11KB. Putting an 11KB **data** buffer in the same 16KB region that's currently reserved for oscillator **code** is a real resource conflict, not free headroom — the earlier draft asserted this "fits perfectly," which isn't something the code supports as written. This needs an actual measurement (how much of the 16KB the current `INITCM`-tagged functions occupy, via `arm-none-eabi-size`/the linker `.map` file after a real build) before committing to ITCM as the buffer's home, and a fallback plan (DTCM instead, accepting the non-DMA-accessible constraint that's already true either way since ITCM isn't DMA-accessible either; or reducing which oscillator functions stay `INITCM`-tagged to make room) if it doesn't fit.

### 6.2 Granular instrument

Built as a complete instrument (advanced-buffer tier), not an oscillator variant — this was your explicit correction to the initial framing. Reads directly from the 1.5MB sample flash region (the same one that already backs regular sample playback, so the flash-read-speed question is answered by "it already works for samples as-is," per your answer). Pitch parameters, per your spec: assign a scale/interval, fine detune, and a "distance" value that moves up/down that scale/interval — rather than free continuous pitch, grain pitch is quantized to a chosen scale and stepped through it. A feedback path with a short delay/decay is also wanted; per your round-2 answer, this is meant to use the same BBD-style buffer described in 6.7 to "stack" the grain sound, giving density without needing true overlapping-grain polyphony.

Grounding from actual granular-synthesis practice, since this is new DSP territory for the project: the standard approach windows each grain with an amplitude envelope to avoid clicks at non-zero-crossing boundaries, and the shape of that window is itself a real timbral control, not just anti-click housekeeping — an equal-power/Hann-style crossfade gives the smoothest, most "fused" texture, while sharper (near-rectangular) windows give a more clicky/metallic character and are cheaper to compute. Grain parameters worth having beyond pitch (standard across granular implementations): grain length, density (grains per second / overlap amount), and position jitter (randomizing the read-start point slightly for a less mechanical texture) — these map naturally onto the existing per-parameter automation/morph infrastructure once they exist as real parameters.

### 6.3 New voices

- **West Coast.** Sine/triangle oscillator core, wavefolder (depth + symmetry), FM ratio/index, and an LPG-style decay envelope in place of a standard ADSR. Two pieces of this reuse existing code directly: the sine/triangle core is already `SINE`/`TRI` in `Oscillator.h`, and 2-operator FM already exists (`calcFmSineBlock`/`calcFmBlock` in `Oscillator.c`) — the FM ratio/index parameters are largely exposing controls on code that's already there, not writing new FM synthesis from scratch. The wavefolder and LPG are genuinely new. For the wavefolder: the cheapest embedded-appropriate approach is a triangle-style fold (mirror the signal back down once it crosses a threshold, piecewise-linear, computationally trivial), which is the same family of technique used in Serge/Buchla-style analog wavefolders being modeled in current DSP research — worth noting that any digital wavefolder aliases hard at high fold depth on high-pitched material, and this platform has no spare CPU budget for oversampling the wavefolder stage, so fold depth may need a soft ceiling (or an explicit "this gets aliasy at extreme settings" acceptance, which is arguably in keeping with an intentionally lo-fi/8-bit-adjacent voice anyway). For the LPG: real Buchla-style LPGs are a combined VCA+lowpass filter driven by one control signal with a distinctly *asymmetric* response — fast to open, slow/lazy to close — which is what gives the characteristic percussive "ring." The computationally cheap way to get that same asymmetric behavior digitally is a one-pole smoother on the strike/decay envelope with two different time constants depending on whether the envelope is rising or falling, driving both the VCA gain and the filter cutoff from that single smoothed value simultaneously (rather than two independent envelopes) — this is a well-documented digital model of the real Buchla 292 circuit and is cheap enough to run per-voice on this part.
- **Drone.** Interacting sub-oscillators, slow chaotic LFOs, internal bit-crush, a BBD-style short delay/feedback loop (again the 6.7 buffer), integrated ring modulation, bit inversion, and some extra pre-wired or limited-selection internal LFOs (i.e., not the full general-purpose LFO routing the main voices get — a smaller, purpose-built set). No transient/envelope in the normal sense — it's meant to sit and drone once triggered.
- **Karplus-Strong.** This is worth calling out as the *cheapest* of the new voices to implement well: the algorithm is a short noise burst fed into a delay line of length `N = Fs / f0` (sample rate over target fundamental), read back through a simple one-pole averaging filter (`y[n] = (y[n-N] + y[n-N-1]) / 2`, or a loss-factor-weighted version for controllable decay time), fed back into the delay line. A single delay line plus a one-pole filter per voice is far cheaper than any of the other new voice types, and the classic extension for better pitch accuracy at higher notes (an allpass filter correcting the fractional part of the delay length that a purely integer-sample delay line can't represent) is a small, well-documented addition if pitch accuracy on higher-pitched plucks turns out to matter.
- **Convolution chamber.** A pitch-enveloped transient or user sample run through a simulated resonant chamber (spring reverb, cabinet, etc.) via convolution. This is real convolution reverb, which is the most CPU-expensive item in this entire phase — a true convolution against an arbitrary-length impulse response scales with IR length, and even a short IR (a few hundred samples) is meaningfully more expensive per sample than anything else in this document. This needs an explicit CPU budget decision (how long an IR is affordable per voice, whether it's one shared chamber IR set or per-kit-selectable, whether a cheaper structured/algorithmic reverb approximation is an acceptable substitute for true convolution) before committing to "convolution" as the literal implementation rather than as the description of the desired *sound*.

### 6.4 New oscillators

- **Wavetable.** Reads `.wav` files of any length from `WAVETABLES/` (confirmed by your answer — not a fixed single-cycle format, no Serum-style multi-frame container). Morphable (interpolating between two selected waves) but not modulatable directly; scanning through a wavetable set happens via LFO or envelope targeting the wavetable-position parameter, same as any other modulatable parameter.
- **PWM.** You flagged this as "probably wavetables, but suggest another method if you have one" — a direct suggestion: implementing PWM as a genuine variable-duty-cycle square calculation (compare the oscillator's phase accumulator against a duty-cycle threshold instead of the fixed 50% used by `REC`) is cheaper than storing a set of wavetable frames at different duty cycles, and only needs one new parameter (duty cycle) rather than wavetable memory. The tradeoff is the same aliasing consideration as any hard-edged digital waveform on this platform (no oversampling budget), so it inherits the same character as the existing `SAW`/`REC` waveforms rather than being cleaner than them — which is likely fine, since they're presumably an accepted part of the current sound already.
- **Buffer oscillator.** Reads directly from the L or R DTCM buffer described in 6.7, using the same scanning parameters as the granular oscillator/instrument (position, loop size, retrigger, rate/sync, retrigger randomization). This is effectively "granular, but reading from the BBD buffer instead of sample flash" and can likely share most of its scanning-parameter code with 6.2's granular instrument rather than being a wholly separate implementation.
- **Swarm / hypersaw.** A detune-and-phase-spread oscillator stack (multiple copies of a base waveform, each slightly detuned and phase-offset) — standard "supersaw" technique, computationally is just N oscillator instances summed, so its cost scales linearly with however many stacked voices are budgeted per instance.
- **Open-ended items from the source doc worth a decision, not an implementation yet:** "some other smart FM arrangements, still keeping the filter" (a natural extension once 2-op FM's existing code, 6.3, gets exposed as a first-class oscillator option — 3-operator or feedback-FM are the obvious next steps) and "other places to put wavefolding" (the FX stack in 6.7 is one obvious answer — wavefolding as an insert effect rather than only an oscillator-stage effect).

### 6.5 FX bus & send routing

Currently `mixer.c` has no FX bus or send concept at all — `mixer_audioRouting[6]` is a flat per-voice output-destination array (which of the physical outputs each voice's dry signal goes to), and there's no shared send/return path anywhere in the DSP chain. This entire subsystem is new, not an extension.

Per your spec: a stereo send per voice, with send amount as a parameter alongside the voice's existing volume parameter. Three selectable fader modes:
- **Pre-FX (normal):** the voice's main fader attenuates both the dry mix and the FX-send mix together.
- **Post-FX:** the fader only controls the dry/main-mix amount; the FX send is unaffected by fader position.
- **FX:** the fader controls FX-send amount as an additional stage after the send point, and the voice's normal volume parameter effectively becomes a send-only control to the voice's usual output assignment rather than controlling a dry signal directly.

The FX send itself gets its own output assignment (stereo 1/2, L1/L2/R1/R2 — the same destination set voices already route to) and its own output volume, and can be summed back into the main mix if the FX send and a voice share an output destination.

Each FX **stack** (a chain of FX types — the 6.7 tech-demo stack is the first one) is its own file, with up to 64 arbitrary parameters, remembered in both kit and morph endpoints (so FX parameters morph the same way voice parameters do), and tagged with a stack-type identifier so a kit remembers *which* FX stack type it was using even as stacks are swapped between kits — mirroring how instrument type is handled for voices.

### 6.6 FX sequencer

A dedicated 16-step sequencer for FX parameter automation, kept deliberately **static** rather than routed through the Phase 3 dynamic event pool — per your answer, this is small and fixed enough (16 steps × 24 possible automated parameters × 1 byte per automation slot = exactly 384 bytes) that dynamic allocation would be overhead for no benefit. FX parameters are also automatable from the regular track steps, using the same 9-bit parameter ID space from Phase 3.4 (so an FX parameter and a voice parameter are addressed the same way from track-step automation — only the FX sequencer's own dedicated 16-step array is a separate, static structure).

Run modes, per `putting it together`: **Off** (pressing the FX sequencer's `SEQ` button shows its current settings rather than running it), **Fwd** (steps 1–16 straight through), **Rnd** (a random step per division), **FirstX** (the first X steps play in a fixed random order each cycle, the remaining steps run straight), **LastX** (mirror of FirstX — the last X steps are randomized, the preceding steps run straight). Plus a scale and length setting, matching the track-level scale/length concept from Phase 3.7.

### 6.7 The 8-bit tech demo stack

The first real FX stack, meant to double as a proof of concept for the FX bus itself. Three stages: bit-crush (bit-off/invert per bit — a literal bitmask/bit-toggle effect on the sample word, not a bit-depth-reduction crusher), a wavefolder (same technique as 6.3's West Coast wavefolder, available here as an insert effect), and an 8-bit stereo BBD-style delay, followed by a selectable multimode filter at 16-bit resolution — reusing the same filter DSP the voices already use (`Core/DSPAudio/ResonantFilter.c` already has the state-variable filter core — `SVF_recalcFreq`, plus `fastTanh`/`tanhXdX`/`softClipTwo` saturation helpers — tagged `INITCM_EFFECT`, currently disabled by default via `ENABLE_EFFECT_INITCM_CODE 0` in `config.h`; this filter stage is a real, existing, tested building block, not new DSP).

**Memory, checked against the real linker script rather than assumed:** DTCM is 128KB total. Right now only about 3.2KB of it is explicitly claimed via the `INDTCM`/`INDTCMZ` tags — `audioOutBuffer`/`audioOutBuffer2` in `AudioCodecManager.c` (2 × 2 × `AUDIO_DMA_FRAMES` × 4 bytes each ≈ 1.5KB each) and two small oscillator interpolation buffers in `Oscillator.c`. Your own math for the delay buffer — 8-bit halves the size of a 16-bit buffer, so a slightly-under-1-second stereo delay at 44.1kHz fits comfortably under 128KB even with the interpolation/filter state alongside it — checks out arithmetically and there does appear to be real headroom (unlike the ITCM situation in 6.1, DTCM genuinely looks mostly free right now). That said, "checks out arithmetically against a rough tally of currently-tagged buffers" is different from "confirmed by the linker" — worth a real `.map`-file check once other Phase 6 features (which may also want DTCM for hot state) are further along, so the delay buffer's exact size gets finalized against actual remaining headroom rather than the budget being assumed fixed this early.

**A design choice worth surfacing rather than deciding silently:** real BBD hardware pairs its delay line with companding (compress going in, expand coming out) and pre/post low-pass filtering specifically to keep an 8-ish-bit-equivalent signal path usably clean — raw *linear* 8-bit PCM, with no companding, is considerably noisier and more aliased than that. Given this is explicitly framed as an "8-bit tech demo," the gritty raw-linear character might be exactly the point rather than a flaw — worth deciding whether this stack ships as intentionally lo-fi (raw 8-bit, cheapest to implement, matches the "tech demo" framing) or as a more faithful-sounding BBD emulation (adds a compander stage, more DSP cost, cleaner result) — possibly as a toggle, since the compander is a small addition on top of a working raw-8-bit delay rather than a different architecture.

### Open Engineering Questions

- **ITCM headroom for the granular buffer (6.1/6.2)** — needs an actual `.map`-file measurement of current `INITCM`-tagged code size before committing an 11KB data buffer to the same 16KB region; DTCM is the fallback if it doesn't fit, at the cost of no longer being able to double-buffer with the audio-thread DMA-facing buffers that must stay in plain SRAM1.
- **Convolution chamber CPU budget (6.3)** — needs a decision on maximum affordable IR length before "convolution" is locked in as the literal technique rather than a cheaper structured-reverb approximation of the same target sound.
- **DTCM final tally for the BBD buffer (6.7)** — the arithmetic checks out today, but should be re-verified against a real build once other Phase 6 features that also want DTCM (LPG envelope state, Karplus-Strong per-voice delay lines, etc.) are sized.
- **FX stack file format** — 64 arbitrary parameters per stack, remembered in kit and morph endpoints, with a stack-type tag: this needs the same kind of parameter-ID-space decision Phase 3.4 made for step automation (is FX-stack-parameter-64 the same "9-bit ID" space, or a separate per-stack-type namespace?) before the file format can be finalized.

### Suggested Complementary Features

- **Grain windowing (granular, 6.2):** selectable grain envelope shapes — sharp/rectangular (clicky, cheap), Hann/equal-power (smooth, the standard default), and a "windowed/bell" middle ground — as a single parameter, since the window shape is one of the most audible and cheapest-to-implement granular controls available.
- **LPG "ping" modifier (West Coast, 6.3):** a velocity-sensitive strike control that governs how hard the LPG's envelope is hit, independent of note velocity's usual volume role — since the asymmetric-envelope LPG model in 6.3 is naturally sensitive to how its input transient is shaped, this is a small addition on top of that model rather than new DSP.
- **Shared wavefolder stage (6.3/6.4/6.7):** since the same triangle-fold technique shows up in the West Coast voice, the "other places to put wavefolding" open question, and the 8-bit FX stack, implementing it once as a shared inline function (gain-in, fold-depth, symmetry parameters) rather than three separate copies keeps behavior consistent and is less to maintain.
- **BBD compander toggle (6.7):** as discussed above — a cheap way to get both the "authentic-tech-demo-crunch" and "cleaner vintage delay" versions of the same delay line without building two delay effects.

---

## Note on this revision

The version of this document that existed before this pass was generated by spawning ~19 parallel sub-agents against a phase outline that had already drifted from what you'd actually specified, and several of its specifics don't hold up against the real source:

- It claimed `frontPanelParser.c` has "178+ switch cases" to delete. The real number is 70.
- Its granular-buffer code allocates the *entire* 16KB ITCM region to one `int16_t[8192]` buffer (16-bit samples, not the 8-bit mono buffer you specified) — which would leave no room for the oscillator hot-path functions (`calcSineBlock`, `calcFmBlock`, etc.) that are already `INITCM`-tagged into that same 16KB region today, and likely wouldn't link.
- Its wavetable memory plan invents a `.sram1_bss` section attribute that doesn't exist in this codebase — the real attributes are `INDTCM`/`INDTCMZ`/`INITCM`, defined in `config.h`, and ordinary `static` data already lands in SRAM1 via `.bss` without needing a special section at all.
- It recommended shrinking `AUDIO_DMA_FRAMES` to 64 "to buy headroom," despite the code's own comment above that `#define` documenting 64 as already tried and rejected for causing freezes during pattern-load testing.

This revision replaces those with figures checked directly against `Core/` and the linker script where they could be verified, and flags the rest as open questions with a clear path to resolving them (mainly: build once, read the `.map` file) rather than asserting numbers the code doesn't support.

It also goes back to the two rounds of architectural questions you answered earlier in the process — the 128-step/8-bar sequencer redesign, the dynamic event pool's exact bit layout and defragmentation approach, the three-tier voice model, the FX sequencer's static 384-byte structure, and the morph engine's one-parameter-per-cycle drain order — and uses your actual answers as the source of truth throughout, rather than the compressed one-line paraphrases those answers got reduced to in the intermediate draft.
