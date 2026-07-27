# Initial boot parameter misload — implementation plan

## Scope and constraints

Fix only the cold-boot mismatch between a loaded Scene's stored Instrument
types/parameters and the six live tagged DSP runtime slots. The selected
Bank Scene must boot identically to switching away from it and then back.

- Up to 32 bytes of unanticipated retained-memory growth is authorized for
  this work without further chat approval. The repair still must not add
  unnecessary globals, static state, arrays, cache rows, staging, union
  capacity, or a material stack frame; any growth must be measured and noted.
- The completed lifecycle does not add an explicit retained object: it reuses the existing
  `drumset_apply_*` and `instrument_apply_*` fields. A clean detached-baseline
  comparison is retained in the session evidence. The current implementation
  has no linked retained-memory increase versus that baseline.
- The filesystem payload format, the 2,048-byte typed stage, HCNAMES, Bank
  masks, Pattern storage, and the tagged-runtime slot size remain unchanged.
- Instrument membership is fully dynamic. No repair may assume the usual
  `drm, drm, drm, snr, cym, hat` layout, a fixed Snare slot, or that any Scene
  has the same type arrangement as the previous Scene. Every slot always uses
  its active Scene's stored registry type.

The earlier Pattern-selection experiment was reverted after hardware testing
showed it did not fix this issue.

## Observed fixture and static evidence

`SD_CARD/Bank/000 Full/bankset.bcg` selects local Scene 06 (the seventh
physical Scene button). Its `Kit DocWire/kitset.kcg` contains
`drm, drm, drm, snr, cym, hat`, and its Snare file contains the expected
`noise_freq=127` and `osc1_noise_mix=102` values. The SD-card payload is not
missing those parameters.

The active fault boundary is the tagged runtime:

1. `main()` calls `dsp_init()` before `scene_initAll()`.
2. `dsp_init()` calls `instrumentManager_runtimeInit()`, which consults
   zero-initialized `scenes[]` to seed `runtime_slot_type[]`.
3. `INSTRUMENT_TYPE_DRM` equals zero, so all six initially live runtime union
   members are Drums.
4. Normal boot is supposed to repair those members through
   `menu_startSoundApply()` → `preset_sendDrumsetParameters()` →
   `instrumentManager_resetRuntimeSlot()`.
5. Runtime writes dispatch through `runtime_slot_type[]`. If a slot is still
   tagged Drum while Menu/Scene storage identifies it as Snare, a Snare
   descriptor offset writes into the Drum union member. This exactly explains
   the reported Snare mix/noise controls changing the wrong behavior.

The runtime Scene-switch flow additionally clears and later rebuilds its
modulation graph. The synchronous boot flow currently does not complete that
same graph lifecycle, which is a separate explanation for incorrect LFO
behavior even when ordinary parameter values appear plausible.

## Required final invariant

Before `audioCodec_init()` enables audio, and after every later Scene/Bank
activation completes:

| Runtime item | Required owner/source |
|---|---|
| `runtime_slot_type[slot]` | `scene_instrumentSlotConst(active_scene, slot)->type` |
| tagged union member | initialized by that type's `*_initVoice()` |
| descriptor parameter write | descriptor from that same stored type, applied to that same live union member |
| routing and Scene settings | active Scene's retained settings |
| LFO/velocity source | active Scene source slot's retained supplemental cells |
| LFO target | resolved only after every applicable target runtime member is valid; target token is interpreted in the selected target type's descriptor namespace |

This invariant is per slot. It allows any registry-valid type in any of the six
slots, including repeated Snares/HiHats or no Drums at all.

## Required changes

### 1. Initialize SceneData before tagged runtime construction

**Implementation note (completed)**: `main.c` now calls `scene_initAll()` and
`bank_init()` before `dsp_init()`. This is an ordering-only change: it creates
no object and changes no Scene default. `instrumentManager_runtimeInit()` now
sees the complete default per-slot type vector rather than raw zeroed BSS.

