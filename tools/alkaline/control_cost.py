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
            renderA; --keep (leave the build in build/s093p3/cost).
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
}
# functions whose loops sit on alternate branches (one runs per block): the
# loop part is the largest loop's, not their sum (S093 P3 T2-2)
EXCLUSIVE = {"dl_slowUpdate"}
DISPATCH = 14   # instrumentManager_calcSlotAsync's own path (firmware, as Drum)

STUBS = r"""
#include <stdint.h>
#include <string.h>
#include "AlkalineVoice.h"
const int16_t sawTable[11][1024];
const int16_t alkSqrTable[11][1024];
void lfo_init(Lfo *l) { memset(l, 0, sizeof(*l)); }
void osc_setBaseNote(OscInfo *o, uint8_t n) { o->baseNote = n; }
void osc_setFreq(OscInfo *o) { o->phaseInc = (uint32_t)o->freq; }
void osc_setTableFreq(OscInfo *o) { o->phaseInc = (uint32_t)o->freq; }
void calcWavetableOscBlock(OscInfo *o, const int16_t t[][1024], int16_t *b,
                           const uint8_t n, const float g)
{ (void)o; (void)t; (void)g; memset(b, 0, n * 2u); }
AlkalineVoice v;
int16_t buf[32];
int main(void)
{
	Alkaline_initVoice(&v);
	for (;;) {
		Alkaline_triggerVoice(&v, 101u, 63u, 5514u);
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
            sys.exit(f"control_cost.py: no iteration count for loop {rank} of {base} "
                     f"(length {length}); add it to ITERS")
        lv = sum(1 for i in members if r[i][1].startswith("vdiv"))
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
    ap.add_argument("--rows", type=int, default=4, help="network rows solved per block (DL_NET_ROWS)")
    a = ap.parse_args()
    ITERS[("dl_slowUpdate", 1)] = a.rows      # the latch branch's chunk
    ITERS[("dl_slowUpdate", 2)] = a.rows      # the following branch's chunk (either/or)
    elf, obj = build(a.tables, ["-D" + d for d in a.define])
    # the oscillator calls, from the standalone object
    ext = {}
    dis = E.disassembly(obj)
    for f in ("osc_setBaseNote", "osc_setFreq", "osc_setTableFreq"):
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
    per_block = asy[0] + syn[0] + DISPATCH
    vdiv = asy[1] + syn[1]
    print(f"per block: async {asy[0]} + sync block {syn[0]} + dispatch {DISPATCH} = "
          f"{per_block} instructions, {vdiv} VDIV")
    print(f"per sample (/32): {per_block / 32:.1f} instructions, {vdiv / 32:.2f} VDIV")


if __name__ == "__main__":
    main()
