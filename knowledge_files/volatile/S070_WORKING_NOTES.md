# S070 Inter-Session Working Notes

Last updated: 2026-09-22 (Session 069 closure)

## Pass 2 Hardware Validation (PENDING)

Session 069 Pass 2 implementation is code-complete at commit `9627f70` but has
not been hardware-validated. The validation must confirm:

- Shared background CPU budget enforces 2.5%/5% limits during playback/stopped
- `H` trace reports show per-class budget accounting (DEV build)
- Load/Save repair gate suppresses repair on Load/Save pages
- Pattern quiet window (250 ms) and max latency (5000 ms) behave correctly
- No audio glitches under sustained editing with budget active

## Non-Semantic Maintenance Observations

The non-semantic scheduler rung was hardware-validated in S069 (PASS). Key
observation: the arm/due-tick debounce with non-active-first Scene selection
works correctly. The idle tick-tail `patSvc_countUsed()` call was removed;
mutation-path recounts are retained.

## Reactive Compaction Notes

Periodic Tier 2 sweep is deleted. Reactive recovery triggers only on blocked
allocation. Two-pass search: reservation-respecting first, then reclaim surplus
when density inactive. This was hardware-validated (PASS) in S069.

## Known Deferred Items from S069

1. **Budget extraction**: The budget primitive lives in filesystem.c alongside
   its only consumers. If future non-filesystem modules need budget gating,
   extract to a standalone module. Not needed now.

2. **Repair gate during Load/Save**: Currently uses `menu_activePage` check.
   If Load/Save lifecycle becomes more complex, consider a dedicated
   `filesystem_isLoadSaveActive()` predicate.

3. **Per-track scale and per-track shuffle**: Listed in SCOPING_TARGETS as
   Phase 4 remaining items. Not addressed in S069.

## AutoSave Re-Enable Observations

The OFF-to-ON implementation is correct per S070_AUTOSAVE_REENABLE.md
analysis. The apparent non-convergence was caused by Pattern maintenance churn
(now fixed by S069 non-semantic separation). Test matrix items 1-6 from that
document should be exercised in S070.

## Build Metrics at S069 Closure

- text: 450,140 bytes
- data: 416 bytes
- bss: 291,756 bytes
- image: 450,572 bytes
- Commit: `9627f70` on `dev-ph5-effects`