**Change**: In `main.c`, move `scene_initAll()` ahead of `dsp_init()` while
preserving all existing pre-audio initialization guarantees. `bank_init()` may
remain adjacent to Scene initialization. Do not initialize any DSP engine from
a raw or guessed type.

**What it does**: establishes six valid default Scene slots before
`instrumentManager_runtimeInit()` reads them. The initial tagged runtime vector
therefore becomes the actual Scene default vector instead of six accidental
Drums.

**Why it must exist**: zero is a valid `INSTRUMENT_TYPE_DRM`, not an "unknown"
sentinel. The current order turns uninitialized ownership into a legitimate
but wrong engine assignment. This fix removes that invalid bootstrap state
even if a card is absent or a later filesystem request fails.

**Inputs**: power-on BSS; `scene_initAll()`'s existing default types;
`instrumentManager_runtimeInit()`.

**Outputs**: valid `scenes[]` type records before `runtime_slot_type[]` and
the tagged union members are initialized.

**Code affiliates**: `main.c:dsp_init`, `main.c:scene_initAll`,
`SceneData.c:scene_initAll`, `InstrumentManager.c:instrumentManager_runtimeInit`,
the six type-specific `*_initVoice()` functions.

**Storage impact**: none. This is ordering only.

### 2. Split loaded-slot application into image application and graph binding

**Implementation note (completed)**: the former combined
`preset_applyKitVoice()` is now a reset-and-image helper. It resets one tagged
member, applies routing, and applies its Morph descriptor image only. The
existing `preset_applyKitVoiceSupplemental()` remains the dedicated graph
binding helper and is intentionally invoked only by a later full-source pass.

**Change**: Refactor the current private `preset_applyKitVoice()` in
`presetManager.c` into explicit internal operations:

1. reset one slot's tagged runtime member from the active Scene's stored type;
2. apply that slot's routing and Morph-derived descriptor image; and
3. normalize/install its non-image modulation bindings.

The existing `preset_applyKitVoiceSupplemental()` remains the owner of step 3;
it must not run until the lifecycle selects the graph-binding phase.

**What it does**: separates writes that only need the source slot's fresh
runtime member from LFO/velocity target installation, which may need any other
slot's fresh runtime member.

**Why it must exist**: an LFO source can target any voice and a target token is
descriptor-local to that selected target type. Installing source-slot LFO
bindings while later slots are still being reset can bind against a stale
union type/member. Applying every image first removes this order dependence.

**Inputs**: active Scene index; source slot; its `kit_instrument_slot_t`; the
existing descriptor registry; Scene routing; Morph endpoint images.

**Outputs**: a reset, type-correct tagged member with its normal audible
parameter image, followed later by fully normalized source LFO/velocity
bindings.

**Code affiliates**: `presetManager.c:preset_applyKitVoice`,
`preset_applyKitAudioRouting`, `presetMorph_applyVoiceNow`,
`preset_applyKitVoiceSupplemental`, `preset_normalizeSlotModulationTargets`,
`InstrumentManager.c:instrumentManager_resetRuntimeSlot` and
`instrumentManager_writeRuntime`.

**Storage impact**: none. These are private function/loop boundaries; they
reuse existing SceneData and tagged slots.

### 3. Make boot apply a complete three-phase active-Scene transaction

**Implementation note (revised after hardware check)**:
`preset_sendDrumsetParameters()` now clears the old graph, applies Scene
settings, reset/image-applies all six slots, and drains pending Morph writes
as a safe temporary pre-audio image. It deliberately does not install target
bindings. Immediately after `audioCodec_init()`, `main.c` calls
`preset_startDrumsetApply()`, so boot reruns the complete existing Scene-switch
worker—clear, six-slot image commit, then all-source rebind—after audio is
live. The prior partial rebind still did not match the working manual Scene
path. This adds no boot-only cursor or cache.

