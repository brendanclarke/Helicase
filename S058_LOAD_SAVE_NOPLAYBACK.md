# Session 058 — top-level Load/Save while playback is stopped

## Purpose and implementation status

This is the complete implementation plan for accelerating selected top-level
Load/Save commands whenever sequencer playback is stopped. Implementation is
in progress; the notes below record the applied source changes and remaining
verification.

The feature has two coordinated runtime effects:

1. suspend codec hardware and stop foreground DSP rendering while an eligible
   accepted command owns the Load/Save UI and transport is stopped;
2. use the released foreground/interrupt budget to call `afatfs_poll()` four
   times, rather than once, during each busy `filesystem_tick()` pass.

The plan is grounded against the current working source, including the
uncommitted Session 058 Bank traversal/HCNAMES work already in the tree. Those
changes are user work and must be preserved.

## Implementation notes

- 2026-08-29: The user's implementation request is treated as explicit
  approval of the one-byte `fs_fast_drain_active` SRAM1 allocation described by
  the approval gate. Existing user changes in the working tree are preserved.
- 2026-08-29: Added the codec suspension observer, filesystem fast-drain
  selector/accessor and four-poll busy drain, plus Menu's explicit eligible
  command predicate and bidirectional stop/start lifecycle. Added the renderer
  early return while the codec is suspended. Build and size verification remain.
- 2026-08-29: A clean build exposed an existing dirty-tree Bank Save prototype
  mismatch: `filesystem_requestSaveBank()` already had the new `force_save`
  implementation/call site but its header declaration had not been updated.
  Synchronized that declaration and its contract so the current user changes
  can compile; this is unrelated to S058 runtime behavior.
- 2026-08-29: `git diff --check` passed. `make clean && make && make img`
  completed successfully with `text=382,412`, `data=404`, `bss=96,160`; the
  generated image is 382,816 bytes. `arm-none-eabi-nm -S --size-sort` confirms
  `fs_fast_drain_active` is one byte in BSS. Existing project warnings remain
  in unrelated unused helpers and embedded libc stubs.
- 2026-08-29: Hardware validation is still pending: stopped/running command
  transitions, four-poll timing, audio/DSP freeze and resume, exclusion cases,
  and final Scene/Bank index cleanup require the target device and test cards.
- 2026-08-29: The hardware failure analysis below supersedes the earlier
  implementation verdict. The stopped Bank Save failure was traced to the
  lower SD shim's read-token and write-busy poll-count deadlines, which expired
  too quickly after codec suspension and four-pass draining. The S058 Menu,
  codec, and filesystem fast-drain behavior remains unchanged.
- 2026-08-29: Implemented the authoritative fifteen-site correction: the
  existing SD retry counter is now a two-byte TIM6 wait-start timestamp, both
  response waits use wrapping elapsed-millisecond deadlines, all wait exits
  clear ownership before callbacks, and transport diagnostics publish
  state-conditional `wait_ms` without changing capsule geometry. Schema 2 is
  emitted while both schema 1 and schema 2 remain decodable in both host tools.
- 2026-08-29: Synthetic schema tests passed for schemas 1 and 2, including the
  E7 label distinction, and schema 3 correctly uses raw fallback. A clean
  `make clean && make && make img` passed; final linked size is
  `text=382,508`, `data=404`, `bss=96,160`, and `wait_started_tick` is exactly
  two bytes in BSS. Target-card retest and fault-injected timing remain pending.
- 2026-08-29: A temporary logging-off clean build also passed with
  `text=374,844`, `data=408`, `bss=79,664`; the final configuration was restored
  to `DEV_MODE_LOGGING=1` and rebuilt. The image tool reports the 382,912-byte
  binary payload; the 382,928-byte `.img` includes its fixed 16-byte wrapper.

## Complete file inventory

Only these six source files need implementation changes:

| File | Required change |
| --- | --- |
| `Core/Hardware/AudioCodecManager.h` | Document suspend/resume state and declare `audioCodec_isSuspended()` |
| `Core/Hardware/AudioCodecManager.c` | Document the existing state owner and define the accessor |
| `Core/Hardware/SD/filesystem.h` | Declare/document the fast-drain setter and accessor |
| `Core/Hardware/SD/filesystem.c` | Add the four-pass policy, one-byte state, setter/accessor, and bounded busy-poll loop |
| `Core/Menu/menu.c` | Own eligibility and enter/update/leave lifecycle; hook command begin, Menu poll, and command finish |
| `main.c` | Return from the renderer while codec hardware is suspended |

No code change is required in `menu.h`, `sequencer.c/.h`,
`presetManager.c/.h`, `BankData.c/.h`, any Scene/Kit/Bank `_tick()` state
machine, AsyncFATFS, the SD driver, `config.h`, or the Makefile.

### Code-site checklist

Line numbers will move with the current dirty worktree, so implementation must
use these stable symbol/section anchors rather than stale numeric locations:

| # | File and anchor | Exact edit at that place |
| ---: | --- | --- |
| 1 | `AudioCodecManager.h`, current modal suspend/resume declaration block | Replace its public contract and add the accessor declaration |
| 2 | `AudioCodecManager.c`, `audio_hw_suspended` | Add the state-owner/lifetime/readers contract; do not add state |
| 3 | `AudioCodecManager.c`, immediately before `audioCodec_suspend()` | Add the accessor definition and implementation contract |
| 4 | `filesystem.h`, beside `filesystem_tick()`/status declarations | Add the setter/accessor public contract and declarations |
| 5 | `filesystem.c`, private scheduling constants | Add `FS_FAST_DRAIN_POLL_PASSES` and its tuning/affiliate contract |
| 6 | `filesystem.c`, beside `fs_last_idle_poll_tick` and facade scheduler state | Add/document `fs_fast_drain_active` |
| 7 | `filesystem.c`, facade controls near `filesystem_tick()`/`filesystem_status()` | Define/document setter and accessor |
| 8 | `filesystem.c`, busy/non-ready polling branch in `filesystem_tick()` | Replace one poll with the bounded mode-selected loop and expanded invariant comment |
| 9 | `menu.c`, `menu_loadSaveCommandActive` declaration | Extend ownership/exclusion documentation and add early helper prototypes |
| 10 | `menu.c`, `menu_beginLoadSaveCommand()` | Invoke the updater after both ownership flags are set and before repaint |
| 11 | `menu.c`, `menu_finishLoadSaveCommand()` | Invoke ordered exit after the active guard and before clearing flags |
| 12 | `menu.c`, after Instrument state and `menu_saveOptions` declarations | Define/document predicate, entry, exit, and bidirectional updater |
| 13 | `menu.c`, start of `menu_pollPresetStatus()` | Invoke updater before every early-return-capable worker |
| 14 | `main.c`, start of `audio_check_and_render()` | Add suspended early return before queue, trigger, or DSP work |

There is deliberately no fifteenth edit in the top-level `btnClicked` switch:
centralizing initial reconciliation in `menu_beginLoadSaveCommand()` covers the
accepted request exactly once and also proves exclusions for its other callers.

## Approval gate: one retained byte

The proposed `fs_fast_drain_active` is a new persistent allocation. The earlier
draft incorrectly called this “no new retained RAM.” Before implementation the
user must explicitly approve:

| Property | Exact contract |
| --- | --- |
| Size | 1 byte at source level (`static uint8_t`) |
| Region | normal SRAM1 `.bss` |
| Owner | filesystem facade (`filesystem.c`) |
| Lifetime | entire process; meaningful only during an eligible accepted command |
| Contents | normalized boolean: zero = one poll; one = four busy polls |
| Purpose | pair Menu-owned codec suspension with faster filesystem draining |
| Release | every transition to running playback and every command terminal path |

Linker padding may absorb the byte, but that does not exempt it from the RAM
policy. No DTCM, DMA memory, cache, payload stage, name storage, or logging-only
storage is added. The two automatic `uint8_t` loop values are expected to
remain registers and are not a material stack increase; final assembly and
linked sections must still be checked.

## Behavioral contract

The feature is active only while all four facts are true:

- `menu_loadSaveCommandActive` owns an accepted OK/OW command;
- the command belongs to the eligible top-level set below;
- the workflow is not nested Instrument Load/Save;
- `seq_isRunning()` reports stopped.

While active:

- `audioCodec_suspend()` disables I2S, DMA IRQ activity, and PLLI2S and clears
  the render queue;
- `audio_check_and_render()` returns before `voiceControl_processPending()` or
  `mixer_calcNextSampleBlock()`;
- the busy/non-ready branch of `filesystem_tick()` invokes `afatfs_poll()` four
  times;
- the filesystem state-machine switch still dispatches exactly one `_tick()`
  per `filesystem_tick()`;
- all other foreground services, TIM3 sequencing, MIDI, buttons, LEDs, LCD,
  and the filesystem watchdog feed keep their current cadence.

The condition is dynamic:

- stopped at acceptance: enter immediately;
- running at acceptance: remain online/one-poll;
- running then stopped: enter on the next Menu poll;
- stopped then started: clear fast drain and resume audio on the next Menu poll;
- repeated stop/start changes: repeat the idempotent transition;
- success or error: leave before accepted-command ownership is released.

This resolves the old draft's “start during operation” open decision. Existing
button/MIDI/TIM3 owners apply START immediately. Staying offline afterward
would allow sequencer time and pending triggers to advance without DSP/audio
service, so S058 must resume instead.

## Exact scope

### Eligible commands

| Page | Type | Accepted request |
| --- | --- | --- |
| Save | `SAVE_TYPE_KIT` | `preset_saveDrumset(..., 0u, source_scene)` |
| Save | `SAVE_TYPE_KIT_MORPH` | `preset_saveDrumset(..., 1u, source_scene)` |
| Save | `SAVE_TYPE_SCENE` | `preset_saveScene(slot, source_scene)` |
| Save | `SAVE_TYPE_BANK` | `preset_saveBank(slot, scene_mask, 0u)` |
| Load | `SAVE_TYPE_SCENE` | `preset_loadSceneForScenes(slot, scene_mask)` |
| Load | `SAVE_TYPE_BANK` | `preset_loadBank(slot, scene_mask)` |

### Explicit exclusions

| Workflow | Exclusion proof |
| --- | --- |
| Load Kit / KitMrp | Browser-selection request, not accepted OK/OW ownership |
| Instrument Load / InstrumentMrp | Nested VOICE flow; `menu_instrumentLoadActive != 0` |
| Instrument Save / InstrumentMrp | Nested VOICE flow; it calls command-begin and often has `what == SAVE_TYPE_KIT`, so the explicit nested guard is mandatory |
| Globals | Small settings operation; type excluded |
| Samples/Loops | Existing synchronous modal owns transport, suspend/resume, flash work, and blocking pump; type excluded |
| File/Dir/sDir | Retired compatibility types; excluded |
| Boot, AutoSave, trace, settings writer | No accepted runtime OK/OW owner |

The promoted arrays currently expose Kit, KitMrp, Scene, and Bank only, but
latent switch cases remain. The predicate must stay explicit rather than rely
on current reachability.

## Invariants to preserve

### Audio

`audioCodec_suspend()` and `audioCodec_resume()` are idempotent but not
reference-counted. Suspend stops hardware and clears the queue before setting
`audio_hw_suspended`; resume clears buffers, rebuilds hardware, then clears it.
The Samples modal is the only current independent suspend client.

Menu may claim suspension only when both fast-drain and codec-suspended states
are false. After the void-returning suspend call, it must verify suspension
before enabling fast drain. This prevents acceleration while audio stays live
and prevents later resuming a foreign suspension.

### Filesystem

`filesystem_tick()` currently feeds the optional IWDG, checks the cooperative
deadline, polls AsyncFATFS, offers an idle facade to background schedulers, and
dispatches one busy operation tick. S058 changes only the busy/non-ready lower-
layer poll count. It must not:

- dispatch the operation switch four times;
- multiply IWDG/deadline/scheduler work;
- accelerate idle polling;
- create a second polling context or ISR caller;
- alter `filesystem_blockPoll()`, `SDCARD_BURST_SIZE`, or AsyncFATFS/SD code;
- reinterpret polls as elapsed time or restore a Bank total-duration watchdog.

Each SD poll clocks at most the existing 16-byte burst. Four polls therefore
give a 64-byte/pass ceiling, reducing a sector from 32 to eight foreground
passes when every poll reaches SD. Cache/FAT state can produce no burst, so
this is a maximum, not a guaranteed transfer amount.

### Accepted-command lifecycle

`menu_beginLoadSaveCommand()` is used by top-level OK/OW, nested Instrument
Save, and Samples. `menu_finishLoadSaveCommand()` is the shared terminal owner
for save/load completion, failure, apply, modal, final-index callback, and
defensive cleanup.

