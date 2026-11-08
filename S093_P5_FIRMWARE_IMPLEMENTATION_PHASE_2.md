# S093 P5 — Alkaline Firmware Implementation Schedule (Phase 2: P4-5 … P4-8)

- **Session:** 093. This continues `S093_P4_FIRMWARE_IMPLEMENTATION.md`
  (phase 1: P4-0 … P4-4, the code into the build). This document covers:
  - **P4-5:** storage and platform verification;
  - **P4-6:** the static gates and the two tool changes they need;
  - **P4-7:** the device trial and its OPEN_TESTS entry;
  - **P4-8:** documentation;
  - the risks and the complete file index of both phases.
- **Conventions:** as in phase 1: **ADD** / **MODIFY** / **REMOVE** /
  **NONE**, line numbers from the S093 P3 working tree, and comment blocks
  written into the source as shown.
- **Verified against the tree:** every site, name and tool option was read
  (phase 1 §7). `elf_loopsrc.py` below was run in scratch on the P3 `-g`
  twin. What is left open needs the device or the user's ear (§4).
- **Starts when** phase 1's build gate (P4 §6.5) passes.

---

## 1. Work order (phase 2)

| Phase | Content | Gate |
|---|---|---|
| **P4-5** | Storage and platform: the confirmed record (§2.1) and the device round trips it leaves (§2.2) | Device step 5 (§4) |
| **P4-6** | `control_cost.py --elf` (§3.1); `elf_loopsrc.py` (§3.2); the Drum ceiling on the P4 image (§3.3); gates G1–G8 (§3.4) | G1–G8 pass |
| **P4-7** | Device trial (§4); OPEN_TESTS entry (§4.2) | User's listening and the CPU worst case |
| **P4-8** | Documentation (§5) | — |

P4-6 needs only the phase 1 build. P4-7 needs P4-6.

---

## 2. P4-5: storage and platform

### 2.1 Confirmed in the tree (no code beyond phase 1)

Every row was read. The change each needs is in phase 1, or there is none.

| # | Site | Read | Result |
|---|---|---|---|
| S1 | Type folder at boot (`filesystem.c` l. 4733–4778) | Opens `entry->storage_directory`; a missing folder finishes DONE; mkdir only in runtime index recovery (l. 4762–4777) | A card without `/Instrument/Alkaline` boots. The card image ships it (P4 §4.6) |
| S2 | `filesystem_trimDirectoryTailsBlocking()` (l. 24260) | Walks every subdirectory of `/Instrument` from the directory | Covers the new folder |
| S3 | `filesystem_createBootIndexBlocking()` (l. 29431) | Repair (l. 30403), then scan + `.hcindex` per registry row | `/Instrument/Alkaline/.hcindex` written at boot |
| S4 | HCNAMES rows/header (l. 24579, 24616; validator l. 6591) | ToText tokens, 3 bytes; header walks type values until NULL | Registry-driven ToText (P4 §6.3). `/.hcnames` rebuilt once, as designed (P4 §5.1) |
| S5 | Instrument Load (l. 17445–17556) | `resetSlot()`, then the file's lines through the parser; `self` → loading slot (`storageTypes.c` l. 2313) | `init.alk` sets all 26 cells |
| S6 | Load type list (`menu.c` l. 9695, 11174, 11196) | Registry walk with `typeSelectableForSceneSlot()` | Alkaline and its Morph row listed |
| S7 | Load guards (`menu.c` l. 8498, 9498) | `>= INSTRUMENT_TYPE_UNKNOWN` | Move with the enum |
| S8 | `pm_instrument_snapshot` (`presetManager.c` l. 2817–2925) | 129 B; restore loops `entry->descriptor_count` | 26 rows fit |
| S9 | AutoSave type field (`filesystem.c` l. 30859) | `instrumentManager_typeFromText()`, 3 bytes | `alk` restores |
| S10 | NRPN (`MidiControl.c` l. 316–329 → `menu_commitTargetEdit()`, `menu.c` l. 5674) | Target ID = `slot × 64 + index`; the descriptor lookup refuses an index without a row | Rows 0–25 reachable; 26–63 refused |
| S11 | MIDI Learn (`MidiLearn.c` l. 416) | `typeFromText()` + `descriptorIndexByKey()` | `alk` keys learnable |
| S12 | Basic/Advanced (`InstrumentManager.c` l. 361, 389) | Basic returns 1 at l. 402–403 | Selectable in all six slots |
| S13 | Instrument Save (`filesystem.c` ≈ l. 35190, 17859–17895) | Saves the slot's type; creates the type folder when missing | Works once ToText is registry-driven |
| S14 | Instrument file writer (`storageTypes.c` l. 981–1134) | Format, `[morph]` = MORPHABLE rows, `self`, LF, final blank line | `init.alk` written in that form (P4 §4.6) |

