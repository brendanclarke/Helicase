# DRAFT — Helicase as a Standalone App / Audio Plugin

Status: draft feasibility note, 2026-11-18. General assessment only; no code
was changed. Effort figures are rough orders of magnitude for one developer
who already knows the codebase, and they assume an AI-assisted workflow like
the current one.

---

## 1. Summary

**Moderate difficulty. The sound engine is the easy part, and the effort
depends mostly on how native the result has to feel.**

- The DSP, mixer, sequencer and MIDI logic are plain C with almost no
  hardware coupling. The host test bench (`tools/dsp_test/`) already compiles
  the real `Core/` sources on macOS, so the engine is known to build on a host.
- The hard parts are the things built around the LXR-02 hardware: the
  interrupt-driven timing model, the async SD filesystem layer, the 2×16 LCD
  and panel UI, the fixed 44,108 Hz sample rate, and the fact that the whole
  engine is one global instance.
- A **single-instance standalone "emulator" app with a virtual front panel**
  is the cheapest useful target, at roughly **6–10 weeks**.
- A **VST3/AU/CLAP plugin** costs about the same again, mainly for
  per-instance state, host state save/restore, and exposing host parameters,
  at roughly **+6–12 weeks**.
- A **plugin with a native desktop GUI** in place of the panel emulation is a
  separate UI project that could cost as much as everything above.
- **Licensing may block more than the engineering does.** The core DSP and
  the sequencer are still in files carrying Julian Schmidt / Sonic Potions
  copyright under the LXR licence, which forbids sale and commercial use.
  A free, source-available release is possible as things stand. A paid or
  closed release needs permission from Sonic Potions or clean rewrites of
  those files. §6 lists the files.

## 2. What the codebase looks like from a port's point of view

| Area | Approx. lines (.c/.h) | Port outlook |
|------|----------------------:|--------------|
| `Core/DSPAudio` (osc, filters, mixer, EGs, LFO) | 24k | Reuse as is |
| `Core/DSP` (instruments, effects) | 34k | Reuse as is |
| `Core/Sequencer` | 11k | Reuse; it needs a new clock driver |
| `Core/MIDI` | 6k | Reuse; feed it host MIDI bytes |
| `Core/Bank` (Bank/Scene/Kit/Preset/Morph) | 22k | Reuse; storage hooks need work |
| `Core/Menu` | 31k | Reuse behind a virtual panel, or replace |
| `Core/Hardware/SD/filesystem.c` | 36k | Needs a shim or a rewrite (§3.3) |
| `Core/Hardware/SD/asyncfatfs`, `SPI` | 10k | Drop |
| `Core/Hardware/USB` | 10k | Drop |
| `Core/Hardware/frontPanel` (LCD, LEDs, buttons) | 9k | Replace with a virtual panel |
| `Core/Hardware` other (clocks, codec, flash image, memtest, timebase) | ~8k | Drop or stub |

About 60 % of the source (DSP, sequencer, MIDI, Bank, Menu) can carry over.
About 30 % (USB, SD drivers, panel drivers, clocks) is dropped and replaced
by much smaller host code. The rest is the filesystem facade, which is large
and needs a decision of its own.

## 3. Work areas

### 3.1 DSP engine (low difficulty)

- Few hardware intrinsics: `__SSAT` (~35 uses), `__USAT` (~7) and `__QADD`
  / `__QADD16` (3). `tools/dsp_test/prelude.h` already has portable versions
  of these.
- Memory placement macros (`INITCM`, `INDTCM`, `INCCM`, about 94 uses in DSP)
  are defined as nothing on the host. The prelude already does this.
- The arithmetic is int16/int32 fixed point with float in places, so host
  output should be bit-identical or very close. That lets the existing host
  tests and baselines act as the reference for the port.
- `GetRngValue()` reads the hardware RNG. A host build needs a software
  PRNG. On hardware a software PRNG was rejected for its sound (S073); on a
  desktop there is no other choice, so the noise character will differ
  slightly.
- Samples and wavetables are read from memory-mapped flash at
  `0x080C0000`. On the host this becomes a RAM buffer filled at start-up by
  the existing `SampleInstall.c` logic, which is already host-compiled in
  `catalogue_host.c`.
