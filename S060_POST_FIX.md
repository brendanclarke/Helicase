# Session 060 — Post-Implementation Review and Fix Plan

Status: review and hardware-evidence plan only. No firmware change is made by
this document. The review is against commit `3929745` (`s060 first
implementation`) and the copied card at `SD_CARD_S060_TEST_RESULT/`.

## 1. Scope and conclusions

The implementation should be repaired rather than redesigned again. Its useful
core is present: boot-time A/B validation, a 24-byte mounted source cache that
includes the approved source CRC, v2 full-copy A/B publication, commit-last and
final-sync protection, HCNAMES source/type/pending fields, and immediate
Load/Save metadata continuations.

The current failures come from a smaller set of boundary errors:

1. Kit and Instrument Load release the filesystem facade while their DSP apply
   is still running. The diagnostic trace appender can take the facade before
   the mandatory HCNAMES continuation is posted. The load has therefore already
   changed the sound when Menu reports the request refusal as generic `FsErr`.
2. A real Bank transition can leave `settings.cfg`, `/.hcnames`, and the mounted
   AutoSave source on different Bank identities. Runtime Q discovery recognizes
   the mismatch but treats “no matching winner” as terminal instead of using the
   already implemented asynchronous B-then-A recovery path.
3. Initial/recovery AutoSave masks are formatted incorrectly and currently do
   not mark the live payload cells dirty.
4. The Bank payload commit is visible in the trace, but the trace does not show
   enough of the later DSP-apply worker to prove why the newly active Bank kit
   was not audible until a manual Scene selection.
5. Two review findings remain outside the reported runtime symptoms: boot
   HCNAMES component failures are not displayed, and the Python verifier is not
   yet an authoritative model of firmware selection and partial-Bank rules.

No change to AsyncFATFS, the ordinary Bank/Scene/Kit/Instrument file formats,
the single Autosave mutation mask, or the full-copy A/B transaction is proposed.

## 2. Firmware review carried forward

### 2.1 Implemented behavior worth retaining

- `Core/Bank/Scene/Autosave.h` and `Autosave.c` define the v2 record as 35,026
  bytes: the existing 30,848-byte payload, the mask, and a 258-byte logical
  source table beginning at record offset 34,768.
- `Core/Hardware/SD/filesystem.c` retains the selected source generation,
  filename index, probe, Bank identity, and expected source CRC in an exact
  24-byte mounted cache. A normal runtime drain begins from that cache instead
  of validating both records again.
- The runtime writer still streams the selected source into the inactive peer,
  recomputes and compares the physical source CRC with the stored and cached
  values, computes the target CRC from the transformed bytes, syncs the invalid
  target, publishes CRC, publishes the commit marker, and performs the final
  sync before promoting the mounted cache.
- HCNAMES has fixed source, type, and pending metadata, and the targeted
  Scene/Kit/Instrument update paths exist. The captured card proves that the
  Scene-load pending handoff can reach the physical file.
- A clean out-of-tree build of this implementation succeeded. The logging build
  measured `text=395476`, `data=404`, and `bss=96216`; the Python files compiled
  and the prior static checks passed. Those checks establish buildability, not
  the runtime correctness addressed below.

### 2.2 Confirmed defect: recovery baseline mask is zero

At `Core/Bank/Scene/Autosave.c:466-477`, the initial-record formatter subtracts
`AUTOSAVE_PAYLOAD_OFFSET` from a mask-record offset. Every mask offset is below
the payload, so the unsigned result wraps into the 61,680..65,535 range. The
live-payload predicate rejects it and the generated mask byte is zero.

That contradicts the recovery contract. A newly created B-then-A baseline
contains names, sources, and Bank identity, but its live parameter payload is
still zero/default data. Its mask must say which cells need to be drained from
the resident Bank. A power loss before the first complete drain can otherwise
leave a CRC-valid record that falsely claims those zero payload cells are
complete.

The fix is local: derive the eight payload positions represented by each mask
byte from `mask_offset * 8 + bit`, test each position with
`autosave_initialPayloadIsLive()`, and OR the corresponding bits. Reserved and
name-owned cells remain clear. Add direct tests for Bank fields, a Scene field,
Kit fields, each Instrument type, the final live cell, and reserved/name cells.

