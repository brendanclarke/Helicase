# AsyncFATFS: recommended follow-up work

This is the Session 040 follow-up list for Core/Hardware/SD/asyncfatfs only.
It records the difference between the verified native object/delete support
and the broader API that is declared or planned but not safely implemented.
Bank-specific regression testing is retained in the Session 040 handoff log,
not here.

## Current verified baseline

AsyncFATFS is a single-context, foreground-polled FAT32/VFAT layer. LFN
component creation/open, object iteration, exact short-alias removal, and
native non-blocking recursive directory deletion are implemented.

afatfsObjectId_t retains an object's kind, display component, printable short
alias, LFN/SFN entry locations, first cluster, logical size, and attributes.
afatfs_deleteTree copies that identity into its private operation state, walks
the selected directory without re-resolving a display name, releases its
retained cache state, resets the private handle, and invokes the result callback
once on every terminal path. afatfs_getDeleteTreePhase exposes its current
phase for diagnostics.

This baseline repaired the cleanup failure that appeared as TOut06: it uses the
full LFN object finder, initializes a recycled handle, does not treat a real
lookup failure as end-of-directory, and releases cache ownership before the
operation completes. Product code now uses that identity-based deletion for
same-slot Bank/Scene cleanup.

## 1. Make the public API truthful before expanding it

The header exposes AFATFS_CREATE_REPLACE_FILE and declares parent-relative,
move, copy, and tree-replace functions. They must not be presented as usable
features until each has a real state machine and a complete lifecycle.

In particular, afatfs_moveObject currently allocates a handle but does not
initialize recycled state, retains the caller's destination-name pointer, and
enters a dispatcher continuation that does no move. Copy-tree and tree-replace
are likewise declarations/operation slots rather than completed product APIs.

Recommended action:

- Remove or clearly mark unsupported public declarations until implemented.
- Remove AFATFS_CREATE_REPLACE_FILE, or make it fail explicitly, until it has a
  documented durable replace protocol. A conventional create call must not
  imply atomic replacement.
- For every accepted operation, enforce the existing contract: false means no
  callback; accepted inputs that outlive the call are copied; a recycled handle
  is initialized; cache sectors are released before rebind/finish; and exactly
  one terminal callback is delivered.
- Expand terminal reporting beyond a generic boolean where the caller needs to
  distinguish collision, not found, wrong type, corrupt LFN, invalid name,
  unsupported layout, and I/O failure.

## 2. Complete parent-relative object capability and namespace APIs

afatfsObjectId_t is sufficient for the current exact-delete use case but is not
yet a complete parent-bound mutation capability. It needs durable parent
identity plus the raw SFN identity required to prove that a later rename/move
or delete still addresses the selected entry.

The declared afatfs_findFirstObjectInDir, afatfs_fopenChild, and
afatfs_mkdirChild APIs are not implemented. Existing LFN open/create APIs
operate through mutable current-directory state, which is unsafe when
asynchronous product flows interleave.

Recommended action:

- Implement parent-relative lookup, open, create, and directory-create around
  a captured parent handle/capability instead of global currentDirectory.
- Give each create call an explicit policy: fail-if-exists, open-existing, or
  create-exclusive. Do not silently merge with a stale directory.
- Return a result that distinguishes absent object, existing collision, wrong
  object type, invalid component, and real storage failure.
- Copy display components into operation-owned storage; never retain a caller
  buffer pointer across foreground polls.

## 3. Harden the native recursive-delete primitive

The current native delete-tree is the correct baseline, not the end state.
Before it becomes a universal destructive primitive, add:

- parent/object relation validation at each destructive SFN entry update;
- structural validation of dot and dot-dot entries rather than relying on their
  display labels;
- a visited-directory or bounded-depth/cycle guard for corrupt cluster chains;
- structured partial-progress reporting when deletion fails after children have
  already been removed; and
- a defined caller recovery policy for a partially deleted replacement tree.

Maintain the current error semantics: failure must never be converted to
end-of-directory, and completion must always release the retained cache and
private handle before the callback can start new work.

## 4. Implement rename and cross-parent move deliberately

Same-parent rename and cross-parent move are different operations. Same-parent
rename modifies one directory entry run. Moving a directory across parents also
requires updating its dot-dot entry, validating LFN/SFN construction in the
destination, applying collision policy, and rejecting a move into itself or a
descendant.

The implementation must use source and destination parent capabilities,
operation-owned names, explicit foreground phases, initialized handles, and
one terminal result. FAT has no atomic multi-directory rename: any
intermediate state must be explicitly documented and recoverable after power
loss.

## 5. Implement non-blocking tree copy

afatfs_copyObjectTree needs an explicit bounded source/destination work stack
or queue. It must open each source by identity, create the destination with an
explicit collision policy, copy file data in bounded chunks, descend into child
directories, and report an error without blocking the foreground audio loop.

Define the maximum nesting/state capacity, corruption/cycle behavior, cleanup
of a partial destination, and the result presented to a caller when a copy is
only partly complete. The operation must not depend on global current-directory
state or caller buffer lifetime.

## 6. Implement durable tree replacement and recovery

afatfs_beginTreeReplace, afatfs_commitTreeReplace, and
afatfs_abortTreeReplace need a real on-card transaction/recovery protocol
before any feature claims power-loss-safe replacement.

The required durable sequence is:

    create a unique staging sibling
    write, close, and sync the staged payload
    validate the staged tree
    rename/preserve the old target as a unique backup
    promote staging to the final target name
    remove the backup only after durable successful promotion

Staging and backup names must be discoverable at boot. Recovery must decide
whether to resume, roll back, or report an interrupted operation. Closing a
file is not a persistence boundary; the relevant FAT/directory writes must be
flushed/synced before the next durable state transition. Two VFAT LFN renames
cannot be atomic, so recoverable intermediate state is mandatory.

## 7. Integrate only after the primitive contracts exist

After the primitives above are complete, migrate Bank, Scene, Kit, and settings
save paths to parent-relative selection, exclusive creation, physical identity,
durable replacement state, recovery, and structured UI-visible errors.
Until then, preserve the current narrower safe behavior: namespace-aware slot
matching, captured object identity for deletion, LFN-aware scanning/opening,
promotion preflight, and explicit bounded failure reporting.