### 2.2 Left for the device

The round trips of device step 5 (§4.1): save, load, Kit, Scene, Bank,
AutoSave and the `kit` snapshot on the real card. The code paths are
confirmed above; the device shows they run.

---

## 3. P4-6: static gates and their tooling

### 3.1 `tools/alkaline/control_cost.py`: count the linked image (`--elf`) — MODIFY

**Why:** P3 counted the per-block work on a stub link, with the oscillator
calls costed from a standalone object. G4 needs the same count on
`build/lxr02.elf`, where:
- the oscillator calls are real;
- LTO may inline the static control helpers (`alkAcc_step`, `alkMeg_step`,
  `alk_expoCurrent`) into `Alkaline_calcVoiceAsync`, so the loop ranks in
  `ITERS` may move to the caller.

`E.rows()` already matches the `.lto_priv.N` / `.constprop.N` suffixes
(`elf_looppath.py` l. 63–75), so no symbol resolver is needed.

**Module docstring, `Inputs:` line — MODIFY**

```python
Inputs:     the sources above; --kernel renderB2 (default), renderB,
            renderA; --tables/--define (an experiment's build); --rows
            (DL_NET_ROWS); --elf PATH (S093 P4: count a linked firmware image
            instead of the stub link: the oscillator, LFO and platform calls
            are then real and costed by their own paths, and the standalone
            oscillator object is not used); --iters FUNC:RANK=N (an
            iteration count for a loop that inlining moved, repeatable);
            --dispatch N (instrumentManager_calcSlotAsync's own path to the
            Alkaline case, default 14).
```

**`main()` — MODIFY** (from `ap = argparse.ArgumentParser()` to
`memo, trace = {}, []`)

```python
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
        for f in ("osc_setBaseNote", "osc_setFreq", "osc_setTableFreq"):
            if re.search(r"<%s>:" % f, dis):
                # straight paths; osc_setFreq's longest path is its user-sample
                # bind branch (calls counted 1), a bound above Alkaline's WAV use
                ext[f] = (B.block_path(obj, f)[0], 0)
        osc_entry = B.block_path(obj, "calcWavetableOscBlock")[0]
        ext["calcWavetableOscBlock"] = (osc_entry, 0)
        ext["memset"] = (0, 0)
    memo, trace = {}, []
```

**`main()`, the totals — MODIFY** (`per_block = asy[0] + syn[0] + DISPATCH`)

```python
    # whole per-block work: both entry points plus the dispatcher's own path
    per_block = asy[0] + syn[0] + a.dispatch
    vdiv = asy[1] + syn[1]
    print(f"per block: async {asy[0]} + sync block {syn[0]} + dispatch {a.dispatch} = "
          f"{per_block} instructions, {vdiv} VDIV")
```

**`total()`, the missing-iteration exit — MODIFY** (the hint)

```python
        if it is None:
            # S093 P4: on a linked image this is usually a helper's loop that
            # LTO inlined (alkAcc_step 4, alkMeg_step 4, alk_expoCurrent 3,
            # the network rows --rows): find it with elf_loopsrc.py on the -g
            # twin and pass --iters FUNC:RANK=N
            sys.exit(f"control_cost.py: no iteration count for loop {rank} of {base} "
                     f"(length {length}); add it to ITERS or pass --iters {base}:{rank}=N")
```

**The G4 procedure:**

```
python3 tools/alkaline/control_cost.py --elf build/lxr02.elf
#   on an exit for loop R of F:
python3 tools/dsp_test/elf_loopsrc.py --elf build/s093p4/dbg_now/lxr02.elf --sym F
#   read which inlined helper loop R belongs to; rerun with --iters F:R=<its count>
python3 tools/dsp_test/elf_blockpath.py --elf build/lxr02.elf --sym instrumentManager_calcSlotAsync
#   if its path to the Alkaline case differs from 14, pass --dispatch
```

**Pass:** per block ≈ 1,285 (r6) and 9 VDIV, within the LTO tolerance of
G4 in §3.4.

### 3.2 `tools/dsp_test/elf_loopsrc.py` — ADD (new file): source attribution of loops

