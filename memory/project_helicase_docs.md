---
name: project-helicase-docs
description: "How this project (LXR-02/Helicase firmware port) organizes its durable documentation, separate from its code"
metadata: 
  node_type: memory
  type: project
  originSessionId: 653c31c6-2147-4672-a527-2ec02f252e00
  modified: 2026-09-12T15:01:09.380Z
---

Helicase is a from-scratch port of the LXR-02 (LXR 0.37) drum machine
firmware to STM32F765VIH6, built session-by-session over an extended period
(sessions numbered from 001, past 063 as of 2026-09-12). It carries an
unusually rigorous, three-tier documentation system that a session doing any
non-trivial work is expected to maintain:

- **`knowledge_files/log_archive/000_SESSION_INDEX.md` +
  `0NN_SESSION_HANDOFF_LOG.md`** — the permanent, append-only session
  history. Every session gets a terse index entry and a full verbose
  handoff log. This is the "what happened and when" record.
- **`knowledge_files/specification_reference/*.md`** (currently 9 files:
  `FILESYSTEM_SPEC.md`, `AUTOSAVE.md`, `DEV_MODES.md`,
  `ASYNCFATFS_REFERENCE.md`, `MODULE_INTERCHANGE_SPEC.md`,
  `SRAM_MANIFEST.md`, `OSC_INTERP_AUDIT.md`, `CPU_USE_DSP_AUDIT.md`,
  `PATTERN_DYNAMIC_STACK.md`) — the
  durable, authoritative "what is currently true" record, each owning a
  distinct subsystem. These are living documents, edited in place session
  over session, not append-only.
- **`MEMORY.md`** (project root) — working memory read at the start of every
  session: current source-tree pointer, a dense running narrative of recent
  sessions, known issues, hardware pin/IRQ tables, and DSP/RAM rules. Distinct
  from this Claude Code auto-memory system — it is the project's own file,
  read by whatever agent works the codebase next.
- **`SCOPING_TARGETS.md`** (project root) — the deferred-work/roadmap
  tracker, organized by numbered Phase plus dated per-session sections for
  items found and deferred along the way. Cross-referenced by exact line
  number from other documents, so it is edited by appending dated resolution
  sections rather than rewriting entries in place.
- **Root-level `S0NN_*.md` files** are session-scoped working documents
  (plans, diagnostic reports, test checklists) — ephemeral by design, meant
  to be consolidated into the four bullets above and then deleted once a
  session closes.

This project also enforces a strict RAM-allocation approval policy (see
`MEMORY.md`'s opening section): any new/enlarged static allocation needs an
explicit byte count, region, lifetime, and owner before implementation, which
is why `SRAM_MANIFEST.md` tracks allocation deltas so carefully across
sessions.