### 2.3 Confirmed defect: mounted-source lifecycle has dead ends

The relevant paths are spread across `Core/Hardware/SD/filesystem.c`:

- Bank Load detects a different target at lines 25098-25115 and revokes the old
  source, but `FS_AUTOSAVE_DISCOVERY_BANK_PENDING` is set and never consumed.
- successful Bank Save changes Bank identity at lines 17025-17082 without
  revoking an old mounted source;
- OFF-to-ON at lines 22576-22618 revokes scheduling but does not invalidate the
  mounted source identity;
- drain admission at lines 7516-7522 reports a source/Bank mismatch by setting
  both setup-pending and setup-failed, which prevents the scheduler from doing
  the setup it just requested;
- Q completion at lines 22943-22975 accepts only an already matching winner. If
  both valid files belong to the previous Bank, it disables tracking instead of
  rebuilding B then A for the new settled Bank;
- setup completion at lines 22978-23034 can skip Q when stale `source_valid`
  survived a lifecycle change.

There is also an unsafe mixing of boot and runtime state. Bank Load reads the
raw `FS_AUTOSAVE_DISCOVERY_HCNAMES_PRESERVE` bit at lines 12726-12737,
13383-13385, and 13485-13498. That bit can exist for a runtime source
contradiction when boot reconstruction is not active. It can therefore suppress
the Bank child overlays and final Bank HCNAMES writer during a normal runtime
Bank Load. Runtime Bank publication must not be skipped merely because a prior
source was being preserved for Q.

The repair should centralize one small mounted-source revoke operation. Use it
when a different-Bank Load request is accepted, when a successful Bank Save
commits a different slot/name identity, on OFF-to-ON, and on a proven source
contradiction. It must clear authorization and scheduling without deleting
either record. A failed Bank durability tail remains unauthorized. After the
new Bank's HCNAMES and settings tails are durable, runtime Q should either mount
a matching valid winner or, when the root HCNAMES singleton is safe, invoke the
existing asynchronous B-then-A baseline recovery and mount the new A record.
Only terminal I/O, duplicate-name ambiguity, or unsafe/malformed HCNAMES should
set the persistent setup-failed latch.

Boot-only HCNAMES suppression must use the boot-active accessors. A normal
runtime Bank Load always publishes the selected Bank row and selected child
blocks before it reports success.

### 2.4 Confirmed defect: boot component errors are not user-visible

`main.c:487-495` resets a failed HCNAMES component and calls
`filesystem_noteBootHcnamesComponentFailure()`. The latter only fills
`fs_error_code` at `Core/Hardware/SD/filesystem.c:23918-23927`. The only normal
error overlay is Menu-private and is never invoked for this boot continuation.

Retain the per-Scene reset-and-continue policy, but latch one boot notification
that Menu displays once after normal UI startup. This is not an AsyncFATFS retry
and must not turn a component source mismatch into a whole-boot failure.

### 2.5 Confirmed defect: the verifier is not authoritative

`tools/verify_bank_autosave.py` currently:

- selects the newest CRC-valid record at lines 283-301 without applying the
  firmware's Bank-match preference;
- reports every absent Scene child as an error at lines 259-271, although a
  partial or empty Bank is valid;
- accepts a looser text grammar than the firmware parser; and
- does not check the initial/recovery mask's live-owner semantics.

The tool remains useful for raw header, CRC, Bank-field, and byte comparison
evidence, but its full PASS/FAIL result must not be treated as a firmware oracle
until these differences are fixed and fixture-tested.

### 2.6 Cleanup after correctness

`Core/Menu/menu.c:3484-3515` retains two no-op legacy resident-name helpers and
their call sites. They can be removed after the direct publication paths pass
hardware tests. Comments and specifications that claim the implementation is
fully verified must also be changed to distinguish static build verification
from hardware acceptance. This cleanup is deliberately after the functional
fixes.

## 3. Reported Kit and Instrument `FsErr`

### 3.1 High-confidence cause

The failure is a facade-ownership race, not a failed Kit or Instrument payload
read:

1. `preset_completeFilesystemOp()` acknowledges the completed filesystem
   operation immediately at `Core/Bank/Scene/Preset/presetManager.c:232-255`.
   This correctly explains why the new sound data is resident.
