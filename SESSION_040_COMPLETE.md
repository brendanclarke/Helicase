# Session 040: verified completed work

This document is the closeout record for the implementation that is present in
the current source tree. It retains the implementation substance of the
individual work logs. Items that remain design intent, incomplete APIs, or
historical claims that cannot be verified in this checkout are recorded
separately in SESSION_040_TBD.md.

## 1. Eight-bit parameter and target-token refactor

Source record: 8_BIT_PARAM_REFACTOR.md

The resident Instrument, Scene, and Morph parameter domain is implemented as
bytes. InstrumentManager.h defines instrument_param_value_t as uint8_t and
instrument_target_token_t as uint8_t. INSTRUMENT_TARGET_TOKEN_OFF is 0xff, so
the stored target token has a compact, explicit off value rather than using a
packed 16-bit parameter ID.

The selector namespace is now role-specific:

- Local Instrument targets use compact local indices from 0 through the target
  count minus one.
- Voice targets use user-facing voice numbers 1 through INSTRUMENT_SLOT_COUNT.
- The Scene-wide voice namespace is exactly INSTRUMENT_SLOT_COUNT + 1.
- The LFO target vocabulary accepts self, the voice range, and the Scene
  namespace. The Scene value is displayed as scn; it is not a spare arbitrary
  voice number.
- Velocity modulation is self-scoped. A Velocity row exposes only targets
  owned by that Instrument plus the one source-voice Morph token (0x40) where
  applicable. It does not browse arbitrary other voice or Scene targets.

SceneData stores the velocity amount, velocity target, LFO amount, LFO target,
and related resident parameter arrays in the byte domain. The preset Morph
engine interpolates instrument_param_value_t values, and InstrumentManager,
menu handling, SceneModTargets, presetManager, and storage now use the same
byte-domain values rather than reconstructing a 16-bit packed identity.

SceneModTargets keeps wide identifiers only where they are needed to name or
resolve a parameter. Its new ID-to-index and index-to-ID helpers convert that
wide identity at the boundary; the user-editable stored token remains one byte.
Menu code expands a token to a descriptive label only while rendering. It does
not store the display expansion.

Storage parsing and writing were migrated accordingly. Compact parameter rows
are emitted as byte values and parsed as byte values. Target rows use 255 for
off and no longer write packed parameter IDs. For an LFO target voice, the
parser accepts self or a valid user-facing target voice and clamps the result
to the supported 1 through Scene namespace. Other compact rows use unsigned
8-bit parsing. This keeps saved scenes, kits, and effects within the resident
byte representation.

Pattern automation IDs were intentionally not changed. They are separate
dynamic Pattern references and are not resident Instrument parameter tokens.
That separation prevents the current refactor from accidentally changing
Pattern automation semantics.

The checked SD-card fixtures were migrated to the compact representation as
part of the refactor. Broad compatibility for legacy packed 16-bit parameter
files was deliberately not retained because this project controls the current
fixtures and generated files. The non-adopted legacy-conversion proposal is
preserved in the TBD record.

## 2. AsyncFATFS additions that are present

Source records: AFATFS_ADDITIONS_SUMMARY.md, AFATFS_EXPANSION_PLAN.md, and
ASYNCFATFS_MISSING_OPS.md.

### Physical object identity used for deletion

AsyncFATFS has afatfsObjectId_t, which carries the resolved object kind,
display/short-name information, LFN/SFN metadata, first cluster, logical size,
and attributes. Directory scanning supplies this identity to the product layer.
The product layer captures the selected existing Bank or Scene object before it
starts cleanup rather than rediscovering it later from a normalized display
name.

filesystem_directoryObjectMatchesSlot is namespace-aware. At the root it uses
the root Bank/Scene parser. Inside a Bank it uses storage_parseBankSceneFolder,
which recognizes the fixed two-digit Bank-local folders 00 through 15. This is
what prevents an existing local Scene from being missed during save cleanup.

### Native non-blocking recursive delete

afatfs_deleteTree is implemented as a foreground-polled state machine. It
accepts a captured afatfsObjectId_t, validates that the object is a deletable
file or directory, initializes a recycled file handle before use, and walks
the target recursively without blocking. Its operation state is observable
through afatfs_getDeleteTreePhase for diagnostics.

The delete-tree path retains the async lifetime rules needed by this codebase:

- A request returning false did not accept a callback; callers must not wait
  for one.
- The operation copies the root identity into durable operation state.
- A reused file handle is initialized before a new operation starts.
- The full object finder is used for LFN-bearing entries; a short-name lookup
  alone is not sufficient.
- Cache ownership is released before a handle is rebound or finished.
- A real filesystem failure is not treated as end-of-directory.
- Completion releases the cache/handle state and invokes the supplied callback
  once, including on failure.

This repairs the earlier cleanup failure that surfaced as TOut06. In
particular, it removes the bad assumption that a lookup failure meant
end-of-directory, avoids a too-small finder for LFN entries, initializes
recycled handle state, releases cache ownership, and gives the product layer a
single terminal completion instead of silently continuing.

afatfs_removeObject is also implemented for exact short-name aliases. The
legacy filesystem_deleteTree_tick path uses that exact alias to remove an empty
directory when available, falling back to the LFN route only when no exact
alias was captured. This avoids resolving an ambiguous display name to the
wrong sibling.

## 3. Sixteen-Scene Bank implementation

Source record: BANK_16_SCENE_EXPANSION.md.

SCENE_COUNT is 16 in Core/Bank/Scene/SceneData.h. BankData owns a sixteen-bit
Scene-present/voice-edit mask, the active Scene, and the restore Bank slot.
The active Scene is constrained to a Scene that is present; the current
restore convention permits the full 0xffff mask.

