# S061 — "Err BKKit14" on Bank Load investigation

**Status:** Root cause confirmed by static trace + SD_CARD content audit. This
was a **test-fixture data issue, not a firmware bug** (see §5) — no
`filesystem.c` change was made. §7 records the data fix that was applied:
every offending instrument filename across `SD_CARD/` was renamed to fit the
8-character contract and every `kitset.kcg` that referenced one was updated
to match.

## 1. Symptom

Loading `013 MidSet` (and other Banks — see §4) prints `Err BKKit14`. The Bank
still appears to load correctly. Banks the user tried with all 16 Scene slots
filled did not show the error.

## 2. What `BKKit14` decodes to

`filesystem_makeNamedErrorCode()` ([filesystem.c:3358](Core/Hardware/SD/filesystem.c#L3358))
renders its `failed_phase` byte as two hex digits. `14` hex = **20 decimal**,
so `BKKit14` is generated from exactly one call site:
[filesystem.c:12813](Core/Hardware/SD/filesystem.c#L12813), inside
`filesystem_loadBankDirectory_tick()`'s `case 20:` — the per-child completion
handler that runs after every Bank-local child Scene finishes loading
(success or failure):

```c
case 20:
{
    uint8_t child_slot;
    if (op_close_status != FS_STATUS_DONE) {
        op_bank_scene_load_mask &= ~(1u << op_bank_child_cursor);
        op_bank_scene_failed_mask |= (1u << op_bank_child_cursor);
        filesystem_makeNamedErrorCode("BKKit", op_phase);   // <- line 12813
    }
    for (child_slot = op_bank_child_cursor + 1; ...) { /* advance to next selected child */ }
    ...
}
```

This is a **per-child, recoverable** failure path by design (see the
Session 060 handoff log, `knowledge_files/log_archive/060_SESSION_HANDOFF_LOG.md`
§4.5, which already documents this exact code and error string from an
earlier hardware test): one failed child clears its own present/load bits and
the loop continues to the next selected child, so the rest of the Bank loads
normally. That matches "the Bank appears to load correctly."

I ruled out the Bank-child selection/presence masking as the cause first:
`filesystem_loadBankDirectory_tick()` case 17
([filesystem.c:12642-12643](Core/Hardware/SD/filesystem.c#L12642)) already
intersects the requested Scene mask with the mask discovered by scanning the
Bank's actual subdirectories, so **genuinely missing/empty tail slots (e.g.
MidSet's absent slots 10-15) are never attempted** and cannot be what's
producing this error. The failure is happening on one of the Scene slots that
*does* exist on disk.

## 3. Confirmed root cause: an 8-character filename contract inside Bank-embedded Kits

Inside the shared Scene/Kit loader, after a Bank-local child's `kitset.kcg` is
parsed, there's a Bank-only validation
([filesystem.c:11036-11060](Core/Hardware/SD/filesystem.c#L11036)):

```c
if (st == STORAGE_STATUS_OK && current_op == FS_INTERNAL_OP_LOAD_BANK) {
    for (member_slot = 0; member_slot < STORAGE_KIT_SLOT_COUNT; member_slot++) {
        if (!filesystem_kitMemberNameIsCanonical(op_kitset.instrument_file[member_slot])) {
            st = STORAGE_STATUS_INVALID_FORMAT;
            break;
        }
    }
}
```

`filesystem_kitMemberNameIsCanonical()`
([filesystem.c:20329](Core/Hardware/SD/filesystem.c#L20329)) rejects any
instrument member filename whose **stem exceeds
`STORAGE_KIT_DISPLAY_NAME_LEN` = 8 characters**
([storageTypes.h:92](Core/Hardware/SD/storageTypes.h#L92)) — the code's own
comment explains why: a Bank-embedded child's identity has to fit an 8-cell
resident-name register, so a longer host-edited name "would force resident
state to keep a separate FAT key," which the Bank path doesn't support. This
check runs *only* for `current_op == FS_INTERNAL_OP_LOAD_BANK` — it's the
only call site of `filesystem_kitMemberNameIsCanonical()` in the file — so
the identical Kit loaded as a **root** Kit (not embedded in a Bank) never
hits it.

When the check fails, [filesystem.c:11151-11155](Core/Hardware/SD/filesystem.c#L11151)
sets `op_close_status = FS_STATUS_ERROR` / `op_load_invalid_layer =
FS_LOAD_INVALID_KIT`. For a Bank-local child this skips the normal
quarantine-rename branch by design
([filesystem.c:11933-11951](Core/Hardware/SD/filesystem.c#L11933), comment:
"Bank-local Kit failures no longer rename... the partial-failure contract
(clearing the present-mask bit + error overlay) is sufficient") and falls
straight through to phase 72, then to the Bank loader's `case 20` — producing
`BKKit14` and dropping just that one child Scene.

### The actual offending data in `013 MidSet`

Two of MidSet's ten populated Scenes embed a Kit with an over-8-character
instrument filename:

| Scene | Kit | Offending `file=` (kitset.kcg) | stem length |
|---|---|---|---|
| `03 JungleR` | `Kit JungleBr` | `rollinh1 6.hat` | 10 |
| `03 JungleR` | `Kit JungleBr` | `tomHigh 2.drm`, `percShk 3.drm` | 9 |
| `04 HybridA` | `Kit HybridA` | `casiops1 4.snr`, `cymMetal 5.cym`, `forestd1 1.drm`, `kickVynl 2.drm` | 10 |
| `04 HybridA` | `Kit HybridA` | `percClk 3.drm`, `hatOpen 6.hat` | 9 |

Both Kits are copies of root-library Kits that intentionally **borrow one
sample from each of several other Kits** and tag the borrowed filename with a
trailing " N" (source slot number) to keep it traceable/collision-free:

- `SD_CARD/Kit/052 JungleBr/rollinh1 6.hat` (vs. the plain
  `SD_CARD/Kit/008 Rollin/rollinh1.hat`, stem exactly 8 chars, no tag)
- `SD_CARD/Kit/057 HybridA/casiops1 4.snr` (vs. the plain
  `SD_CARD/Kit/028 CasioPop/casiops1.snr`, stem exactly 8 chars, no tag)

So the " N" suffix is what pushes these specific composite Kits over the
8-character budget — every other (non-composite) Kit in the library keeps its
sample stems at ≤8 characters and passes the check fine.

## 4. The "only sparse Banks fail" correlation does not hold up

I audited every `kitset.kcg` under `SD_CARD/Bank/` for member names >8
characters and cross-referenced against each Bank's populated Scene count:

| Bank | # Scenes | Affected Scenes |
|---|---|---|
| 001 Full | 16 | — |
| 002 LoadTst | 16 | — |
| 003 Genesis | 4 | — |
| 010 FullSet | **16** | `10 PunchIt, 11 TrapFlo, 12 JungleR, 13 GlitchX, 14 MetalW, 15 DrySnap` |
| 013 MidSet | 10 | `03 JungleR, 04 HybridA` |
| 014 AllNu | **16** | `00 PunchIt … 09 ChaosKt` (9 scenes) |
| 015 Classic | 16 | — |
| 020 Odyssey | **16** | `00 DrySnap, 15 ChaosKt` |
| 021 Finale1 | **16** | 8 scenes |
| 022 Finale2 | **16** | 6 scenes |
| (all other Banks, 004–009, 011, 016–019) | mixed | also affected |

The composite Kits (`PunchIt`, `TrapLo`, `JungleBr`, `GlitchLb`, `MetalWsh`,
`DrySnap`, `HatWork`, `HybridA`, `LoFiDrm`, `ChaosKt`) are reused across most
of the fixture set, **including several 16/16 "full" Banks**
(`010 FullSet`, `014 AllNu`, `020 Odyssey`, `021 Finale1`, `022 Finale2`).
Per the code path above, those should reproduce `BKKit14` too. Only 5 Banks
in the whole fixture are actually clean of this: `001 Full`, `002 LoadTst`,
`003 Genesis`, `012 Fusion1`, `015 Classic` — and it's a reasonable guess that
whichever "full" Bank was tried so far (likely `001 Full`, going by name) is
one of those clean ones. **Recommend trying `010 FullSet` or `014 AllNu`
next** — if `BKKit14` also appears there despite 16/16 Scenes being present,
that confirms the correlation with "empty slots" was coincidental, not
causal.

## 5. Is this a firmware bug?

I don't think so. The 8-character cap is an explicit, commented product
contract (Bank-local child identity must fit the 8-cell resident-name
register), it's enforced consistently, and the failure mode is a graceful
per-child skip rather than a crash or data corruption — the design already
matches "the Bank appears to load correctly." The same Kits load fine as
**root** Kits (outside a Bank) because the check doesn't apply there, which
is consistent with these composite Kits existing in the root `Kit/` library
as ordinary, loadable entries.

The actual defect, if there is one, is in the **test-fixture content**: the
composite/"borrowed-sample" Kits (`052 JungleBr`, `057 HybridA`, and their
siblings) use filenames that are valid for a root Kit but not valid once
copied into a Bank-embedded Scene. If the intent is for these Banks to load
with zero errors, the simple fix is a **data fix, not a code fix**: rename
the offending instrument files (and their `file=` lines in each affected
`kitset.kcg`) to ≤8-character stems — e.g. drop the trailing " N" tag or
shorten it (`casiops1 4.snr` → `casiop14.snr`, `rollinh1 6.hat` →
`rollinh6.hat`, etc.) — in every Bank-embedded copy of these 10 Kits (51
unique files total, listed via the audit script used above). Root-library
copies (`Kit/052 JungleBr/`, `Kit/057 HybridA/`, etc.) can keep their current
names since the check never applies to them.

If, instead, these two MidSet Scenes were placed there deliberately to
exercise this exact rejection path, then `BKKit14` on `013 MidSet` is working
as intended and no fix (data or code) is needed at all.

## 6. Files/lines referenced

- [Core/Hardware/SD/filesystem.c:3358](Core/Hardware/SD/filesystem.c#L3358) — `filesystem_makeNamedErrorCode()`
- [Core/Hardware/SD/filesystem.c:12642](Core/Hardware/SD/filesystem.c#L12642) — Bank child load-mask ∩ present-mask (rules out missing-slot theory)
- [Core/Hardware/SD/filesystem.c:11036](Core/Hardware/SD/filesystem.c#L11036) — Bank-only member-name canonical check loop
- [Core/Hardware/SD/filesystem.c:11151](Core/Hardware/SD/filesystem.c#L11151) — sets `FS_STATUS_ERROR` / `FS_LOAD_INVALID_KIT` on failure
- [Core/Hardware/SD/filesystem.c:11933](Core/Hardware/SD/filesystem.c#L11933) — Bank-local children skip quarantine, fall through to phase 72
- [Core/Hardware/SD/filesystem.c:12798](Core/Hardware/SD/filesystem.c#L12798) — `case 20:`, source of the `BKKit` code (0x14 = phase 20)
- [Core/Hardware/SD/filesystem.c:20329](Core/Hardware/SD/filesystem.c#L20329) — `filesystem_kitMemberNameIsCanonical()`
- [Core/Hardware/SD/storageTypes.h:92](Core/Hardware/SD/storageTypes.h#L92) — `STORAGE_KIT_DISPLAY_NAME_LEN = 8`
- `knowledge_files/log_archive/060_SESSION_HANDOFF_LOG.md` §4.5 — prior hardware-test note about this exact error string from a different (already-fixed) root cause; worth cross-referencing if this comes up again.

Also checked and ruled out along the way: stale/shared `op_close_status`
carrying over from an unrelated prior operation (Session 060 already fixed
the one instance of that class of bug); a scan/off-by-one in the Bank child
presence scan (exhaustive, order-independent, correctly excludes non-`SS
Name` entries); `bankset.bcg`'s `active_scene` value (4, in-range); macOS
metadata files (`.DS_Store` etc.) leaking into the FAT scan (none present
inside any `Bank/*/` subtree).

## 7. Fix applied: SD_CARD instrument filenames renamed

Applied the data fix from §5: every instrument member filename anywhere
under `SD_CARD/` whose stem exceeded 8 characters was shortened to fit, and
every `kitset.kcg` `file=` line referencing one was rewritten to match.

**Scheme:** each offending name has the form `<base> <slot digit><ext>`
(e.g. `casiops1 4.snr`). New name = `<base, truncated to 7 chars><slot
digit><ext>` (e.g. `casiops4.snr`) — always ≤8 characters, and unique within
its Kit folder since the 6 slot digits (1-6) never collide. Names already
≤8 characters (e.g. `Kit Barf`'s `barfd1 1.drm`, `Kit Pop`'s `popd1  1.drm`)
were left untouched — they aren't rejected by
`filesystem_kitMemberNameIsCanonical()` and weren't part of this bug.

**Scope:** all 10 composite Kits (`PunchIt`, `TrapLo`, `JungleBr`,
`GlitchLb`, `MetalWsh`, `DrySnap`, `HatWork`, `HybridA`, `LoFiDrm`,
`ChaosKt`), in every copy found under `SD_CARD/Kit/` (the root library
originals, `050`-`059`), `SD_CARD/Scene/` (root Scene-embedded copies), and
every `SD_CARD/Bank/*/*/Kit */` (Bank-embedded copies) — **88 `kitset.kcg`
files, 449 instrument files renamed**, applied programmatically (walk every
directory containing a `kitset.kcg`, rename each matching `file=` target on
disk, rewrite the line) so the mapping is guaranteed consistent between disk
and each kitset.kcg. `013 MidSet`'s two offending Kits now read:

```
# 03 JungleR/Kit JungleBr/kitset.kcg
file=tomMid 1.drm       (unchanged, already ≤8)
file=tomHigh2.drm       (was tomHigh 2.drm)
file=percShk3.drm       (was percShk 3.drm)
file=snrPop 4.snr       (unchanged, already ≤8)
file=erisc1 5.cym       (unchanged, already ≤8)
file=rollinh6.hat       (was rollinh1 6.hat)

# 04 HybridA/Kit HybridA/kitset.kcg
file=forestd1.drm       (was forestd1 1.drm)
file=kickVyn2.drm       (was kickVynl 2.drm)
file=percClk3.drm       (was percClk 3.drm)
file=casiops4.snr       (was casiops1 4.snr)
file=cymMeta5.cym       (was cymMetal 5.cym)
file=hatOpen6.hat       (was hatOpen 6.hat)
```

**Verification:** re-scanned every `kitset.kcg` under `SD_CARD/` after the
rename — zero remaining `file=` stems exceed 8 characters, and zero
`file=` references point to a filename that doesn't exist on disk. Re-running
the rename script afterward reports zero further changes needed (idempotent).

**Note on unrelated pre-existing dirty state:** `git status` on `SD_CARD/`
also shows a large number of executable-mode-bit changes (100644→100755)
across essentially the whole tree, plus modified `.hcindex`/`settings.cfg`/
`.hcnames`/`.hcprms1`/`.hcprms2` files. None of that was touched by this
rename — it predates this change (from the "post load attempt" SD_CARD
update mentioned at the start of the session) and is left as-is.