2. Menu deliberately keeps the load transaction open while it applies the
   resident image to the live DSP in bounded foreground steps. Only after the
   apply completes does `menu_publishStableKitNames()` or
   `menu_requestAppliedInstrumentNameUpdate()` request the mandatory HCNAMES
   write (`Core/Menu/menu.c:475-524`, 3593-3644, and 3747-3805).
3. The main loop calls `menu_pollPresetStatus()` before `filesystem_tick()`
   (`main.c:1384-1390`). During the multiple apply passes, the facade is IDLE at
   the later filesystem call.
4. The autonomous trace scheduler suppresses itself only while
   `menu_isLoadSaveCommandActive()` is true
   (`Core/Hardware/SD/filesystem.c:23325-23389`). Scroll-triggered Kit and
   Instrument loads are intentionally not OK/OW commands, so that predicate is
   false. With a large pending trace ring, `asavetrc.bin` can claim the facade
   during the DSP apply.
5. The later targeted HCNAMES request sees `FS_STATUS_BUSY` and returns false at
   `filesystem.c:25453-25456` or 25535-25538. Menu converts that request refusal
   to its generic error overlay. No filesystem operation actually failed, so no
   named error exists and the overlay says `FsErr`.

The captured trace supports this explanation: it contains successful
Instrument commit/dirty records, but no recent `E` operation-error record for
the reported loads. A synchronous busy refusal never starts an internal
operation and therefore cannot emit `E`.

### 3.2 Minimal repair

Expose the already existing Menu transaction state as one read-only
“foreground Load/Save persistence pending” predicate. It is true from accepted
payload load through bounded DSP apply and the required HCNAMES callback. Gate
all autonomous filesystem admissions—settings, diagnostic trace, and
AutoSave—on that predicate. Existing foreground operations continue to run;
only a new autonomous owner is deferred.

This is a scheduling guard, not a new queue or filesystem layer. The current
trace records remain in RAM until the completed HCNAMES continuation releases
the transaction. The targeted HCNAMES request should still fail visibly for a
real invalid coordinate or terminal filesystem state, but an autonomous task
must never create the busy condition between the payload and metadata halves of
one user operation.

Add a diagnostic assertion/trace for an unexpected targeted-HCNAMES busy
refusal so a future ownership regression is distinguishable from card I/O.

## 4. Reported Bank live-sound defect

The intended code path is present. Bank completion selects and aligns the
active Scene at `Core/Hardware/SD/filesystem.c:13430-13483`; Menu starts the
normal full drumset apply at `Core/Menu/menu.c:8271-8318`; and
`preset_startDrumsetApply()` arms all six slots from the current active Scene at
`Core/Bank/Scene/Preset/presetManager.c:1372-1393`. Each voice is then applied
when its old amp envelope is quiet or by the bounded force path at lines
1395-1477.

The current trace proves Bank payload/metadata commit, but it has no record for
the apply worker's selected Scene, pending mask, or terminal completion. The
card cannot reveal live DSP state. It is therefore not yet justified to name a
specific assignment as the cause of “old kit until a new Scene was selected.”

Repair the persistence/scheduling and Bank lifecycle defects first, then add a
small DEV-only start/end witness around the existing worker. It should record:

- Bank slot and `op_bank_active_scene` at Bank commit;
- `scene_getActiveIndex()`, `bank_activeSceneSlot()`, and the worker Scene when
  Menu starts Bank sound apply; and
- the final six-bit pending mask when the worker completes or is superseded.

Reproduce Bank 051, whose `bankset.bcg` selects Scene 10. If the three Scene
coordinates disagree, repair the handoff at the first disagreement. If they
agree but the pending mask does not drain, repair the existing quiet/force
worker rather than adding another Bank-specific sound path. If the worker drains
and the wrong sound remains, compare each applied voice's resident type/image
with Scene 10 immediately before `preset_resetAndApplyKitVoiceImage()`.

## 5. Post-test card findings

### 5.1 Direct final-state evidence

The final authorities do not describe one Bank:

| Authority | Bank | Other material state |
| --- | ---: | --- |
| `settings.cfg` | 051 | `autosave=1` |
| root `/.hcnames` row 0 | 050 `Full` | source 050, pending 0 |
| `.hcprms1` | 050 `Full` | valid v2, generation 19, probe 18, active Scene 5, voice mask `0x0020` |
| `.hcprms2` | 050 `Full` | valid v2, generation 20, probe 19, active Scene 4, voice mask `0x0010` |