Entry belongs inside command-begin after ownership is set; the exact predicate
excludes the other callers. Exit belongs inside command-finish before ownership
is cleared. This covers all existing terminal paths without duplicating hooks
through the large completion switch.

## Implementation specification and adjacent comment blocks

### 1. `Core/Hardware/AudioCodecManager.h`

Replace the short suspend/resume comment and add the accessor declaration:

```c
/*
 * Suspend, resume, and observe the complete external-codec hardware path.
 *
 * What: suspend stops I2S, both DMA streams/IRQs, and PLLI2S and clears the
 * render queue; resume zeroes DMA/render buffers, rebuilds hardware, and starts
 * with an empty queue. Both mutators are idempotent, void-returning, and not
 * reference-counted. audioCodec_isSuspended() reads the existing hardware flag.
 *
 * Why: sample installation and eligible stopped-transport Load/Save must
 * remove audio ISR/render cost, while main and Menu need one authoritative
 * observation instead of duplicate state.
 *
 * Inputs: none; the caller must own the foreground audio lifecycle. Outputs:
 * isSuspended returns 1 only after suspend completes and until resume completes;
 * it returns 0 before audioCodec_init(). No function changes transport.
 *
 * Affiliates: menu.c's sample and no-playback storage owners, main.c's render
 * guard, audio_hw_suspended, and Menu-paired filesystem fast drain.
 */
void audioCodec_suspend(void);
void audioCodec_resume(void);
uint8_t audioCodec_isSuspended(void);
```

Input: none. Output: normalized suspension state. Side effects from accessor:
none. Clients: `main.c` and `menu.c`. No include change is needed.

### 2. `Core/Hardware/AudioCodecManager.c`

#### 2.1 Document the existing state owner

Do not add another audio byte. Add this adjacent contract:

```c
/*
 * Authoritative codec-hardware suspension state; no S058 allocation.
 *
 * Writers: init/resume clear it only after hardware is live; suspend sets it
 * only after I2S/DMA/PLLI2S stop and queue reset. Readers:
 * audioCodec_isSuspended(), main's renderer, and Menu storage ownership.
 * Volatile preserves observation across the foreground/IRQ hardware boundary.
 * This is a boolean, not a reference count; callers must not resume a state
 * they do not own.
 */
static volatile uint8_t audio_hw_suspended = 0u;
```

#### 2.2 Define the accessor beside suspend/resume

```c
/*
 * Report whether the codec manager completed a hardware suspend.
 *
 * What/why: expose audio_hw_suspended so foreground clients can suppress DSP
 * and verify ownership without duplicating state. Input: none. Output: 0 before
 * init/while live, 1 after suspend until resume completes. Side effects: none;
 * no IRQ, register, queue, or transport change. Affiliates: suspend/resume,
 * main.c audio_check_and_render(), and Menu's no-playback lifecycle.
 */
uint8_t audioCodec_isSuspended(void)
{
    return audio_hw_suspended;
}
```

No suspend/resume logic, register order, clearing, or IRQ masking changes.

### 3. `Core/Hardware/SD/filesystem.h`

Place the public control beside `filesystem_tick()`/status:

```c
/*
 * Select and observe the bounded runtime fast-drain policy.
 *
 * What: setFastDrain normalizes `on` to one facade-owned boolean. While set,
 * the BUSY/non-READY branch of filesystem_tick() performs the private fast
 * poll count instead of one. fastDrainActive returns that boolean.
 *
 * Why: after Menu suspends codec DMA/ISR/DSP work, released foreground time
 * can advance bit-banged SD while preserving AsyncFATFS's one-context rule and
 * the operation state machine's once-per-tick cadence.
 *
 * Inputs: zero selects ordinary drain; nonzero selects fast drain. Outputs:
 * accessor returns 0/1. Neither call polls, starts/acks an operation, changes
 * status, or invokes a callback. Foreground-only and not reference-counted.
 *
 * Ownership: Menu is the sole setter; it enables only after verified codec
 * suspend and clears before resume. Affiliates: filesystem_tick(), Menu's
 * no-playback helpers, and AudioCodecManager's suspended accessor.
 */
void    filesystem_setFastDrain(uint8_t on);
uint8_t filesystem_fastDrainActive(void);
```

No private AsyncFATFS type or pass constant is exposed.

### 4. `Core/Hardware/SD/filesystem.c`

#### 4.1 Add the private pass constant

Keep it with filesystem scheduling constants, not `config.h`:

```c
/*
 * Lower-layer polls per busy facade pass while Menu proves that an eligible
 * stopped-playback command owns a suspended codec.
 *
 * Input: fs_fast_drain_active selects this instead of one. Output: at most four
 * afatfs_poll() calls before one operation-state-machine tick. Four gives an
 * initial 64-byte SD-burst ceiling without changing driver/idle/callback
 * behavior. Tune only from hardware evidence. Affiliates: filesystem_tick(),
 * SDCARD_BURST_SIZE, and Menu's no-playback lifecycle.
 */
#define FS_FAST_DRAIN_POLL_PASSES 4u
```

#### 4.2 Add the retained state beside facade scheduling state

```c
/*
 * Runtime fast-drain selector (+1 byte normal SRAM1 .bss; approval required).
 *
 * Writer: filesystem_setFastDrain(), called only by Menu after verified codec
 * suspend and before resume. Readers: filesystem_tick() chooses one/four polls;
 * the accessor makes Menu transitions idempotent. Lifetime: process-wide,
 * meaningful only during one eligible accepted command. It owns no payload,
 * FAT handle, name, callback, or transport state and is not volatile because
 * all access is foreground-only.
 */
static uint8_t fs_fast_drain_active = 0u;
```

#### 4.3 Define the setter

```c
/*
 * Normalize the foreground filesystem drain policy.
 *
 * Input: zero for one poll, nonzero for bounded fast drain. Output: only the
 * selector changes; no SD/FAT work, status transition, callback, or scheduler
 * admission occurs. Why: Menu pairs audio and storage without teaching
 * AsyncFATFS about UI/transport. Affiliates: accessor, filesystem_tick(), and
 * Menu's begin/end helpers.
 */
void filesystem_setFastDrain(uint8_t on)
{
    fs_fast_drain_active = on ? 1u : 0u;
}
```

#### 4.4 Define the accessor

```c
/*
 * Read normalized fast-drain ownership.
 *
 * Input: none. Output: 0 ordinary or 1 fast. Side effects: none. Why: this is
 * Menu's sole proof that S058—not Samples—owns the matching codec suspension
 * and may resume it. Affiliates: setter, filesystem_tick(), and Menu lifecycle.
 */
uint8_t filesystem_fastDrainActive(void)
{
    return fs_fast_drain_active;
}
```

#### 4.5 Replace the one busy poll with a bounded loop

Only the busy/non-ready branch changes; idle/scheduling/switch behavior stays:

```c
/*
 * Pump the one AsyncFATFS context with a mode-dependent lower-layer budget.
 *
 * Inputs: facade BUSY, AsyncFATFS readiness, and fast-drain state. Output: one
 * ordinary poll or four consecutive polls during eligible stopped playback.
 * Why: turn released audio CPU/priority-4 ISR time into bit-banged SD progress
 * without moving FAT work into an ISR.
 *
 * Only AsyncFATFS/SD internals repeat. The `_tick()` switch below still runs
 * once, so phase-stall cadence, callbacks, watchdog/deadline, autonomous
 * admission, and idle rate limiting are not multiplied. Affiliates: private
 * pass constant, setter, operation switch, and sdcard_poll().
 */
if (status == FS_STATUS_BUSY ||
    afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY) {
    uint8_t passes = fs_fast_drain_active
        ? FS_FAST_DRAIN_POLL_PASSES : 1u;
    uint8_t pass;

    for (pass = 0u; pass < passes; pass++)
        afatfs_poll();
} else {
    /* Existing idle-rate-limited branch unchanged. */
}
```

Constraints:

- compute `passes` once per facade tick;
- do not early-break merely because readiness changes during the four calls;
- do not call Menu/Sequencer/AudioCodecManager from filesystem;
- do not change `filesystem_blockPoll()` or add instrumentation state;
- do not multiply the operation switch or stall detectors.

### 5. `Core/Menu/menu.c`

Menu owns policy because it alone knows accepted command identity, nested
workflow state, and transport. Filesystem owns mechanism; codec owns hardware.

#### 5.1 Extend command-state documentation and add early prototypes

The existing command byte is not widened or repurposed:

```c
/*
 * Accepted OK/OW presentation/ownership state (existing Menu byte; no S058
 * allocation). Inputs: accepted requests only. Outputs: `...`, cursor/input
 * gating, and terminal reset. S058 uses it as one input to a derived predicate;
 * eligibility and codec ownership are not packed here because Instrument and
 * Samples share this lifecycle but are excluded. Affiliates: command begin/
 * finish, menu_updateNoPlaybackStorage(), and all completion paths.
 */
static uint8_t menu_loadSaveCommandActive = 0u;
```

Before the early begin/finish definitions add:

```c
static void menu_updateNoPlaybackStorage(void);
static void menu_endNoPlaybackStorage(void);
```

Full definitions belong after `menu_instrumentLoadActive`,
`menu_instrumentSaveMode`, and `menu_saveOptions` declarations. Do not move
existing state merely to satisfy lexical visibility.

#### 5.2 Add the exact predicate

```c
/*
 * Classify the accepted command for stopped-playback acceleration.
 *
 * Inputs: active page, SAVE_TYPE, and nested Instrument-session flag; command
 * and transport are checked by the caller. Output: 1 only for top-level Save
 * Kit/KitMrp/Scene/Bank or Load Scene/Bank; otherwise 0.
 *
 * Why the Instrument guard is mandatory: nested Instrument Save also begins
 * an accepted command while entry commonly resets `what` to SAVE_TYPE_KIT.
 * Page/type alone would misclassify it. Affiliates: policy updater, top-level
 * dispatch, and menu_instrumentSaveRequestSelection().
 */
static uint8_t menu_loadSaveCommandInNoPlaybackScope(void)
{
    if (menu_instrumentLoadActive)
        return 0u;
    if (menu_activePage == SAVE_PAGE) {
        return (uint8_t)(menu_saveOptions.what == SAVE_TYPE_KIT ||
                         menu_saveOptions.what == SAVE_TYPE_KIT_MORPH ||
                         menu_saveOptions.what == SAVE_TYPE_SCENE ||
                         menu_saveOptions.what == SAVE_TYPE_BANK);
    }
    if (menu_activePage == LOAD_PAGE) {
        return (uint8_t)(menu_saveOptions.what == SAVE_TYPE_SCENE ||
                         menu_saveOptions.what == SAVE_TYPE_BANK);
    }
    return 0u;
}
```

Unexpected pages fail closed; do not use final `else` as “Load.”

#### 5.3 Add idempotent entry

```c
/*
 * Enter codec-offline/fast-filesystem state for one eligible command.
 *
 * Inputs: both ownership states must be clear. Output: suspend codec first;
 * only after the authoritative accessor confirms it is fast drain enabled.
 * Existing/failed suspension changes no ownership and schedules no resume.
 *
 * Why: enabling fast drain while audio is live competes with audio; claiming a
 * foreign suspension would later resume hardware Menu did not stop. Transport
 * is unchanged and no Menu state is allocated. Affiliates: codec manager,
 * filesystem setter, Samples' independent owner, and the exit helper.
 */
static void menu_beginNoPlaybackStorage(void)
{
    if (filesystem_fastDrainActive() || audioCodec_isSuspended())
        return;
    audioCodec_suspend();
    if (!audioCodec_isSuspended())
        return;
    filesystem_setFastDrain(1u);
}
```

#### 5.4 Add ordered exit

```c
/*
 * Leave only S058-owned offline storage state.
 *
 * Input: fastDrainActive is the ownership token; bare codec suspension is not,
 * because Samples has an independent owner. Output: clear fast drain before
 * restarting codec DMA/I2S/PLL; resume only if still suspended. Repeated calls
 * no-op. Why: ordinary scheduling must return before audio demand, and foreign
 * owners must not be resumed. Transport is unchanged. Affiliates: command
 * finish, dynamic restart, entry helper, and Samples.
 */
static void menu_endNoPlaybackStorage(void)
{
    if (!filesystem_fastDrainActive())
        return;
    filesystem_setFastDrain(0u);
    if (audioCodec_isSuspended())
        audioCodec_resume();
}
```

#### 5.5 Add bidirectional updater

