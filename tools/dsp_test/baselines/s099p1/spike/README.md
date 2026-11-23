# S099 P1 spike sources (not available)

`S099_P1_IMPLEMENTATION.md` §2 step 0.6 asks for the spike sources and counts
named by `DRAFT_S-R_PWM_WAVE.md` and `DRAFT_ADVANCED_BBD_CHORUS.md` to be
copied from the session scratchpad into this folder.

The scratchpad for that session is no longer reachable from this working tree,
and nothing in the tree holds the spike sources (checked: `tools/dsp_test/spike/`
holds the S095 effects retry kit only). The spike figures this schedule quotes
(54 instructions a chop pair, 22 / 25 + 15 for the chorus loops, the 1.2 / 1.4 /
1.6 CPI table) therefore remain quoted figures from the drafts, not re-runs.

Every firmware loop this schedule adds is measured directly at its phase gate
(`elf_looppath.py`, `control_cost.py`, `colloid_cpu.py`), so the gates do not
depend on this folder.
