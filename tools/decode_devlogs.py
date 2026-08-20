#!/usr/bin/env python3
"""Read-only decoder for the LXR-02 development-mode trace files.

The firmware can produce these root *.bin traces:

- /bootlog.bin - an eight-byte printable operation token written after a
  pre-audio filesystem timeout/failure, optionally followed by the frozen
  64-byte HCPRMS capsule when the token is ASENSURE;
- /asavetrc.bin - a stream of eight-byte AutoSave lifecycle records
  (stage, flags, tick16, value32).

With no arguments the decoder scans the repository-root SD_CARD/ for every
*.bin trace file, creates SD_CARD/logs/ when missing, and writes one
human-readable text file per trace:
<ssmmhhdaymonthyear>_<filename-without-.bin>.txt. The default SD_CARD is
resolved from this script's own location, so the scan works from any working
directory. One path argument instead decodes that single file to stdout and
writes nothing.

Every record's raw stage, flags, tick, and value bytes are preserved in the
text output next to the decoded function name, inputs, outputs, statuses, and
field meanings.

Format authorities: knowledge_files/specification_reference/DEV_MODES.md,
Core/Bank/Scene/AutosaveTrace.h, Core/Bank/Scene/Autosave.h, and
Core/Hardware/SD/filesystem.c.
"""

from __future__ import annotations

import argparse
import time
from collections import Counter
from pathlib import Path


RECORD_BYTES = 8
CAPSULE_BYTES = 64


# ---------------------------------------------------------------------------
# /bootlog.bin: stable eight-byte operation tokens from
# filesystem_bootLogCodeForOperation(). The token carries no phase or payload;
# its meaning is the operation that reached the boot timeout/failure path.
# ---------------------------------------------------------------------------

BOOT_CODES = {
    "FSFLUSH ": ("filesystem_flushFinish_tick",
                 "final FAT sync gate after a completed operation"),
    "INSINDEX": ("filesystem_createBootIndex_tick",
                 "boot-time Instrument registry .hcindex creation"),
    "LIBINDEX": ("filesystem_createLibraryIndex_tick",
                 "library .hcindex creation/rewrite"),
    "HCNAMES ": ("filesystem_writeResidentNames_tick",
                 "root /.hcnames identity-register write"),
    "ASENSURE": ("filesystem_ensureAutosaveFiles_tick",
                 "hidden .hcprms1/.hcprms2 A/B pair ensure; a frozen "
                 "timeout appends the 64-byte capsule decoded below"),
    "KITLOAD ": ("filesystem_loadKitDirectory_tick",
                 "root Kit directory load"),
    "SCNELOAD": ("filesystem_loadSceneDirectory_tick",
                 "root Scene directory load"),
    "BANKLOAD": ("filesystem_loadBankDirectory_tick",
                 "root Bank directory load"),
    "GLOBLOAD": ("filesystem_loadGlobals_tick",
                 "root settings.cfg load"),
    "KITSCAN ": ("filesystem_scanKits_tick",
                 "/Kit directory scan"),
    "SCNSCAN ": ("filesystem_scanScenes_tick",
                 "/Scene directory scan"),
    "BNKSCAN ": ("filesystem_scanBanks_tick",
                 "/Bank directory scan"),
    "INSSCAN ": ("filesystem_scanInstruments_tick",
                 "Instrument library directory scan"),
    "NAMEREPR": ("filesystem_repairNames_tick",
                 "library display-name repair"),
    "BIDXLOAD": ("filesystem_loadLibraryIndex_tick",
                 "read-only /Bank/.hcindex reload"),
    "SIDXLOAD": ("filesystem_loadLibraryIndex_tick",
                 "read-only /Scene/.hcindex reload"),
    "KIDXLOAD": ("filesystem_loadLibraryIndex_tick",
                 "read-only /Kit/.hcindex reload"),
    # Historical token from older firmware; kept so old cards stay readable.
    "KITQUAR ": ("legacy Bank-child Kit quarantine",
                  "retired all-Bank quarantine probe from older builds"),
}


# ---------------------------------------------------------------------------
# /asavetrc.bin: eight-byte records (stage, flags, tick16, value32).
# Stage constants and flag/value layouts come from AutosaveTrace.h.
# ---------------------------------------------------------------------------