```c
/*
 * Reconcile command, scope, and transport every Menu pass.
 *
 * Inputs: command-active, exact predicate, seq_isRunning(), and subsystem
 * states. Output: stopped eligible commands enter; running/ineligible/ended
 * commands leave. Stop/start/stop works without a latch or Sequencer change.
 *
 * Why: transport can change through front panel or MIDI/TIM3 while Menu input
 * is storage-gated. A start must restore audio rather than advance sequencing
 * against frozen DSP. Affiliates: command begin, Menu poll, command finish,
 * and seq_isRunning().
 */
static void menu_updateNoPlaybackStorage(void)
{
    if (!menu_loadSaveCommandActive ||
        !menu_loadSaveCommandInNoPlaybackScope() ||
        seq_isRunning()) {
        menu_endNoPlaybackStorage();
        return;
    }
    menu_beginNoPlaybackStorage();
}
```

No `seq_setRunning()` call is added; S058 follows transport, never owns it.

#### 5.6 Hook accepted-command entry

Inside `menu_beginLoadSaveCommand()`, after setting both existing flags and
before repaint:

```c
/*
 * Reconcile no-playback storage immediately after accepted ownership exists.
 * Inputs: immutable accepted page/type/session and current transport. Output:
 * eligible stopped work suspends before the next render opportunity; running
 * and excluded workflows are unchanged. Centralizing covers all accepted
 * callers while the predicate excludes Instrument and Samples. Affiliate:
 * menu_updateNoPlaybackStorage().
 */
menu_updateNoPlaybackStorage();
```

Final order:

```c
menu_loadSaveCommandActive = 1u;
menu_storageBusy = 1u;
menu_updateNoPlaybackStorage();
menu_repaintAll();
```

No edit is required in the `btnClicked` dispatch. Rejected requests never call
begin. Samples calls begin but fails the scope predicate before its modal owns
its existing suspend.

#### 5.7 Hook transport transitions before every early return

At the top of `menu_pollPresetStatus()`, after locals and before pending page
switch, Kit restore, apply, warning, or completion work:

```c
/*
 * Observe front-panel/MIDI transport before early-return-capable Menu work.
 * Input: accepted command and seq_isRunning(). Output: STOP enters and START
 * leaves on the first observing Menu poll. Why here: apply/retry workers can
 * return for many passes and must not starve transition. Affiliates: transport
 * owners and menu_updateNoPlaybackStorage().
 */
menu_updateNoPlaybackStorage();
```

A mid-operation transport event can be followed by the existing bounded number
of render checks before the next Menu poll; no ISR-to-Menu callback is added.
Stopped-at-acceptance is immediate via command-begin.

#### 5.8 Hook all terminal paths

Inside `menu_finishLoadSaveCommand()`, after the inactive guard and before
clearing flags:

```c
/*
 * Release no-playback resources before command/input ownership.
 * Input: active command at true success/error/modal/apply/final-index terminal
 * boundary. Output: ordinary drain first, then codec resume if S058 owns it;
 * existing flag/UI reset follows. Why: one hook covers every terminal caller
 * and prevents stranded audio on errors/rejected final reload. Affiliate:
 * menu_endNoPlaybackStorage() and every existing finish caller.
 */
menu_endNoPlaybackStorage();
```

Final order:

```c
if (!menu_loadSaveCommandActive)
    return;
menu_endNoPlaybackStorage();
menu_loadSaveCommandActive = 0u;
menu_storageBusy = 0u;
menu_resetSaveParameters();
```

Samples explicitly resumes first and never sets fast drain, so this no-ops.
Nested Instrument and other exclusions behave the same way.

### 6. `main.c`

Make the first executable statement of `audio_check_and_render()`:

```c
/*
 * Freeze foreground audio work while codec hardware is deliberately offline.
 *
 * Input: authoritative codec suspension. Output: when suspended, return before
 * querying/filling the queue, draining pending triggers, advancing DSP/control
 * blocks, or committing a buffer; when live, preserve the complete render loop.
 *
 * Why: suspend leaves the queue empty, so without this guard main would still
 * calculate/commit unused slots, and DSP/trigger time must not advance without
 * a DMA consumer. Resume zeroes/restarts hardware and later calls refill it.
 * Affiliates: codec accessor/mutators, voiceControl_processPending(), mixer,
 * and Menu's no-playback lifecycle.
 */
if (audioCodec_isSuspended())
    return;
```

Do not change main-loop render call sites or the one filesystem call. Do not
move `voiceControl_processPending()`; it remains a live render-boundary owner.

## State transition table

| Event | Command | Transport | Codec | Fast | Busy polls |
| --- | --- | --- | --- | --- | --- |
| Idle/rejected | inactive | either | live | 0 | 1 |
| Eligible accepted while running | active | running | live | 0 | 1 |
| Eligible accepted while stopped | active | stopped | suspended | 1 | 4 |
| Running command gets STOP | active | stopped | suspend next Menu poll | set after verification | 4 |
| Offline command gets START/CONTINUE | active | running | resume next Menu poll | clear before resume | 1 |
| Offline success/error | finishing | stopped | resume | clear | ordinary before UI unlock |
| Scene/Bank load applies DSP while stopped | active | stopped | suspended | 1 | no busy polls until final index request |
| Final index reload | active | stopped | suspended | 1 | 4 |
| Samples modal | active/ineligible | stopped | modal-owned suspend | 0 | existing blocking pump |
| Nested Instrument Save | active/ineligible | stopped | live | 0 | 1 |

Scene/Bank apply needs focused testing: envelopes do not advance while DSP is
frozen. STOP turns notes/triggers off, and `preset_tickDrumsetApply()` already
has a bounded force-commit for a non-quiet frozen tail. S058 must not modify
Preset, but hardware must prove final index completion does not strand `...`.

## Files with no code change

| File/module | Reason |
| --- | --- |
| Sequencer | Existing `seq_isRunning()` is authority; button/MIDI/clock retain transport ownership |
| Preset | Request/completion/Scene apply/final-index ordering unchanged |
| BankData | Bank state and current uncommitted clean-mask work unrelated |
| Scene/Kit/Bank `_tick()` | One dispatch per facade tick is preserved |
| AsyncFATFS/SD/SPI | Existing repeated foreground polling and 16-byte burst remain |
| `menu.h` | New helpers are private |
| `config.h` | Four-pass policy is private, not a build option |
| Makefile | No new source/include path |

## Documentation required in the implementation/closeout turn

These are not to be changed during this planning turn:

| Document | Required update |
| --- | --- |
| `MODULE_INTERCHANGE_SPEC.md` | Add codec observer, filesystem control, Menu lifecycle ownership, and main render client |
| `ASYNCFATFS_REFERENCE.md` | Record bounded consecutive polls from the same foreground context with one facade state tick |
| `SRAM_MANIFEST.md` | Regenerate totals and record approved one-byte SRAM1 owner/lifetime while preserving current Bank-speedup allocations |
| `FILESYSTEM_SPEC.md` | Record top-level stopped-playback behavior and exclusions alongside reachability |
| `MEMORY.md` | Record implementation commit, sizes, poll count, hardware results, and remaining tuning |
| Session 058 handoff/index | Preserve approval, implementation, evidence, and rejected alternatives |

## Build/static verification

Two headers change, and the Makefile has no header dependencies. A clean build
is mandatory; the old draft's “normal rebuild is sufficient” statement was
wrong.

1. Record status and focused diffs; preserve existing dirty/user changes.
2. Obtain explicit approval for the byte.
3. Change only the six source files.
4. Run `git diff --check`.
5. Run `make clean`, then the normal build and `make img`.
6. Run `arm-none-eabi-size` and `arm-none-eabi-size -A`.
7. Run `arm-none-eabi-nm -S --size-sort` to confirm owner/region and no
   unintended state.
8. Compare to a clean pre-S058 build from the same current working source, not
   `f99329c`; current uncommitted Bank work already changes BSS.
9. Inspect optimized `filesystem_tick()`/renderer assembly if size or stack
   output is unexpected.

Expected: +1 source byte normal SRAM1, modest measured FLASH growth, no
DTCM/DMA/SRAM2/file-format/IRQ change, and no material stack growth.

## Hardware verification matrix

### Performance

- Same card, fixtures, masks, and logging configuration before/after.
- Three or more timings for full Bank Save/Load while running and stopped;
  report median/spread.
- Time Scene Save/Load, Kit Save, and KitMrp Save while stopped.
- Start with four passes; do not raise it before all correctness/UI tests pass.

### Entry, transitions, cleanup

- Accept all six eligible commands while stopped: verify suspended codec, fast
  flag, no render-count growth, and terminal resume.
- Accept all while running: verify codec live and one poll.
- During long work perform running -> stop -> start -> stop, using front panel
  and MIDI START/CONTINUE/STOP; verify every transition and final live/zero.
- Force ordinary filesystem errors and final Scene/Bank index failure/rejection;
  verify resume occurs before overlay/unlock and nothing strands.

### Exclusions

- Browser-load Kit/KitMrp while stopped: remain live/ordinary.
- Test nested Instrument Load/Mrp and especially Save/Mrp while stopped; the
  `what == KIT` alias must not misclassify Save.
- Run Samples install: only its modal suspend/resume, fast flag always zero.
- If latent Settings/test cases are exercised, verify exclusion.

### Audio/DSP

- Stop mid-tail during Scene/Bank Load; verify forced apply completes and final
  index returns from `...`.
- After resume audition all outputs/voices, images, modulation, and triggers.
- Listen for pops/clicks on all outputs and compare Samples modal behavior.
- Verify no stale trigger burst after MIDI restart.
- Do not reset render/underrun diagnostics as part of S058.

### Filesystem/UI/regression

- Verify saved trees and loaded state, not merely elapsed time.
- Confirm Bank `NN.` progress repaints and UI/MIDI remain responsive.
- Confirm no stall detector fires from the new cadence.
- Confirm settings, trace, and AutoSave scheduling resumes after terminal ack.
- Repeat sparse/full/failed-child Bank Load, overwrite Save, and direct/fallback
  index cases from the existing uncommitted Bank speedup work.

## Tuning and acceptance

Four passes is the only starting value. Raising toward eight is a separate
evidence-based edit after correctness, UI/MIDI response, and callback density
are proven. Do not tune via SD burst size, TIM5/ISR polling, delays, sector-cache
growth, or poll-count timeouts.

Implementation is complete only when:

- the byte is approved and recorded in `SRAM_MANIFEST.md`;
- only the six named source files change;
- every declaration, state owner, helper, and altered block has adjacent
  what/why/input/output/affiliate documentation;
- clean build/image/diff checks pass against the current working source;
- eligible stopped work is suspended/four-poll, running and excluded work is
  live/one-poll, and stop/start transitions work both directions;
- no terminal path strands codec or drain state;
- Scene/Bank post-load apply completes with rendering frozen;
- data, current Bank-speedup regressions, audio, UI, and timing are verified on
  hardware and preserved in the Session 058 closeout.

## Implementation assessment — 2026-08-29

### Verdict

The focused S058 implementation matches the planned ownership model and all
planned C/H edit sites. No additional source-code change is identified by this
static review. In particular, no missing terminal hook, scope case, renderer
guard, or filesystem cadence change was found.

That is a code-review verdict, not final hardware acceptance. The implementation
still needs the allocation/documentation closeout and the hardware matrix below
before S058 can be called complete.

### Code coverage checked

| Area | Assessment |
| --- | --- |
| Codec state (`AudioCodecManager.c/.h`) | Correctly reuses the existing volatile `audio_hw_suspended` byte and exposes a side-effect-free observer. The observer's declaration and definition document behavior before init, after suspend, and after resume, plus the non-reference-counted ownership rule. |
| Renderer (`main.c`) | `audio_check_and_render()` returns before queue inspection, trigger draining, DSP/control advancement, and buffer commit whenever the codec is suspended. The complete live path remains below the guard. |
| Filesystem policy (`filesystem.c/.h`) | Adds one normalized, foreground-only byte; a setter and observer; and a private four-pass constant. The interfaces do not poll, start, acknowledge, or complete an operation. Header and source contracts agree. |
| Filesystem tick | Only the existing BUSY/non-READY AsyncFATFS pump is repeated. Fast mode performs four consecutive `afatfs_poll()` calls, while ordinary busy work still performs one and idle polling retains its existing rate limit. Autonomous schedulers and the facade operation `_tick()` switch remain once per foreground call. |
| Menu scope | The derived predicate includes exactly stopped top-level Save Kit/KitMrp/Scene/Bank and Load Scene/Bank. It excludes Load Kit/KitMrp, Settings/test types, Samples, and nested Instrument Load/Save; the explicit Instrument-session guard closes the `what == KIT` alias. |
| Entry ownership | The hook runs only after an OK/OW request is accepted and command/storage ownership is set. Codec suspend occurs first; fast drain is enabled only after the codec observer confirms suspension. A pre-existing suspension is not claimed. |
| Dynamic transport | The updater runs at the start of every `menu_pollPresetStatus()` pass, before its early-return workers. STOP can enter the offline state and START/CONTINUE can clear fast drain and resume audio without changing sequencer ownership or adding a transport latch. |
| Terminal cleanup | The common accepted-command finish helper clears fast drain before codec resume and does so before releasing command/input ownership. Existing success, filesystem-error, post-apply, final-index callback, and final-index rejection paths converge on this helper. Repeated cleanup is harmless. |
| Samples ownership | Samples remains on its independent modal suspend/resume path. Because it is outside the predicate, it never acquires the filesystem fast-drain token, and S058 cleanup cannot resume a bare suspension that it does not own. |
| Adjacent documentation | The new state, declarations, definitions, altered tick/render blocks, Menu classifier, and Menu lifecycle hooks carry the requested what/why/input/output/affiliate contracts in the C/H files. |