**Change**: Rework `preset_sendDrumsetParameters()` as the synchronous,
pre-audio implementation of the shared transaction below. It must not remain
a separate abbreviated apply path.

1. **Prepare**: ensure Morph state is initialized, clear all existing runtime
   modulation destinations through
   `instrumentManager_clearAllRuntimeModulationTargets()`, then apply active
   Scene-wide settings.
2. **Reset and image-apply all six slots**: for each slot 0..5, read the active
   Scene's actual stored type, reset the matching tagged union member, apply
   audio routing, and apply all Morph-derived descriptor values. No LFO or
   velocity target may be installed in this pass.
3. **Settle Morph writes**: drain any existing pending Morph descriptors from
   the loaded active Scene before a target adapter snapshots its base.
4. **Replay the complete Scene worker after audio starts**: immediately after
   DMA/I2S startup call `preset_startDrumsetApply()`. Its existing clear,
   six-slot commit, and all-source rebind performs velocity and both LFO target
   pairs only after the complete target set is type-correct and live. Reuse the
   existing Instrument and Scene apply cursors; do not maintain a boot-only
   binding loop.

The current synchronous Morph drain remains only if required after these
passes; it must never reapply stale descriptors or install target bindings
against a pre-reset member.

**What it does**: produces an initial live runtime type vector and modulation
graph from the fully loaded active Scene before audio starts.

**Why it must exist**: all six Bank children can load while the currently live
runtime is still the bootstrap/default state. The Bank's final active Scene
must take ownership of every live slot in one deterministic transaction; it is
not enough to parse its retained data successfully.

**Inputs**: final Bank-selected `scene_getActiveIndex()`; the active Scene's
six stored slots/settings; existing Morph engine; existing modulation target
records.

**Outputs**: six matching runtime types, six fully applied parameter images,
six routing values, and a graph whose source/target pairs resolve against
those same six members.

**Code affiliates**: `Menu/menu.c:menu_startSoundApply` pre-audio branch,
`presetManager.c:preset_sendDrumsetParameters`,
`preset_applySceneSettings`, `instrumentManager_clearAllRuntimeModulationTargets`,
`presetMorph_applyVoiceNow`, and the InstrumentManager runtime dispatchers.

**Storage impact**: none. Fixed six-slot loops use existing objects and no
new retained cursor/state.

### 4. Give deferred Scene switching the same final rebind phase

**Implementation note (completed)**: when the existing deferred pending mask
reaches zero, it starts the existing `instrument_apply_*` target-rebind phase
instead of declaring Scene apply complete. `preset_tickDrumsetApply()` keeps
the existing Scene gate active while it advances that cursor. Trigger-time
replacement likewise applies only the image immediately and starts the same
rebind when it finishes the last pending slot. No apply-state field was added.

**Implementation verification (completed)**: `preset_tickDrumsetApply()` owns
the existing Scene apply gate until the shared rebind cursor drains. Therefore
`menu_tickSoundApply()` cannot finish a successful fully-applied Scene while a
cross-slot target pair still refers to an incomplete type layout.

**Change**: Retain the existing quiet-envelope / trigger-time slot replacement
behavior in `preset_startDrumsetApply()` and `preset_tickDrumsetApply()`, but
defer the all-source supplemental rebind until its pending slot mask is empty.
Use the existing `instrument_apply_active`, `instrument_apply_scene`,
`instrument_apply_phase`, and `instrument_apply_rebind_source` state machine
for this final target-rebind phase after confirming its existing request
serialization prevents overlap with Instrument Load.

**What it does**: the runtime path resets and image-applies a Scene slot only
when it is safe, then performs the same all-source LFO/velocity binding pass
as boot once every target runtime member is valid.

**Why it must exist**: fixing boot only would leave two different definitions
of a "loaded Scene." The current Instrument Load transaction already uses this
Morph-then-all-source-rebind model; Scene/Bank activation must use it too so a
later Scene switch cannot recreate stale cross-slot LFO associations.