- The CPU mandates (cycle budgets, ITCM ordering, LTO register pressure) do
  not apply on a desktop. Desktop CPUs have plenty of headroom.

### 3.2 Timing and threading (medium difficulty)

The firmware has three execution contexts:

- **TIM3 ISR at 4 kHz:** `seq_tick()`, MIDI realtime, trigger jacks.
- **Main loop:** renders 32-sample blocks into a 2-slot queue, and runs
  menu, filesystem and LED work between blocks.
- **`BASEPRI` and critical sections** (about 31 sites in the logic code)
  protect data shared between those contexts.

In a plugin, the host's audio callback drives everything:

- Split each host buffer at sequencer tick boundaries (one tick ≈ 11.03
  samples at 44,108 Hz) and call `seq_tick()` the right number of times
  between 32-sample renders. TIM3 already runs before render, so this keeps
  the same ordering.
- Take tempo and transport from the host's playhead instead of MIDI clock
  and `clockSync.c`. External MIDI clock can stay as an option for the
  standalone app.
- Move menu and storage work to a UI/worker thread, with a lock-free command
  queue into the audio thread. Each critical section must be checked to see
  whether it can become a no-op (same thread) or needs a real lock. This is
  the main correctness risk of the port.

### 3.3 Storage (medium–high difficulty)

`filesystem.c` (36k lines) and AutoSave were written around one constraint:
a bit-banged SD card that must never block the audio loop. On a desktop that
constraint is gone, but the logic is built on top of it.

- **Option A: shim (lower effort).** Implement the `afatfs_*` API used by
  `filesystem.c` on top of POSIX file I/O, keeping the async poll contract.
  Everything above it (index files, `.hcnames`, Bank/Scene/Kit formats,
  AutoSave) keeps working unchanged. FAT-specific repair code (directory
  tail trim, cluster-rounded sizes) becomes a no-op. This is the
  recommended first step.
- **Option B: rewrite (higher effort, cleaner).** Replace the facade with
  synchronous file I/O on a worker thread. This would remove a lot of code,
  but it touches the most heavily specified and most heavily debugged part of
  the project.
- **Plugin state (new work in either case).** DAWs save the plugin's state
  inside the project file (`getState`/`setState`). The current Bank must
  serialise to a single in-memory blob and restore from one, without
  depending on the library directories. AutoSave would likely be turned off
  in plugin builds, since the host owns persistence.
- The on-card data layout (human-readable directories) maps directly to a
  user library folder, such as `~/Music/Helicase/`.

### 3.4 Sample rate (low–medium difficulty)

44,108 Hz is hard-coded in several places, for example `ALK_DT`, `SK_FS`,
`FXBUF_VOICE_RATE_HZ_DEFAULT`, the sequencer tick maths, the voice-fold and
LFO rate tables, and every table generator that assumes it.

- **Recommended:** keep the engine at a fixed internal 44,108 Hz and
  resample to the host rate (44.1/48/96 kHz) at the output. This is simple,
  keeps the sound identical, and adds a few samples of latency that can be
  reported to the host.
- Making the sample rate a true runtime variable would mean regenerating
  tables per rate and re-checking the Alkaline model work. That is a large
  job and is not needed.

### 3.5 Global state and multiple instances (high difficulty, plugins only)

The firmware ELF has about **967 mutable global symbols** and **~420 KB of
`.bss`**. Every module assumes there is one engine. A standalone app is fine
with that. A plugin is not, because DAWs load several instances into one
process.

- **Option 1:** limit the product to one instance per process. Some hosts
  can run plugins in separate processes (Bitwig, Reaper), but this is not
  reliable everywhere. This is acceptable for an early beta.
- **Option 2:** gather all engine state into one context struct and pass it
  through, or reach it through a thread-local pointer. This is the correct
  fix, and it is mechanical but large: weeks of work that touch nearly every
  file.
- **Option 3:** compile the engine as C++ inside a class or namespace so the
  globals become members. This is less churn than Option 2, but it brings
  C/C++ semantic differences into code with strict bit-exact rules.

### 3.6 User interface (scales with ambition)