**Why:** G6 (mixer) and G7 (oscillators) compare loop dumps whose layout LTO
moves when a fifth engine joins (K2). `elf_hotloops.py` pairs loops by
opcode similarity, so a GREW line may be a mispairing. P3 §1 attributed the
mixer's loops by hand from a `-g` twin (`objdump -l`). This tool does it
mechanically.

**Verified in scratch** on `build/s093p3/dbg/lxr02.elf`:
- `mixer_calcNextSampleBlock` gives 86 loop bodies, nested ones included
  (P3 §1 counted 45 by hand);
- `transient_calcClickBlock` is 22 per caller, matching P3 §1;
- `voicePost_drum`'s `distortion_curveSampleF` loop is 47 long;
- a self-compare of the mixer and `SVF_calcBlockZDF` gives 0 grew.

Two bugs in the first draft were fixed by these runs:
- the repository path contains a space, which broke the file column;
- shared leaves such as `bufferTool_satAdd32` need their caller in the
  pairing key.

```python
#!/usr/bin/env python3
"""Name the source of every per-sample loop in a function (S093 P4).

What:       for each backward-branch loop of the listed functions (the loops
            elf_loopdump.py keeps: no jump table, literal, call or return
            inside), asks arm-none-eabi-addr2line -f -i for the loop head's
            inline chain on a -g build and prints
              <function>  <length>  <innermost function> <file>:<line>  [<outer chain>]
            With --compare BASE NOW (two -g images), pairs the loops of each
            function by (innermost function, file, its caller, occurrence
            order) and prints every pair whose length changed and every loop
            without a partner; exit 1 when a paired loop grew.
Why:        S093 P4 gates G6/G7: LTO re-lays out mixer_calcNextSampleBlock()
            when the set of inlined engines changes (P3 §2.3), so pairing by
            opcode similarity (elf_hotloops.py) can mispair. The source of a
            loop does not move. A -g build has the same code as the release
            build (P3 §1: 168 bytes of veneer branch offsets differ, every
            loop length is identical). The caller is part of the key because
            shared leaves (bufferTool_satAdd32) are inlined into many callers.
Inputs:     --elf PATH --sym NAME ... (a -g image), or --compare BASE NOW
            --sym NAME ...
Outputs:    the listing; with --compare, exit 1 when a paired loop grew.
Accessors:  python3 tools/dsp_test/elf_loopsrc.py ...; S093_P5 §3.4 G6/G7,
            §3.3 (the Drum ceiling's mixer-inlined parts).
Affiliates: elf_loopdump.py (functions(), the loop rule), elf_hotloops.py
            (the opcode pairing it checks).
"""
import argparse
import collections
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import elf_loopdump as L   # noqa: E402


def loops_with_heads(rows):
    """(head address, length) for every loop elf_loopdump.loops() keeps.

    The same scan as loops(): a backward branch at addr to lo closes the body
    lo..addr; a body holding dispatch code is skipped. The head address is
    kept because addr2line needs it."""
    out = []
    for addr, op, args in rows:
        if not L.BRANCH.match(op):
            continue
        t = L.TARGET.search(args)
        if not t or int(t.group(1), 16) >= addr:
            continue
        lo = int(t.group(1), 16)
        body = [o + " " + g for a, o, g in rows if lo <= a <= addr]
        if any(L.DISPATCH.search(b) for b in body):
            continue
        out.append((lo, len(body)))
    return out


def inline_chain(elf, addrs):
    """addr -> [(function, file:line), ...], innermost first.

    addr2line -a -f -i prints, per address, a 0x... line and then a
    (function, path:line) pair for each inline level, innermost first. The
    path may contain spaces, so only a trailing " (discriminator N)" is cut
    before the basename is taken."""
    if not addrs:
        return {}
    res = subprocess.run(["arm-none-eabi-addr2line", "-e", elf, "-f", "-i", "-a", "-C"]
                         + ["0x%x" % a for a in addrs],
                         check=True, capture_output=True, text=True).stdout.splitlines()
    out, cur, i = {}, None, 0
    while i < len(res):
        line = res[i]
        if line.startswith("0x"):
            cur = int(line, 16)
            out[cur] = []
            i += 1
            continue
        # one inline level: the function name, then its path:line
        func = line
        where = res[i + 1] if i + 1 < len(res) else "??:0"
        where = re.sub(r" \(discriminator \d+\)$", "", where)
        out[cur].append((func, os.path.basename(where)))
        i += 2
    return out


def table(elf, syms):
    """function base -> [(innermost function, file:line, length, outer chain)]."""
    out = {}
    for name, rows in L.functions(elf, set(syms)).items():
        lh = loops_with_heads(rows)
        ch = inline_chain(elf, [h for h, _ in lh])
        out[L.base(name)] = [(ch[h][0][0], ch[h][0][1], n, ch[h][1:]) for h, n in lh]
    return out


def key_list(entries):
    """((innermost function, file, caller, k), length, file:line) per loop.

    k counts loops of the same (function, file, caller) in address order. The
    line number is left out of the key because an edited file shifts it."""
    seen = collections.Counter()
    keys = []
    for func, where, n, outer in entries:
        k = (func, where.split(":")[0], outer[0][0] if outer else "")
        keys.append((k + (seen[k],), n, where))
        seen[k] += 1
    return keys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf")
    ap.add_argument("--sym", action="append", default=[])
    ap.add_argument("--compare", nargs=2)
    a = ap.parse_args()
    if not a.sym:
        ap.error("at least one --sym is required")
    if not a.compare:
        if not a.elf:
            ap.error("--elf or --compare is required")
        for fn, entries in sorted(table(a.elf, a.sym).items()):
            for func, where, n, outer in entries:
                chain = " < ".join(f for f, _ in outer)
                print(f"{fn}\t{n:4d}\t{func} {where}\t[{chain}]")
        return 0
    base, now = table(a.compare[0], a.sym), table(a.compare[1], a.sym)
    grew = 0
    for fn in sorted(set(base) | set(now)):
        b = {k: (n, w) for k, n, w in key_list(base.get(fn, []))}
        c = {k: (n, w) for k, n, w in key_list(now.get(fn, []))}
        for k in sorted(set(b) | set(c), key=str):
            if k in b and k in c:
                if c[k][0] != b[k][0]:
                    tag = "GREW" if c[k][0] > b[k][0] else "SHRANK"
                    grew += tag == "GREW"
                    print(f"{tag} {fn}: {k[0]} < {k[2]} {c[k][1]} #{k[3]}: {b[k][0]} -> {c[k][0]}")
            elif k in b:
                print(f"GONE {fn}: {k[0]} < {k[2]} {b[k][1]} #{k[3]} ({b[k][0]})")
            else:
                print(f"NEW  {fn}: {k[0]} < {k[2]} {c[k][1]} #{k[3]} ({c[k][0]})")
    print(f"elf_loopsrc: {grew} paired loops grew")
    return 1 if grew else 0


if __name__ == "__main__":
    sys.exit(main())
```