STAGE_ENUM = {
    "D": "AUTOSAVE_TRACE_STAGE_DIRTY",
    "I": "AUTOSAVE_TRACE_STAGE_INSTRUMENT_MARK",
    "J": "AUTOSAVE_TRACE_STAGE_INSTRUMENT_COMMIT",
    "N": "AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY",
    "L": "AUTOSAVE_TRACE_STAGE_LOAD_MARK",
    "S": "AUTOSAVE_TRACE_STAGE_SCHEDULED",
    "A": "AUTOSAVE_TRACE_STAGE_ADMITTED",
    "V": "AUTOSAVE_TRACE_STAGE_VALIDATED",
    "M": "AUTOSAVE_TRACE_STAGE_MASK_MERGED",
    "C": "AUTOSAVE_TRACE_STAGE_CAPTURED",
    "P": "AUTOSAVE_TRACE_STAGE_PUBLISHED",
    "T": "AUTOSAVE_TRACE_STAGE_TERMINAL",
    "R": "AUTOSAVE_TRACE_STAGE_SCENE_LOAD_COMPLETE",
    "W": "AUTOSAVE_TRACE_STAGE_WRITER_SUPPRESSED",
    "F": "AUTOSAVE_TRACE_STAGE_TRACE_SUPPRESSED",
    "G": "AUTOSAVE_TRACE_STAGE_TRACE_DROPPED",
    "B": "AUTOSAVE_TRACE_STAGE_BANK_PRESENT",
    "X": "AUTOSAVE_TRACE_STAGE_PHASE_STALL",
    "O": "AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE",
    "E": "AUTOSAVE_TRACE_STAGE_OPERATION_ERROR",
    "Y": "AUTOSAVE_TRACE_STAGE_SCAN_PARENT_DIAG",
}

STAGE_PRODUCER = {
    "D": "autosave_markPayloadOffsetDirty()",
    "I": "autosave_markWholeInstrumentDirty()",
    "J": "presetManager on_instrument_load_complete()",
    "N": "menu_traceInstrumentEntry()",
    "L": "autosave_markKitDirty() / autosave_markSceneWithoutPatternDirty()",
    "S": "filesystem_autosaveWriterSchedule_tick()",
    "A": "filesystem_autosaveWriterSchedule_tick()",
    "V": "filesystem_autosaveParameterDrain_tick()",
    "M": "filesystem_autosaveParameterDrain_tick()",
    "C": "filesystem_autosaveTraceCaptured()",
    "P": "filesystem_autosaveParameterDrain_tick()",
    "T": "filesystem_autosaveWriterCompleted()",
    "R": "on_scene_load_complete()",
    "W": "filesystem_autosaveWriterSchedule_tick()",
    "F": "filesystem_autosaveTraceFlushSchedule_tick() / "
         "filesystem_autosaveTraceFlushCompleted()",
    "G": "filesystem_autosaveTraceFlushCompleted()",
    "B": "filesystem_loadBankDirectory_tick() / autosave_getLivePayloadByte()",
    "X": "filesystem_pollPhaseStall() call sites (delete-slot / Bank Save "
         "entry / AutoSave drain)",
    "O": "Kit/Scene/Bank/Instrument Save request+tick functions, and "
         "menu_requestKitEntryNames()/menu_handleLoadSaveMenu()",
    "E": "filesystem_complete() / filesystem_completeLibraryIndexRebuild() "
         "-- fires for ANY internal operation's FS_STATUS_ERROR completion, "
         "not just Save",
    "Y": "RETIRED -- was filesystem_deleteSlotDirectory_tick() "
         "FS_DELETE_SLOT_DELETE_MATCH, only emitted when the native "
         "delete's failure site was SCAN_PARENT_FOR_SELF_EXHAUSTED; that "
         "producer no longer exists (the bug it was diagnosing was fixed "
         "by eliminating the code path), kept only to decode already-"
         "captured Session 054 evidence",
}

PHASE_STALL_SITES = {
    0: "delete-slot resolver (filesystem_deleteSlotDirectory_tick)",
    1: "Bank Save entry (filesystem_saveBankDirectory_tick)",
    2: "runtime AutoSave drain (filesystem_autosaveParameterDrain_tick)",
}

SAVE_LIFECYCLE_TYPES = {0: "Kit", 1: "Scene", 2: "Bank", 3: "Instrument"}

SAVE_LIFECYCLE_CHECKPOINTS = {
    0: "REQUEST",
    1: "DELETE_RESULT",
    2: "CREATE_RESULT",
    3: "SOURCE_STAGED",
    4: "FINISH",
}

# fs_delete_slot_reason_t (filesystem.c), packed into a failed DELETE_RESULT
# record's value bits 16-19.
DELETE_SLOT_REASONS = {
    0: "NONE",
    1: "SCAN_IO (AFATFS_OPERATION_FAILURE from findNextObject)",
    2: "MALFORMED_LFN (matching candidate's LFN run failed validation)",
    3: "WRONG_KIND (matching candidate is not a directory)",
    4: "DUPLICATE (a second same-slot candidate was seen during the scan)",
    5: "MATCH_COUNT_BACKSTOP (match_count>1 with no per-match reason set "
       "-- should not normally happen independently of DUPLICATE)",
    6: "DELETE_REJECTED (afatfs_deleteTree() refused to start)",
    7: "DELETE_RESULT (native delete finished with a non-OK "
       "afatfsResultCode_t; see the detail field)",
    8: "DIR_OPEN_FAILED (the synchronous \".\" open resolved to a NULL "
       "handle -- open-handle-pool pressure or an invalid "
       "afatfs.currentDirectory, not a scan/match problem)",
    9: "STALL_ABANDONED (the 50,000-poll stall observer gave up on "
       "OPEN_SCAN/SCAN_NEXT/CLOSE_SCAN; see the paired X/PHASE_STALL "
       "record for the exact phase)",
}