The panel is a 2×16 character LCD, four mode buttons plus shift, 16 step
buttons, track/select/bar buttons, LEDs, endless pots, sliders and the main
encoder. `menu.c` alone is 21k lines.

- **Virtual front panel (recommended first).** Draw the LCD text, LEDs,
  buttons and knobs in a window and feed events into the existing
  button/encoder/pot handlers. The whole menu system is reused unchanged and
  behaves exactly like the hardware. Effort is moderate, because the panel
  drivers in `frontPanel/` are the only replacement needed. Calls such as
  `led_pulseLed()` from the sequencer and MIDI code keep working against a
  virtual LED buffer.
- **Native GUI.** Proper pages, waveform displays and mouse editing of steps
  and automation. This is essentially a new product UI built on the
  Bank/Preset APIs, and it is the largest single item in this report.

### 3.7 Host parameters and automation (medium difficulty, plugins only)

- Voice parameters are **slot-generic**: NRPN = `slot × 64 + index`, and an
  index means something different for each instrument type. Hosts expect a
  fixed, named parameter list.
- Options: expose 384 generic parameters ("V1 P00" …) whose names update
  when the type changes (some hosts handle renaming poorly), or expose a
  smaller set of macros (Morph, per-voice Morph, FX Morph, levels) and leave
  the rest to MIDI/NRPN, which already works.
- Every host write must go through `menu_commitTargetEdit()` so the
  automation-priority rules (R12/R13) stay intact. Host automation would
  count as a menu edit, just like MIDI does today.

### 3.8 Audio and MIDI I/O (low difficulty)

- The mixer already produces two stereo pairs (`output2` = St1, `output` =
  St2). Expose them as a main stereo out plus an aux stereo out, converting
  signed-24-in-int32 to float.
- Host MIDI events become byte streams into the existing `Core/MIDI` parser,
  timestamped to the matching sub-block.
- Trigger jacks and DIN/USB MIDI out are dropped for plugins. The standalone
  app can map MIDI out to a virtual or system port.

## 4. Suggested route

| Phase | Deliverable | Rough effort |
|-------|-------------|--------------|
| 0 | Platform layer: an `#ifdef HOST` build with no-op placement macros, intrinsic shims, software RNG, RAM sample store. A command-line tool renders a Bank to WAV. | 1–2 weeks |
| 1 | Standalone app: audio device output, 44,108 Hz → device-rate resampling, MIDI in, sequencer driven by sample count, afatfs-over-POSIX shim. | 3–4 weeks |
| 2 | Virtual front panel in the standalone app (LCD, buttons, LEDs, pots, encoder). | 2–4 weeks |
| 3 | Plugin wrapper (VST3/AU/CLAP): host transport sync, state blob save/restore, two stereo outputs, single-instance guard. | 3–5 weeks |
| 4 | Multi-instance support (engine context struct). | 3–6 weeks |
| 5 | Host parameter map and naming. | 1–3 weeks |
| — | Native desktop GUI (optional, separate project). | months |

**Framework choice:** JUCE gives the standalone app, VST3, AU and the GUI
from one C++ project, with the C engine linked in unchanged. JUCE is
AGPLv3-or-commercial, and AGPL does not allow extra restrictions such as the
LXR licence's no-commercial-use clause, so the free JUCE tier is probably
not usable while LXR-licensed files remain (§6). CLAP with `clap-wrapper`
(MIT) is a lighter, permissively licensed alternative for VST3/AU/CLAP, but
needs a separate GUI toolkit. The current VST3 SDK is also MIT-licensed. The
permissive options avoid the conflict.

## 5. Risks and open questions

- **Codebase divergence.** Every host change must be a platform layer or an
  `#ifdef`, never a fork, so firmware and plugin share one `Core/`. The
  binding mandates in `MEMORY.md` then apply to the shared code exactly as
  they do now.
- **Threading correctness.** Critical sections built for an ISR/main-loop
  model need checking one by one when they move to an audio thread plus a UI
  thread (§3.2).
- **Licensing.** See §6. This is the main non-technical risk.
- **Sound parity.** The software RNG and the output resampler will cause
  small differences from the hardware. Decide whether "sounds like the
  hardware" is a requirement or just a goal.
