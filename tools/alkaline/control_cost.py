#!/usr/bin/env python3
"""Per-block cost of the Alkaline voice on the M7 (S093 P3).

What:       links the real AlkalineVoice.c, AlkalineControl.c, DiodeLadder.c
            and the generated tables for the M7 (the firmware's -Ofast flags,
            newlib-nano libm) with stubs for the oscillator and LFO calls,
            and counts the per-block work as S093_P2 §8 defines it: the
            longest static path through each function, every remaining loop
            x its iteration count, and every callee's own total added at its
            call site (libm included). The per-sample render loop is
            excluded (it is the audio loop, elf_looppath.py), its block
            entry and exit included. The oscillator calls are not linked;
            their per-block costs are taken from a standalone -Ofast
            Oscillator.o (osc_setBaseNote(), osc_setFreq(),
            osc_setTableFreq(), calcWavetableOscBlock()'s entry), and the
            InstrumentManager dispatch from the linked firmware (14, as for
            Drum).
            Prints the items, the per-block total, its VDIV count and the
            total per sample: B2R loop + oscillator loop + per-block / 32.
Why:        MEMORY.md (cost comparisons include per-block work, S093): the
            Basic gate is the whole voice, and the per-block part was counted
            by hand before S093 P3.
Inputs:     the sources above; --kernel renderB2 (default), renderB,
            renderA; --tables/--define (an experiment's build); --rows
            (DL_NET_ROWS); --elf PATH (S093 P4: count a linked firmware image
            instead of the stub link: the oscillator, LFO and platform calls
            are then real and costed by their own paths, and the standalone
            oscillator object is not used); --iters FUNC:RANK=N (an
            iteration count for a loop that inlining moved, repeatable);
            --dispatch N (instrumentManager_calcSlotAsync's own path to the
            Alkaline case, default 14).
Outputs:    a report on stdout; exit 0.
Accessors:  python3 tools/alkaline/control_cost.py
Affiliates: tools/dsp_test/elf_looppath.py and elf_blockpath.py (rows,
            successors, frame loops); S093_P3_PREP_WORK_WAVES_KERNEL_CONTROL.md.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "dsp_test"))
import elf_looppath as E     # noqa: E402
import elf_blockpath as B    # noqa: E402

OUT = os.path.join(ROOT, "build", "s093p3", "cost")
ALK = os.path.join(ROOT, "Core", "DSP", "Instruments", "Alkaline")
MCU = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
# Loops left at control rate, by (function, rank by length): iterations.
ITERS = {
    # T2-2: the state rows solved per block (DL_NET_ROWS; --rows)
    ("dl_slowUpdate", 1): 4,
    ("alk_solve5", 1): 5,             # S093 P0 form only
    ("alkAcc_step", 1): 4,
    ("alkMeg_step", 1): 4,
    ("alk_expoCurrent", 1): 3,
    # the per-sample render loops are the audio loop (elf_looppath.py), not
    # per-block work: weight 0 here
    ("DiodeLadder_renderB2", 1): 0,
    ("DiodeLadder_renderB2Osc", 1): 0,       # S093 P3: the fused oscillator read
    ("DiodeLadder_renderB", 1): 0,
    ("DiodeLadder_renderA", 1): 0,
    # S093 P4 (G4) / S093 P7: on the linked image LTO inlines the fused B2R
    # reads into the sync block: wav (renderB2Osc), slp, wtb (8/16-bit
    # copies) and the wtb blend (two copies) are its six loops, all audio
    # loops (weight 0); smp stays its own function (two call sites)
    ("Alkaline_calcVoiceSyncBlock", 1): 0,
    ("Alkaline_calcVoiceSyncBlock", 2): 0,
    ("Alkaline_calcVoiceSyncBlock", 3): 0,
    ("Alkaline_calcVoiceSyncBlock", 4): 0,
    ("Alkaline_calcVoiceSyncBlock", 5): 0,
    ("Alkaline_calcVoiceSyncBlock", 6): 0,
    ("DiodeLadder_renderB2Smp", 1): 0,
    ("DiodeLadder_renderB2Slp", 1): 0,
    ("DiodeLadder_renderB2Wtb", 1): 0,
    ("DiodeLadder_renderB2Wtb", 2): 0,       # the 8- and 16-bit read copies
    ("DiodeLadder_renderB2WtbBlend", 1): 0,
    ("DiodeLadder_renderB2WtbBlend", 2): 0,
    # S093 P7: the blend plan's segment loop runs once or twice per block
    # (a glide crossing one frame splits it); the constant cost is 2
    ("osc_wtbPlanBlock", 1): 2,
}
# functions whose loops sit on alternate branches (one runs per block): the
# loop part is the largest loop's, not their sum (S093 P3 T2-2)
EXCLUSIVE = {"dl_slowUpdate"}
DISPATCH = 14   # instrumentManager_calcSlotAsync's own path (firmware, as Drum)

STUBS = r"""
#include <stdint.h>
#include <string.h>
#include "AlkalineVoice.h"
#include "modulationNode.h"
const int16_t sawTable[11][1024];
const int16_t alkSqrTable[11][1024];
void lfo_init(Lfo *l) { memset(l, 0, sizeof(*l)); }
/* S093 P4: the trigger's platform calls (the trigger is not on the block
 * path, so the stubs only satisfy the link) */