# fs_internal_op_t (filesystem.c), in declaration order -- its numeric value
# is simply its index here since the enum assigns no explicit values.
# Packed into an E/OPERATION_ERROR record's value bits 0-7.
FS_INTERNAL_OPS = [
    "FS_INTERNAL_OP_NONE",
    "FS_INTERNAL_OP_FLUSH_FINISH",
    "FS_INTERNAL_OP_CREATE_BOOT_INDEX",
    "FS_INTERNAL_OP_CREATE_LIBRARY_INDEX",
    "FS_INTERNAL_OP_WRITE_HCNAMES",
    "FS_INTERNAL_OP_WRITE_BOOT_LOG",
    "FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES",
    "FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN",
    "FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH",
    "FS_INTERNAL_OP_LOAD_HCNAMES_INSTRUMENT",
    "FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT",
    "FS_INTERNAL_OP_LOAD_HCNAMES_KIT",
    "FS_INTERNAL_OP_UPDATE_HCNAMES_KIT",
    "FS_INTERNAL_OP_LOAD_HCNAMES_SCENE",
    "FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE",
    "FS_INTERNAL_OP_LOAD_KIT",
    "FS_INTERNAL_OP_LOAD_KIT_MORPH",
    "FS_INTERNAL_OP_LOAD_SCENE",
    "FS_INTERNAL_OP_LOAD_BANK",
    "FS_INTERNAL_OP_SAVE_KIT",
    "FS_INTERNAL_OP_SAVE_SCENE",
    "FS_INTERNAL_OP_SAVE_BANK",
    "FS_INTERNAL_OP_SAVE_INSTRUMENT_MORPH",
    "FS_INTERNAL_OP_LOAD_MORPH",
    "FS_INTERNAL_OP_LOAD_PATTERN",
    "FS_INTERNAL_OP_SAVE_PATTERN",
    "FS_INTERNAL_OP_LOAD_ALL",
    "FS_INTERNAL_OP_SAVE_ALL",
    "FS_INTERNAL_OP_LOAD_PERFORMANCE",
    "FS_INTERNAL_OP_SAVE_PERFORMANCE",
    "FS_INTERNAL_OP_LOAD_GLOBALS",
    "FS_INTERNAL_OP_SAVE_GLOBALS",
    "FS_INTERNAL_OP_SCAN_KITS",
    "FS_INTERNAL_OP_SCAN_SCENES",
    "FS_INTERNAL_OP_SCAN_BANKS",
    "FS_INTERNAL_OP_SCAN_BANK_SCENES",
    "FS_INTERNAL_OP_SCAN_INSTRUMENTS",
    "FS_INTERNAL_OP_REPAIR_NAMES",
    "FS_INTERNAL_OP_LOAD_INSTRUMENT",
    "FS_INTERNAL_OP_SAVE_INSTRUMENT",
    "FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP",
    "FS_INTERNAL_OP_LOAD_NAME",
    "FS_INTERNAL_OP_LOAD_INDEX",
    "FS_INTERNAL_OP_LOAD_LIBRARY_INDEX",
]
# NOTE: keep this list in sync with the fs_internal_op_t enum in
# filesystem.c by hand -- there is no automatic extraction here. If a
# decoded op name looks wrong, re-diff this list against the enum first.

# afatfsResultCode_t (asyncfatfs.h), packed into a failed DELETE_RESULT
# record's value bits 20-23 only when reason == 7 (DELETE_RESULT).
AFATFS_RESULT_CODES = {
    0: "AFATFS_RESULT_OK",
    1: "AFATFS_RESULT_NOT_FOUND",
    2: "AFATFS_RESULT_ALREADY_EXISTS",
    3: "AFATFS_RESULT_INVALID_NAME",
    4: "AFATFS_RESULT_UNSUPPORTED_NAME",
    5: "AFATFS_RESULT_NOT_EMPTY",
    6: "AFATFS_RESULT_NOT_DIRECTORY",
    7: "AFATFS_RESULT_NOT_FILE",
    8: "AFATFS_RESULT_IO_ERROR",
    9: "AFATFS_RESULT_CORRUPT_LFN_RUN",
    10: "AFATFS_RESULT_UNSUPPORTED_LAYOUT",
}