### Static and linked-image evidence

- Focused `git diff --check` passes for all six planned implementation files.
- The focused S058 slice is confined to the six planned files:
  `AudioCodecManager.c/.h`, `filesystem.c/.h`, `menu.c`, and `main.c`. The
  worktree also contains pre-existing/parallel Bank-speedup and other session
  edits; those should be staged and reviewed separately rather than attributed
  to S058.
- The existing build is newer than the changed C/H sources. All 87 object files
  were rebuilt in one 13-second interval, followed by the ELF and image, which
  is consistent with the required clean/full rebuild. `make -q
  build/lxr02.bin` reports the binary up to date. This review deliberately did
  not rerun a mutating build because its authorized write was limited to this
  assessment.
- The linked image reports `text=382,412`, `data=404`, `bss=96,160`
  (`dec=478,976`). Against the immediately preceding Bank-speedup review build
  (`text=380,100`, `data=404`, `bss=96,152`), the observed delta is +2,312 B
  text, unchanged data, and +8 B aggregate BSS. The logical new retained owner
  is still exactly one byte; section/alignment effects account for the linked
  aggregate rather than authorizing eight bytes of state.
- `arm-none-eabi-nm -S` confirms `fs_fast_drain_active` is exactly one byte at
  `0x20035ba1` in normal SRAM1. It also confirms the pre-existing one-byte
  `audio_hw_suspended` observer source remains in normal SRAM1. The accessor
  functions themselves may be inlined by LTO, as expected.
- `arm-none-eabi-size -A` reports `.dma_nocache=3,100`, `.data=404`, normal
  SRAM1 `.bss=89,488`, `.dtcm=8,708`, and `.dtcmz=3,572`; S058 adds no DTCM,
  DMA, SRAM2, file-format, or IRQ allocation/change.

### Remaining non-code requirements

1. **Allocation acknowledgement and manifest:** the source accurately labels
   `fs_fast_drain_active` as a one-byte normal-SRAM1, process-lifetime,
   filesystem-owned allocation, but the required user acknowledgement is not
   recorded in the reviewed documents. Add that approval and regenerate
   `knowledge_files/specification_reference/SRAM_MANIFEST.md`, including both
   the one-byte symbol and the observed linked +8 B aggregate-BSS movement.
2. **Interface/spec closeout:** update
   `MODULE_INTERCHANGE_SPEC.md`, `ASYNCFATFS_REFERENCE.md`,
   `FILESYSTEM_SPEC.md`, `MEMORY.md`, and the Session 058 handoff/index as
   listed earlier in this plan. No such S058 contract text was found in those
   authoritative documents during this review.
3. **Hardware correctness:** execute the entry/transition/cleanup and exclusion
   matrix. The highest-risk checks remain STOP -> START -> STOP during a long
   operation, MIDI START/CONTINUE/STOP, failure and final-index-rejection
   cleanup, nested Instrument Save with its Kit alias, and Samples modal
   ownership.
4. **Scene/Bank post-load apply:** prove that the bounded force-commit completes
   while DSP/envelopes are frozen, the final index restore terminates `...`,
   and resumed audio has no stale-trigger burst or stranded voice state.
5. **Performance/UI/data:** record repeated same-card running-versus-stopped
   timings, validate saved/loaded trees and current Bank-speedup regressions,
   and confirm LCD/MIDI responsiveness and no new stall detection at four
   polls. Do not increase the poll count until these results are clean.

### Review conclusion

No code amendment is recommended from the static inspection. If the listed
documentation, allocation acknowledgement, and hardware tests pass, the
implementation is ready to close as written. Any failure in those tests should
be treated as new evidence and traced to the owning boundary before changing
the four-pass policy or adding state.

## Hardware failure analysis and targeted correction — 2026-08-29

### Superseding verdict

The preceding static verdict is superseded by hardware evidence. Bank Save is
not correct as written when S058 enters its codec-offline/fast-drain state:

- starting a Bank Save while transport is already stopped does not complete;
- starting while running and then pressing STOP also prevents completion;
- leaving transport running preserves the pre-S058 behavior and completes;
- more than three minutes stopped produces neither completion nor visible
  progress beyond the first child.

The targeted defect is not the Bank Save facade, Menu scope predicate, codec
ownership, or the four-pass budget itself. It is a pre-existing lower SD shim
timeout whose unit is raw calls to `sdcard_poll()`. S058 changes that call rate
by orders of magnitude, turning the old poll-count constants into very short
real-time deadlines and exposing an endless failed-write/retry cycle.

### On-card evidence

The supplied `SD_CARD_BANK_NOPLAY_SAVE/` tree proves that request acceptance and
early Bank writing both occurred:

| Attempt | Durable tree left on card | Last bounded conclusion |
| --- | --- | --- |
| `Bank/040 Full` | `bankset.bcg`, then `00 Barf/sceneset.scg` only | Entered first delegated Scene and stopped before the embedded Kit became durable |
| `Bank/041 Full` | `bankset.bcg` only | Bank metadata became durable; first child did not |
| `Bank/042 Full` | `bankset.bcg`, then `00 Barf/sceneset.scg` only | Repeats the same first-child write-boundary failure as 040 |

The 76/77-byte Bank metadata and 239-byte Scene metadata files are complete
text payloads. The absence of `00 Barf/Kit Barf/`, its six Instrument files,
`pattern.pat`, `effects.fx`, and every later child places the failure after real
FAT/data progress, in the Scene writer's close/create/cache-flush boundary. It
rules out failure to enter the command, failure to select Bank Save, an empty
save mask, and a purely cosmetic lack of progress repaint.

`asavetrc.bin` is not a reliable Bank-stall trace in this capture. It is 33,280
bytes: the first 32,768 bytes are not eight-byte trace records, while only the
final 512 bytes decode as current trace records and those end in ordinary
AutoSave dirty publication. There is no Bank lifecycle/stall record in that
durable suffix. This is consistent with the trace sink being unable to flush
while the foreground Bank owner never releases the one filesystem facade. The
known trace-file namespace/content issue should remain a separate diagnostic
problem; it neither explains nor disproves the partial Bank trees.

### Root cause

`Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c` currently defines:

```c
#define SDCARD_TOKEN_TIMEOUT    5000
#define SDCARD_BUSY_TIMEOUT     50000
static uint16_t retry_count;
```

The read-token and write-busy states increment that counter once for every
`sdcard_poll()` call:

```c
else if (++retry_count > SDCARD_TOKEN_TIMEOUT) { ... }
...
if (++retry_count > SDCARD_BUSY_TIMEOUT) { ... }
```

Those are not timeouts; they are call-count ceilings. Before S058, expensive
foreground rendering and live audio IRQ work inserted substantial real time
between calls. When S058 suspends codec DMA/I2S and `audio_check_and_render()`
returns immediately, the main loop becomes much faster. Its fast branch also
calls `afatfs_poll()` four consecutive times, and each call reaches
`sdcard_poll()` while a block transfer is active. A normal card program-busy
interval therefore consumes the 50,000-count ceiling far sooner in wall time
than it did during playback.

The write-timeout callback supplies a null buffer to
`afatfs_sdcardWriteComplete()`. AsyncFATFS deliberately marks that cache sector
DIRTY again so it can retry. `afatfs_flush()` then starts the same sector write
again. If every retry is subjected to the same artificially shortened
poll-count deadline, the sector can cycle DIRTY -> WRITING -> timeout -> DIRTY
forever. The facade remains BUSY, Menu remains on `...`, later Bank children
never start, and the optional trace writer cannot acquire the facade. This
matches all three partial trees and the strict correlation with playback
stopping.

The read-token count has the same latent unit bug even though this capture
exposes the write side. Both waits must be corrected together; otherwise
stopped Bank/Scene Load can fail under the same accelerated cadence.

### Precise direct fix

Replace only the SD shim's two poll-count deadlines with elapsed TIM6
millisecond deadlines. Keep S058's four consecutive foreground polls, codec
suspension, renderer freeze, Menu ownership, Bank/Scene state machines, cache
retry policy, SD burst size, and all IRQ placement unchanged.

#### 1. `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c`: use elapsed time

Add `timebase.h` for the existing volatile `time_sysTick`. Replace the private
poll constants with conservative real-time budgets:

```c
#define SDCARD_TOKEN_TIMEOUT_MS 1000u
#define SDCARD_BUSY_TIMEOUT_MS  5000u
```

Rename the existing two-byte `retry_count` storage to
`wait_started_tick`; do not add another global/static. Add a wrapping elapsed
helper:

```c
static uint8_t sdcard_waitTimedOut(uint16_t timeout_ms)
{
    return (uint8_t)((uint16_t)(time_sysTick - wait_started_tick) >=
                     timeout_ms);
}
```

Adjacent implementation contract:

```c
/*
 * Test one SD response wait against real foreground-independent time.
 *
 * What: compares TIM6's wrapping millisecond counter with the timestamp saved
 * when the current read-token or write-busy wait began. Why: afatfs/sdcard poll
 * frequency changes when audio is suspended and may change again with a drain
 * budget; a protocol timeout must not expire sooner merely because the caller
 * polls more often. Input: a timeout below one 16-bit wrap. Output: one after
 * that many elapsed milliseconds, otherwise zero. Side effects: none; it sends
 * no clocks and changes no transfer/callback state. Affiliates: time_sysTick,
 * READING_WAIT_TOKEN, WRITING_WAIT_BUSY, and the transport snapshot.
 */
```

Set `wait_started_tick = time_sysTick` at exactly two transitions:

1. in `sdcard_readBlock()`, after CMD17 is accepted and immediately before
   entering `SDCARD_STATE_READING_WAIT_TOKEN`;
2. in the accepted data-response branch of `SDCARD_STATE_WRITING_CRC`,
   immediately before entering `SDCARD_STATE_WRITING_WAIT_BUSY`.

Do not start the write-busy deadline in `sdcard_writeBlock()`: transmitting the
token, 512 data bytes, and CRC is a different bounded state sequence and must
not consume the card's program-busy allowance.

In `SDCARD_STATE_READING_WAIT_TOKEN`, retain the successful `0xFE` transition
and existing timeout cleanup/callback, but replace
`++retry_count > SDCARD_TOKEN_TIMEOUT` with
`sdcard_waitTimedOut(SDCARD_TOKEN_TIMEOUT_MS)`. In
`SDCARD_STATE_WRITING_WAIT_BUSY`, retain the first-nonzero success test and
existing timeout cleanup/callback, but replace
`++retry_count > SDCARD_BUSY_TIMEOUT` with
`sdcard_waitTimedOut(SDCARD_BUSY_TIMEOUT_MS)`.

Reset `wait_started_tick` in `sdcard_abortTransferForBootLog()` where the old
counter was reset. No delay, sleep, pacing gate, extra poll, or callback change
belongs in this fix. The card must continue receiving clocks on every poll;
only the decision to abandon the wait changes units.

Adjacent transition-block contract:

```c
/*
 * Bound the card response wait in TIM6 milliseconds, not caller polls.
 *
 * Inputs: the current SPI response byte, wait_started_tick, and time_sysTick.
 * Outputs: the existing success transition when the card responds, or the
 * existing null-buffer completion after the real-time deadline. Why: stopped
 * playback removes render/IRQ delay and S058 performs four polls per facade
 * pass; counting those calls repeatedly times out a healthy busy card and
 * makes AsyncFATFS re-dirty/retry the same sector forever. Affiliates:
 * sdcard_waitTimedOut(), afatfs_sdcardReadComplete()/WriteComplete(), and
 * filesystem_tick() fast drain. No facade, FAT, or Menu state is changed here.
 */
```

#### 2. Preserve the transport diagnostic without new SRAM