**Inputs**: existing `drumset_apply_*` pending-mask state; existing
`instrument_apply_*` rebind cursor; active Scene identity; trigger-time force
apply requests.

**Outputs**: deferred replacement stays non-blocking and audio-safe, while the
final modulation graph is complete only after all live tagged types are valid.
During the short handoff, an LFO target may be intentionally detached rather
than pointing into a stale member.

**Code affiliates**: `presetManager.c:preset_startDrumsetApply`,
`preset_tickDrumsetApply`, `preset_applyDeferredSceneSlotForTrigger`,
`preset_tickInstrumentApply`, and `Menu/menu.c:menu_tickSoundApply` /
`menu_tickInstrumentApply` completion gating.

**Storage impact**: no new state is allowed. The implementation must reuse the
existing cursors named above; if that is not sufficient, stop and request
explicit SRAM approval with exact bytes, region, owner, and lifetime.

### 5. Preserve and verify typed LFO/velocity parsing and installation

**Change**: Do not add a second parser or translate LFO target tokens into
fixed physical-voice parameter IDs. Retain the current storage parse into the
source Instrument's parameter image, then make the rebind phase the sole
place that normalizes and installs target pairs after type construction.

For each source slot, process both pairs in this order:

1. read the source type's `lfo_target_voice` / `lfo_target_param` cells;
2. validate the target voice (`self`, voices 1..6, or Scene namespace);
3. interpret the paired parameter token in the selected target type's
   descriptor namespace; invalid or unsupported combinations become explicit
   `INSTRUMENT_TARGET_TOKEN_OFF`;
4. install the resolved descriptor, slot-decimation, or Scene adapter; and
5. repeat for the `_2` pair, then apply the source velocity target.

**What it does**: keeps LFO files portable across arbitrary type arrangements
while ensuring target descriptors are resolved against the final live/stored
type of the target slot.

**Why it must exist**: descriptor index 12 has different meanings across
Instrument types. An LFO assignment is valid only as the pair of its selected
target voice and that type's local token. Parsing correctly but binding before
target type reset is not sufficient.

**Inputs**: `storage_instrumentParseLine` / `storage_instrumentFinalize`
results; `preset_normalizeLfoTargetPair`;
`instrumentManager_lfoTargetIdFromToken`; both InstrumentManager LFO adapter
tables; velocity target cells.

**Outputs**: parsed source cells remain the persisted authority; live LFO and
velocity bindings are either valid for the active type layout or cleanly off.

**Code affiliates**: `storageTypes.c`, `filesystem.c` staged Instrument
commit, `presetManager.c:preset_normalizeLfoTargetPair`,
`preset_applyKitVoiceSupplemental`, and
`InstrumentManager.c:instrumentManager_installLfoModulationTarget`,
`instrumentManager_installVelocityModulationTarget`, and runtime LFO dispatch.

**Storage impact**: none. Reuse the retained byte tokens and the existing
InstrumentManager installation records; do not introduce per-Bank target
caches.

### 6. Preserve active-Scene selection and loader ownership

**Change**: Do not move Bank payload parsing or type selection into Menu.
`filesystem_loadBankDirectory_tick()` remains responsible for parsing and
committing all selected Scene payloads, then selecting the validated Bank
active Scene. Menu/Preset remains responsible only for activating that
already-selected retained Scene in DSP runtime.

**What it does**: keeps the data transaction (filesystem/staging) separate
from the audio transaction (Preset/InstrumentManager), while giving Bank,
root Scene, Kit, and future autosave callers one common active-Scene runtime
activation boundary.

**Why it must exist**: coupling typed parsing to runtime dispatch risks partial
live type replacement while a Bank child is still unvalidated. The selected
Bank active Scene must be completely committed before its runtime is built.

**Inputs**: committed `SceneData`; active Scene index selected by BankData;
normal Preset completion status.