- **Product scope.** Is the aim a companion to the hardware (shared Bank
  files, editor/librarian) or a separate instrument? A companion favours the
  virtual-panel route and keeps file compatibility. A separate instrument
  pushes toward a native GUI and host-first persistence.

## 6. Code ownership and licensing

Based on the file headers and attribution comments in `Core/` as of
2026-11-18. This is an inventory, not legal advice. The original LXR source
is not in the tree, so it was not possible to measure how much of Julian
Schmidt's code is still present in each modified file.

### 6.1 The licence terms involved

- **LXR licence.** It is the text in `LICENSE`. It is used both by the Sonic
  Potions files (Copyright 2013 Julian Schmidt) and by your own Helicase
  files, since your headers reuse the same wording. Its terms:
  - no sale and no commercial use;
  - a modified redistribution must include its complete source;
  - the copyright notice must be kept.

  It is not a permissive (BSD-style) licence. The first draft of this report
  was wrong on that point.
- **Your own code.** As the copyright holder you can relicense code that is
  wholly yours, for example to a commercial or permissive licence for a
  plugin.
- **Files with "Modifications Copyright 2026 Brendan Clarke".** You own
  only the modifications. Julian's copyright and the LXR terms still cover
  whatever original code remains.

### 6.2 Group A: Sonic Potions files, original header only (29 files, ~12.2k lines)

These carry `Copyright 2013 Julian Schmidt` and no Helicase modification
notice. Some may still have been edited.

| Area | Files |
|------|-------|
| DSP building blocks | `Core/DSPAudio/1PoleLp.c/.h`, `Decay.c/.h`, `SlopeEg2.c/.h`, `snapEg.c/.h`, `transientGenerator.c/.h`, `transientTables.c/.h` (1.8k lines of data), `wavetable.c/.h` (7.2k lines of data), `squareRootLut.c/.h`, `dither.c/.h`, `valueShaper.h`, `distortion.h`, `ResonantFilter.h`, `medianFilter.h` ("Author: Julian") |
| Instrument headers | `Core/DSP/Instruments/Drum/DrumVoice.h`, `Snare/Snare.h`, `Cymbal/CymbalVoice.h`, `HiHat/HiHat.h` |
| Sequencer | `Core/Bank/Scene/Pattern/EuklidGenerator.c/.h` |
| Shared types | `Core/datatypes.h` |

### 6.3 Group B: Sonic Potions files with a Helicase modification notice (23 files, ~21.0k lines)

These have the Julian Schmidt copyright header plus "Modified on 17.05.2026
by Brendan Clarke" or "Modifications Copyright 2026 Brendan Clarke".

| Area | Files |
|------|-------|
| DSP core | `Core/DSPAudio/Oscillator.c/.h` (5.4k lines), `mixer.c/.h`, `modulationNode.c/.h`, `lfo.c/.h`, `ResonantFilter.c`, `distortion.c`, `BufferTools.c/.h` |
| Instrument voices | `Core/DSP/Instruments/Drum/DrumVoice.c`, `Snare/Snare.c`, `Cymbal/CymbalVoice.c`, `HiHat/HiHat.c` |
| Sequencer | `Core/Sequencer/sequencer.c` (7.4k lines), `clockSync.c/.h` |
| MIDI | `Core/MIDI/MidiDefs.h`, `MidiVoiceControl.c/.h` |
| Parameters | `Core/Bank/Scene/Preset/ParameterArray.h` |

### 6.4 Group C: Helicase headers that say the file was ported or derived from the original (34 files, ~32.2k lines)

These carry your copyright or modification notice, but state that they are
ported from, derived from, or mirror the API of Julian Schmidt's original.

