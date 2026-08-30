# Session 058 load/save speedup final check

Date: 2026-08-29

## Final decision

No further load or save speedup is proposed for implementation from
`S058_BANK_LOAD_SPEEDUP_PROPOSAL.md` or `S058_BANK_SPEEDUP_REVIEW.md`.

Bank Load works correctly and is explicitly out of scope. Do not change its
request, traversal, parsing, commit, completion, clean-authority, or UI paths.

## Original Option 3: implemented, slower, and reverted

Option 3 attempted to accelerate Bank Save by proving that an existing Scene
tree was canonical and then rewriting its ten files in their existing clusters.
Hardware testing measured the resulting Bank Save at approximately 15 seconds
slower than the Option 2 baseline, so the implementation was reverted.

Post-revert inspection found that the implementation speculatively attempted
the retained rewrite for every selected dirty child. When its strict proof
rejected a child, it paid for some or all of the following before still running
the complete normal save:

- an exact Scene-child lookup;
- full Scene and embedded-Kit directory scans;
- additional open, close, and directory transitions;
- a return to root and reopening of `/Bank/` and the target Bank; and
- the unchanged recursive delete, recreation, ten-file write, and flush.

The implementation also omitted the proposal's Scene/Kit/Instrument rename
handling. Any desired identity or Instrument-type filename difference therefore
caused rejection and fallback. All rejection conditions were collapsed into
the single `op_bank_inplace_bad` byte, so the historical hardware run cannot
identify which exact predicate rejected. The evidence proves that speculative
rejection added work; it does not prove that a successfully admitted retained
rewrite was intrinsically slower.

The reverted `afatfs_finalizeRetainedSize()` was also incomplete as a safe
general primitive: it assigned the new logical size without enforcing writable
normal-file mode, a legal first cluster, one-cluster capacity, cursor/size
agreement, or prevention of allocation beyond the retained cluster. It must not
be restored independently.

## Option 3B: proposed and rejected

Option 3B was considered as a narrower correction. It would have retained a
volatile, current-mount witness that this firmware had already created a given
Bank Scene layout. Only a later dirty save of that same witnessed child, with
unchanged Scene/Kit/Instrument names and types, would attempt the retained-file
rewrite. Fresh, legacy, renamed, different-Bank, remounted, and force-save cases
would have gone directly through the existing delete/recreate path without the
old speculative probe.

Option 3B is rejected and must not be implemented.

Its witness would start empty after every boot or mount. Consequently it would
save **no time at all on the first full Bank Save after a fresh boot**, which is
the workflow that needs improvement. It could help only a later same-Bank save
during the same mounted session after a successful earlier save established the
witness and payload edits subsequently made one or more Scenes dirty.

That benefit is too narrow and substantially overlaps Option 2:

- unchanged, card-proven Scenes are already skipped entirely by Option 2;
- Option 3B could accelerate only the remaining dirty children whose physical
  names/types had not changed; and
- fresh-boot, fresh-target, remount, rename/type-change, and force-save work
  would retain current timing.

The required layout authority, fingerprints, alias table, retained-write mode,
failure semantics, diagnostics, and hardware matrix are not justified for a
speedup that cannot improve the target fresh-boot full-save case. The detailed
Option 3B implementation plan has therefore been removed from this document.

## Closed paths

Do not restore Option 3 or implement Option 3B. Do not change Bank Load. The
previously rejected pack-file formats, larger caches/foreground bursts,
whole-file staging, compression, and hardware-SPI/DMA proposals also remain
closed. Any future Bank Save optimization must demonstrate a credible reduction
in the first full Save after boot before receiving an implementation plan.

## AsyncFATFS investigation moved to Session 059

The stopped-playback investigation found a general AsyncFATFS directory-create
inefficiency rather than another Bank-specific optimization. LFN creation scans
past the FAT `0x00` end marker and retires the unused remainder of each newly
created directory cluster; new-directory initialization also zero-writes every
sector in the allocated cluster even when only one or two sectors become
visible. Both costs occur for any matching AsyncFATFS caller. Stopped playback
only makes the existing foreground pump drain them faster.

The complete evidence, algorithm, source-change register, compatibility matrix,
test gates, and adjacent comment-block specifications have moved to
`S059_ASYNCFATFS_SPEEDUP.md`. That work is explicitly deferred to the next
session and is not part of Session 058.

Session 058 therefore closes with the following decisions unchanged:

- Bank Load works and must not be changed.
- Option 3 remains reverted and Option 3B remains rejected.
- Do not alter the Bank schema, serializers, playback-running SD burst size, or
  filesystem ownership merely to pursue this speedup.
- The current stopped-playback four-poll drain is not a proven physical limit,
  but poll-count tuning is secondary to eliminating redundant directory I/O.
- No general AsyncFATFS source change is authorized by this document. Implement
  and validate the separately gated work in `S059_ASYNCFATFS_SPEEDUP.md`.