ModulationNode velocityModulators[6];
void lfo_retrigger(uint8_t voiceNr) { (void)voiceNr; }
void modNode_updateValue(ModulationNode *vm, float val) { vm->lastVal = val; }
void instrumentManager_applyVelocityModulationTarget(uint8_t source_slot, float velocity_0_1)
{ (void)source_slot; (void)velocity_0_1; }
void osc_setBaseNote(OscInfo *o, uint8_t n) { o->baseNote = n; }
void osc_setFreq(OscInfo *o) { o->phaseInc = (uint32_t)o->freq; }
void osc_setTableFreq(OscInfo *o) { o->phaseInc = (uint32_t)o->freq; }
/* S093 P7: the typed path (the user reads are fused; these satisfy the link) */
void osc_userBind(OscInfo *o) { (void)o; }
void osc_typedPositionApply(OscInfo *o, uint8_t bend) { (void)o; (void)bend; }
uint32_t osc_typedTriggerPhase(OscInfo *o) { (void)o; return 0u; }
uint32_t osc_slpBlockIncFor(uint32_t inc, uint32_t ph, uint32_t ls, uint32_t len)
{ (void)ph; (void)ls; (void)len; return inc; }
float osc_bindGainFor(const OscInfo *o) { (void)o; return 1.0f; }
const uint8_t *osc_wtbFrameFor(const OscInfo *o, const wavetable_catalogue_entry_t *w,
                               uint32_t f) { (void)o; (void)w; (void)f; return 0; }
