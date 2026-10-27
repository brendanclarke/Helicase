# tools/plots — S085 oscillator plots

Design and illustration plots for the S085 oscillator/modulation work. Nothing
here is part of the firmware build.

## Requirements

Python 3 with `numpy`, `matplotlib` and `pillow`. These are not project
requirements (`requirements.txt` stays stdlib-only), so use a throwaway venv:

```
python3 -m venv /tmp/lxrplots && /tmp/lxrplots/bin/pip install numpy matplotlib pillow
cd tools/plots && /tmp/lxrplots/bin/python plot_pwm.py   # etc.
```

## Scripts and outputs

| Script | Output | What it shows |
|---|---|---|
| `plot_stock_waves.py` | `stock_waves_303_analog.png`, `stock_wave_harmonics.csv` | The stock waves `sq3 sw3 sqa tra swa`: reference trace, the level-0 table, band-limiting at 1 kHz and 4 kHz, harmonic spectra; the CSV is the harmonic recipe (k = 1..511) the install-time generator uses |
| `plot_pwm.py` | `pwm_families.png` | The `pwm` families `prc psw ptr pq3 pw3 swp` at four control values, as played at 220 Hz |
| `plot_wavetable_formats.py` | `wavetable_formats.png` (+ printed table; `--stock` prints the stock-wave layouts) | Wavetable storage formats played as the oscillator plays them: aliasing/images and brightness per note against flash size, on a folder of single cycles (default `SD_CARD/wavetables/ak_hvox`) |
| `lxr_plotlib.py` | — | Shared code: Python ports of the firmware oscillator/filter helpers, band-limiting helpers, the reference-image digitisers, plot style |

## Reference images (`reference/`)

- `303_square_saw.png` — TB-303 square (left) and saw (right) captures;
  digitised for `sq3` and `sw3`.
- `tra_dirty_triangle.png` — the dirty triangle wanted for `tra`.

The digitisers find the trace pixels per column, measure the period by
autocorrelation, average all full cycles, and keep the first 30–40 harmonics
(what the screenshots resolve). The remaining harmonics up to 511 are extended
with a fitted roll-off (`lxr_plotlib.extend_harmonics()`).

## Firmware formulas ported

| Python | Firmware |
|---|---|
| `filter_input_stage()`, `ZdfFilter` | `SVF_calcBlockZDF()` batched solver (`ResonantFilter.c`) |
| `shaper_f2f()`, `fast_tan()` | `valueShaperF2F()`, `fastTan()`; filter cutoff/resonance writers in `InstrumentManager.c` |

If any of those firmware functions change, update the port here before
trusting the plots.