All names imported from `elf_loopdump.py` are module-level there (l. 42–64).
`arm-none-eabi-addr2line` 2.43.1 supports `-a -f -i -C`.

**Reading a result:**
- **NEW:** a loop that moved to a new inline owner. No Alkaline loop
  appears in the mixer, because both entry points are `noinline`.
- **GONE:** must pair with a NEW of the same length, or it is a real change.
- **GREW** on any non-Alkaline loop fails G6/G7.

### 3.3 The Drum ceiling on the P4 image

The ceiling was frozen on the P3 image (P3 §1). Re-measure it on the P4
image, because LTO may change the mixer's Drum loops when Alkaline joins
(K2). G5 compares against whichever figure is higher; the frozen figure
stays the record.

```
# release image and its -g twin, after phase 1
make all
make BUILD=build/s093p4/dbg_now MCU="-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -g" \
     build/s093p4/dbg_now/lxr02.elf
# the per-sample Drum loops (P3 §1 table rows), linked
for s in osc_slpFmRampBlock calcNextOscSampleBlock SVF_calcBlockZDF mixer_decimateBlock; do
  python3 tools/dsp_test/elf_looppath.py --elf build/lxr02.elf --sym $s --cond-heads --in-range; done
# the mixer-inlined parts (transient, voicePost_drum, the QADD16 mix-in), by source
python3 tools/dsp_test/elf_loopsrc.py --elf build/s093p4/dbg_now/lxr02.elf --sym mixer_calcNextSampleBlock \
  | grep -E "transient|voicePost|mixer"
# the per-block paths (P3 §1: 391 + 531)
for s in Drum_calcVoiceAsync slopeEg2_calc osc_setFreq instrumentManager_calcSlotAsync; do
  python3 tools/dsp_test/elf_blockpath.py --elf build/lxr02.elf --sym $s; done
```