uint32_t osc_wtbNearestFrame(uint8_t p, uint32_t c) { (void)p; (void)c; return 0u; }
void osc_wtbPlanFor(const OscInfo *o, uint32_t c, osc_wtbPlan_t *plan)
{ (void)o; (void)c; (void)plan; }
const sample_catalogue_entry_t *sampleMemory_sampleEntry(uint8_t i) { (void)i; return 0; }
const wavetable_catalogue_entry_t *sampleMemory_wavetableEntry(uint8_t i) { (void)i; return 0; }
uint8_t modNode_interpClaim(void *o) { (void)o; return 0u; }
void calcWavetableOscBlock(OscInfo *o, const int16_t t[][1024], int16_t *b,
                           const uint8_t n, const float g)
{ (void)o; (void)t; (void)g; memset(b, 0, n * 2u); }
AlkalineVoice v;
int16_t buf[32];
int main(void)
{
	Alkaline_initVoice(&v);
	for (;;) {
		Alkaline_triggerVoice(&v, 0u, 101u, 63u, 5514u);
		Alkaline_calcVoiceAsync(&v);
		Alkaline_calcVoiceSyncBlock(&v, buf, 32u);
		if (Alkaline_isQuiet(&v)) break;
	}
	return 0;
}
"""


def incs():
    mk = open(os.path.join(ROOT, "Makefile")).read().split("ASFLAGS")[0]
    return ["-I" + os.path.join(ROOT, i.rstrip("\\")) for i in re.findall(r"-I(\S+)", mk)] + ["-I" + ALK]


def build(tables=None, defines=()):
    os.makedirs(OUT, exist_ok=True)
    stub = os.path.join(OUT, "stubs.c")
    open(stub, "w").write(STUBS)
    # an experiment's tables: compile copies of the two sources from its
    # directory, so their quoted #include "AlkalineTables.h" finds its header
    src_dir = ALK
    if tables:
        import shutil
        for f in ("AlkalineVoice.c", "AlkalineControl.c"):
            shutil.copy(os.path.join(ALK, f), os.path.join(tables, f))
        src_dir = tables
    srcs = [os.path.join(src_dir, f) for f in ("AlkalineVoice.c", "AlkalineControl.c")]
    srcs += [os.path.join(tables or ALK, "AlkalineTables.c")]
    pre = (["-I" + tables] if tables else []) + list(defines)
    srcs += [os.path.join(ROOT, "Core", "DSPAudio", "DiodeLadder.c"), stub]
    elf = os.path.join(OUT, "alk_cost.elf")
    subprocess.run(["arm-none-eabi-gcc"] + MCU + ["-Ofast", "-std=gnu11", "-w",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    "-specs=nano.specs", "-specs=nosys.specs"] + pre + incs() + srcs +
                   ["-o", elf, "-lm"], check=True, capture_output=True)
    obj = os.path.join(OUT, "Oscillator.o")
    subprocess.run(["arm-none-eabi-gcc"] + MCU + ["-Ofast", "-std=gnu11", "-w",
                    "-ffunction-sections", "-fdata-sections"] + incs() +
                   ["-c", os.path.join(ROOT, "Core", "DSPAudio", "Oscillator.c"), "-o", obj], check=True)
    return elf, obj


def callee(ops):
    name = re.sub(r".*<|>.*", "", ops)
    name = re.sub(r"\+0x[0-9a-f]+$", "", name)
    return re.sub(r"^__(.*)_veneer$", r"\1", name)


def total(elf, sym, memo, ext, trace, depth=0):
    """(instructions, vdiv) per block of sym: weighted longest path + loops."""
    base = re.sub(r"\.(constprop|lto_priv|part|isra)\.\d+$", "", sym)
    if base in ext:
        return ext[base]
    if sym in memo:
        return memo[sym]
    r = E.rows(elf, sym)
    idx = {addr: i for i, (addr, _, _) in enumerate(r)}
    loops = B.frame_loops(r, idx)
    in_loop = set()
    for _, _, members in loops:
        in_loop.update(members)
    n = len(r)
    succ = [E.successors(r, idx, i) for i in range(n)]
    state, order, dag, stack = [0] * n, [], [[] for _ in range(n)], [(0, 0)]
    state[0] = 1
    while stack:
        i, k = stack.pop()
        if k < len(succ[i]):
            stack.append((i, k + 1))
            j = succ[i][k]
            if state[j] == 1:
                continue
            dag[i].append(j)
            if state[j] == 0:
                state[j] = 1
                stack.append((j, 0))
        else:
            state[i] = 2
            order.append(i)
    w, vd = [0] * n, [0] * n
    for i, (_, op, ops) in enumerate(r):
        if i in in_loop:
            continue
        w[i], vd[i] = 1, int(op.startswith("vdiv") or op.startswith("vsqrt"))
        # calls, and tail calls (a b/b.w to another function's entry)
        tail = (op in ("b", "b.w") and "<" in ops and "+0x" not in ops
                and callee(ops) != base and callee(ops) != sym)
        if op == "bl" or tail:
            c, cv = total(elf, callee(ops), memo, ext, trace, depth + 1)
            w[i] += c
            vd[i] += cv
    down, dv = [0] * n, [0] * n
    for i in order:
        best = max(dag[i], key=lambda j: down[j], default=None)
        down[i] = w[i] + (down[best] if best is not None else 0)
        dv[i] = vd[i] + (dv[best] if best is not None else 0)
    insn, vdiv = down[0], dv[0]
    parts = []
    for rank, (length, head, members) in enumerate(loops, 1):
        it = ITERS.get((base, rank))
        if it is None:
            # S093 P4: on a linked image this is usually a helper's loop that
            # LTO inlined (alkAcc_step 4, alkMeg_step 4, alk_expoCurrent 3,
            # the network rows --rows): find it with elf_loopsrc.py on the -g
            # twin and pass --iters FUNC:RANK=N
            sys.exit(f"control_cost.py: no iteration count for loop {rank} of {base} "
                     f"(length {length}); add it to ITERS or pass --iters {base}:{rank}=N")
        lv = sum(1 for i in members if r[i][1].startswith("vdiv"))
        # S093 P5 §9 B2: the weight-0 entry for the sync block assumes its
        # longest loop is the fused B2R audio loop (3 VDIV, ~214). If a later
        # layout stops inlining DiodeLadder_renderB2Osc, rank 1 would be a
        # control loop and weight 0 would undercount G4 silently.
        if (base == "Alkaline_calcVoiceSyncBlock" or base.startswith("DiodeLadder_renderB2")) \
                and it == 0 and (lv != 3 or length < 200):
            sys.exit(f"control_cost.py: loop {rank} of {base} (length {length}, {lv} VDIV) "
                     f"is not the fused B2R audio loop; give it an iteration count "
                     f"(--iters {base}:{rank}=N)")
        if any(r[i][1] == "bl" for i in members):
            sys.exit(f"control_cost.py: a call inside loop {rank} of {base}")
        parts.append((it * length, it * lv))
    if parts and base in EXCLUSIVE:
        parts = [max(parts)]
    insn += sum(p[0] for p in parts)
    vdiv += sum(p[1] for p in parts)
    memo[sym] = (insn, vdiv)
    trace.append((depth, base, insn, vdiv, len(loops)))
    return insn, vdiv


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", default="renderB2", choices=("renderB2", "renderB", "renderA"))
    ap.add_argument("--tables", default=None, help="a directory with an experiment's AlkalineTables.h/.c")
    ap.add_argument("--define", action="append", default=[], help="-D for the build")
    ap.add_argument("--rows", type=int, default=3,
                    help="network rows solved per block (DL_NET_ROWS = ALK_NET_STATES / "
                         "phases: r6 at 64 samples = 6 / 2 = 3)")
    # S093 P4 (G4): count a linked firmware image instead of the stub link
    ap.add_argument("--elf", default=None, help="a linked image (build/lxr02.elf)")
    # S093 P4: loops that LTO inlined into another function keep their
    # iteration count but change owner and rank; name them here
    ap.add_argument("--iters", action="append", default=[],
                    help="FUNC:RANK=N, an iteration count for loop RANK of FUNC")
    ap.add_argument("--dispatch", type=int, default=DISPATCH,
                    help="instrumentManager_calcSlotAsync's own path to the Alkaline case")
    a = ap.parse_args()
    ITERS[("dl_slowUpdate", 1)] = a.rows      # the latch branch's chunk
    ITERS[("dl_slowUpdate", 2)] = a.rows      # the following branch's chunk (either/or)
    # FUNC:RANK=N -> ITERS[(FUNC, RANK)] = N; a malformed item is an error,
    # not a silent 0 (an uncounted loop would undercount the gate)
    for item in a.iters:
        m = re.fullmatch(r"([A-Za-z_]\w*):(\d+)=(\d+)", item)
        if not m:
            sys.exit(f"control_cost.py: --iters {item!r}: expected FUNC:RANK=N")
        ITERS[(m.group(1), int(m.group(2)))] = int(m.group(3))
    ext = {}
    if a.elf:
        # the linked image: every callee is real and counted by its own path;
        # only memset keeps weight 0 (it is not on the Alkaline block path in
        # the fused form; any call to it that does appear is listed in the
        # trace and must be explained)
        elf = a.elf
        ext["memset"] = (0, 0)
    else:
        elf, obj = build(a.tables, ["-D" + d for d in a.define])
        # the oscillator calls, from the standalone object
        dis = E.disassembly(obj)
        for f in ("osc_setBaseNote", "osc_setFreq", "osc_setTableFreq",
                  "osc_userBind", "osc_typedPositionApply"):
            if re.search(r"<%s>:" % f, dis):
                # straight paths; osc_setFreq's longest path is its user-sample
                # bind branch (calls counted 1), a bound above Alkaline's WAV use
                ext[f] = (B.block_path(obj, f)[0], 0)
        osc_entry = B.block_path(obj, "calcWavetableOscBlock")[0]
        ext["calcWavetableOscBlock"] = (osc_entry, 0)
        ext["memset"] = (0, 0)
    memo, trace = {}, []
    asy = total(elf, "Alkaline_calcVoiceAsync", memo, ext, trace)
    syn = total(elf, "Alkaline_calcVoiceSyncBlock", memo, ext, trace)
    kern = "DiodeLadder_" + a.kernel
    if a.kernel != "renderB2":
        kb = total(elf, kern, memo, ext, trace) if kern in E.disassembly(elf) else None
        if kb is None:
            sys.exit(f"{kern} is not called by the voice; build it into the voice to cost it")
    for d, name, ins, vd, nl in sorted(trace, key=lambda t: -t[2]):
        print(f"  {'  ' * min(d, 3)}{name:34s} {ins:6d} insn {vd:3d} vdiv  ({nl} loops)")
    for f, (ins, vd) in ext.items():
        print(f"  [osc] {f:30s} {ins:6d} insn {vd:3d} vdiv")
    # whole per-block work: both entry points plus the dispatcher's own path
    per_block = asy[0] + syn[0] + a.dispatch
    vdiv = asy[1] + syn[1]
    print(f"per block: async {asy[0]} + sync block {syn[0]} + dispatch {a.dispatch} = "
          f"{per_block} instructions, {vdiv} VDIV")
    print(f"per sample (/32): {per_block / 32:.1f} instructions, {vdiv / 32:.2f} VDIV")


if __name__ == "__main__":
    main()