**Outputs**: a single, fully committed active Scene presented to the shared
activation transaction.

**Code affiliates**: `filesystem.c:filesystem_loadBankDirectory_tick`,
`SceneData.c:scene_selectActive`, `BankData.c`, `presetManager.c`, and
`Menu/menu.c:menu_pollPresetStatus`.

**Storage impact**: none.

## Implementation order

1. Add temporary development-only verification first (below); do not alter
   persistence or allocate diagnostic storage.
2. Correct the SceneData-before-DSP initialization order.
3. Split the private per-slot apply helper and implement the synchronous
   prepare → all-images → all-rebind boot transaction.
4. Convert the deferred Scene switch to the same final rebind phase using only
   existing cursor state, and audit Menu completion gating so it does not
   release a transaction before the rebind completes.
5. Run the complete type/LFO matrix, then remove temporary diagnostics and
   update MEMORY/specification references only after hardware confirmation.

## Verification matrix

Development diagnostics (temporary and storage-free) must report the active
Scene index plus, for slots 1..6, the stored Scene type and
`instrumentManager_runtimeType()` at these points: after bootstrap, after
Bank/Scene payload commit, after every reset/image pass, and after rebind.

| Test | Required result |
|---|---|
| Cold boot `000 Full`, local Scene 06 | stored and live types both `drm, drm, drm, snr, cym, hat`; Snare mix/noise control the Snare; expected LFOs run |
| Switch away and back | same stored/live vector and audible result as cold boot |
| Root Scene Load | its selected destination's live types/parameters match its retained slots |
| Bank Load at runtime | quiet/trigger-time handoff never writes a descriptor through an outgoing type |
| Nonstandard fixture | arbitrary valid six-slot arrangement, including repeated types and a Snare outside slot 4, matches exactly after boot and switch |
| Cross-type LFO pair 1 and pair 2 | target token resolves in the selected target type's descriptor namespace; no stale or wrong-owner modulation |
| Velocity target / slot decimation / Scene target | valid targets apply after rebind; invalid pairs become off without corrupting a runtime member |
| No SD card / invalid payload | default Scene types initialize correctly; no untyped or out-of-range runtime dispatch |

Build verification remains `make -j2`, `make img`, and `git diff --check`.
The implementation build passed `make -j2`, `make img`, and
`git diff --check`; normal pre-existing newlib syscall and LTO-serial warnings
remain. The linked implementation reports 404 bytes of `.data` and 69,940
bytes of `.bss`; the clean detached baseline reports 404 and 69,948 bytes,
respectively, so this repair consumes no additional retained SRAM. Hardware
success is required before this plan is marked complete.

### Follow-up LFO audit (no code change)

The active `06 Slak` fixture has two nonzero LFO source configurations. Slot 2
(`docwird2.drm`) has rate 26, amount 115, sync 8, retrigger voice 2, and the
file-only `self` destination resolves during parsing to slot 2 / local
descriptor 1 (`osc1_pitch_coarse`). Slot 3 (`docwird3.drm`) has rate 28,
amount 120, and resolves its `voice=2, param=12` pair to slot 2's
`amp_envelope_decay`. The boot transaction parses these compact values, first
constructs all six runtime types/images, then normalizes and installs both
source pairs through `preset_applyKitVoiceSupplemental()`; this is the same
installer used by the deferred Scene rebind.

Hardware established that neither LFO target applies after cold boot, while
both apply after a manual Scene round-trip and a manual target edit. The
partial post-audio rebind did not resolve this, proving that reusing only its
last phase was still not the same operation. The revised boot path therefore
runs the complete existing Scene-switch worker after audio starts. The phase
note remains applicable: slot 2's stored offset 101 becomes active when
visible track 2 triggers.

## Completion criteria

The selected initial Scene produces the same stored/live type vector,
descriptor behavior, routing, and LFO/velocity behavior as a manual return to
that Scene. The result holds for any valid Instrument arrangement without new
SRAM use.