Write the result as `build/s093p4/drum_ceiling.txt`, using the P3 §1 table
form.

### 3.4 Gates G1–G8

| # | Gate | Command | Pass |
|---|---|---|---|
| G1 | Build and flash | `make all`; `python3 tools/diagnostic_tools/link_budget.py` | Clean; headroom ≥ 16 KiB (expected ≈ 53 KB); `.bss` +2 B against `build/s093p4/base.elf` (R1); RAM otherwise unchanged |
| G2 | Host laws | `make -C tools/dsp_test alkaline alkaline_tables oscillator stock_tables special_tags sequencer seq_steplen`, **one by one** (the bench `make all` stops at `run_modes`, S093 P0 O7) | All green; every break build caught (AL-T's 32-bit product, AL-S's legato); `special_tags` includes the registry check and `make_init_alk.py --check` |
| G3 | Linked audio loop | `python3 tools/dsp_test/elf_looppath.py --elf build/lxr02.elf --sym DiodeLadder_renderB2Osc --cond-heads --in-range` | ≈ 213 and 3 VDIV (±3 for LTO layout); oscillator included |
| G4 | Linked per block | §3.1 procedure | ≈ 1,285 and 9 VDIV (±5 %) |
| G5 | Whole voice | G3 + G4 / 32 against §3.3 | ≤ Drum: expected ≈ 253 against ≈ 261 |
| G6 | Mixer | `make -C tools/dsp_test elf_p4_check` (phase 1 §2.2); on a GREW, `elf_loopsrc.py --compare build/s093p4/dbg/lxr02.elf build/s093p4/dbg_now/lxr02.elf --sym mixer_calcNextSampleBlock` | No other engine's per-sample loop grew (by source pairing) |
| G7 | Oscillators | the same target's `osc.txt` comparison; `elf_blockpath.py --sym osc_setFreq` | 0 MISSING; `osc_setFreq`'s per-block path stays 128 (P3 §2.3: linking `osc_setTableFreq()` once moved its layout) |
| G8 | ITCM | `arm-none-eabi-size -A build/lxr02.elf \| grep itcm` | Unchanged (14,512 B) |

**On a G5 failure:** the device gate (§4.1 step 4) still runs, because the
static figures are longest paths. If the device confirms the failure, set
`ALKALINE_INSTRUMENT_TYPE_FLAGS` to `INSTRUMENT_FLAG_ADVANCED` (K1). That
fallback is the user's decision, not this schedule's.

---

## 4. P4-7: device

### 4.1 Steps

1. **First boot with five types:**
   - the card carries `SD_CARD/`'s `Instrument/Alkaline/init.alk` (P4 §4.6);
   - boot completes; `/.hcnames` is rebuilt once (its header gains `alk`,
     P4 §5.1);
   - `/Instrument/Alkaline/.hcindex` exists afterwards.
2. **First instrument and the Load list (F1, F4):**
   - Instrument Load on any slot lists Alkaline after HiHat;
   - Load → Alkaline → `init`; every page shows the `init.alk` values (P4
     §4.2 layout);
   - the Morph row reads `AlkalMrp`, Cymbal's `CymbaMrp`, Drum's and HiHat's
     unchanged (P4 §4.7).