The existing transport snapshot/capsule exposes `retry_count`. Since the
counter becomes the wait start timestamp, do not publish that raw timestamp as
a retry count. Preserve the struct layout and the existing two-byte capsule
field, but change its meaning to elapsed wait milliseconds:

- in `sdcard_lxr02.h`, rename snapshot member `retry_count` to `wait_ms`;
- in `sdcard_getTransportSnapshot()`, return
  `(uint16_t)(time_sysTick - wait_started_tick)` only in
  `READING_WAIT_TOKEN` or `WRITING_WAIT_BUSY`, otherwise zero;
- in `filesystem_hcprmsCapsuleFreeze()`, pack `sd_snapshot.wait_ms` into the
  same transport bytes 5..6;
- increment `HCPRMS_BOOT_CAPSULE_SCHEMA_VERSION` from 1 to 2 in `config.h`;
- make `tools/decode_devlogs.py` label schema-1 bytes as `retry_count` and
  schema-2 bytes as `wait_ms`, so old evidence remains correctly decoded;
- update the transport-field wording in `DEV_MODES.md`.

No capsule size/layout, linker section, callback data, filesystem format, or
on-card runtime payload changes. This is a semantic version change for a
logging-only forensic field. The functional fix remains wholly inside
`sdcard_lxr02.c`; the listed affiliates keep diagnostic truthfulness.

Header contract for the renamed field/getter:

```c
/*
 * Read-only SD transport copy used by boot-time failure forensics.
 *
 * What: reports transfer state, operation, callback ownership, block/offset,
 * and elapsed milliseconds in an active token/busy wait. Why: timeout policy
 * is real-time based, so diagnostics must expose the same coordinate rather
 * than the retired poll count. Input: caller-owned snapshot. Output: scalar
 * copy valid until the next poll/abort; wait_ms is zero outside either wait
 * state. Side effects: none—no SPI clock, callback, deadline, CS, or transfer
 * mutation. Affiliates: sdcard_waitTimedOut(), TIM6 time_sysTick, the schema-2
 * HCPRMS capsule, and tools/decode_devlogs.py.
 */
```

### Why narrower alternatives are rejected

- **Do not remove Bank Save from S058 scope.** That hides the cadence bug and
  discards the requested stopped-save acceleration while stopped Load retains
  the same latent read-token failure.
- **Do not reduce four polls to one as the fix.** Codec/DSP suspension alone
  makes the main loop much faster, so a call-count timeout remains dependent on
  unrelated foreground load and can recur at a different card/phase.
- **Do not merely raise 5,000/50,000.** Any finite poll count changes its real
  duration when render cost, logging, compiler output, or drain count changes.
- **Do not add a delay between polls.** That wastes the released budget, lowers
  throughput, and treats a timeout-unit defect as scheduling policy.
- **Do not suppress AsyncFATFS's re-dirty-on-write-failure behavior.** That
  policy protects data after a genuine transient failure; changing it would be
  broader and risk converting a failed write into false success.
- **Do not change `SDCARD_BURST_SIZE`, Bank/Scene phases, stall detectors, or
  Menu cleanup.** The card proves those layers entered and wrote correctly up
  to the lower transport livelock.

### Verification required after the fix

1. Clean-build because `sdcard_lxr02.h` and `config.h` change; run image, size,
   symbol, and diff checks. Expected retained-RAM delta is zero: the existing
   two-byte counter is repurposed as the two-byte start timestamp and the
   snapshot layout is unchanged.
2. Preserve the supplied 040/041/042 partial trees as evidence. Use a fresh
   Bank slot for first proof, then separately verify overwrite/recovery of an
   incomplete Bank such as 042.
3. On the same card, save a full 16-Scene Bank while stopped from acceptance.
   Confirm child progress reaches 00..15, the full tree and Bank index are
   durable, `...` terminates, fast drain clears, and audio resumes.
4. Start a full Bank Save while running, press STOP during Bank metadata, first
   Scene metadata, embedded Kit/Instrument writes, and a later child. Each run
   must continue advancing while stopped. Perform START -> STOP again during
   one run to retain S058 transition coverage.
5. Repeat running-only Bank Save to prove no regression in the previously
   working cadence. Repeat stopped Scene Save and stopped Scene/Bank Load to
   exercise both write-busy and read-token time bases.
6. Inject or simulate a permanently missing read token and permanently busy
   write response. Verify failure occurs after approximately 1,000 ms and
   5,000 ms respectively, independent of playback state and fast-drain mode,
   and that the existing null-buffer callback/error path remains bounded.
7. Check a schema-2 ASENSURE capsule shows `wait_ms`; decode a preserved
   schema-1 capture and confirm it still shows `retry_count`.
8. Reinspect `asavetrc.bin` only after the separate trace namespace/content
   issue is resolved or from a newly proven clean singleton. Do not make trace
   repair a prerequisite for validating the SD timeout fix; the Bank tree,
   terminal UI state, and timed fault injection are the primary evidence.

### Corrected acceptance statement

S058 cannot close until the SD response deadlines are independent of poll
frequency and the stopped/full Bank Save matrix passes on the reporting card.
The precise correction is elapsed-time accounting in `sdcard_lxr02.c`, with
diagnostic affiliates updated but no new state. The four-pass policy should
remain at four for the retest; tuning it upward is still out of scope.

## Authoritative corrective edit inventory — 2026-08-29

### Completeness verdict and authority

The preceding targeted correction identifies the right functional defect, but
it is not a complete implementation checklist as written. It does not
enumerate every wait-state exit that must retire the new timestamp, it omits
the compact decoder that independently parses the same capsule, and it does
not give an adjacent code contract for every changed C/H site. This section
supplies those details and supersedes both:

- the earlier statement that no SD-driver or `config.h` change is required;
- any less-detailed instruction in **Precise direct fix** above.

After this expansion, the complete corrective code set is exactly these six
files:

| File | Required correction | Product behavior or diagnostic only |
| --- | --- | --- |
| `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c` | Replace read-token/write-busy poll counts with wrapping TIM6-millisecond deadlines; maintain timestamp lifetime and expose elapsed wait time | **Functional fix** plus snapshot producer |
| `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.h` | Rename/document the snapshot's two-byte wait field without changing layout | Diagnostic interface |
| `Core/Hardware/SD/filesystem.c` | Pack the renamed elapsed-wait snapshot member into the existing E7 bytes | Diagnostic serialization |
| `config.h` | Change the HCPRMS semantic schema from 1 to 2 and document the reason | Diagnostic schema |
| `tools/decode_devlogs.py` | Decode both schema 1 (`retry_count`) and schema 2 (`wait_ms`) | Host diagnostic compatibility |
| `tools/devlog_unpack.py` | Apply the same dual-schema handling to the independent compact capsule decoder | Host diagnostic compatibility |

Only `sdcard_lxr02.c` changes runtime SD behavior. The other five code files
keep logging truthful and preserve old evidence. No filesystem facade, FAT
layout, callback signature, Menu state, Bank/Scene phase, audio path, linker
section, or on-card product file changes as part of this correction.

### Exact code-site checklist

Use symbols and state names as anchors; line numbers are deliberately omitted
because the working tree is active.

| # | File and stable anchor | Exact edit |
| ---: | --- | --- |
| 1 | `sdcard_lxr02.c`, file overview and include list | State that burst work is poll-counted but response abandonment is elapsed-time based; include `timebase.h` for the existing `time_sysTick` observer |
| 2 | `sdcard_lxr02.c`, SD configuration constants | Keep `SDCARD_BURST_SIZE`; replace `SDCARD_TOKEN_TIMEOUT`/`SDCARD_BUSY_TIMEOUT` with `SDCARD_TOKEN_TIMEOUT_MS=1000u` and `SDCARD_BUSY_TIMEOUT_MS=5000u` |
| 3 | `sdcard_lxr02.c`, private transfer state | Rename the existing `uint16_t retry_count` storage to `wait_started_tick`; do not allocate a second field |
| 4 | `sdcard_lxr02.c`, after private state and before snapshot getter | Add the wrapping, side-effect-free `sdcard_waitTimedOut(uint16_t timeout_ms)` helper |
| 5 | `sdcard_lxr02.c`, `sdcard_getTransportSnapshot()` | Publish elapsed milliseconds only while state is `READING_WAIT_TOKEN` or `WRITING_WAIT_BUSY`; publish zero in every other state |
| 6 | `sdcard_lxr02.c`, `sdcard_abortTransferForBootLog()` | Clear `wait_started_tick` with the other discarded transfer coordinates before any future operation can be admitted |
| 7 | `sdcard_lxr02.c`, accepted path in `sdcard_readBlock()` | Capture `time_sysTick` immediately before entering `READING_WAIT_TOKEN` |
| 8 | `sdcard_lxr02.c`, `READING_WAIT_TOKEN` | Keep one SPI byte per poll; on token acceptance retire the timestamp, and on elapsed timeout retire it before the existing null-buffer callback |
| 9 | `sdcard_lxr02.c`, accepted response in `WRITING_CRC` | Capture `time_sysTick` only after the card accepts the data-response token and immediately before entering `WRITING_WAIT_BUSY` |
| 10 | `sdcard_lxr02.c`, `WRITING_WAIT_BUSY` | Keep one SPI byte per poll; on ready success or elapsed timeout retire the timestamp before the existing callback |
| 11 | `sdcard_lxr02.h`, `sdcardTransportSnapshot_t` and getter contract | Rename `retry_count` to `wait_ms`, retain member order/type/layout, and describe state-conditional meaning and observer behavior |
| 12 | `filesystem.c`, E7 writes in `filesystem_hcprmsCapsuleFreeze()` | Replace `sd_snapshot.retry_count` with `sd_snapshot.wait_ms`; bytes 5..6 remain little-endian and in place |
| 13 | `config.h`, HCPRMS capsule geometry/schema block | Set `HCPRMS_BOOT_CAPSULE_SCHEMA_VERSION` to `2u`; document that only E7 bytes 5..6 changed semantics |
| 14 | `tools/decode_devlogs.py`, `decode_capsule()` | Accept schemas 1 and 2, print the actual schema, and select `retry_count` versus `wait_ms` for E7; continue raw fallback for every unknown schema |
| 15 | `tools/devlog_unpack.py`, `decode_capsule_compact()` | Mirror the same schema acceptance and conditional E7 label in compact output |

There are no hidden counter references beyond these sites: the current source
uses the private `retry_count` only in the snapshot getter, boot-log abort,
read admission/token wait, and accepted-write/busy wait; the only serialized
consumer is `filesystem_hcprmsCapsuleFreeze()`. Both host decoders have their
own schema-1 check and E7 label, so both must change.

### 1. `sdcard_lxr02.c`: file-level timing rule and timebase input

Add `#include "timebase.h"`; do not redeclare `time_sysTick` locally. Expand
the file overview with this adjacent contract:

```c
/*
 * Transfer work remains cooperative: one sdcard_poll() clocks one bounded
 * token/busy byte or SDCARD_BURST_SIZE data bytes. Response-wait abandonment,
 * however, is measured against TIM6 milliseconds rather than poll calls.
 *
 * Why: callers may legally change foreground poll density, including S058's
 * four consecutive AsyncFATFS polls while audio is suspended. Inputs are the
 * foreground poll stream and the interrupt-owned time_sysTick observer.
 * Outputs are unchanged SD callbacks and transfer states; extra polls advance
 * useful SPI work but cannot shorten a protocol deadline. Affiliates:
 * filesystem_tick(), afatfs_poll(), timebase.h, and SDCARD_BURST_SIZE.
 */
```

`time_sysTick` is already started by `time_initTimer()` before runtime storage
commands and continues in TIM6 while codec DMA/I2S is suspended. This is an
observer dependency only: no timer initialization, IRQ priority, or timebase
source change belongs here.

### 2. `sdcard_lxr02.c`: real-time constants and retained state

Replace only the two response-wait constants:

```c
#define SDCARD_BURST_SIZE       16u
#define SDCARD_TOKEN_TIMEOUT_MS 1000u
#define SDCARD_BUSY_TIMEOUT_MS  5000u
```

Place this contract immediately above them:

```c
/*
 * Bound asynchronous SD response phases in real milliseconds.
 *
 * What: allows up to 1 s for a CMD17 data token and 5 s for CMD24 program-busy
 * release while retaining the existing 16-byte data burst. Why: token/busy
 * latency belongs to the card, whereas poll density belongs to the caller;
 * coupling them made stopped-playback fast drain reject healthy writes.
 * Inputs: TIM6's 1 kHz time_sysTick and the state-entry timestamp. Outputs:
 * the existing success or null-buffer completion path after a stable elapsed
 * deadline. Both intervals remain below the project's 32,768 ms safe
 * uint16_t comparison range. Affiliates: sdcard_waitTimedOut(),
 * READING_WAIT_TOKEN, WRITING_WAIT_BUSY, and filesystem fast drain.
 */
```

