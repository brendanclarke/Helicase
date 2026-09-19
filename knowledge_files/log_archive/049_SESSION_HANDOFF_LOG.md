# Session 049 Handoff Log — AutoSave Write/Load Operations

**Project**: LXR-02 firmware port (STM32F765VIH6)  
**Session goal**: Complete and test the remaining AutoSave write-on-load
boundaries for normal Kit Load, KitMrp, root Scene Load without Pattern, and
selective Bank Load.

## End of session

```
DATE: 2026-08-12
SESSION GOAL: Complete the Session 049 AutoSave mutation boundaries and run
hardware SD-card tests for Kit, KitMrp, root Scene, and Bank loads.
COMPLETED: Implemented/documented normal Kit, KitMrp, root Scene, and
selective Bank completion marking. Hardware testing confirmed KitMrp and root
Scene persistence. Bank-load testing exposed a tracking-disabled rejection
and a stale Bank restore slot; Bank-load fixes are deferred.
VERIFIED ON HARDWARE: Yes. KitMrp and root Scene tests persisted valid
generation-5 autosave data. Bank load changed HCNAMES but did not change the
autosave payload; asavetrc.bin identifies the failure path.

CHANGES THIS SESSION:
- Core/Bank/Scene/Preset/presetManager.c: completed normal Kit, KitMrp, root
  Scene, and selective Bank dirty-marking boundaries.
- Core/Bank/Scene/Preset/presetManager.h, Core/Bank/Scene/Autosave.c,
  Core/Bank/Scene/Autosave.h: updated ownership and caller documentation.
- SCOPING_TARGETS.md: pinned the future type-first autosave reader handling
  for Choke versus non-Choke slot-6 track-7 decay.
- AUTOSAVE_WRITE_LOAD_OPERATIONS_COMPLETE.md: recorded implementation status,
  hardware results, and the Bank-load failure evidence.
- SD-card fixtures were read only; no firmware source was changed during the
  hardware investigation.

KNOWN ISSUES INTRODUCED: None intentionally introduced. Bank Load currently
  reaches completion marking while AutoSave tracking is disabled, so Bank and
  child Scene payload changes are not persisted. A successful Bank Load also
  left settings.cfg active_bank and the autosave restore-slot field stale.
KNOWN ISSUES RESOLVED: Normal Kit, KitMrp, root Scene-without-Pattern, and
  selective Bank write-on-load coverage was added and the KitMrp generated
  slot-6 Choke decay handling was preserved.

NEXT SESSION RECOMMENDED GOAL: Fix Bank-load persistence and restore identity:
  ensure AutoSave tracking is enabled before Bank completion markers run,
  verify the effective child mask and Bank metadata are marked, and decide the
  correct settings.cfg active_bank update policy. Re-test reboot restoration.
BLOCKERS: No build toolchain was available in this environment. Bank-load
  behavior requires firmware rebuild/flash and hardware retest.

CRITICAL REMINDERS FOR NEXT SESSION:
- Do not treat HCNAMES identity as proof that .hcprms payload was persisted.
- Decode I records using flags: 0x01 is only BASE_VALID; tracking-enabled is
  0x02 and all-published is 0x04. Expected/published counts are packed in the
  value field.
- Preserve the AutoSave reader pin in SCOPING_TARGETS.md: resolve the
  instrument's three-byte type first; slot-6 Choke owns its decay endpoint,
  while only non-Choke slot 6 uses the separate Kit track-7 decay field.
- Do not broaden this work into Pattern persistence, Save-side marking, or a
  boot reader until the Bank-load boundary is corrected.
```

## Detailed work and evidence

### Implementation boundary

The Session 049 source work completed the remaining write-on-load hooks:

1. Normal Kit completion marks each requested resident Scene's Kit scope only
   after successful filesystem completion and Scene-presence promotion.
2. KitMrp marks only compatible Morph endpoints after the staged normal-to-Morph
   commit. The generated slot-6 track-7 Morph decay continues through its
   named change-aware setter.
3. Root Scene completion marks the requested non-Pattern Scene scope only at
   terminal success, after the existing presence promotion.
4. Selective Bank completion reads the effective child mask once before
   acknowledgement and marks only those loaded child Scenes. BankData scalar
   fields remain owned by their existing setters.

The implementation retained the existing RAM, wire format, filesystem state
machine, writer policy, settings schema, HCNAMES ownership, and Pattern
exclusions. Static review confirmed callback ordering and `git diff --check`
passed for the edited source files. A compiler/build was not available.

### KitMrp and root Scene hardware test

The new `.hcprms1` was generation 5 and valid; `.hcprms2` was generation 4.
The two morph-kit test changes produced payload differences confined to two
Kit regions (reported as Scene 2 and Scene 9 in the fixture comparison). No
unrelated Bank, Scene-setting, or Effect payload bytes changed.

Two root Scene loads updated `.hcnames` to `Hard 002` and `FilMod 011` in the
resident Scene identity rows. The autosave records remained structurally
valid. The slot-6 Choke decay discrepancy remained a reader interpretation
issue, not a failed persisted Choke endpoint.

### Bank-load hardware test

The test then loaded Bank `005 Full`. `.hcnames` changed to:

```text
Full    005
```

and its child Scene identities became `Slak`. However, `.hcprms1` and
`.hcprms2` differed only in their headers (generation and CRC); the complete
34,704-byte payload was unchanged.

The final `asavetrc.bin` records contain whole-instrument `I` markers for the
Bank child destinations. Every marker had `flags=0x01`: the payload base was
valid, but the tracking-enabled (`0x02`) and all-published (`0x04`) flags were
clear. Expected counts were 74 or 76 bytes, while accepted/published counts
were zero. A following `A/V/M/T` writer sequence completed with no accepted
dirty bits. This is an obvious rejection path: the Bank completion callback
ran while AutoSave mutation tracking was disabled.

The restore identity was also inconsistent after the load:

```text
.hcnames       -> Full 005
settings.cfg   -> active_bank=12
.hcprms Bank   -> restore slot 12
```

The Bank-load fix is deliberately deferred to Session 050. The next session
must resolve both the tracking gate and the policy for scheduling a
`settings.cfg` rewrite after a successful Bank Load, then verify cold-boot
restoration.

## Documentation review at close

`AUTOSAVE_WRITE_LOAD_OPERATIONS_COMPLETE.md` now contains the implementation
record and all hardware findings. The specification references remain broadly
consistent for Bank file contents, `scene_mask_voice_edit`, HCNAMES ownership,
and AutoSave exclusions.

`MEMORY.md` is partially stale: its Quick Start still describes Session 048 as
the current source state, and its Session 049 scope note still says the Kit,
Scene, and Bank hooks are future work. Those entries should be refreshed at
the start of Session 050 to point to this handoff and to record the newly
observed Bank-load tracking/restore-slot failure. No conflicting format rule
was found in `AUTOSAVE.md` or `FILESYSTEM_SPEC.md`.