bankset.bcg is version 2. It writes and reads:

    format=helicase.bankset
    version=2
    active_scene=<0..15>
    scene_mask_voice_edit=0xNNNN

Bank Save serializes the selected Scene children, including all 16 possible
local folders. Bank Load reads the manifest, applies the present mask and
active Scene, and iterates the selected children rather than assuming the
older four-Scene arrangement. The local folder convention is two digits,
00 through 15, and its parser is distinct from the root bank/scene slot
parser.

At boot, the restore path uses the persisted Bank slot and the all-scenes
present mask so the normal Bank restore can make the complete Scene set
available. The active-Scene logic and navigation were updated to use the
16-Scene range rather than a fixed four-way selection.

Performance controls select the linked Scene and Pattern together. The
selection path calls seq_selectActivePattern, realigns the master pattern
state, releases notes as needed, and starts the drumset/preset apply flow.
seq_setNextPattern remains a compatibility no-op so it cannot reintroduce
Pattern-only switching. Voice-held menu handling toggles the Scene-present
mask and returns the event as consumed. LED and button overlay handling use
the same selected/present Scene state.

Scene application is intentionally staged for audio safety. Scene-wide mirrors
and decimation-related settings update immediately where required, while
individual Instrument slot changes wait until the corresponding amplifier is
quiet or until a trigger boundary makes the change safe. InstrumentManager
maintains the runtime type shadow used by that decision, and Morph work is
prioritized so the transition does not fight the staged Scene commit.

Settings migration is also present. settings.cfg has a strict current
format/version and an allowlist of global-only settings: bpm, external sync,
quantisation, global MIDI channel, MIDI TX/RX filters, MIDI routing,
screensaver, bar reset mode, input/output prescalers, follow mode, oscillator
wave interpolation, and active_bank. Scene-owned values such as Morph,
voice-Morph, and decimation are not restored as global settings. There is no
legacy GLO fallback path in the current implementation.

The save/load retest repairs described in the expansion record are present:
namespace-aware local-folder matching, LFN-aware root Scene discovery, exact
object identity for deleting an existing slot, and promotion preflight that
detects conflicting temporary/old names. Bank-promotion failures have their
own BProm diagnostic path rather than being silently merged into a generic
save result.

## 4. Bank Save failure trace repairs

Source record: BANK_SAVE_FAIL_TRACE.md.

The failure trace correctly identified a save-side cleanup problem rather than
a serialization problem. A pre-existing Bank-local folder could be missed
because the root parser did not accept its two-digit local name. Cleanup then
entered the old recursive name-based path, whose LFN resolution could select a
wrong sibling. Failed removal left an old directory in place; later save steps
then collided with that remaining directory or froze while waiting for a
callback that was never accepted.

The implemented repair is stronger than routing a special-case call through
the old delete function:

1. The product layer recognizes a Bank-local Scene with
   storage_parseBankSceneFolder.
2. It captures the physical afatfsObjectId_t while scanning the containing
   directory.
3. It passes that identity to afatfs_deleteTree for native recursive cleanup.
4. Exact short-name aliases are used by afatfs_removeObject when the fallback
   empty-directory removal path needs one.
5. The Bank-save phase that reopens the target Bank directory uses
   afatfs_opendir with the stored short/open name, not opendir_lfn with a
   display label.

These changes keep a duplicate-LFN or display-name collision from redirecting
cleanup to an unintended directory. They also separate an actual delete
failure from normal completion, so the UI can report an error instead of
remaining frozen.

## Closeout verification

The source audit used the current implementation, not only the historical work
logs. The Bank Load repair was hardware-confirmed by the user. Static diff
whitespace validation passed. Firmware compilation remains an environment
limitation because the ARM cross compiler is not installed on PATH.

## 5. Bank Load child-discovery reset

Source record: BANK_LOAD_FIX.md

The immediately preceding Bank Save work produced structurally valid Banks.
Loading SD_CARD/Bank/000 Full nevertheless failed with ERR BnkL14 because
Bank Load reused stale per-child names while delegating to the shared Scene
loader.

The valid bankset.bcg was version 2 with active_scene=6 and
scene_mask_voice_edit=0x0040. Its local Scene payloads 00 through 15 included
sceneset.scg, a Kit directory, pattern.pat, and effects.fx. The failure was
therefore loader state leakage, not a corrupt saved Bank.

Core/Hardware/SD/filesystem.c now contains the private
filesystem_resetSceneLoadChildDiscovery helper. It clears:

    op_scene_child_open_name
    op_scene_child_display_name
    op_scene_pattern_open_name
    op_scene_effect_open_name

The root Scene loader uses the helper at its phase 0. Bank Load calls it before
delegating the first selected Bank child and again before every later selected
child. This gives each child a fresh discovery lifetime. Without the later
call, child 01 could retain child 00's Kit Brezel name and try to open it
inside a directory that actually contains Kit Forest.

filesystem.h now documents the public Bank Load contract: every local child is
an independent Scene payload, and its Kit/pattern/effect discovery cannot
inherit filenames from child 00. The code comments document that BnkL14 is the
Bank wrapper's decimal phase 20 rendered in hexadecimal, not a direct report
of Scene phase 14.

The reset changes only transient discovery scratch. It does not reset the Bank
manifest, selected-child iterator, active Scene, present mask, or loaded Scene
data. Hardware confirmation from the user established that the repaired Bank
Load succeeds. git diff --check also passed. A local firmware build was not
possible because arm-none-eabi-gcc is unavailable in this checkout, and no
build artifact was modified.
