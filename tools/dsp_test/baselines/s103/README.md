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