Rename, rather than duplicate, the existing state:

```c
static uint16_t wait_started_tick;
```

Place this owner/lifetime contract beside it:

```c
/*
 * Start time for the one currently active asynchronous SD response wait.
 *
 * Writer/lifetime: read admission owns it through READING_WAIT_TOKEN; an
 * accepted write-data response owns it through WRITING_WAIT_BUSY. Every exit
 * from either wait and boot-log abort clears it before invoking a callback.
 * Input is time_sysTick at wait entry; consumers produce timeout decisions or
 * diagnostic elapsed milliseconds. It has no meaning in data/CRC/idle states.
 * This repurposes retry_count's existing two bytes and adds no SRAM. Affiliates:
 * sdcard_waitTimedOut(), sdcard_getTransportSnapshot(), and the two wait states.
 */
```

Zero-initialized BSS is sufficient at boot. Do not add an initializer, second
deadline, 32-bit tick, pacing variable, or per-operation allocation.

### 3. `sdcard_lxr02.c`: timeout helper

Add exactly one helper:

```c
/*
 * Test one active SD response wait against foreground-independent time.
 *
 * What: subtracts the saved wait-entry tick from TIM6's wrapping millisecond
 * counter. Why: poll frequency changes when audio is suspended and can change
 * with any future drain budget, while a protocol deadline must retain one
 * real-time duration. Input: timeout_ms, nonzero and below 32,768 ms; the
 * current wait_started_tick and time_sysTick are private affiliates. Output:
 * one when elapsed time has reached the limit, otherwise zero. Side effects:
 * none—this function sends no clocks and changes no state, callback, buffer,
 * deadline, or chip select. Affiliates: READING_WAIT_TOKEN,
 * WRITING_WAIT_BUSY, timebase.h, and sdcard_getTransportSnapshot().
 */
static uint8_t sdcard_waitTimedOut(uint16_t timeout_ms)
{
    return (uint8_t)((uint16_t)(time_sysTick - wait_started_tick) >=
                     timeout_ms);
}
```

The helper must use unsigned 16-bit subtraction so one TIM6 wrap between entry
and comparison is handled. Do not use absolute `now >= deadline`, foreground
poll counts, a blocking wait, or `systick_ticks`; the selected intervals fit
the existing millisecond clock's documented comparison convention.

### 4. `sdcard_lxr02.c`: snapshot producer

Replace the implementation contract and final field assignment in
`sdcard_getTransportSnapshot()` with this behavior:

```c
/*
 * Copy the live SD transfer for boot-time failure forensics.
 *
 * What: reports scalar transfer coordinates and elapsed milliseconds only for
 * an active read-token or write-busy wait. Why: boot recovery destroys the
 * transport state, and a real-time timeout must be diagnosed in its own unit
 * rather than as the retired number of caller polls. Input: caller-owned,
 * non-null snapshot plus private driver state. Output: a copy valid until the
 * next poll/abort; wait_ms is zero outside READING_WAIT_TOKEN and
 * WRITING_WAIT_BUSY. Side effects: none—no SPI clock, callback, allocation,
 * chip-select change, deadline reset, or ownership mutation. Affiliates:
 * sdcardTransportSnapshot_t, wait_started_tick, time_sysTick,
 * filesystem_hcprmsCapsuleFreeze(), and HCPRMS schema 2.
 */
```

The assignment is exactly state-conditional:

```c
snapshot->wait_ms =
    (state == SDCARD_STATE_READING_WAIT_TOKEN ||
     state == SDCARD_STATE_WRITING_WAIT_BUSY)
    ? (uint16_t)(time_sysTick - wait_started_tick) : 0u;
```

All other snapshot fields remain byte-for-byte sourced as before. In
particular, `block`, `offset`, operation, callback-pending, and state do not
change meaning. The getter must not call `sdcard_waitTimedOut()` because it is
an observer, not a control decision, and it must still tolerate a null output
pointer by returning without effect.

### 5. `sdcard_lxr02.c`: timestamp lifecycle and callback ordering

The timestamp's owner must be truthful at every state transition. Apply all
of the following; omitting any terminal reset leaves stale private state and
can corrupt diagnostics or a callback-admitted successor operation.

#### Accepted read command

At the accepted end of `sdcard_readBlock()`, replace the old counter reset with
the timestamp capture immediately before the wait-state assignment:

```c
/*
 * Arm the read-token deadline only after CMD17 has been accepted.
 *
 * Inputs: accepted R1 response and current time_sysTick. Output: the transfer
 * owns wait_started_tick while entering READING_WAIT_TOKEN; command transport
 * and callback data remain unchanged. Why: command transmission must not
 * consume the card's token-response allowance, and subsequent caller poll
 * density must not alter it. Affiliates: sdcard_waitTimedOut(), sdcard_poll(),
 * and afatfs_sdcardReadComplete().
 */
wait_started_tick = time_sysTick;
state = SDCARD_STATE_READING_WAIT_TOKEN;
```

#### Read-token poll state

Use this adjacent state-block contract and replace only the timeout predicate:

```c
/*
 * Poll one read-token response byte and bound only the wait in milliseconds.
 *
 * Inputs: current SPI byte, wait_started_tick, and time_sysTick. Outputs: 0xFE
 * advances to READING_DATA; elapsed expiry releases CS and reports the existing
 * null-buffer read completion; any other byte remains in this state. Why:
 * S058 may call this state four times per filesystem pass, so caller polls are
 * not a valid duration. On either terminal transition, clear the timestamp
 * before any callback can admit another transfer. Affiliates:
 * sdcard_waitTimedOut(), SDCARD_TOKEN_TIMEOUT_MS, SPI_receive(), and
 * afatfs_sdcardReadComplete().
 */
```

Required branch behavior:

```c
if (r == 0xFE) {
    state = SDCARD_STATE_READING_DATA;
    xfer_offset = 0u;
    wait_started_tick = 0u;
} else if (sdcard_waitTimedOut(SDCARD_TOKEN_TIMEOUT_MS)) {
    SD_CS_DEASSERT;
    SPI_transmit(0xFF);
    state = SDCARD_STATE_IDLE;
    wait_started_tick = 0u;
    if (xfer_callback)
        xfer_callback(SDCARD_BLOCK_OPERATION_READ,
                      xfer_block, NULL, xfer_callbackData);
    return true;
}
```

Preserve the current one-byte-per-poll clocking, CS cleanup, callback arguments,
return values, and success data/CRC states.

#### Accepted write-data response

Do **not** set the busy timestamp in `sdcard_writeBlock()` or
`WRITING_TOKEN`/`WRITING_DATA`. Set it only after `WRITING_CRC` receives the
accepted `(resp & 0x1f) == 0x05` data-response token:

```c
/*
 * Arm program-busy timing only after the card accepts the complete block.
 *
 * Inputs: accepted data-response token and current time_sysTick. Output:
 * WRITING_WAIT_BUSY owns a fresh wait_started_tick. Why: transmitting the
 * start token, 512-byte payload, and CRC is cooperative transfer work and must
 * not consume the card's separate internal-programming allowance. Rejected
 * responses keep their existing immediate failure callback and never enter or
 * arm the busy wait. Affiliates: WRITING_CRC, WRITING_WAIT_BUSY,
 * SDCARD_BUSY_TIMEOUT_MS, and afatfs_sdcardWriteComplete().
 */
wait_started_tick = time_sysTick;
state = SDCARD_STATE_WRITING_WAIT_BUSY;
```

#### Write-busy poll state

Use this adjacent state-block contract and replace only the timeout predicate:

```c
/*
 * Poll one program-busy byte and bound only the busy wait in milliseconds.
 *
 * Inputs: current SPI byte, wait_started_tick, and time_sysTick. Outputs: the
 * first nonzero byte releases CS and reports the existing successful buffer;
 * elapsed expiry releases CS and reports the existing null buffer; zero before
 * expiry remains busy. Why: fast foreground polling previously consumed a
 * count ceiling before a healthy card completed internal programming, after
 * which AsyncFATFS re-dirtied and retried the same sector indefinitely. Clear
 * the timestamp before either callback so a callback-admitted successor owns
 * its own deadline. Affiliates: sdcard_waitTimedOut(),
 * SDCARD_BUSY_TIMEOUT_MS, afatfs_sdcardWriteComplete(), cache retry policy,
 * and filesystem_tick() fast drain.
 */
```

Required branch behavior is the existing code with these additions/replacement:

```c
if (r != 0x00) {
    SD_CS_DEASSERT;
    SPI_transmit(0xFF);
    state = SDCARD_STATE_IDLE;
    wait_started_tick = 0u;
    if (xfer_callback)
        xfer_callback(SDCARD_BLOCK_OPERATION_WRITE,
                      xfer_block, xfer_buffer, xfer_callbackData);
    return true;
}
if (sdcard_waitTimedOut(SDCARD_BUSY_TIMEOUT_MS)) {
    SD_CS_DEASSERT;
    SPI_transmit(0xFF);
    state = SDCARD_STATE_IDLE;
    wait_started_tick = 0u;
    if (xfer_callback)
        xfer_callback(SDCARD_BLOCK_OPERATION_WRITE,
                      xfer_block, NULL, xfer_callbackData);
    return true;
}
```

#### Boot-log abort

Replace `retry_count = 0u` with `wait_started_tick = 0u` and extend the existing
abort comment with:

```c
/*
 * The abort also retires any active response-wait timestamp before discarding
 * the callback. Input may be any transfer state; output is an idle transport
 * with no live deadline. Why: the diagnostic snapshot is frozen before this
 * call, and the recovery mount must not inherit timing ownership from the
 * destroyed operation. Affiliates: sdcard_getTransportSnapshot(),
 * wait_started_tick, afatfs_destroy(true), and boot-log remount recovery.
 */
```

The timestamp must always be cleared **before** invoking a completion callback.
AsyncFATFS callbacks can advance ownership synchronously; clearing after a
callback could erase the start time of a newly admitted block operation.

### 6. `sdcard_lxr02.h`: snapshot ABI and public observer contract

Rename only the final `uint16_t` member:

```c
typedef struct {
    uint8_t state;
    uint8_t operation;
    uint8_t callback_pending;
    uint32_t block;
    uint16_t offset;
    uint16_t wait_ms;
} sdcardTransportSnapshot_t;
```

Replace the existing snapshot/getter contract with this ready-to-place block:

```c
/*
 * Read-only SD transport copy used by boot-time failure forensics.
 *
 * What: reports transfer state, operation, callback ownership, block/offset,
 * and elapsed milliseconds in an active token/busy wait without exposing the
 * private state machine. Why: timeout policy is real-time based, so diagnostics
 * must expose the same coordinate rather than the retired caller-poll count.
 * Input: caller-owned snapshot passed to sdcard_getTransportSnapshot(). Output:
 * scalar copy valid until the next poll/abort; wait_ms is zero unless state is
 * READING_WAIT_TOKEN or WRITING_WAIT_BUSY. Side effects: none—no SPI clock,
 * callback, deadline, CS, buffer, or transfer mutation. The member rename
 * preserves type, order, struct size, and HCPRMS E7 byte width. Affiliates:
 * sdcard_lxr02.c's elapsed-time waits, TIM6 time_sysTick,
 * filesystem_hcprmsCapsuleFreeze(), HCPRMS schema 2, and both host decoders.
 */
```

Keep the getter signature unchanged. Do not expose the private start timestamp
or timeout constants through the header and do not change `sdcard.h`; callers
still submit and complete blocks through the same upstream interface.

### 7. `filesystem.c`: schema-2 E7 packing

In `filesystem_hcprmsCapsuleFreeze()`, change only the member used for E7 bytes
5..6:

```c
/*
 * Freeze the SD wait coordinate using the schema-2 elapsed-time meaning.
 *
 * Input: read-only sd_snapshot.wait_ms, already zero outside the two response
 * waits. Output: the same little-endian E7 bytes 5..6 in the fixed 64-byte
 * capsule. Why: the driver no longer owns a retry counter, and serializing its
 * raw start tick would be both mislabeled and dependent on boot uptime.
 * Geometry, stage tags, callback-pending byte, and product storage are
 * unchanged. Affiliates: sdcard_getTransportSnapshot(),
 * HCPRMS_BOOT_CAPSULE_SCHEMA_VERSION, DEV_MODES.md, and the host decoders.
 */
filesystem_hcprmsCapsulePut16(transport + 5u, sd_snapshot.wait_ms);
```