Both records are exactly 35,026 bytes, carry valid commit markers, and pass
their physical CRC32C checks. Record B is the newer record, but neither is a
Bank match for settings Bank 051. The last Q trace record, `#146468`, reports
exactly that classification: no matching winner, `BANK_MISMATCH=1`, generation
20, and no setup-I/O/ambiguity error.

This is the runtime lifecycle dead end described in section 2.3. Q proved that
both records belong to the prior Bank and then left AutoSave unauthorized rather
than constructing the first valid Bank-051 source.

### 5.2 The two Scene loads reached HCNAMES

Root HCNAMES contains two direct Scene-source blocks:

- logical Scene 02: Scene `Chip2`, source root Scene 010, pending 1; its Kit is
  `Chip`, and its six Instrument rows are also pending 1;
- logical Scene 07: the same `Chip2` source 010, Kit `Chip`, and six pending
  Instrument rows.

This matches the reported two-destination Scene Load. It shows that Scene Load
completed its required physical HCNAMES rewrite before power removal.

The AutoSave records deliberately have not incorporated those two operations:

- both payloads still name Scene 02 `RedSnap` and Scene 07 `SoyEared`, with the
  corresponding old Kits; and
- source-table rows 3 and 8 remain inherited (`1000`) rather than direct source
  010.

That difference is not itself a failure. It is the exact reason for the durable
HCNAMES `pending=1` handoff: if power is removed while still on Load/Save, the
next boot must prefer those component sources over the older AutoSave payload.
Pending is handoff/clear evidence, not the component-winner gate: under the
approved rule the resolvable HCNAMES source/name difference wins even if its
pending field is already zero.
The failure is one level above it: HCNAMES still identifies Bank 050 while
settings identifies Bank 051, so the Bank-agreement prerequisite rejects the
otherwise useful component handoff.

The generation-19 record has only payload offsets 12..14 dirty (active Scene and
voice-edit mask); generation 20 has those bits cleared. Neither carries pending
Scene-02/07 parameter work. This is consistent with the Scene operations being
left to HCNAMES while the page guard prevented a later normal AutoSave drain.

### 5.3 Trace limitations

The trace contains a dropped-record publication of 12,197 entries and a very
large number of per-field dirty records. It is reliable for the terminal Q,
generation promotions, and the absence of a recent internal-operation `E`, but
it is not complete enough to reconstruct every user action solely from record
order. The copied card proves the final identities and pending rows directly;
those facts take precedence over an inferred click chronology.

The verifier reports additional differences when comparing the final state to
Bank 050 or Bank 051. The root Bank mismatch and raw record fields above are
direct evidence. Some full-tree messages are secondary because the current
verifier assumes a complete Bank and does not yet model firmware's Bank-matched
winner selection. Do not expand the firmware fix in response to those secondary
messages until the verifier changes in section 2.5 land.

## 6. General implementation plan

### Phase 1 — protect foreground Load/Save completion

1. Add one Menu accessor for the existing transaction interval: accepted
   payload request through post-apply HCNAMES completion.
2. Make settings, trace, and AutoSave schedulers decline new work while that
   interval is active. Do not cancel an operation already in progress.
3. Keep Kit/Instrument scroll responsiveness and supersession behavior
   unchanged. Saves and explicit Scene/Bank loads retain their current input
   lock.
4. Add a diagnostic distinction between “targeted HCNAMES request was refused
   busy” and a real HCNAMES I/O failure.

This phase should remove the repeated Kit/Instrument `FsErr` without changing
payload readers or AsyncFATFS.

### Phase 2 — correct baseline safety

Fix the initial/recovery mask formatter and add direct geometry/semantic tests
before enabling any new Q recovery route. This phase must precede a hardware
test that cuts power between recovery creation and the first full drain.

### Phase 3 — make a Bank transition one complete identity transaction

1. Centralize mounted-source revocation and use it for true Bank Load/Save
   identity changes, OFF-to-ON, and source contradiction. Same-Bank operations
   retain the cache.