# afatfsDeleteTreeFailureSite_e (asyncfatfs.c), packed into a failed
# DELETE_RESULT record's value bits 24-31, only meaningful when reason == 7
# (DELETE_RESULT) -- i.e. which of the ~17 distinct checks inside
# afatfs_deleteTreeContinue() actually produced that afatfsResultCode_t.
ASYNCFATFS_DELETE_TREE_FAILURE_SITES = {
    0: "NONE",
    1: "OPEN_DIR_BAD_ROOT_ON_FAT32 (target's firstCluster==0 but "
       "filesystem isn't FAT16)",
    2: "OPEN_DIR_CLUSTER_OUT_OF_RANGE (target's firstCluster out of the "
       "volume's legal range)",
    3: "SCAN_ROOT_CLUSTER_MISMATCH (directory exhausted at what should be "
       "the delete root, but its firstCluster disagrees with the copied "
       "root identity)",
    4: "SCAN_MALFORMED_OBJECT (a scanned child failed "
       "afatfs_validateObjectInfo(), and is not simply a malformed-LFN "
       "case)",
    5: "SCAN_CHILD_CLUSTER_OUT_OF_RANGE (a scanned child's firstCluster is "
       "out of range or an end-of-chain marker)",
    6: "SCAN_STRUCTURAL_BUDGET_EXHAUSTED_DESCEND (the numClusters*2 "
       "structural budget hit zero before descending into a child "
       "directory)",
    7: "ASCEND_BAD_DOTDOT_ENTRY (a child directory's own \"..\" entry is "
       "missing/malformed)",
    8: "ASCEND_PARENT_CLUSTER_OUT_OF_RANGE (the cluster read from a "
       "child's \"..\" entry is out of the volume's legal range)",
    9: "ASCEND_PARENT_CLUSTER_MISMATCH (a child directory's own \"..\" "
       "cluster disagrees with the parent cluster recorded when this "
       "traversal descended into it -- the leading hypothesis for "
       "Session 054's slot-11 evidence; see the paired "
       "SCAN_PARENT_FOR_SELF_EXHAUSTED site)",
    10: "SCAN_PARENT_BAD_ROOT_ON_FAT32 (recovered parentCluster==0 but "
        "filesystem isn't FAT16)",
    11: "SCAN_PARENT_FOR_SELF_EXHAUSTED (re-scanning the recovered parent "
        "never found an entry whose exact SFN pointer AND firstCluster "
        "match what was recorded on descent -- the other half of the "
        "slot-11 pair with ASCEND_PARENT_CLUSTER_MISMATCH)",
    12: "SCAN_PARENT_FOR_SELF_MATCH_CLUSTER_OUT_OF_RANGE (found the exact "
        "matching entry in the parent, but its firstCluster is now out of "
        "range)",
    13: "RETIRE_ENTRIES_ROOT_CLUSTER_MISMATCH (finished retiring what "
        "should be the delete root's own name entries, but its "
        "firstCluster disagrees with the copied root identity)",
    14: "FREE_CLUSTERS_NONFILE_NONZERO_SIZE (currentCluster==0 but the "
        "target isn't a zero-length file -- an empty directory should "
        "never reach this check with a nonzero size)",
    15: "FREE_CLUSTERS_CLUSTER_OUT_OF_RANGE_OR_BUDGET (currentCluster out "
        "of range, or the structural budget hit zero while freeing a "
        "cluster chain)",
    16: "FREE_CLUSTERS_NEXT_CLUSTER_INVALID (the FAT's next-cluster link "
        "is free space, or out of range and not an end-of-chain marker)",
    17: "CORRUPT_PHASE (defensive catch-all: op->phase held a value no "
        "case matches -- should not be reachable)",
}

INSTRUMENT_TYPES = {0: "drm", 1: "snr", 2: "cym", 3: "hat"}

ENTRY_PHASES = {
    1: "request",
    2: "HCNAMES request",
    3: "HCNAMES complete",
    4: "HCNAMES flush",
    5: "HCNAMES flushed",
    6: "temp request",
    7: "temp complete",
    8: "index request",
    9: "index complete",
}

# AutoSave payload geometry (payload-relative), from Autosave.h/AUTOSAVE.md.
BANK_BYTES = 128
SCENE_BYTES = 1920
SCENE_PARAMS_OFF = 8
SCENE_PARAM_COUNT = 40
EFFECT_OFF = 128
EFFECT_BYTES = 512
KIT_OFF = 640
KIT_PARAMS_OFF = 8
KIT_PARAM_COUNT = 2
KIT_INST_OFF = 128
INST_BYTES = 192
INST_TYPE_BYTES = 3
INST_NAME_OFF = 3
INST_NAME_BYTES = 8
INST_NORMAL_OFF = 11
INST_NORMAL_BYTES = 72
INST_MORPH_OFF = 83
INST_MORPH_BYTES = 72


def u16(data: bytes) -> int:
    return int.from_bytes(data, "little")


def u24(data: bytes) -> int:
    return int.from_bytes(data + b"\0", "little")


def u32(data: bytes) -> int:
    return int.from_bytes(data, "little")


def scene_mask_text(value: int) -> str:
    scenes = [i for i in range(16) if value & (1 << i)]
    return "{" + ",".join(str(i) for i in scenes) + "}" if scenes else "{}"


def instrument_type_text(value: int) -> str:
    if value in INSTRUMENT_TYPES:
        return f"{value} ({INSTRUMENT_TYPES[value]})"
    return f"{value} (unknown)"