All E0..E6 fields and every other E7 byte remain unchanged. This code remains
inside `#if DEV_MODE_LOGGING`; it must not influence runtime error handling or
become an input to filesystem control flow.

### 8. `config.h`: semantic schema bump

Set:

```c
#define HCPRMS_BOOT_CAPSULE_SCHEMA_VERSION 2u
```

Replace/extend the capsule comment with:

```c
/*
 * Fixed boot-time AutoSave-ensure failure capsule geometry and schema.
 *
 * What: reserves eight fixed eight-byte records only in a logging build;
 * schema 2 changes E7 bytes 5..6 from schema 1's SD retry_count to elapsed
 * wait_ms. Why: SD response abandonment is now time-based and old captures
 * must remain distinguishable from new evidence. Inputs are logging-only
 * AsyncFATFS/SD snapshot producers. Output is the unchanged 64-byte suffix
 * after the eight-byte ASENSURE token. Ownership: filesystem.c; lifetime: one
 * boot attempt. No file is opened and no storage exists when logging is off.
 * Record size/count, linker placement, and product on-card formats do not
 * change. Affiliates: sdcardTransportSnapshot_t, DEV_MODES.md,
 * decode_devlogs.py, and devlog_unpack.py.
 */
```

Do not change `HCPRMS_BOOT_CAPSULE_RECORD_BYTES`, record count, capsule bytes,
stage tags, or bootlog payload selection. The version increments because field
meaning changed even though geometry did not.

### 9. Host decoders: preserve both meanings

#### `tools/decode_devlogs.py`

`decode_capsule()` must derive `schema = rows[0][1]`, validate the E0..E7 stage
sequence, accept only `schema in (1, 2)`, and retain the current raw fallback
for every other value. The rendered heading must use the actual schema. Select
the E7 name without changing byte extraction:

```python
wait_name = "retry_count" if schema == 1 else "wait_ms"
```

and render `f"{wait_name}={u16(transport[5:7])}"`. Replace its docstring with:

```python
"""Decode HCPRMS schemas 1/2; E7 is retry_count/wait_ms respectively."""
```

Inputs remain exactly 64 bytes. Output remains human-readable lines. The
decoder performs no file mutation when called directly; its affiliates are
the E0 schema byte, `DEV_MODES.md`, bootlog dispatch, and preserved schema-1
card captures.

#### `tools/devlog_unpack.py`

This file does not delegate capsule parsing to `decode_devlogs.py`; it owns a
second schema-1 gate and `retry` label. Therefore `decode_capsule_compact()`
must independently perform the same `schema in (1, 2)` validation, include the
actual schema in compact output, and use `retry_count` for schema 1 or
`wait_ms` for schema 2. Add this adjacent docstring:

```python
"""Compact-decode HCPRMS schemas 1/2 without changing E0..E7 geometry."""
```

Its inputs/side effects remain unchanged. Affiliates are
`decode_devlogs.py`'s integer helpers/constants, compact bootlog dispatch,
`DEV_MODES.md`, and the same historical captures. Do not silently call schema
2 `retry`, and do not drop schema-1 support merely because new firmware emits
2.

### Required source invariants after implementation

- `wait_started_tick` is set exactly twice: accepted CMD17 immediately before
  `READING_WAIT_TOKEN`, and accepted write data-response immediately before
  `WRITING_WAIT_BUSY`.
- It is cleared on read-token success, read-token timeout, write-ready success,
  write-busy timeout, and boot-log abort, always before a callback.
- `sdcard_waitTimedOut()` is called only from the two response-wait states.
- Each wait-state poll still clocks exactly one response byte. Data states
  still transfer `SDCARD_BURST_SIZE` bytes per poll.
- Success and error callbacks retain their current operation, block, buffer or
  null-buffer, callback-data, and return-value behavior.
- The AsyncFATFS write-failure re-dirty/retry policy remains unchanged; the fix
  prevents healthy writes from entering it prematurely.
- Snapshot `wait_ms` is elapsed time only in the two wait states and zero
  otherwise. It never exposes `wait_started_tick` itself.
- HCPRMS remains 64 bytes after the normal eight-byte `ASENSURE` token. Only
  schema and E7 bytes 5..6 semantics change.
- Schema-1 captures still decode as `retry_count`; schema-2 captures decode as
  `wait_ms`; unknown schemas remain raw rather than being guessed.
- The correction adds no static/retained RAM: one existing two-byte scalar and
  one existing two-byte snapshot member are renamed/reinterpreted.

### Explicit no-change boundary

No change is required in any of the following, and adding one would broaden
the direct fix without evidence:

| Unchanged area | Reason |
| --- | --- |
| `Core/Hardware/SD/asyncfatfs/sdcard.h` | Public read/write/poll types, callbacks, and status values do not change |
| `Core/Hardware/timebase.c/.h` | TIM6 already provides the public volatile 1 kHz wrapping counter and remains active while audio is suspended |
| `Core/Hardware/SD/SPI/sd_routines.c` | Initialization/command readiness has separate timing; this failure is in the runtime async shim |
| `send_cmd_keep_cs()`'s local `retry` | This is a fixed synchronous bound on command-response bytes, not either long asynchronous token/busy timeout |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c/.h` | Cache re-dirty and retry semantics are correct after a genuine failed block write |
| `Core/Hardware/SD/filesystem.h` | Fast-drain API and facade contract are unchanged by the transport correction |
| `Core/Menu/menu.c`, codec manager, `main.c` | S058 ownership, four-poll selection, codec suspend/resume, and renderer freeze remain as implemented |
| Bank/Scene/Preset/data modules | Their state machines correctly reach the lower write boundary and require no retry workaround |
| Makefile/linker script | `timebase.h` is an existing project header and no section or source unit is added |
| `AUTOSAVE.md` | It deliberately delegates the diagnostic wire layout to `DEV_MODES.md`; no AutoSave record behavior changes |

Also do not change `FS_FAST_DRAIN_POLL_PASSES`, `SDCARD_BURST_SIZE`, the
filesystem idle-poll cadence, IRQ placement, SPI clock speed, cache count,
stall detectors, or Menu progress rendering as part of this correction.

### Documentation closeout after source implementation

These are documentation changes, not additional product-code edits, but they
are required before Session 058 closes:

