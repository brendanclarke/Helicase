# S103 P1 records (Katran)

What:       the measurements the S103 P1 schedule's gates are judged by.

- mixer_base.txt: elf_loopdump.py --sym mixer_calcNextSampleBlock on the
  pre-Katran image (commit 36e6d9e5, built from a git archive copy).
- katran_cpu.txt: katran_cpu.py on the linked Katran image.

Results (2026-11-24):

- elf_hotloops.py --compare mixer_base.txt /now: **0 hot loops grew**,
  1 shrank, 40 equal. Gate G-CPU3 passes (the Katran objects are
  -fno-lto, so the mixer's inlined loops did not move).
- katran_cpu.py: loops 469 + per block 2,102/32 = **534.7** all-in
  against the 380 gate (G-CPU1 fails). At the schedule's 1.52 cycles per
  instruction this is about 26.0k cycles a block against the 18.6k device
  gate.
- link_budget.py: image 694,472 B of 753,664 (59,192 B headroom); ITCM
  15,424 B (unchanged); FXBUF 126,656 B.
- Katran objects: KatranVoice 11,968 B, KatranOsc 3,620 B, KatranTables
  3,592 B (text); the image grew 19,952 B.
- sizeof(KatranVoice) is held by a _Static_assert <= 1,200 B in
  InstrumentManager.c; the build passes it.

S103 P4 (2026-11-24): cores A / B, the stereo rotation, scale folding.

- katran_cpu_p4.txt: katran_cpu.py on the linked image after S103 P4:
  loops 293.0 + per block 2,251/32 = **363.3** all-in (gate 380; was
  406.3 after S103 P2). At 1.485-1.53 cycles an instruction, about
  17.3-17.8k cycles a block: 9.3-12 % under the 19,620 Basic share.
  The count now includes kat_stereoTargets (out of line).
- elf_hotloops.py --compare mixer_base.txt (now): 40 equal, 1 shrank,
  **0 grew** (G-CPU3 unchanged).
- link_budget.py: image 695,992 B (headroom 57,672 B); ITCM 15,424 B
  (unchanged).
- sizeof(KatranVoice): 1,084 B (was 1,092 B; read from the memset in
  Katran_initVoice()), under the 1,200 B assert.

S103 P5 (2026-11-24): kat_width bipolar (no-collapse spread at 0).

- katran_cpu_p5.txt: loops 295.0 + per block 2,290/32 = **366.6** all-in
  (gate 380; P4 was 363.3: the 4-gain matrix, +2 in the output loop, and
  a third VSQRT a block). At 1.485-1.53 CPI about 17.4-17.9k cycles a
  block: 8.5-11.2 % under the 19,620 Basic share.
- elf_hotloops.py against mixer_base.txt: 40 equal, 1 shrank, **0 grew**.
- link_budget.py: image 696,256 B (headroom 57,408 B); ITCM 15,424 B.
- sizeof(KatranVoice): 1,100 B (P4 1,084; +16 B for the four matrix
  gains and steps), under the 1,200 B assert.
