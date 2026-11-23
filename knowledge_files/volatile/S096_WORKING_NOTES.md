# S096 Inter-Session Volatile Notes

## Open items carried forward from S096

- **Tetrakt CPU classification:** Tetrakt is marked Basic but its final
  per-block cycle count under all-layer worst case has not been measured on
  hardware. The fused sample read (§19) and space stage share the Basic
  budget with other instruments. Confirm on device.

- **Flash headroom decision:** 10,648 B remaining (below the 16 KiB
  `link_budget.py` warning threshold). ITCM 408 B free. Any new feature
  that adds flash or ITCM needs a measured trade: either existing code
  shrinks to pay for it, or the user approves an overshoot. This is now a
  binding constraint for future sessions.

- **Host test gaps:**
  - **IM-OVL:** overlay interaction with Tetrakt layer triggers not yet
    covered by `tools/dsp_test/modules/`.
  - **LR-VAL:** LiveRecord validation for Tetrakt four-note-per-track
    triggers not yet covered.
  - **CO-NOISE:** Colloid noise source (`nos` type) spectral verification
    not yet covered. The hardware RNG is the accepted character source, but
    the one-pole HP + TPT SVF shaping chain has not been frequency-swept in
    a host test.

- **P4 baseline re-snapshot:** no frozen baseline snapshot was taken at
  S096 close. Before the next session that changes DSP, copy the current
  build outputs to `tools/dsp_test/baselines/s096/` for before/after
  comparison.

## Decisions deferred

- Tetrakt Advanced-class evaluation deferred: all modes fit Basic after
  the fused sample read, but a future layer-chain extension (e.g. per-layer
  filter) would need the Advanced budget.
- Colloid oscillator count: reduced to 10/6 for Basic-share acceptance
  (B1-B6). A future session could re-evaluate if Basic headroom grows.

## Notes for the next session

- The `.itcm.s096p3` subsection holds the Colloid noise pair loop. The
  linker order is now: `.itcm.s089` → `.itcm.s089p3` → `.itcm.s094p1` →
  `.itcm.s095p2` → `.itcm.s095p3` → `.itcm.s096p3`.
- `MidiNoteModes.c/h` is the authority for note-mode selection. Mode is
  automatic per track based on instrument type and channel configuration;
  there is no user setting.
- Colloid `ColloidVoice` is now 1,236 B; Tetrakt `TetraktVoice` is
  1,064 B. Both fit the existing 1,176 B tagged runtime slot only because
  the slot was enlarged to accommodate Colloid's S096 extensions.
