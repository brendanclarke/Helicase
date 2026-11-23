# S099 Inter-Session Volatile Notes

## Open items carried forward from S099

- **Fix 3 and §11 not yet device-tested:** flash the 730,952 B image, test
  scalar edit (5 s wait), step edit, both together, after a Load/Save page
  visit, and the VOICE step-edit return page. See P3 §14.

- **P2 device matrices outstanding:** R-S by ear at the new 0.39 % floor,
  REx removal on stored Scenes, effect page and step edit UX, library save
  confirmation on clx and alx.

- **R-S low-frequency alias (P2 §6):** options E–H documented with costs.
  Decision deferred until device tests confirm audibility.

- **D-G5 at CPI > 1.4:** if the device p95 exceeds 21,803 cycles/block,
  C1 (drop pre-pole) is the remaining option; needs user decision.

- **Other `#if DEV_MODE_LOGGING` blocks (P3 §5, §15):** not yet audited
  against the §0 diagnostic priority rule.

- **ADV_2 boot 5 drain error (P3 §6):** phase 41, unexplained, not repeated
  on later cards.

- **Legato (P2 §11):** the `leg` roll value and its rules wait for answers
  to the eight questions asked in the session.

## Flash budget after S099

- **730,936 B** (headroom **22,728 B**)
- ITCM: **15,432** / 16,384 B (952 B free)
- SRAM1: ColloidVoice 1,224 B (was 1,236), AlkalineVoice 640 B (was 564),
  `instrumentManager_rightBlock` 24 B
- DTCM: voice units from existing arena (4,416 B per chorus slot while
  fx != off), no new allocations
- RAM (AutoSave P3): no new bytes

## ELF identity gates

- `elf_p4_check` (retained from S094)
- `elf_s099_check` 15/15 exact

## Out of scope (deferred)

- Drum-family chop (P-F / Q-2): blocked on ITCM.
- Hiss: a later Alkaline-only option.
- Colloid form S and hot-code ITCM moves (D-10): not now.
- Eight Colloid oscillators: shown not to fit.
- Saw 4 / Saw 5 (+3-octave chops), Pulse 3.
- Ensemble tap: deferred until ear check.
