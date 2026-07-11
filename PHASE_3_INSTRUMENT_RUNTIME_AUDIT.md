# Phase 3 Instrument Runtime Audit

## 2026-07-10 - Descriptor menu naming cleanup

Scope:

- Reworked instrument descriptor source order for Drum, Snare, Cymbal, and
  HiHat parameter tables.
- Kept runtime behavior and storage keys unchanged.

Changes:

- Descriptor rows now appear in source/read order:
  `file_key`, `category`, `long_name`, `short_name`.
- `ParamDescriptor`'s C struct field order remains the original ABI layout:
  `file_key`, `short_name`, `long_name`, `category`. The row macros remap the
  new source order into that stable storage order so stale-object/header layout
  issues cannot put file keys on the menu screen.
- The row macros remap the readable source order back into the existing
  positional `ParamDescriptor` ABI layout; no menu-side name fallback is used.
- Local descriptor macros now take strings in the same order:
  `ROW(key_, cat_, long_, short_, dtype_, ...)`.
- All instrument descriptor rows were mechanically rewritten from the old
  `key, short, long, category` order to the new
  `key, category, long, short` order.
- The LFO target rows now read as display-first descriptors, for example:
  `ROW_NOBIND("lfo_target_voice", "LFO", "DstVoice", "voi", ...)`.

Expected menu effect:

- Single-parameter edit view should continue to show descriptor
  `category + long_name` in the top row.
- The file key, for example `lfo_target_voice`, remains available only through
  `descriptor->file_key` for instrument file parsing and runtime special-case
  lookup. Menu display paths use `descriptor->category`, `descriptor->long_name`,
  and `descriptor->short_name`.

Verification:

- `make` passes.
- Build warnings are the existing libc-nano syscall stubs (`_close`, `_lseek`,
  `_read`, `_write`) plus the existing serial LTO note.

Follow-up after hardware still displayed `lfo_*` keys:

- Removed the defensive `menu_descriptorLongName()` helper and the
  designated-initializer macro layer. They did not fix the observed hardware
  behavior and obscured the real menu/render trace.
- See `MENU_LFO_MISNAMED_AUDIT.md` for the current trace from encoder button
  sampling through descriptor lookup and LCD rendering.
- Root cause found: the fixed-width descriptor display helper read past the
  first string NUL while padding fields. `"LFO"` became `LFO lfo_`, and `"FM"`
  could become `FM  osc2`, because adjacent string-literal bytes were copied
  into the remaining field columns.
- The helper is now the general `menu_copyPaddedField()`: copy at most field
  width, stop at the first NUL, and pad the remainder with spaces. This covers
  descriptor category, long-name, short-name, and target display fields without
  any LFO-specific fallback.