| Area | Files | Desktop port |
|------|-------|--------------|
| Menu | `Core/Menu/menu.c/.h` (22k lines), `menuPages.h`, `MenuText.h`, `Cc2Text.c` ("Julian Schmidt / AS"), `CcNr2Text.h`, `screensaver.c/.h` ("Author: Julian") | Kept |
| MIDI | `Core/MIDI/MidiIn.c/.h`, `MidiTransport.c/.h`, `MidiBuffer.c/.h`, `MidiDin.c/.h`, `MidiPitch.h` | Mostly kept (`MidiDin` dropped) |
| Front panel | `Core/Hardware/frontPanel/buttonHandler.c/.h`, `ledHandler.c/.h`, `IO/adcPots.h`, `IO/din.h`, `IO/dout.h` | Handlers kept behind a virtual panel; IO dropped |
| Samples | `Core/SampleRom/SampleMemory.h` | Kept |
| RNG | `Core/DSPAudio/random.c/.h` | Replaced |
| SD driver | `Core/Hardware/SD/SPI/sd_routines.c/.h` (also credits CC Dharmani) | Dropped |
| USB glue | `Core/Hardware/USB/App/usb_bsp.c`, `usb_it.c`, `usb_manager.h`, `usbd_conf.h`, `OTG_Driver/inc/usb_conf.h` | Dropped |

The MIDI files were rebuilt in S083 and `menu.c` is now about 21k lines, so
little original text may remain. However, "derived from" in the header is
itself a claim of derivation. To treat a file as wholly yours, check it
against the original source and update its header.

### 6.5 Group D: Sonic Potions code under GPLv3 (3 files, ~0.9k lines)

`Core/Hardware/USB/App/usb_manager.c`, `usb_midi_core.c`, `usb_midi_core.h`:
"Cortex Hardware Audio Operating System (CHAOS)", Copyright 2012 Julian
Schmidt / Sonic Potions, GPLv3 or later. These are dropped from a desktop
build.

**Worth checking for the current firmware:** GPLv3 does not allow extra
restrictions, and the firmware links these files with code carrying the LXR
no-commercial clause. That conflict exists in the hardware release today,
not only in a plugin.

### 6.6 Group E: other third-party code (dropped from a desktop build)

- **ST USB stack.** `Core/Hardware/USB/OTG_Driver/*` and
  `Device_Library/*` (~8.2k lines) are under the MCD-ST Liberty SW Licence
  V2, which is limited to ST microcontrollers. `USB/App/usbd_desc.c` is ST
  code that also carries the Sonic Potions USB vendor/product strings and
  the product ID.
- **asyncfatfs.** `Core/Hardware/SD/asyncfatfs/asyncfatfs.c/.h`,
  `fat_standard.c/.h` and `sdcard.h` (~9.3k lines) have no licence notice in
  the files. The upstream (Nicholas Sherlock, used in Cleanflight and
  Betaflight) is distributed under GPLv3; confirm that against the upstream
  repository. The same question as §6.5 applies to the firmware. If the
  desktop shim from §3.3 replaces asyncfatfs rather than wrapping it, this
  code is out of the desktop build entirely.

### 6.7 Your own files with no notice (60 files, ~25.7k lines)

These are new Helicase code with no copyright or licence header at all, for
example:

- the Alkaline, Colloid and Tetrakt instruments;
- the `*Parameters.c/.h` descriptor tables;
- `InstrumentManager`;
- `Autosave`, `PatternData` and `PatternStackService`;
- `SceneData`, `BankData` and `presetMorphEngine`;
- `CopyClear`;
- `StepScale`, `SampleInstall`, `DiodeLadder` and the generated tables.

Adding your header to these settles ownership before any relicensing.
`Core/Bank/Scene/Preset/presetManager.c` and `Core/SampleRom/SampleMemory.h`
carry "Modifications Copyright" wording without naming the original; check
whether they come from LXR files.

### 6.8 What this means for each target

- **Free, open-source desktop build under the LXR licence.** This works as
  things stand, provided it ships the complete source and keeps the
  notices. Use a permissive plugin framework (CLAP / VST3 SDK), not
  AGPL JUCE.
- **Commercial or closed-source build.** Groups A and B are the DSP and
  sequencer core of the engine, and Group C covers the menu and MIDI layers
  that the virtual-panel route reuses. There are two ways forward:
  - get a licence or permission from Sonic Potions / Julian Schmidt; or
  - write clean replacements for every file in Groups A–C that the desktop
    build uses. The replacements must sound identical, the existing host
    tests can check them, and they can reuse the same file formats.

  Groups D and E are not needed on a desktop.
