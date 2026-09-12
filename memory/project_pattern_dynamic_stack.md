---
name: project-pattern-dynamic-stack
description: "Phase 4 dynamic Pattern system: storage verified S062, v4 PAT4 file format + load/save completed S063, S064 next for Pattern AutoSave"
metadata: 
  node_type: memory
  type: project
  originSessionId: 9f95f8c0-48d1-4eeb-9d5b-ae79ff02e893
  modified: 2026-09-12T15:00:51.859Z
---

Session 062 (2026-09-10) hardware-verified the Phase 4 dynamic Pattern storage system. Session 063 (2026-09-12) completed the persistence layer: v4 binary PAT4 file format, Pattern Save/Load with full Scene-mask support, and HCNAMES 145-row expansion.

**Architecture**: 16-Scene `pat_scene_region_t` packed structs (168,304 B SRAM1) with per-Scene address array (1,792 B), 256-chunk event pool (8,192 B), free bitmap (512 B), plus per-track params and pattern_change/next fields. 10,519 B per Scene. Option B accessors: `pat_sceneRegion()` / `pat_sceneRegionMut()`.

**v4 PAT4 format**: 10,656 B binary file. 160B header (magic "PAT4", CRC32C at offset 14, generation counter at offset 10) + 1,792B address array + 512B bitmap + 8,192B pool. Scene-mask fan-out via `filesystem_requestLoadPatternForScenes(slot, scene_mask, cb)`.

**Why:** The original LXR stored per-step note, velocity, probability, and automation in a fixed `Step` struct. The bitmap-only Session 043 representation dropped all per-step data. This dynamic system restores per-step specials with a memory-efficient pooled allocation instead of fixed-size step records.

**How to apply:** Read `PATTERN_DYNAMIC_STACK.md` before modifying PatternData internals. Copy operations are deliberate no-ops — do not enable without pool block duplication. The full v4 byte-level layout is in `063_SESSION_HANDOFF_LOG.md` §1. Related: [[feedback-session-log-consolidation]]

**Next step**: Session 064 implements Pattern AutoSave — per-Scene A/B pair files (`.pat00a`/`.pat00b` through `.pat15b`), v4 PAT4 format, whole-file drain with 16-bit dirty mask, 17th Scene snapshot region (10,519 B) with TIM3-masked memcpy. Plan: `S064_DYNAMIC_PATTERN_AUTOSAVE.md`.
