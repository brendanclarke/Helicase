# S071 Inter-Session Working Notes

Last updated: 2026-09-25 (Session 070 closure)

## S071 Plan

`S071_VOICE_MORPH_AUTOMATION_MODULATION_CLEANUP.md` defines three parts:

- **Part A** — Per-Scene voice-edit mask (BankData array, Autosave format
  expansion, bankset.bcg per-Scene keys with legacy fallback, morph rebuild
  on Scene switch). 10 implementation items (A1–A10).
- **Part B** — Base-independent LFO voice-morph contribution (direction+depth
  representation, resolver rewrite, polarity encoding without base read).
  4 implementation items (B1–B4).
- **Part C** — Scene superpage live display, held-step underline, boot-state
  cleanup. 3 items (C1–C3). C2 is an independent one-liner. C3 is resolved
  by Part A landing. C1 depends on Q-C1 decision.

Implementation order: A1–A3 → A4–A7 → A8–A9 → A10 → C2 → C1 → C3 → B1–B3 → B4.

## Open Questions

**Q-C1**: Should the Scene superpage show live effective values for all
automatable Scene settings (morph, audio out, FX send) or only for voice
morph? If all, new effective-value getters are needed for audio out and
FX send.

**Q-C3**: Is Part A sufficient for boot-state cleanup, or should a
present-mask intersection also be added as defensive measure in
`bank_setSceneMaskVoiceEdit()`?

## Build Metrics at S070 Closure

- text: 455,804 bytes
- data: 416 bytes
- bss: 291,820 bytes
- image: 456,236 bytes
- Commit: `e3ae961` on `dev-ph5-effects`

## Carry-Over From Prior Sessions

1. **FX_SEND automation (targets 398..403)**: wired for editing/storage, apply
   path is no-op until Phase 5 FX bus.
2. **Per-track step scale and shuffle**: stored/edited/persisted, no sequencer
   playback effect. See `PATTERN_DYNAMIC_STACK.md` §6.4.
3. **Phase 4.5 copy operations**: `pat_copyTrack`, `pat_copyPattern`,
   `pat_copyBar` remain queued.
4. **Budget extraction**: filesystem.c budget primitive may need extraction to
   standalone module if non-filesystem consumers appear.
5. **Repair gate**: `menu_activePage` check works; consider dedicated
   `filesystem_isLoadSaveActive()` if Load/Save lifecycle grows complex.

## S070 Session Summary (Reference)

Four-phase systems fitness pass on `dev-ph5-effects`:
- Phase 1: Makefile `-MMD -MP` header dependency tracking
- Phase 2 (LSR-01..04): HCNAMES checkpoint, deferred write, blank/empty
  display, selection generation counter
- Phase 3: Probability gating, Scene automation targets 384–403, LED layer
  consolidation
- Phase 4: Q1 morph/Scene runtime overlay architecture, Q2 Pattern generation
  fix, Q3 transport restart restore

Durable authority: `070_SESSION_HANDOFF_LOG.md` and specification reference
updates (`MODULE_INTERCHANGE_SPEC.md`, `PATTERN_DYNAMIC_STACK.md`,
`AUTOSAVE.md`, `BANK_PRESET_ARCHITECTURE.md`).