3. **Listening:**
   - an Alkaline track plays the S092 P4 pattern;
   - compare with `tools/alkaline/alk_c_acid_B2R.wav` and
     `alk_c_lowres_B2R.wav` (regenerated after r6 in P4-0);
   - a `res` sweep, as `tools/alkaline/listen/alk_net_sweep_r6.wav`;
   - drive ×1/×4/×8, **low resonance at high drive first** (B2R's weak spot);
   - saw and square.
4. **Make-up by ear (B-2):**
   - set `ALK_MAKEUP_OCT` (`AlkalineVoice.h` l. 48) against a Drum at the
     same `vol`;
   - keep AL-D's headroom: rerun `make -C tools/dsp_test alkaline` after a
     change;
   - regenerate `check_c.py` (C2 divides the make-up out, so its gate does
     not move).
5. **CPU (binding):**
   - the worst-case Scene: **Alkaline in all four Basic slots** at their
     dearest (Res full, drive ×8, accents, slides), the two Advanced slots at
     their dearest;
   - the `cpu` widget at or below the baseline Scene of the same layout with
     Drum in the Basic slots;
   - 0 underruns over 5 minutes.
6. **Storage round trips (§2.2):**
   - Instrument Save of a changed `init` under a new name: the file appears
     in `/Instrument/Alkaline/` and reloads;
   - Kit, Scene and Bank round trips with an Alkaline slot;
   - AutoSave restore after a power cycle;
   - the `kit` snapshot restore (Load preview, then back out).
7. **Modulation and MIDI:**
   - Morph, LFO and step automation on every row; an LFO on `coa` must move
     the pitch (T1-7 keeps `modNodeValue`);
   - `wav` changes by automation only between Saw and Sqr (never blended);
   - MIDI velocity → accent at ≥ 101 and not at 100;
   - MIDI note on;
   - NRPN rows 0–25 on the Alkaline slot (S10);
   - the LFO retrigger (`rtg`) restarts on each trigger (AL-P on the device).
8. **Sequencing:**
   - the gate follows the step scale: change the track's scale and the BPM
     during play, and the note length follows on the next trigger (AL-T);
   - slide into a rest closes the gate;
   - external clock.
9. **W7 (Re3/Sw3 removal):**
   - a Kit with `sNN` modulators re-selected;
   - the `wav` menu of a Drum shows six built-in waves.
10. **Known behaviour to confirm, not fix:**
    - tracks 6/7 share slot 5 (B-9);
    - MIDI note-off does not end the gate (B-6);
    - roll hits each get a full gate (B-7).

### 4.2 `knowledge_files/volatile/OPEN_TESTS.md` — ADD

Add under `## Hardware soak / confirmation` (l. 8), as its first item:

```markdown
- **S093 P4 Alkaline (TB-303) device trial (HIGH PRIORITY):**
  - A1: card from `SD_CARD/` (with `Instrument/Alkaline/init.alk`); boot
    completes; `/Instrument/Alkaline/.hcindex` written
  - A2: Instrument Load lists Alkaline after HiHat; `init` loads with the
    init values on every page; Morph rows read `AlkalMrp` / `CymbaMrp`
  - A3: S092 P4 pattern against `tools/alkaline/alk_c_acid_B2R.wav` and
    `alk_c_lowres_B2R.wav`; `res` sweep; drive x1/x4/x8 at low resonance
    first; saw and square
  - A4: `ALK_MAKEUP_OCT` by ear against a Drum at the same `vol`
  - A5: CPU worst case: Alkaline in the four Basic slots at Res full, drive
    x8, accents and slides; `cpu` widget ≤ the Drum baseline; 0 underruns
    over 5 min. **Write down the widget figures**
  - A6: `.alk` save under a new name and reload; Kit/Scene/Bank; AutoSave
    after power cycle; `kit` snapshot restore
  - A7: Morph/LFO/automation on every row (LFO on `coa` moves the pitch);
    velocity 101 accents, 100 does not; NRPN 0-25; `rtg` restarts per trigger
  - A8: gate follows step scale and BPM changes; slide into a rest closes;
    external clock
  - A9 (W7): Kit with `sNN` modulators re-selected; Drum `wav` lists six waves
  - Confirm, not fix: tracks 6/7 share slot 5; note-off does not end the gate;
    roll hits get full gates
```

---

## 5. P4-8: documentation

All paths are under `knowledge_files/specification_reference/` unless
stated. Write every entry after the device trial, using measured figures.

| File | Section | Content |
|---|---|---|
| `dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | §3.1 (l. 146) | the fifth union member (540 B at r6); the slot reserve unchanged (1,264 B) |
| | §3.2 (l. 182) | the Alkaline dispatch rows (P4 §5.6), the `ampEnvelopeQuiet` special case, the `triggerTrack` step-length argument |
| | §5 (l. 311): new **§5.5 Alkaline (`AlkalineVoice.c`, Basic)**; the current §5.5 becomes §5.6 | the voice: the oscillator (table wave, `osc_setTableFreq()`, the fused read), the B2R ladder, the N1 network at 64 samples with r6, the OTA, the control path (MEG, VEG, accent network T2-3, expo); the 26 rows and their tapers; the 303 sequencing rules |
| | §6 (l. 563): new **§6.8 Diode ladder (`DiodeLadder.c/.h`)** | the three kernels (only B2R called); `dl_slowUpdate()` phases/rows; the A-R + T2-2 instability note (K5) |
| | §8.1 / §8.2 (l. 1253, 1279) | the linked G3/G4/G5 figures beside the Drum ceiling (the P3 §9.4 table form, linked) |
| | §8.4 (l. 1337) | the Alkaline per-block items (from G4's trace) |
| | §9 (l. 1348): new **§9.2 Add an instrument type** | the checklist of P4 §5.3's comment: enum value before UNKNOWN; designated registry row; `<Type>Parameters.h/.c`; runtime member, accessor and the §5.6 dispatch cases; `ampEnvelopeQuiet` if the type has no SlopeEg2; Makefile include path, `SRCS`/`DSP_SRCS`, per-file `-Ofast` rules; a `check_special_tags.py` `TABLES` row; a 3-character token; a label ≤ 8 cells; `SD_CARD/Instrument/<Dir>/` with an init file |
| `FILESYSTEM_SPEC.md` | "Instrument Files" (l. 1775), "Canonical Instrument Keys" (l. 1858) | the `alk` token, `.alk`, `/Instrument/Alkaline`; the 26 keys; `init.alk` in the card image (F1); the registry invariants (rows in enum order, 3-character tokens) and the one-time `/.hcnames` rebuild when a type is added |
| | Kit Morph Load (l. 268) | `<Type>Mrp` is the first 5 label characters + `Mrp` (P4 §4.7) |
| `MIDI_NRPN_TABLE.md` (root) | "Voice parameter table" (l. 26), "Where the four types diverge" (l. 76) | an Alkaline column: rows 0–25 by index; the heading becomes "five types" |
| `MIDI_SPEC.md` | the NRPN and velocity sections | velocity ≥ 101 = accent on Alkaline; no note-off gate (B-6) |
| `dsp_instruments_effects/CPU_USE_DSP_AUDIT.md` | new entry after the S091 items (≈ l. 2299 block) | S093: Alkaline measured figures (G3–G5), the frozen and re-measured Drum ceiling, the device `cpu` result |
| `MEMORY.md` (root) | the binding rules | `OSC_SAMPLE_START` 6; no `osc_setFreq()` on the Alkaline oscillator (`osc_setTableFreq()`, table waves only); the fused read; `ALK_NET_STATES` 6 on the common basis (regenerate and re-check P3 §9.3 on any model change); a switchable sample/wavetable Alkaline oscillator is a class decision (P3 §9.5); the registry invariants and the add-a-type checklist pointer |
| `knowledge_files/log_archive/093_SESSION_HANDOFF_LOG.md` | new | the S093 log (P0–P4), in `SESSION_HANDOFF_TEMPLATE.md` form |
| `knowledge_files/log_archive/000_SESSION_INDEX.md` | the index | one line for S093 |
| `knowledge_files/volatile/OPEN_TESTS.md` | — | §4.2 above; tick items as the user reports them |

---

## 6. Risks

| # | Risk | Handling |
|---|---|---|
| K1 | Budget margin ≈ 8 per sample (static figures, r6) | G4/G5 on the linked image; the device `cpu` widget decides; Advanced is the fallback (user's decision) |
| K2 | LTO re-lays out the mixer when the fifth engine joins | `noinline` on both Alkaline entry points; G6 with source pairing (§3.2); the Drum ceiling re-measured (§3.3) |
| K3 | Control-rate figures are static longest paths | G4 on the linked image (§3.1); device |
| K4 | `osc_setTableFreq()` returns level 10 for a NaN frequency (`osc_setFreq()` returns 0) | The Alkaline pitch is always finite; the contract says so |
| K5 | A-R is unstable with the 64-sample network | A-R is not called; if it is ever chosen, use the 32-sample network |
| K6 | The bench `make all` stops at `run_modes` (O7) | Targets one by one (G2) |
| K7 | A switchable sample/wavetable oscillator raises the dearest setting over Drum (P3 §9.5) | Not in this prototype; a class decision |
| K8 | r6's tables depend on a reduced basis recomputed from the model | `gen_alk_tables.py --check`; any model change regenerates and re-runs P3 §9.3 |
| K9 | The per-block figures exclude the trigger, which now makes the three platform calls and one 64-bit division (`__aeabi_uldivmod`, confirmed in the M7 object) | Once per step in the main loop, not the audio path |
| K10 | `seq_effectiveTrackScale[]` / `seq_tempo` are read from the main loop while the foreground refresh or `seq_setBpm()` writes them | Single aligned reads (P4 §3.2 comment); a gate one refresh stale is inaudible |
| K11 | A card's `/.hcnames` is rebuilt on the first boot with five types | Designed behaviour of the header check (`filesystem.c` l. 6591); device step 1 observes it |
| K12 | Cymbal's Morph Load row changes from `CymbalMr` to `CymbaMrp` | Intended (P4 §4.7); no test or spec pins the old text |

---

## 7. Complete file index (both phases)

| File | Tag | Where |
|---|---|---|
| `tools/alkaline/gen_alk_tables.py` | MODIFY (l. 45 `NET_STATES = 6`) | P4 §2.1 |
| `Core/DSPAudio/DiodeLadder.h` | MODIFY (l. 74–76 `ALK_NET_STATES 6u`; stale wording l. 61–62, 72, 104) | P4 §2.1 |
| `Core/DSP/Instruments/Alkaline/AlkalineTables.h/.c` | regenerated (r6) | P4 §2.1 |
| `tools/alkaline/alk_c_*_B2R.wav` | regenerated (`check_c.py`) | P4 §2.1 |
| `tools/dsp_test/Makefile` | ADD `FW_ELF`, `elf_p4_snapshot` / `elf_p4_check`, `seq_steplen`; MODIFY `.PHONY`, `all:`, `special_tags` | P4 §2.2, §3.4, §4.4 |
| `tools/alkaline/control_cost.py` | MODIFY (`--rows` 3, stubs and include, trigger call, `--elf`, `--iters`, `--dispatch`, the exit hint) | P4 §2.3, §3.3; P5 §3.1 |
| `Core/DSP/Instruments/Alkaline/AlkalineVoice.h` | MODIFY (trigger prototype) | P4 §3.1 |
| `Core/DSP/Instruments/Alkaline/AlkalineVoice.c` | MODIFY (includes, trigger, init defaults) | P4 §3.1, §4.5 |
| `Core/Sequencer/sequencer.h` | ADD (`seq_trackStepSamples` prototype) | P4 §3.2 |
| `Core/Sequencer/sequencer.c` | ADD (`seq_trackStepSamples()`) | P4 §3.2 |
| `tools/dsp_test/modules/alkaline.c` | MODIFY (stand-ins, 15 trigger calls); ADD (AL-P) | P4 §3.3 |
| `tools/dsp_test/modules/seq_steplen.c` | ADD (AL-T) | P4 §3.4 |
| `Core/DSP/Instruments/Alkaline/AlkalineParameters.h` | ADD | P4 §4.1 |
| `Core/DSP/Instruments/Alkaline/AlkalineParameters.c` | ADD | P4 §4.2 |
| `Core/Menu/MenuText.h` | ADD (`ONOFF_TAG_ALK_WAVE`, `alkWaveNames`) | P4 §4.3 |
| `Core/Menu/menu.c` | MODIFY (`menu_onoffTagNames()`, `menu_instrumentLoadCopyTypeLabel()`); ADD (assert) | P4 §4.3, §4.7 |
| `tools/dsp_test/check_special_tags.py` | MODIFY (`TABLES['ALK']`, `registry_check()`, docstring, `main()`) | P4 §4.4 |
| `tools/alkaline/make_init_alk.py` | ADD | P4 §4.6 |
| `SD_CARD/Instrument/Alkaline/init.alk` | ADD (new directory and file; generated) | P4 §4.6 |
| `Core/DSP/Instruments/InstrumentManager.h` | MODIFY (enum) | P4 §5.1 |
| `Core/DSP/Instruments/InstrumentManager.c` | MODIFY (includes, designated registry + assert, union + assert, accessor, eleven dispatch sites, quiet test, trigger) | P4 §5.2–§5.6 |
| `Makefile` | MODIFY (include path, `SRCS`, `DSP_SRCS`, four `-Ofast` rules) | P4 §6.1 |
| `Core/Hardware/SD/storageTypes.h` | ADD (`STORAGE_INSTRUMENT_ALK`) | P4 §6.2 |
| `Core/Hardware/SD/storageTypes.c` | MODIFY (`storage_instrumentTypeToText()`) | P4 §6.3 |
| `tools/dsp_test/elf_loopsrc.py` | ADD | P5 §3.2 |
| `knowledge_files/volatile/OPEN_TESTS.md` | ADD | P5 §4.2 |
| Documentation (eight files) | MODIFY / ADD | P5 §5 |

**Verified unchanged (NONE):**
- the InstrumentManager sites of P4 §5.7;
- the sites outside it in P4 §6.4 and P5 §2.1;
- `ResonantFilter.c`, every existing oscillator loop, and ITCM.
