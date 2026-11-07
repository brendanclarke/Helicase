# tools/plots — S084 P1 voice-expansion plots

Design and illustration plots for `S084_P1_VOICE_EXPANSIONS.md`. Nothing here
is part of the firmware build.

Copied unchanged from the S084 spike (`Helicase-dev-ph7-spike/`) in S086.
`stock_wave_harmonics.csv` is the harmonic recipe for the five new stock
waves. In S086 it is turned into fixed `[11][1024]` octave tables in program
flash, built the same way as `triTable`/`sawTable`/`recTable`. It is not used
by an install-time generator. See `086_SESSION_HANDOFF_LOG.md §3`.

## Requirements

Python 3 with `numpy`, `matplotlib` and `pillow`. These are not project
requirements (`requirements.txt` stays stdlib-only), so use a throwaway venv:

```
python3 -m venv /tmp/lxrplots && /tmp/lxrplots/bin/pip install numpy matplotlib pillow
cd tools/plots && /tmp/lxrplots/bin/python plot_current_drive.py   # etc.
```

## Scripts and outputs

| Script | Output | What it shows |
|---|---|---|
| `plot_current_drive.py` | `current_drive_profiles.png` | Today's filter `drv` soft clip, the Voice/Mix `drv` distortion, the real ZDF LP/HP loops driven by a full-scale sine, and small-signal gain against the control value |
| `plot_stock_waves.py` | `stock_waves_303_analog.png`, `stock_wave_harmonics.csv` | The new stock waves `sq3 sw3 sqa tra swa`: reference trace, the level-0 table, how they band-limit at 1 kHz and 4 kHz, harmonic spectra; the CSV is the harmonic recipe (k = 1..511) the install-time generator would use |
| `plot_stock_tables.py` | `stock_tables_levels.png` | The firmware's generated stock tables (`Core/DSPAudio/wavetableStock.c`, written by `tools/gen_stock_tables.py` from `stock_wave_harmonics.csv`): levels 0/4/8/10 beside Rec/Saw and the per-level spectra (S086) |
| `plot_303_raw_vco.py` | `303_raw_vco_vs_stock.png` | S092 P0: modelled raw TB-303 VCO saw and Q8-shaped square against today's `Re3`/`Sw3` at C2 (table level 3, same taper); square construction; `Sw3` fitted as a low-passed saw; the VCF input after an assumed coupling high-pass |
| `plot_pwm.py` | `pwm_families.png` | The `pwm` type families `prc psw ptr pq3 pw3 swp` at four control values, as played at 220 Hz |
| `plot_wavetable_formats.py` | `wavetable_formats.png` (+ printed table; `--stock` prints the stock-wave layouts) | Wavetable storage formats played as the oscillator would play them: aliasing/images and brightness per note against flash size, on a folder of single cycles (default `SD_CARD/loops`) |
| `plot_waveshapers.py` | `waveshapers_static.png`, `waveshapers_stateful.png` | The proposed filter drive types (`dty`): memoryless shapers with transfer curves, and the stateful ones (zero-crossing, trackers) on a sine and a kick-like glide |
| `lxr_plotlib.py` | — | Shared code: Python ports of the firmware drive stages, band-limiting helpers, the reference-image digitisers, plot style |

## S092 plots written by other tools

These PNGs live here but are written by the S092 TB-303 tools, not by a
script in this folder:

| Written by | Output | What it shows |
|---|---|---|
| `tools/spice/tb303/vco_q8.py` | `303_vco_q8_spice.png` | S092 P1 results: VCO, Q8 and VCF input (`S092_P1_VCB_MODEL_VCO.md`) |
| `tools/spice/tb303/vcf.py` | `303_vcf_spice.png` | S092 P2 results: the VCF (`S092_P2_VCB_MODEL_VCF.md`) |
| `tools/spice/tb303/vca.py` | `303_vca_spice.png` | S092 P3 results: the VCA (`S092_P3_VCB_MODEL_VCA.md`) |
| `tools/spice/tb303/control.py` | `303_control_spice.png` | S092 P4 results: the control network (`S092_P4_CONTROL_NET.md`) |
| `tools/spice/tb303/e2e.py` | `303_e2e_run_acid.png`, `303_e2e_run_lowres.png` | S092 P4 run: the 8-step pattern end to end (`S092_P4_SLIDE_ACCENT_RUN.md` §1.1) |
| `tools/alkaline/validate.py` (V5) | `alk_dsp_pattern_acid.png`, `alk_dsp_pattern_lowres.png` | S092 P6 V5: the Alkaline DSP model over the SPICE run (ladder current, VCA current, output, two zooms; `S092_P6_DSP_EQUATIONS.md` §6.5) |

## Reference images (`reference/`)

Supplied by the user on 2026-10-25:

- `303_square_saw.png` — TB-303 square (left) and saw (right) captures;
  digitised for `sq3` and `sw3`.
- `tra_dirty_triangle.png` — the dirty triangle wanted for `tra`.
- `waveshapers.png` — the waveshaper set (rectifying … lo square) the drive
  types follow.

The digitisers find the trace pixels per column, measure the period by
autocorrelation, average all full cycles, and keep the first 30–40
harmonics (what the screenshots resolve). The remaining harmonics up to 511
are extended with a fitted roll-off (`lxr_plotlib.extend_harmonics()`).

## Firmware formulas ported

| Python | Firmware |
|---|---|
| `filt_drive()` | `SVF_setDrive()` (`ResonantFilter.c`) |
| `filter_input_stage()`, `ZdfFilter` | `SVF_calcBlockZDF()` batched solver (`ResonantFilter.c`) |
| `dist_k()`, `dist_curve()` | `setDistortionShape()`, `distortion_curveSample16()` (`distortion.c/.h`) |
| `shaper_f2f()`, `fast_tan()` | `valueShaperF2F()`, `fastTan()`; filter cutoff/resonance writers in `InstrumentManager.c` |

If any of those firmware functions change, update the port here before
trusting the plots.