def payload_region_text(offset: int) -> str:
    """Describe one payload-relative dirty byte using the on-card geometry."""
    if offset < BANK_BYTES:
        if offset < 2:
            return f"Bank restore_slot byte{offset}"
        if offset < 10:
            return f"Bank name byte{offset - 2}"
        if offset < 12:
            return f"Bank scene_present_mask byte{offset - 10}"
        if offset == 12:
            return "Bank active_scene"
        if offset < 15:
            return f"Bank scene_mask_voice_edit byte{offset - 13}"
        return f"Bank reserved byte{offset}"
    scene = (offset - BANK_BYTES) // SCENE_BYTES
    rel = (offset - BANK_BYTES) % SCENE_BYTES
    if scene >= 16:
        return f"payload byte {offset} (outside the 16 Scene regions)"
    name = f"Scene{scene}"
    if rel < 8:
        return f"{name} name byte{rel}"
    if rel < SCENE_PARAMS_OFF + SCENE_PARAM_COUNT:
        return f"{name} scene-parameter[{rel - SCENE_PARAMS_OFF}]"
    if rel < EFFECT_OFF:
        return f"{name} scene reserved byte{rel}"
    if rel < EFFECT_OFF + EFFECT_BYTES:
        return f"{name} effect byte{rel - EFFECT_OFF}"
    if rel < KIT_OFF:
        return f"{name} scene padding byte{rel - EFFECT_OFF}"
    if rel < KIT_OFF + 8:
        return f"{name} kit name byte{rel - KIT_OFF}"
    if rel < KIT_OFF + KIT_PARAMS_OFF + KIT_PARAM_COUNT:
        return f"{name} kit-parameter[{rel - KIT_OFF - KIT_PARAMS_OFF}]"
    if rel < KIT_OFF + KIT_INST_OFF:
        return f"{name} kit reserved byte{rel - KIT_OFF}"
    slot = (rel - KIT_OFF - KIT_INST_OFF) // INST_BYTES
    ir = (rel - KIT_OFF - KIT_INST_OFF) % INST_BYTES
    if slot >= 6:
        return f"{name} kit reserved byte{rel - KIT_OFF}"
    base = f"{name} instrument[{slot}]"
    if ir < INST_TYPE_BYTES:
        return f"{base} type-token byte{ir}"
    if ir < INST_NAME_OFF + INST_NAME_BYTES:
        return f"{base} name byte{ir - INST_NAME_OFF}"
    if ir < INST_NORMAL_OFF + INST_NORMAL_BYTES:
        return f"{base} normal[{ir - INST_NORMAL_OFF}]"
    if ir < INST_MORPH_OFF + INST_MORPH_BYTES:
        return f"{base} morph[{ir - INST_MORPH_OFF}]"
    return f"{base} padding byte{ir - INST_MORPH_OFF - INST_MORPH_BYTES}"