| Document | Exact update |
| --- | --- |
| `knowledge_files/specification_reference/DEV_MODES.md` | Mark E0 schema 2; define E7 bytes 5..6 as `retry_count` for schema 1 and `wait_ms` for schema 2; state that both decoders preserve both versions |
| `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md` | Add the runtime shim invariant that token and program-busy waits use elapsed TIM6 milliseconds independent of `afatfs_poll()` density; update snapshot wording from retries to elapsed wait |
| `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` | Record the SD shim's timebase observer dependency and that filesystem fast drain may repeat transport polls without shortening response deadlines |
| `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | Record the hardware-discovered stopped Bank Save failure, its lower transport cause, and the required stopped/running acceptance matrix beside S058 Load/Save reachability |
| `MEMORY.md` | Preserve the 040/041/042 partial-tree evidence, corrected root cause, zero-RAM fix, and final hardware result; do not record success before retest |
| `knowledge_files/log_archive/058_SESSION_HANDOFF_LOG.md` and `000_SESSION_INDEX.md` | Create/index the durable Session 058 closeout only after build and hardware acceptance; include the schema change and exact timings |

The earlier S058 one-byte `fs_fast_drain_active` manifest/approval requirement
still stands. This corrective patch itself has zero SRAM delta and therefore
adds no second `SRAM_MANIFEST.md` allocation; the manifest should mention the
repurposed two-byte diagnostic/timing scalar only if its ownership notes track
semantic changes to existing allocations.

### Verification additions for implementation completeness

In addition to the hardware matrix already listed above:

1. Run `rg` after editing and prove there is no live `retry_count`,
   `SDCARD_TOKEN_TIMEOUT`, or `SDCARD_BUSY_TIMEOUT` reference in the shim or
   capsule packers; schema-1 wording may remain only in decoder compatibility
   and documentation.
2. Feed synthetic, geometry-identical schema-1 and schema-2 capsules to both
   decoders. Confirm both accept E0..E7, report their actual version, and label
   the same two E7 bytes `retry_count` and `wait_ms` respectively. Confirm
   schema 3 takes the raw/unknown path.
3. Clean-build both logging-on and logging-off configurations. The logging-off
   build must not reference capsule geometry, while the elapsed transport fix
   remains active in both builds.
4. Run `git diff --check`, image generation, section size, symbol size, and
   disassembly/source inspection. Expected correction delta is zero BSS/data;
   `wait_started_tick` must be one two-byte symbol replacing `retry_count`.
5. Inspect callback ordering in optimized output or source: every wait exit
   stores zero before the indirect callback, and neither callback path can
   inherit or erase a successor transfer's timestamp.
6. Time fault-injected missing-token and permanent-busy cases under ordinary
   one-poll and S058 four-poll modes. Both modes must fail at approximately the
   same 1,000/5,000 ms deadlines while continuing to clock the card.

### Final implementation instruction

The direct correction is complete only when all fifteen code sites above are
implemented. A patch that changes only the two timeout predicates is
functionally close but incomplete: it leaves stale timestamp ownership and
misdecodes schema-2 evidence. Conversely, no code outside the six-file set is
needed unless new build or hardware evidence contradicts these bounded
interfaces.

## Post-implementation review of the corrective patch — 2026-08-29

### Verdict

The implemented correction matches all fifteen code sites in the authoritative
inventory above. No additional fix-specific source change is identified by
this static and linked-image review.

The runtime change is correctly confined to `sdcard_lxr02.c`: read-token and
write-program-busy abandonment now use elapsed TIM6 milliseconds, independent
of how often S058 calls `afatfs_poll()`. The other five code files preserve the
diagnostic ABI and historical decoder compatibility. The patch is ready for
the same-card hardware matrix, but S058 is not closed until the stopped Bank
Save failure is disproved on hardware and the authoritative documentation is
updated.

### Fifteen-site implementation audit

| Site | Review result |
| ---: | --- |
| 1 | `sdcard_lxr02.c` documents the split between poll-bounded transfer work and elapsed response waits, and includes the existing public `timebase.h` declaration rather than redeclaring the timer. |
| 2 | The 16-byte burst remains unchanged; token and busy limits are now explicitly `1000u` and `5000u` milliseconds, both inside the project's safe wrapping-`uint16_t` comparison interval. |
| 3 | `retry_count` has been replaced by one `uint16_t wait_started_tick`; no second timing scalar was added. |
| 4 | `sdcard_waitTimedOut()` uses `(uint16_t)(time_sysTick - wait_started_tick) >= timeout_ms`, sends no clocks, and mutates no state. |
| 5 | `sdcard_getTransportSnapshot()` computes elapsed `wait_ms` only in `READING_WAIT_TOKEN` or `WRITING_WAIT_BUSY` and returns zero in every other state. All other snapshot coordinates retain their prior meaning. |
| 6 | `sdcard_abortTransferForBootLog()` clears the wait timestamp with the abandoned transport coordinates. It still invokes no stale callback. |
| 7 | Accepted CMD17 captures `time_sysTick` immediately before entering `READING_WAIT_TOKEN`; command transmission does not consume the token allowance. |
| 8 | The read-token state clocks one byte per poll, checks the response before expiry, and clears the timestamp on both token acceptance and timeout. Timeout retains the existing null-buffer callback. |
| 9 | The write timestamp is armed only after the data-response token is accepted, immediately before `WRITING_WAIT_BUSY`; token, payload, and CRC transmission do not consume program-busy time. |
| 10 | The write-busy state clocks one byte per poll and clears the timestamp on both ready success and elapsed timeout before invoking either callback. Existing success-buffer/null-buffer semantics are preserved. |
| 11 | `sdcardTransportSnapshot_t.retry_count` is renamed to `wait_ms` without changing type or member order. The header and implementation contracts agree. |
| 12 | `filesystem_hcprmsCapsuleFreeze()` writes `sd_snapshot.wait_ms` into the unchanged little-endian E7 bytes 5..6. No other capsule byte was altered by this correction. |
| 13 | `HCPRMS_BOOT_CAPSULE_SCHEMA_VERSION` is 2 and its `config.h` contract explains the sole schema-semantic change and unchanged 64-byte geometry. |
| 14 | `decode_devlogs.py` accepts schemas 1/2, emits the actual schema, calls E7 `retry_count` for 1 and `wait_ms` for 2, and preserves raw fallback for unknown versions. |
| 15 | `devlog_unpack.py` independently applies the same dual-schema behavior in compact output; it no longer silently labels schema-2 time as a retry count. |

### State-machine and ordering findings

The wait timestamp has exactly the planned ownership transitions:

- set on accepted read command and accepted write data-response only;
- cleared on read-token acceptance, read-token timeout, write-ready success,
  write-busy timeout, and boot-log abort;
- tested only from the two response-wait states;
- never used as an input to data/CRC transfer work or filesystem policy.

Every timeout/success branch that invokes a wait-state callback clears the
timestamp first. This ordering matters because the callback may synchronously
advance AsyncFATFS and admit a successor block; no post-callback clear can
erase that successor's start time. The write-rejected path does not need an
extra clear because it occurs before the current write ever arms a wait, and
all paths that can return the shim to IDLE already retire the prior wait.

Response priority also remains sensible: each wait state consumes the current
SPI byte first and accepts a valid token/ready byte before evaluating timeout.
The patch changes only when a non-response is abandoned, not what constitutes
a successful card response.

### Scope and interface findings

- `sdcard.h`, callback signatures, block status values, AsyncFATFS cache retry
  behavior, filesystem facade APIs, and Menu/codec ownership are unchanged.
- `send_cmd_keep_cs()` retains its separate local synchronous response-byte
  bound; it is not either asynchronous timeout corrected here.
- `time_initTimer()` starts TIM6 before SD mount and long before runtime S058
  commands, so `time_sysTick` is live during both boot diagnostics and stopped
  playback. Codec suspension does not suspend TIM6.
- The four-pass fast drain, 16-byte SD burst, SPI clock, IRQ placement, Bank and
  Scene state machines, progress UI, and stall detectors were not adjusted.
- The focused `filesystem.c` affiliate is only the renamed E7 packing input.
  That file contains substantial pre-existing Session 057/058 work in the
  current dirty tree; this review does not reattribute or re-review those
  unrelated hunks as part of the timeout correction.

### Static, decoder, and linked-image evidence

- Focused `git diff --check` passes for the six corrective code files.
- Search finds no live old `SDCARD_TOKEN_TIMEOUT`, `SDCARD_BUSY_TIMEOUT`,
  snapshot `.retry_count`, or mutable retry counter. Remaining
  `retry_count` text is intentional schema-1 documentation/decoder
  compatibility and the state comment explaining the repurposed allocation.
- Synthetic E0..E7 capsules were passed directly to both host decoders without
  fixture writes. Both report schema 1 with `retry_count=4660`, schema 2 with
  `wait_ms=4660`, and schema 3 through their raw unknown-schema path.
- The existing build products are newer than all six corrected code files;
  `make -q build/lxr02.elf` and `make -q build/lxr02.bin` both report current.
  The linked image is `text=382,508`, `data=404`, `bss=96,160`
  (`dec=479,072`); the binary is 382,912 bytes and the wrapped image is 382,928
  bytes.
- Relative to the immediately preceding S058 build recorded above
  (`text=382,412`, `data=404`, `bss=96,160`), this correction is +96 bytes of
  text with unchanged data and BSS. `arm-none-eabi-nm -S` reports
  `wait_started_tick` as exactly two bytes in normal SRAM1. The existing
  `fs_fast_drain_active` and `audio_hw_suspended` symbols remain one byte each.
- Section totals remain `.dma_nocache=3,100`, `.data=404`, normal SRAM1
  `.bss=89,488`, `.dtcm=8,708`, and `.dtcmz=3,572`. The correction introduces
  no DTCM, DMA, retained capsule, callback, or filesystem-format allocation.

The review used the already-produced, source-current image rather than running
a destructive clean build. A clean logging-on and logging-off pair remains a
closeout test, particularly because the current configuration has
`DEV_MODE_LOGGING=1`.

### Remaining work and acceptance risk

No further code amendment is recommended before hardware testing. The
remaining blockers are evidence and documentation:

1. **Primary regression proof:** save a fresh full 16-Scene Bank with playback
   stopped from acceptance. It must advance through every child, terminate
   `...`, clear fast drain, and resume audio.
2. **Dynamic transition proof:** begin while running and press STOP at Bank
   metadata, first Scene metadata, embedded Kit/Instrument writing, and a later
   child. Include START -> STOP within one operation.
3. **Control proof:** repeat the previously working running-only Bank Save and
   stopped Scene Save, Scene Load, and Bank Load.
4. **Timeout proof:** missing-token and permanently-busy injection should fail
   at approximately 1,000 ms and 5,000 ms in both one-poll and four-poll modes.
5. **Durability proof:** compare the complete fresh Bank tree and separately
   overwrite/recover one incomplete captured slot such as 042. Preserve
   040/041/042 as the original evidence until the result is recorded.
6. **Build proof:** perform clean logging-on and logging-off builds and record
   warnings, image/section sizes, and the two-byte symbol replacement.
7. **Documentation:** the closeout table in the authoritative inventory is
   still outstanding. In particular, `DEV_MODES.md` still labels E7 as a retry
   count and is currently wrong for schema 2; the AsyncFATFS, interchange,
   filesystem, MEMORY, SRAM-manifest acknowledgement, and eventual Session 058
   handoff/index updates also remain.

### Post-review conclusion

The corrective code is internally complete and consistent with the targeted
root cause. There is no static indication that another product-code change is
needed. Its decisive acceptance criterion is now hardware behavior: stopped
Bank Save must complete on the reporting card without relaxing four-pass drain
or changing the higher-level Bank writer. Until that passes, describe the
patch as **implemented and statically accepted, hardware pending**, not as a
closed fix.

## Hardware acceptance and Bank progress repaint — 2026-08-29

### Stopped/dynamic Bank Save result

The elapsed-time SD correction is hardware-confirmed on the reporting card.
The user reports that a full Bank Save now completes in approximately 30
seconds with playback stopped. During the slot-046 save, playback was started
and stopped again; the operation continued through that transition and
completed. This directly reverses the prior behavior where either starting
stopped or pressing STOP during a running save produced no completion after
more than three minutes.

The supplied `SD_CARD_BANK_NOPLAY_SAVE/Bank/046 Full/` tree is complete:

| Evidence | Slot 046 result |
| --- | --- |
| Bank metadata | one nonempty 76-byte `bankset.bcg`, valid `helicase.bankset` version 2 |
| Scene directories | all 16 numbered children `00` through `15`, with the expected display names |
| Per-Scene root payload | exactly one nonempty `sceneset.scg`, `pattern.pat`, and `effects.fx` in every Scene |
| Embedded Kits | exactly one `Kit <name>/` in every Scene |
| Per-Kit payload | exactly one nonempty `kitset.kcg` plus six nonempty typed Instrument files in every Kit |
| Totals | 33 directories including the Bank root, 161 files, zero empty files |
| Reference comparison | recursive comparison with completed slot `045 Full` reports no difference; every relative filename and every payload byte is identical |
| Bank index | `/Bank/.hcindex` row 046 contains `Full` |
| Settings continuation | `settings.cfg` is valid version 1 and records `active_bank=46` |

The identical aggregate manifest hash for slots 045 and 046 is
`214778579cd1a2d90e67a6e2f61bfc762eb34f19a0496b9071e73686a13b5c4c`.
This is stronger than a directory-count check: the restart/stop attempt emitted
the same complete byte content as the prior finished Bank. No partial child,
missing Instrument, zero-length payload, or malformed terminal file is visible
in slot 046.

This accepts the primary stopped-save and one dynamic START -> STOP Bank Save
case for the timeout fix. The broader Load/Scene/excluded-operation matrix and
fault-injected 1,000/5,000 ms timeout tests remain useful closeout coverage,
but they are no longer blockers to the conclusion that the reported Bank Save
livelock is fixed on this card.

### Progress-display finding

The filesystem cursor and renderer were already correct:

- `op_bank_child_cursor` advances when Bank Save/Load selects the next child;
- `filesystem_bankChildCursor()` exposes `0..15` only while a Bank operation is
  active and otherwise returns `0xFF`;
- `menu_paintLoadSaveConfirmation()` correctly formats that value as `00.`
  through `15.` in bottom-row cells 13..15.

The missing boundary was display invalidation. `menu_paintLoadSaveConfirmation()`
runs only inside a Menu repaint. An asynchronous filesystem child transition
does not itself request one, so the LCD retained the first queued frame even
though the accessor had advanced. Screensaver exit calls `menu_repaintAll()`,
which explains why clearing the screensaver immediately revealed the current
number: it exercised the formatter with fresh state rather than fixing or
advancing filesystem progress.

### Targeted foreground repaint fix

Only `Core/Menu/menu.c` changes for this follow-up.

1. Add private `menu_refreshBankChildProgress()` immediately after
   `menu_repaint()`.
2. Call it near the start of `menu_pollPresetStatus()`, immediately after S058
   transport reconciliation and before any worker can return early.

The helper uses existing state rather than allocating a remembered cursor:

- require an accepted command on the physical Load or Save page;
- leave an active screensaver as sole LCD owner;
- if `menu_lcdRefreshPending` is already set, let the existing queue-space
  retry own that frame;
- read `filesystem_bankChildCursor()` and ignore its non-Bank sentinel;
- format the current tens/ones locally and compare `NN.` with
  `currentDisplayBuffer[1][13..15]`, the frame last queued to the LCD;
- call ordinary incremental `menu_repaint()` only when those cells differ.

The adjacent implementation contract is:

```c
/*
 * Refresh the visible Bank child counter when its filesystem cursor advances.
 *
 * What: compares the live zero-based Bank child with the three command cells
 * last queued to the LCD and requests one ordinary incremental repaint only
 * when `00.`..`15.` differs. Why: the renderer already formats Bank progress,
 * but the asynchronous child transition does not otherwise invalidate the
 * Load/Save frame; the count therefore stayed at its first value until an
 * unrelated full repaint, such as screensaver exit. Inputs: accepted command
 * ownership, active Load/Save page, filesystem_bankChildCursor(), LCD shadow,
 * screensaver state, and the existing queue-retry latch. Output: at most one
 * edge-triggered menu_repaint(); no filesystem, transport, command, or cursor
 * state changes. A pending frame is left to the existing queue-space retry,
 * and an active screensaver remains the sole LCD owner. This reuses display
 * state and allocates no progress byte. Affiliates:
 * menu_paintLoadSaveConfirmation(), sendDisplayBuffer(),
 * menu_lcdRefreshPending, and menu_pollPresetStatus().
 */
```

The call-site contract records why it must precede early returns:

```c
/*
 * Publish Bank child progress before any worker below can return early.
 * Input: the filesystem cursor may have advanced in the preceding main-loop
 * filesystem_tick(). Output: only a changed `NN.` indicator is repainted;
 * unchanged children cost no LCD traffic. Why: filesystem progress and Menu
 * rendering are separate foreground phases, so the latter needs this
 * observer boundary to invalidate its frame. Affiliates:
 * menu_refreshBankChildProgress(), filesystem_tick(), and screensaver exit.
 */
```

### Why this is the narrow fix

- It does not repaint continuously: unchanged cursor values return after three
  shadow comparisons and generate no LCD queue traffic.
- It adds no retained byte. The existing queued-display shadow is the prior
  published value and is already the correct edge detector.
- It does not make filesystem depend on Menu or add a callback from the Bank
  writer; Menu remains the observer of the existing read-only accessor.
- It does not call `menu_repaintAll()`, clear the LCD, or bypass queue capacity.
  Only changed characters are emitted through `sendDisplayBuffer()`.
- It cannot fight the screensaver. Progress is suppressed while the
  screensaver is active, and its existing exit repaint publishes the latest
  cursor.
- It preserves the existing pending-frame retry: if the complete frame cannot
  fit, `sendDisplayBuffer()` latches `menu_lcdRefreshPending`, and the current
  foreground retry sends it after TIM7 drains the queue.
- It applies equally to Bank Save and Bank Load because both already share the
  cursor accessor and formatter, without affecting Kit, Scene, Instrument,
  Samples, or autonomous filesystem work.

### Build and acceptance

Focused `git diff --check` passes. `make all` and `make img` complete; only the
existing embedded-newlib `_close`, `_lseek`, `_read`, and `_write` stub warnings
remain. The linked result is `text=382,700`, `data=404`, `bss=96,160`
(`dec=479,264`). Relative to the preceding timeout-fix image, the display
correction adds 192 bytes of text and no data or BSS. The binary is 383,104
bytes and the wrapped image is 383,120 bytes. The one-byte codec/fast-drain
symbols and two-byte wait timestamp retain their sizes.

Hardware verification for this final UI change is one bounded check: flash the
new image, run a multi-child Bank Save and Bank Load, and confirm the command
surface advances `00.` through `15.` without input or screensaver activity.
Also allow the screensaver to activate during one run and confirm its exit
shows the then-current child and subsequent child transitions continue to
repaint. Payload behavior is unchanged and slot 046 is the accepted durability
reference.
