# S103 P6 records (Colloid mono filter mix; paraphonic release hold)

What:       the measurements S103_P6_COLLOID_TWEAKS.md's gates are judged by
            (2026-11-24, the S103 P6 image).

- colloid_cpu.txt: colloid_cpu.py on the linked image. It now charges
  col_monoMixControl() (60 a block) to the ser modes and fails a ser mode
  over its par mode (gate G6-1). Every ser mode is 7.1-7.2 under its par
  mode (was 26.8 under); every par mode is within 0.1 of S103 P5.
- col_render.txt: elf_loopdump.py --sym col_renderSer --sym col_renderPar.
  col_renderSer's loop is 116 instructions (99 before S103 P6);
  col_renderPar is unchanged. elf_s099_check reports col_renderSer as the
  one changed loop of its 15 (by design; its S099 baseline is kept).
- elf_hotloops.py against ../s103/mixer_base.txt: 40 equal, 1 shrank,
  0 grew.
- sizeof(ColloidVoice) = 1,248 B (+28 B; slot reserve 1,264 B), read from a
  compiled probe; midiNoteMode_relStart = 24 B (.bss, nm).