def trace_record_text(index: int, stage: int, flags: int, tick: int,
                      value: int) -> str:
    """Decode one eight-byte AutoSave trace record into one text line."""
    ch = chr(stage) if 32 <= stage < 127 else f"0x{stage:02x}"
    enum_name = STAGE_ENUM.get(ch, f"unknown stage 0x{stage:02x}")
    producer = STAGE_PRODUCER.get(ch, "unknown producer")
    head = (f"#{index:06d} tick={tick:5d} stage={ch} "
            f"flags=0x{flags:02x} value=0x{value:08x}")
    detail = ""

    if ch == "D":
        detail = (f"{enum_name} via {producer}: "
                  f"payload offset {value} = {payload_region_text(value)}")
    elif ch == "I":
        scene = value & 0xF
        slot = (value >> 4) & 0x7
        expected = (value >> 8) & 0xFF
        published = (value >> 16) & 0xFF
        base = bool(flags & 0x01)
        tracking = bool(flags & 0x02)
        allpub = bool(flags & 0x04)
        detail = (
            f"{enum_name} via {producer}: Scene{scene} slot{slot} "
            f"expected={expected} published={published}; flags: "
            f"BASE_VALID={int(base)} TRACKING_ENABLED={int(tracking)} "
            f"ALL_PUBLISHED={int(allpub)}")
    elif ch == "J":
        scene = value & 0xF
        slot = (value >> 4) & 0x7
        typ = (value >> 8) & 0xFF
        requested = bool(flags & 0x01)
        called = bool(flags & 0x02)
        detail = (f"{enum_name} via {producer}: Scene{scene} slot{slot} "
                  f"type={instrument_type_text(typ)}; flags: "
                  f"REQUESTED={int(requested)} CALLED={int(called)}")
    elif ch == "N":
        scene = value & 0xF
        slot = (value >> 4) & 0x7
        typ = (value >> 8) & 0xFF
        phase = flags & 0x7F
        failed = bool(flags & 0x80)
        phase_name = ENTRY_PHASES.get(phase, f"phase {phase}")
        detail = (f"{enum_name} via {producer}: Scene{scene} slot{slot} "
                  f"type={instrument_type_text(typ)}; phase={phase} "
                  f"({phase_name}), FAILED={int(failed)}")
    elif ch == "L":
        kind = value & 0x3
        scene = (value >> 2) & 0xF
        tracking = bool(flags & 0x01)
        if kind == 0:
            kind_name = "Kit (nested autosave_markKitDirty)"
        elif kind == 1:
            kind_name = "Scene (outer autosave_markSceneWithoutPatternDirty)"
        else:
            kind_name = f"unknown kind {kind}"
        detail = (f"{enum_name} via {producer}: {kind_name}, Scene{scene}; "
                  f"TRACKING_ENABLED={int(tracking)}")
    elif ch == "R":
        done = bool(flags & 0x01)
        detail = (f"{enum_name} via {producer}: filesystem status "
                  f"{'DONE' if done else 'not DONE'}; destination Scene mask "
                  f"0x{value:04x} {scene_mask_text(value)}")
    elif ch == "S":
        detail = (f"{enum_name} via {producer}: writer armed once; "
                  f"five-second debounce deadline tick={value}")
    elif ch == "A":
        detail = (f"{enum_name} via {producer}: parameter drain admitted; "
                  f"the transform now owns the facade")
    elif ch == "V":
        has_winner = bool(flags & 0x01)
        winner = "A (.hcprms1)" if (flags & 0x02) == 0 else "B (.hcprms2)"
        detail = (f"{enum_name} via {producer}: "
                  f"winner_exists={int(has_winner)}, "
                  f"winner={winner if has_winner else 'none'}, "
                  f"winner generation={value}")
    elif ch == "M":
        dirty = bool(flags)
        detail = (f"{enum_name} via {producer}: post-merge canonical mask "
                  f"dirty={int(dirty)}; on-file mask bytes accepted={value}")
    elif ch == "C":
        detail = (f"{enum_name} via {producer}: "
                  f"budget_exhausted={int(bool(flags))}, "
                  f"patch_count={value}")
    elif ch == "P":
        target = "A (.hcprms1)" if flags == 0 else "B (.hcprms2)"
        detail = (f"{enum_name} via {producer}: newly active target "
                  f"{target}, new generation={value}")
    elif ch == "T":
        done = bool(flags & 0x01)
        detail = (f"{enum_name} via {producer}: terminal facade status "
                  f"{'DONE' if done else 'ERROR/other'}; value unused")
    elif ch == "W":
        detail = (f"{enum_name} via {producer}: armed writer held by the "
                  f"intentional Load/Save page guard; canonical mask "
                  f"dirty={int(bool(flags & 0x01))}; debounce deadline "
                  f"tick={value}")
    elif ch == "F":
        if flags & 0x01:
            detail = (f"{enum_name} via {producer}: command-active gate "
                      f"held pending trace records; pending count={value}")
        elif flags & 0x02:
            detail = (f"{enum_name} via {producer}: a started trace append "
                      f"reached ERROR; the ring retains the records for retry")
        else:
            detail = f"{enum_name} via {producer}: flags=0x{flags:02x}"
    elif ch == "G":
        detail = (f"{enum_name} via {producer}: ring dropped-count "
                  f"publication; dropped={value}")
    elif ch == "B":
        drain = bool(flags & 0x01)
        resident = (value >> 16) & 0xFFFF
        low = value & 0xFFFF
        if drain:
            detail = (f"{enum_name} via {producer}: drain capture; "
                      f"resident present mask 0x{resident:04x} "
                      f"{scene_mask_text(resident)}, payload offset {low}")
        else:
            detail = (f"{enum_name} via {producer}: Bank Load commit; "
                      f"resident present mask 0x{resident:04x} "
                      f"{scene_mask_text(resident)}, effective load mask "
                      f"0x{low:04x} {scene_mask_text(low)}")
    elif ch == "X":
        site = flags & 0x07
        in_native_delete = bool(flags & 0x08)
        site_name = PHASE_STALL_SITES.get(site, f"unknown site {site}")
        phase = value & 0xFF
        slot = (value >> 8) & 0x3FF
        extra = (value >> 18) & 0x3FFF
        detail = (f"{enum_name} via {producer}: site={site_name}, "
                  f"phase={phase}, slot={slot}; "
                  f"IN_NATIVE_DELETE={int(in_native_delete)}")
        if site == 0 and in_native_delete:
            detail += f", afatfs_getDeleteTreePhase() subphase={extra & 0xFF}"
        elif site == 2:
            detail += f", stream_offset~={extra * 16} bytes"
        detail += (". Observation only unless site is the runtime drain, "
                   "where a stall also forces FS_STATUS_ERROR completion.")
    elif ch == "O":
        elem_type = SAVE_LIFECYCLE_TYPES.get(flags & 0x03,
                                             f"unknown type {flags & 0x03}")
        checkpoint_id = (flags >> 2) & 0x07
        checkpoint = SAVE_LIFECYCLE_CHECKPOINTS.get(
            checkpoint_id, f"unknown checkpoint {checkpoint_id}")
        failed = bool(flags & 0x80)
        slot = value & 0x3FF
        high_word = (value >> 16) & 0xFFFF
        detail = (f"{enum_name} via {producer}: type={elem_type}, "
                  f"checkpoint={checkpoint}, slot={slot}, "
                  f"FAILED={int(failed)}")
        if checkpoint == "DELETE_RESULT" and failed:
            reason_id = (value >> 16) & 0xF
            detail_id = (value >> 20) & 0xF
            site_id = (value >> 24) & 0xFF
            reason_name = DELETE_SLOT_REASONS.get(
                reason_id, f"unknown reason {reason_id}")
            detail += f"; reason={reason_name}"
            if reason_id == 7:
                detail += (f", native afatfsResultCode_t="
                           f"{AFATFS_RESULT_CODES.get(detail_id, detail_id)}"
                           f", asyncfatfs failure site="
                           f"{ASYNCFATFS_DELETE_TREE_FAILURE_SITES.get(site_id, site_id)}")
        elif elem_type == "Instrument" and checkpoint == "CREATE_RESULT":
            detail += (f"; bits16-31=0x{high_word:04x} is the running "
                       f"(unfinished) CRC32C content fingerprint, top 16 "
                       f"bits")
        elif elem_type == "Kit" and checkpoint == "REQUEST" and high_word:
            detail += (f"; bits16-31=0x{high_word:04x} may be "
                       f"menu_requestKitEntryNames()'s local branch tag "
                       f"(1/2/3xxx) rather than the normal lifecycle field "
                       f"-- only true for Kit REQUEST records emitted from "
                       f"menu.c, ambiguous from the record alone")
        elif high_word:
            detail += f"; bits16-31=0x{high_word:04x}"
    elif ch == "E":
        op_id = value & 0xFF
        phase = (value >> 8) & 0xFF
        slot = (value >> 16) & 0x3FF
        op_name = (FS_INTERNAL_OPS[op_id] if op_id < len(FS_INTERNAL_OPS)
                   else f"unknown op {op_id}")
        from_rebuild = bool(flags & 0x02)
        has_delete_reason = bool(flags & 0x01)
        detail = (f"{enum_name} via {producer}: current_op={op_name}, "
                  f"op_phase={phase}, op_slot={slot}"
                  f"{' [from index-rebuild completion]' if from_rebuild else ''}")
        if has_delete_reason:
            detail += ("; see the most recent O/DELETE_RESULT record for "
                       "the specific delete-slot reason")
    elif ch == "Y":
        slot = value & 0x3FF
        sfn_match = bool(value & (1 << 14))
        cluster_match = bool(value & (1 << 15))
        target_cluster = (value >> 16) & 0xFFFF
        seen_count = flags
        detail = (f"{enum_name} via {producer}: slot={slot}, "
                  f"re-scan target cluster (low 16 bits)={target_cluster}, "
                  f"candidates examined before giving up={seen_count}")
        if seen_count == 0:
            detail += " (parent looked completely empty)"
        elif not sfn_match and not cluster_match:
            detail += (", last directory candidate: no SFN-pointer match, "
                       "no cluster match (or no directory-kind candidate "
                       "was seen at all despite other objects present)")
        elif sfn_match and not cluster_match:
            detail += (", last directory candidate: SFN pointer matched "
                       "but cluster did NOT -- same directory-entry slot "
                       "now reports a different firstCluster")
        elif cluster_match and not sfn_match:
            detail += (", last directory candidate: cluster matched but "
                       "SFN pointer did NOT -- the target cluster reappeared "
                       "under a different directory-entry pointer")
        else:
            detail += (", last directory candidate matched on BOTH SFN "
                       "pointer and cluster (unexpected at EXHAUSTED -- a "
                       "full match should have retired immediately)")
    else:
        detail = f"{enum_name}: no decoder for this stage"

    return f"{head} | {detail}"


