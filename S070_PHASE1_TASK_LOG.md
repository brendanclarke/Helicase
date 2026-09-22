# S070 Phase 1 — Engineering Hygiene Task Log

Session: S070
Branch: `dev-ph5-effects`
Baseline: commit `65e73c6` (latest on branch at session start, 2026-09-22)
Phase scope: Makefile header dependencies, developer mode policy verification,
IWDG policy verification, spec hygiene check.

---

## 1.1 Makefile header dependencies — DONE

**Problem**: A `config.h` edit (or any shared header) could leave stale `.o`
files unless the developer manually ran `make clean`. The Makefile had no
automatic header dependency tracking.

**Change**: Two additions to `Makefile`:

1. Added `-MMD -MP` to `CFLAGS` (line 15). These flags tell GCC to emit `.d`
   dependency files alongside each `.o`, listing every header that translation
   unit includes. `-MP` adds phony targets for each header so that deleting a
   header doesn't break the build with "No rule to make target" errors.

2. Added `-include $(OBJS:.o=.d)` after the `OBJS` definition (line 153). The
   dash prefix makes this a silent include — on a clean build when no `.d` files
   exist yet, Make doesn't error. On incremental builds, Make reads the `.d`
   files and knows exactly which objects to rebuild when a header changes.

**Propagation**: `CFLAGS_DSP` is derived from `CFLAGS` via `$(subst -O2,-Ofast,$(CFLAGS))`,
so DSP sources and the four explicit instrument render rules all inherit `-MMD -MP`
automatically.

**Cleanup**: The existing `clean` target already does `rm -rf $(BUILD)`, which
removes `.d` files alongside `.o` files. No change needed.

**Assembly**: `ASFLAGS` is not modified. The single `.s` file
(`startup_stm32f765xx.s`) has no C-style `#include` directives.

**Files changed**: `Makefile` (2 insertions)

---

## 1.2 Developer mode default — DONE (policy verification)

**Requirement**: `DEV_MODE_LOGGING` remains the default build configuration.
Do not change this default.

**Verification**: `config.h:88` reads `#define DEV_MODE_LOGGING 1`. All trace
and diagnostic features are gated behind `DEV_MODE_LOGGING`,
`DEV_MODE_PATTERN_TRACE`, or similar flags. No unconditionally compiled trace
code found.

**Decision**: No code change. Standing policy confirmed.

---

## 1.3 IWDG — leave inactive — DONE (policy verification)

**Requirement**: `DEV_LOGGING_IWDG` stays present and inactive. Do not
reactivate without explicit discussion.

**Verification**: `config.h:200` reads `#define DEV_LOGGING_IWDG 0`. The
define, its timeout constant (`DEV_LOGGING_IWDG_EXPIRE`, line 220), and all
affiliated code remain in place for future reference but are compiled out.

**Historical context**: Prior activations caused boot-hang regression (S044)
and self-introduced IWDG regression (S054). Reactivation is not part of S070
scope.

**Decision**: No code change. Standing policy confirmed.

---

## 1.4 Spec hygiene — DONE (initial scan)

**Requirement**: Update specification/backlog after each phase so historical
"unresolved" notes do not continue to direct work after their implementation
has landed. This is a rolling obligation.

**Phase 1 scan**: Searched `knowledge_files/specification_reference/` for stale
`unresolved`, `TODO`, `FIXME`, and `open question` markers. The single hit
(`DEV_MODES.md:520`) is prescriptive guidance, not a stale resolution marker.
`SCOPING_TARGETS.md` has no stale unresolved notes.

**Decision**: No spec updates needed at the Phase 1 gate. Will re-scan after
subsequent phases if code changes land.

---

## Exit assessment

Phase 1 is complete. The only code change is the Makefile dependency tracking
addition (1.1). Items 1.2 and 1.3 are policy verifications with no code
changes. Item 1.4 found no stale spec material.

**Next**: Phase 2 (Load/Save revision pass) or Phase 3 (remaining Phase 4
feature behavior), per the S070 plan.

**Build impact**: The `-MMD -MP` flags add negligible compile time. The first
build after this change will generate `.d` files for every source; subsequent
incremental builds will be faster and correct with respect to header changes.
No change to binary output — these are preprocessor/dependency flags only.
