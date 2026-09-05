# `.hcnames` Instrument Type Field — Implementation Draft

Status: pre-implementation, prerequisite for the AutoSave boot reader
(S061_AUTOSAVE_READER.md §6). Must land and be card-verified before any
reader work begins.

---

## Goal

Extend Instrument rows (33..128) in `.hcnames` from three fields
(`name<TAB>source[<TAB>R]`) to four fields
(`name<TAB>source<TAB>type[<TAB>R]`). Bank/Scene/Kit rows (0..32) are
unchanged.

## Why

The boot reader's Case 2 needs to know which of the four typed directories
(`Instrument/Drum|Snare|Cymbal|HiHat/`) an Instrument row belongs to. The
autosave payload's own type byte is not trustworthy as a substitute source
for a component the reader has already decided is unproven. The type must
be in `.hcnames` as durable, independently authoritative state.

## What changes

### Parser: `filesystem_cacheResidentRecord()` (~filesystem.c:5430)

For rows 33..128 only, after parsing the source column and the optional
`R` flag, parse an additional tab-separated `type` field. Valid type tokens
are the four `instrument_type_t` values (map to a compact single-character
or short token — exact encoding TBD during implementation). A missing or
malformed type field on an Instrument row fails the read, same as any other
malformed extended record. Rows 0..32 are unaffected.

Store the parsed type in a suitable location — either a parallel array or
packed into the existing `fs_resident_source[]` register's unused bits
(evaluate during implementation).

### Formatter: `filesystem_formatResidentNameLine()` (~filesystem.c:20423)

For rows 33..128 only, emit the type field as a tab-separated column
between source and the optional `R` flag:
`name<TAB>source<TAB>type[<TAB>R]\n`.

### Writer call sites

Every existing call site that writes an Instrument row must supply type,
which is already known resident state (`kit_instrument_slot_t.type` at
SceneData.h:61) at every one of these call sites:

- Instrument Load completion
- Instrument Save completion
- Kit Load completion (6 Instrument rows)
- Kit Save completion (6 Instrument rows)
- Scene Load completion (6 Instrument rows)
- Scene Save completion (6 Instrument rows)
- Bank Load completion (6 Instrument rows per child Scene)
- Bootstrap writer (`filesystem_writeResidentNamesBlocking()`)

### Spec update

`FILESYSTEM_SPEC.md`'s HCNAMES row-format table must be updated in the
same change to reflect the 4-field Instrument row format.

## Cutover

Delete the existing `.hcnames` from the dev card. The bootstrap writer
regenerates it on the next boot with the new 4-field format.

## Verification

- Instrument Load/Save, Kit Load/Save, Scene Load/Save, Bank Load each
  produce a correctly-typed 4-field Instrument row.
- A malformed or missing type field on an Instrument row (33..128) fails
  the read.
- Bank/Scene/Kit rows (0..32) are unchanged at 2-3 fields.
- Round-trip: `.hcnames` survives a full boot-load-save cycle with types
  intact.