def decode_trace(data: bytes) -> list[str]:
    lines = [f"AutoSave trace stream: {len(data)} bytes, "
             f"{len(data) // RECORD_BYTES} records of "
             f"{RECORD_BYTES} bytes each"]
    count = Counter()
    for index in range(0, len(data) - len(data) % RECORD_BYTES,
                       RECORD_BYTES):
        rec = data[index:index + RECORD_BYTES]
        stage = rec[0]
        flags = rec[1]
        tick = u16(rec[2:4])
        value = u32(rec[4:8])
        key = chr(stage) if 32 <= stage < 127 else f"0x{stage:02x}"
        count[key] += 1
        lines.append(trace_record_text(index // RECORD_BYTES, stage, flags,
                                       tick, value))
    if len(data) % RECORD_BYTES:
        lines.append(f"trailing {len(data) % RECORD_BYTES} bytes: "
                     f"{data[-(len(data) % RECORD_BYTES):].hex(' ')}")
    lines.append("stage totals: " + ", ".join(
        f"{key}={value}" for key, value in sorted(count.items())))
    return lines


def decode_capsule(capsule: bytes) -> list[str]:
    """Decode the frozen 64-byte HCPRMS ensure capsule (stages E0..E7)."""
    if len(capsule) != CAPSULE_BYTES:
        raise ValueError("capsule must be exactly 64 bytes")
    rows = [capsule[offset:offset + 8] for offset in range(0, 64, 8)]
    expected = list(range(0xE0, 0xE8))
    if [row[0] for row in rows] != expected or rows[0][1] != 1:
        return ["unknown HCPRMS capsule schema; raw:", capsule.hex(" ")]

    context, progress, chunk, cursor, allocation, owner, cache, transport = rows
    flags = context[7]
    allocation_flags = allocation[7]
    cluster_bytes = allocation[5] * 512
    if context[2] == 0:
        target = "A (.hcprms1)"
    elif context[2] == 1:
        target = "B (.hcprms2)"
    else:
        target = "unknown"
    return [
        "HCPRMS capsule schema: 1",
        f"E0 context: target={target}, ensure_phase={context[3]}, "
        f"facade_status={context[4]}, file_operation={context[5]}, "
        f"append_phase={context[6]}, active={bool(flags & 0x80)}, "
        f"frozen={bool(flags & 1)}",
        f"E1 progress: bytes_done={u32(progress[1:5])}, "
        f"chunk_len={u16(progress[5:7])}, "
        f"zero_write_streak={progress[7]}",
        f"E2 last fwrite: written={u16(chunk[1:3])}, "
        f"chunk_offset={u16(chunk[3:5])}, requested={u16(chunk[5:7])}, "
        f"generation={chunk[7]}",
        f"E3 file: cursor={u32(cursor[1:5])}, "
        f"logical_size={u24(cursor[5:8])}",
        f"E4 allocator: search_cluster={u32(allocation[1:5])}, "
        f"sectors_per_cluster={allocation[5]}, cluster_bytes={cluster_bytes}, "
        f"wrapped={allocation[6]}, full={bool(allocation_flags & 1)}, "
        f"file_available={bool(allocation_flags & 2)}, "
        f"bytes_done_at_cluster_boundary={bool(allocation_flags & 4)}",
        f"E5 owner: previous_cluster={u32(owner[1:5])}, "
        f"cursor_cluster={u24(owner[5:8])}",
        f"E6 cache: dirty={cache[1]}, locked={cache[2]}, reading={cache[3]}, "
        f"writing={cache[4]}, flush={cache[5]}, "
        f"active_index={'none' if cache[6] == 0xFF else cache[6]}, "
        f"full={cache[7]}",
        f"E7 SD transport: state={transport[1]}, operation={transport[2]}, "
        f"offset={u16(transport[3:5])}, retry_count={u16(transport[5:7])}, "
        f"callback_pending={transport[7]}",
    ]