2. Restrict HCNAMES preserve/rebuild suppression to active boot reconstruction.
   Runtime Bank Load always stages and durably writes its Bank row and selected
   child blocks.
3. Preserve the current Bank completion order: payload -> Bank/index as
   applicable -> HCNAMES -> settings -> callback. Queue Q only after these
   durable authorities agree.
4. Change Q's valid-no-match result from terminal disable to asynchronous
   B-then-A recovery using the settled resident Bank and safe HCNAMES mirror.
   Publish the new mounted cache, enable tracking, and mark the full resident
   Bank dirty only after recovery is durable.
5. Reserve setup-failed for actual terminal media error, malformed/duplicate
   root evidence, or unsafe HCNAMES. A normal previous-Bank pair is recoverable
   state, not a failure.

### Phase 4 — isolate the Bank DSP handoff

Add the bounded DEV witnesses described in section 4 and rerun Bank 051 Load.
Fix only the first proven mismatch or stalled existing worker. Do not create a
second Bank-only DSP apply implementation.

### Phase 5 — close review and tooling gaps

1. Show one post-boot HCNAMES component-failure notification while preserving
   the existing per-Scene fallback.
2. Bring `verify_bank_autosave.py` into exact agreement with firmware candidate,
   grammar, partial-Bank, pending, and mask rules; add small fixtures.
3. Remove the two no-op Menu helpers and stale “fully verified” documentation
   only after hardware acceptance.

## 7. Acceptance checks

### 7.1 Kit and Instrument Load

- Repeatedly scroll Kit and every typed Instrument library while the trace ring
  is both empty and full.
- Confirm payload apply remains responsive, the final stable selection alone is
  published, no generic `FsErr` appears, and no autonomous operation begins
  between payload completion and HCNAMES final sync.
- Exit or switch Load/Save type during an in-flight operation and confirm the
  request is honored only after the final stable HCNAMES continuation.
- Repeat with DEV logging off to confirm the guard is a general ownership rule,
  not a logging-only timing workaround.

### 7.2 Bank transition

- Start with valid Bank-050 A/B records, then Load Bank 051.
- Before success is reported, verify root HCNAMES row 0 and `settings.cfg`
  identify 051. After leaving Load/Save, Q may report the old pair as a Bank
  mismatch, but it must asynchronously rebuild and mount a valid Bank-051 pair
  rather than disable tracking.
- Repeat for Bank Save-as, same-Bank Load, same-Bank Save, and OFF-to-ON. Only
  real identity transitions/re-enable should revoke and rebuild.
- Inject a duplicate HCNAMES name, CRC failure, and terminal media error to
  confirm unsafe evidence still fails closed.

### 7.3 Pending Scene handoff

- With all three Bank authorities on 051, load root Scene 010 into resident
  Scenes 02 and 07 and power off immediately after completion without exiting
  Load/Save.
- Before reboot, require HCNAMES source 010 and pending 1 for both complete
  Scene blocks. It is acceptable for AutoSave payload/source rows to remain on
  the prior generation.
- On reboot, require Bank agreement first, then reconstruct Scene 02 and 07 from
  Scene 010 because HCNAMES wins those component differences. A later normal
  out-of-menu AutoSave generation must incorporate the payload and sources
  before clearing the pending fields.

### 7.4 Recovery mask and power loss

- Inspect freshly recovered A and B and require live-owner mask bits, not an
  all-zero mask.
- Cut power after B sync, after A sync, and before/during the first full drain.
  At the next boot at least one record must validate, and its mask must prevent
  zero baseline payload from being treated as complete.

### 7.5 Bank sound

- Load Bank 051 with Scene 10 selected by its manifest and confirm the first
  post-load triggers use Scene 10's `Chip` Kit without a manual Scene change.
- Correlate Bank commit, Menu apply start, and apply terminal witnesses. Test
  both quiet voices and continuously retriggered voices so the normal and force
  paths are covered.

## 8. Completion boundary

Session 060 is not complete merely when the firmware builds. Completion
requires the repeated Kit/Instrument test, Bank-identity transition test,
pending Scene power-off test, baseline-mask power-cut test, and Bank live-sound
test above, followed by an updated authoritative verifier run. The original
S060 plan remains historical design context; this document is the active fix
scope for the first implementation.
