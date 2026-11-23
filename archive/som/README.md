# SOM generator (archived, S097)

These are the four SOM ("self-organising map") Pattern generator sources, moved
here from `Core/Bank/Scene/Pattern/`. They are reference only: they are not in
the Makefile, and no include path points here.

- `SomGenerator.c/.h`: X/Y position, flux and per-track frequency state;
  `som_tick()` interpolated four map nodes every 8th step and triggered tracks.
- `SomData.c/.h`: the nine map nodes `Node_0..Node_8`, each `[7][128]` uint8.
  Only every 8th step holds data, because `som_tick()` reads only steps that are
  multiples of 8. A re-implementation can store `[7][16]` (1,008 B instead of
  8,064 B).

Why archived: the generator could never run. `seq_SomModeActive` in
`sequencer.c` was a static that nothing set, so `som_tick()` was dead code. The
page (SHIFT+MODE STEP) edited state that nothing read.

What was removed with it, and the ids kept as unused slots so nothing else
moves: `S097_P0_PROGRAM_FLASH_USE_REFACTOR.md` §8.

To bring it back: restore the files to `Core/Bank/Scene/Pattern/`, add both
`.c` files to `SRCS`, call `som_init()` from `main()`, restore the
`menu_parseGlobalParam()` cases for `PAR_POS_X`, `PAR_POS_Y`, `PAR_FLUX` and
`PAR_SOM_FREQ`, the `SOM_PAGE` row in `menuPages.h` and the
`SELECT_MODE_SOM_GEN` page switch in `buttonHandler.c`, and give the sequencer a
way to enter SOM mode.