def decode_bootlog(data: bytes) -> list[str]:
    lines = [f"boot log: {len(data)} bytes"]
    if len(data) < RECORD_BYTES:
        lines.append(f"payload too short to hold a token: {data.hex(' ')}")
        return lines
    token = data[:RECORD_BYTES].decode("ascii", errors="replace")
    if token == "ASENSURE" and len(data) == RECORD_BYTES + CAPSULE_BYTES:
        lines.append("boot token: ASENSURE - frozen AutoSave-ensure timeout; "
                     "the 64-byte capsule follows")
        lines.extend(decode_capsule(data[RECORD_BYTES:]))
        return lines
    if token in BOOT_CODES:
        function, description = BOOT_CODES[token]
        lines.append(f"boot token: {token!r} - {function}: {description}")
    else:
        lines.append(f"boot token: {token!r} (unknown code)")
    if len(data) != RECORD_BYTES:
        lines.append(f"unexpected suffix ({len(data) - RECORD_BYTES} bytes): "
                     f"{data[RECORD_BYTES:].hex(' ')}")
    return lines


def sniff_format(name: str, data: bytes) -> str:
    """Choose a decoder for one trace file; filename takes priority."""
    lower = name.lower()
    if lower == "bootlog.bin":
        return "boot"
    if lower == "asavetrc.bin":
        return "trace"
    if len(data) in (RECORD_BYTES, RECORD_BYTES + CAPSULE_BYTES):
        token = data[:RECORD_BYTES].decode("ascii", errors="replace")
        if all(32 <= ord(ch) < 127 for ch in token):
            return "boot"
    if data and len(data) % RECORD_BYTES == 0:
        return "trace"
    return "boot"


def render(name: str, data: bytes) -> str:
    fmt = sniff_format(name, data)
    if fmt == "boot":
        lines = decode_bootlog(data)
    else:
        lines = decode_trace(data)
    header = [
        "LXR-02 development log decode",
        f"source file: {name}",
        f"decoder kind: {'bootlog.bin' if fmt == 'boot' else 'asavetrc.bin'}",
        "",
    ]
    return "\n".join(header + lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path", nargs="?", type=Path,
        help="decode this single trace file to stdout (no files are written)")
    parser.add_argument(
        "--sdcard", type=Path,
        default=Path(__file__).resolve().parent.parent / "SD_CARD",
        help="SD card directory to scan when no path is given "
             "(default: the repository-root SD_CARD)")
    args = parser.parse_args()

    if args.path:
        data = args.path.read_bytes()
        print(render(args.path.name, data), end="")
        return

    traces = sorted(args.sdcard.glob("*.bin"))
    if not traces:
        raise SystemExit(f"no *.bin trace files found in {args.sdcard}")
    logs_dir = args.sdcard / "logs"
    logs_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%S%M%H%d%m%Y")
    for path in traces:
        data = path.read_bytes()
        out_path = logs_dir / f"{stamp}_{path.stem}.txt"
        out_path.write_text(render(path.name, data), encoding="utf-8")
        print(f"decoded {path.name} ({len(data)} bytes) -> {out_path}")


if __name__ == "__main__":
    main()
