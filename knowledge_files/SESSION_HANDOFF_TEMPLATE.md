# Session Handoff Template

## How to start a new session

Paste the following at the start of each conversation, filling in the bracketed fields:

---

**Project**: LXR-02 firmware port (STM32F765VIH6)  
**Session goal**: [e.g. "Port the sequencer engine from the original LXR AVR/STM32 source"]  
**Last session summary**: [paste the "End of session" block from the previous handoff, or "first session after hardware testing"]  
**Working repository**: [absolute path to the local working directory and current branch/status, if git is available]  
**Constraints today**: [e.g. "keep changes to sequencer files only", "don't touch USB", "15 minutes available"]

Key files to be aware of:
- Original LXR source is at `knowledge_files/LXR-master/` (AVR: `front/LxrAvr/`, STM32F4: `mainboard/LxrStm32/src/`)
- Current port lives in the local working repository directory.
- Knowledge files: `README.md`, `MEMORY.md`, `knowledge_files/log_archive/000_SESSION_INDEX.md`, `knowledge_files/hardware_archive/HARDWARE_MAP.md`, `knowledge_files/hardware_archive/AVR_TO_F765_MIGRATION.md`, `knowledge_files/ENHANCED_FEATURES.md`

---

## End of session block (Fill this in at session end)

```
DATE: [date]
SESSION GOAL: [what we set out to do]
COMPLETED: [what actually got done]
VERIFIED ON HARDWARE: [yes/no, what was tested]

CHANGES THIS SESSION:
- [file]: [what changed]
- [file]: [what changed]

KNOWN ISSUES INTRODUCED: [any new bugs or technical debt]
KNOWN ISSUES RESOLVED: [any previous issues fixed]

NEXT SESSION RECOMMENDED GOAL: [most important thing to do next]
BLOCKERS: [anything that needs hardware testing or decisions before proceeding]

CRITICAL REMINDERS FOR NEXT SESSION:
- [anything you must not forget — e.g. "EXTI_IMR must remain cleared at top of main()"]
```
