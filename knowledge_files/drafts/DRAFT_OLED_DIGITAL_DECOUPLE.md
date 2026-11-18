# DRAFT — OLED Digital Noise Decoupling

Status: draft for review. Nothing in this document has been applied to the tree.
Question: can digital noise in the audio coming from the display be reduced by
changing the display refresh rate, or by changing the GPIO settings on the
display pins so their edges are slower and softer?

---

## 0. Short answer

1. **The display pins already use the slowest edge setting.** `lcd_gpio_init()`
   clears OSPEEDR for PE7–PE12, which selects the slowest of the four speed
   settings ([lcd.c:364](Core/Hardware/frontPanel/lcd.c#L364)). The speed
   register cannot slow them any further.
2. **Two firmware-only edge changes are still possible** (§3.1, §3.2):
   - write each nibble with one register store. At present every data line
     that should be 1 is driven to 0 first, which adds a glitch pulse;
   - make DB4–DB7 open-drain with the internal pull-up, so their rising edges
     take microseconds instead of nanoseconds.
3. **"Refresh rate" means two different things here** (§1.3):
   - The OLED panel's own row scan and its internal power converter
     (switched on by command `0x17`). The driver has no command that changes
     the scan rate. Assume the firmware cannot change it.
   - The rate at which bytes are sent on the bus. This is TIM7 at 5 kHz, and
     bytes are sent only when a character changes. Raising the rate shortens
     each burst and moves its buzz up in frequency, but the display's command
     execution time keeps that buzz inside the audio band (§3.3). Lowering the
     rate makes the buzz lower and slows the UI.
4. **When the screen is not changing, the display bus is idle.** TIM7 stops
   when its queue is empty ([lcd.c:546-556](Core/Hardware/frontPanel/lcd.c#L546-L556)),
   and `sendDisplayBuffer()` sends only changed cells
   ([menu.c:13567](Core/Menu/menu.c#L13567)). **If you hear the noise while
   the screen is static, it is not coming from the display comms pins.** In
   that case the likely sources are:
   - the panel's own drive current;
   - the 500 Hz front-panel SPI1 exchange to the LED and button shift
     registers. Its clock pin PB3 is never given a speed setting, so it stays
     at its reset value, which is the fastest setting (§1.4).
5. **Slower edges mostly reduce high-frequency (RF) content, not audio-band
   content** (§2). The supply still delivers the same charge on every edge;
   it is just spread over a longer time. A slower edge therefore helps when
   the noise reaches the audio through RF pickup or through inductive ground
   bounce. It does little when the noise is the audio-rate pattern of bursts
   itself.

**Recommendation:** run the diagnostics in §4 first. They are cheap and show
which source you are hearing. Then apply the changes in the order in §5. §3.1
(one store per nibble) and §3.5 (PB3 speed) carry no risk and no CPU cost.

### 0.1 First observation (user): quietest with the screensaver on

This is diagnostic D3. While the screensaver is active, two things drop
together:

- **The panel is mostly dark.** It is off for one phase in four and shows a
  single character for the other three, so source B is close to its minimum.
- **Bus traffic almost stops.** `menu_sceneLiveRefreshService()` returns early
  while the screensaver is active ([menu.c:4001](Core/Menu/menu.c#L4001)), and
  there are no knob or widget repaints. What is left is about 4 bytes plus one
  CLEAR every ~412 ms, so source A is close to its minimum too.

**SPI1 (C) keeps running at 500 Hz while the screensaver is on.** So C is not
the main part of what goes quiet. Whatever remains audible with the screensaver
on is the best candidate for C.

**The next step is D1:** sequencer stopped, normal page, hands off, CPU widget
not visible. The screen is lit, but the bus is idle.

- **If D1 is about as noisy as normal use,** the noise is panel drive (B).
  Slowing the edges will not help, and the fix is the module supply filter and
  return path (§3.7).
- **If D1 is about as quiet as the screensaver,** the noise is the bus bursts
  (A). Then §3.1, §3.2 and §3.3 apply.
- **If D1 falls in between,** both contribute. D2 (full page compared with a
  sparse page) shows how much of it follows the lit content.

---

## 1. What the firmware does now

### 1.1 Display pins

| Signal | Pin | MODER | OTYPER | OSPEEDR | PUPDR |
|--------|-----|-------|--------|---------|-------|
| DB4 | PE7  | output | push-pull | 00 (low) | none |
| DB5 | PE8  | output | push-pull | 00 (low) | none |
| DB6 | PE9  | output | push-pull | 00 (low) | none |
| DB7 | PE10 | output | push-pull | 00 (low) | none |
| E   | PE11 | output | push-pull | 00 (low) | none |
| RS  | PE12 | output | push-pull | 00 (low) | none |

Source: [lcd.c:355-367](Core/Hardware/frontPanel/lcd.c#L355-L367). R/W is not
driven (there is no pin for it), so the busy flag cannot be read and the driver
relies on fixed waits.

### 1.2 Bus timing (TIM7 state machine)

- TIM7 ticks at 5 kHz, every 200 µs ([lcd.c:448-454](Core/Hardware/frontPanel/lcd.c#L448-L454)).
- One byte takes 5 ticks, or 1 ms:

  | Tick | Action |
  |------|--------|
  | 0 | RS and high nibble set, E↑ |
  | 1 | E↓ |
  | 2 | Low nibble set, E↑ |
  | 3 | E↓ |
  | 4 | Wait (`LCD_TICKS_NORMAL_WAIT` = 1) |

  CLEAR and HOME wait 10 ticks instead (`LCD_TICKS_CLEAR_WAIT`,
  [lcd.h:136-137](Core/Hardware/frontPanel/lcd.h#L136-L137)).
- Each changed character costs two bytes, because `sendDisplayBuffer()` sends a
  set-cursor command before every data byte. One changed character is therefore
  about 2 ms of bus activity. A full 32-character repaint is about 64–70 ms.
- With no load, this produces a burst of edges at a 1 kHz byte rate, with E
  pulsing twice per byte and TIM7 updates at 5 kHz. **The expected sound is a
  buzz at about 1 kHz with harmonics, lasting as long as the repaint.**
- **Under load the pattern changes.** TIM7 has priority 7 and is masked by
  BASEPRI 6 during every 32-frame render sub-block
  ([main.c:170-181](main.c#L170-L181)). When the CPU is busy, ticks are
  postponed into the gaps between sub-blocks. The bus then advances roughly once
  per sub-block (44,108 / 32 ≈ 1.38 kHz), with jitter, and its edges fall
  between render blocks.

### 1.3 What triggers bus traffic

| Source | Cadence | Location |
|--------|---------|----------|
| Knob service (coalesced) | at most every 20 ms while a knob moves | [menu.c:12100](Core/Menu/menu.c#L12100), [menu.c:18237](Core/Menu/menu.c#L18237) |
| Scene live refresh | every 125 ms while playing, on Scene-relevant pages, sending changed cells only | [menu.c:1957](Core/Menu/menu.c#L1957), [menu.c:4001](Core/Menu/menu.c#L4001) |
| CPU-use widget | every 500 ms when visible | [menu.c:1937](Core/Menu/menu.c#L1937) |
| Screensaver | about every 412 ms: CLEAR + cursor + 1 character + display on | [screensaver.c:42](Core/Menu/screensaver.c#L42), [screensaver.c:143-151](Core/Menu/screensaver.c#L143-L151) |
| Button presses and page changes | on event | various |

With the sequencer stopped and no hands on the panel, no bytes are sent unless
the CPU widget is visible.

### 1.4 Front-panel SPI1 (LED and button shift registers)

This is not part of the display, but it shares the front-panel cabling (PB3
goes to XP12 pin 10, per `XP_CONNECTOR_MAPS.md`) and it never stops.

- **Cadence:** the exchange runs at 500 Hz in the foreground
  ([timebase.c:140-178](Core/Hardware/timebase.c#L140-L178)). Each pass is a
  LATCH pulse on PB2 followed by 5 bytes at 13.5 MHz, about 3 µs of clocking
  ([dout.c:125-128](Core/Hardware/frontPanel/IO/dout.c#L125-L128)).
- **Pin speeds:** `dout_init()` and `din_init()` set the mode and alternate
  function on PB3 (SCK), PA7 (MOSI) and PA6 (MISO), but **never write
  OSPEEDR** ([dout.c:99-109](Core/Hardware/frontPanel/IO/dout.c#L99-L109)).
  The RM0410 reset values are:
  - GPIOB_OSPEEDR = `0x000000C0`, so **PB3 is very high speed** (the fastest
    setting);
  - GPIOA_OSPEEDR = `0x0C000000`, so PA7 is low speed.

  The bootloader runs first and might have changed these. Read GPIOB_OSPEEDR
  bits 7:6 on the device to confirm (§4, D6).
- **Expected sound:** steady, with a 500 Hz fundamental and its harmonics,
  slightly irregular because it runs in the foreground. It is present all the
  time, whether or not the display is changing.

### 1.5 Other fast pins (listed for completeness, not display-related)

| Pin(s) | Use | OSPEEDR | Notes |
|--------|-----|---------|-------|
| PA15, PB5, PB12, PB13, PB15, PC6, PC7, PC10 | I2S2/I2S3 incl. MCK | 11 (very high) | [AudioCodecManager.c:551-567](Core/Hardware/AudioCodecManager.c#L551-L567). MCK is about 11.3 MHz, so medium speed would be enough. Any change here touches the clocks into the DACs, so test it separately. |
| PB10 | MIDI TX | 10 (high) | [MidiDin.c:208-209](Core/MIDI/MidiDin.c#L208-L209). Runs at 31.25 kbaud, so low speed would be enough. |
| PA11, PA12 | USB FS | 11 | Leave as is. |
| PC13 | CLK OUT | 10 | PC13 is speed-limited by hardware anyway. |

---

## 2. How each source would reach the audio

| Source | When active | What it sounds like | Firmware lever |
|--------|-------------|---------------------|----------------|
| A. Display bus edges | only while a repaint drains | ~1–5 kHz buzz in bursts of 2 ms per character; bursts repeat at 8 Hz (Scene refresh) or 2 Hz (CPU widget) | §3.1, §3.2, §3.3 |
| B. Panel drive: row scan plus the internal converter enabled by `0x17` | always, while the display is on | steady; level depends on how many pixels are lit and on which rows | display off or blank only (§3.6); otherwise hardware |
| C. SPI1 LED/button exchange | always | steady 500 Hz with harmonics, slightly jittery | §3.5 |
| D. LED current switching through the 595s | when LED states change (step LEDs while playing) | ticks at the step rate | none that is cheap |

### What slower edges can and cannot do

Each data-line edge moves a charge of about C × V into or out of the line and
the display input. Assuming 10–30 pF per line, that is about 30–100 pC per edge.

- **A slower edge** (a lower OSPEEDR setting, a pull-up, or a series resistor)
  delivers the same charge over a longer time. That lowers the peak current, the
  inductive spike (L × di/dt) and the RF content above about 1 MHz.
- **The audio-band content** comes from how often those charges repeat (bursts
  at a ~1 kHz byte rate), and that stays the same.

So slower edges help when the coupling path is:
- RF pickup that the op-amps or DAC stage rectify into the audio band, which
  sounds like "hash";
- ground bounce on a shared return.

They do little when the audio-rate current drawn through a shared supply
impedance is the problem.

For scale: six lines toggling at a 1 kHz byte rate draw well under 1 µA on
average. An OLED panel's multiplexed drive current is in the milliamp range.
**If the noise is still there with a static screen, source B (or C) is much
more likely than A.**

---

## 3. Candidate changes

### 3.1 One store per nibble (recommended; no risk; slightly less ISR time)

At present each nibble is written as four reset stores followed by up to four
set stores ([lcd.c:589-594](Core/Hardware/frontPanel/lcd.c#L589-L594),
[lcd.c:609-614](Core/Hardware/frontPanel/lcd.c#L609-L614)). A data line that
is 1 in two nibbles in a row is pulled low by the reset store and returned high
a few bus cycles later. That is a short glitch pulse which draws current both
ways for no purpose. RS is also written separately.

DB4–DB7 are the contiguous pins PE7–PE10, so a nibble maps directly onto one
BSRR store:

```c
#define LCD_BUS_MASK ((1UL<<PIN_DB4)|(1UL<<PIN_DB5)|(1UL<<PIN_DB6)| \
                      (1UL<<PIN_DB7)|(1UL<<PIN_RS))

/* One BSRR store: only lines whose level changes make an edge. */
static inline void lcd_putNibble(uint8_t rs, uint8_t nibble)
{
    uint32_t set = ((uint32_t)(nibble & 0x0Fu) << PIN_DB4) |
                   ((uint32_t)(rs ? 1u : 0u) << PIN_RS);
    GPIOE_BSRR = set | ((LCD_BUS_MASK & ~set) << 16);
}
```

In `lcd_tim7_tick()`:
- `SM_IDLE` becomes `lcd_putNibble(cur_rs, cur_data >> 4); PE_HI(PIN_E);`
- `SM_E_LO_HIGH` becomes `lcd_putNibble(cur_rs, cur_data & 0x0F); PE_HI(PIN_E);`

`lcd_write_nibble_blocking()` can use the same helper, keeping RS at its current
level.

- **Edge count:** only lines whose level actually changes now switch. The
  glitch pulses are gone.
- **Timing:** unchanged. E is still a separate store after the data store, so
  the data are set up before E rises at least as well as they are today.
- **CPU:** per tick, about eight stores become one, so the TIM7 body gets
  shorter. There is no DSP cost.
- **Test:** display content is identical on every page, including the Load/Save
  name editor, screensaver entry and exit, and underline/CGRAM characters.

### 3.2 Open-drain with internal pull-up on DB4–DB7 (trial; softens rising edges)

This is the only firmware way to get the "softer edge" you asked about. Rising
edges are then set by the MCU pull-up (R_PU, typically 30–50 kΩ per the
STM32F765 datasheet) charging the line capacitance:

τ ≈ 40 kΩ × 10–50 pF ≈ 0.4–2 µs, compared with roughly tens of nanoseconds now.

Falling edges are still driven by the low-speed output stage.

```c
/* in lcd_gpio_init(), after the existing clears */
GPIOE_OTYPER |= (1UL<<7)|(1UL<<8)|(1UL<<9)|(1UL<<10);      /* DB4-DB7 open-drain */
GPIOE_PUPDR  |= (1UL<<14)|(1UL<<16)|(1UL<<18)|(1UL<<20);   /* DB4-DB7 pull-up    */
```

Why the timing still holds:
- An HD44780-compatible write latches data on the **falling** edge of E.
- In the TIM7 path, data change 200 µs before E falls, which is about 100 × τ.
- E and RS stay push-pull: E needs a clean edge, and RS has an address set-up
  time measured from the E rising edge.

Preconditions:
1. **Do §3.1 first.** Otherwise the glitch pulses become slow, partial
   discharges, which is harmless but muddies the comparison.
2. **The blocking init path must be lengthened.** `lcd_init()` pulses E for
   `LCD_ENABLE_US` = 1, using an approximate spin delay
   ([lcd.c:113-120](Core/Hardware/frontPanel/lcd.c#L113-L120),
   [lcd.c:370-376](Core/Hardware/frontPanel/lcd.c#L370-L376)). With a 2 µs
   rise time, a data line might not be high by the time E falls. Raise it to
   about 20 µs, add a matching settle after the data store, or set open-drain
   only at the end of `lcd_init()`.
3. **Needs the schematic or datasheet (not in the tree):**
   - What supply voltage the display module runs from.
   - Whether its DB inputs have internal pull-ups (HD44780-style controllers
     do). If they pull up to 5 V, an open-drain line will float to 5 V, and
     PE7–PE10 must be FT (5 V-tolerant) in the STM32F765 pin table.
   - With the current push-pull outputs and a 5 V pull-up in the module, a
     small current already flows. That is worth knowing either way.
4. **Static current:** a low line draws about 3.3 V / 40 kΩ ≈ 80 µA through the
   pull-up. This is negligible, but it is a small DC step whenever the data
   change.

**Expected effect:**
- About half of the data edges (the rising ones) lose their high-frequency
  content.
- The effect is worthwhile only if the diagnostics in §4 show the bus bursts are
  audible (source A) and that they sound like "hash" rather than a clean buzz.
- Softening the falling edges, and E, needs hardware (§3.7).

### 3.3 TIM7 tick rate

| Tick | E high | Normal wait (≥ 37 µs) | Ticks per byte | Byte rate | 32-char repaint | Clear wait |
|------|--------|-----------------------|----------------|-----------|-----------------|------------|
| 5 kHz (now) | 200 µs | 1 tick = 200 µs | 5 | 1 kHz | ≈ 64 ms | 10 ticks |
| 10 kHz | 100 µs | 1 = 100 µs | 5 | 2 kHz | ≈ 32 ms | 20 |
| 25 kHz | 40 µs | 2 = 80 µs | 6 | ≈ 4.2 kHz | ≈ 15 ms | 50 |
| 100 kHz | 10 µs | 4 = 40 µs | 8 | 12.5 kHz | ≈ 5 ms | 200 (fits `uint8_t`) |

The 37 µs and 1.52 ms minimums are the ones written in the
[lcd.h:136-137](Core/Hardware/frontPanel/lcd.h#L136-L137) comments.

- **The buzz cannot be pushed above the audio band.** The fixed post-byte wait
  alone (≥ 37 µs, with no busy-flag read) holds the byte rate below about
  20 kHz. A faster tick only moves the buzz upward, where hearing is less
  sensitive and the output stage may filter more, and shortens each burst.
- **The amount of switching does not change.** The number of edges per byte is
  the same. ISR entries per byte grow from 5 to 6–8.
- **Under playing load the change may not show at all.** TIM7 is masked during
  each render sub-block (§1.2), so ticks collapse into the gaps between
  sub-blocks whatever ARR is set to. Measure while stopped and while playing.
- **CPU:** a few more short ISRs per repaint, all outside the masked render
  sub-blocks. There is no per-sample DSP cost. If adopted, check the CPU widget
  on the worst-case Scene with continuous knob repaint.
- **Lowering the rate is not recommended.** The buzz moves to a lower, more
  audible frequency, and a full repaint stretches to hundreds of milliseconds:
  the same lag that was removed from the Save name editor in S082.

**Assessment:** this is worth an A/B listen at 25 kHz only if the bus bursts are
confirmed audible.

### 3.4 Fewer bytes per repaint (not proposed)

Leaving out the set-cursor command when cells are written in sequence would
halve the bytes sent per character. That optimisation was removed deliberately
in Session 6, because it loses sync if the display drops a command (see
`AVR_TO_F765_MIGRATION.md`, resolved pitfall). It is not proposed here.

### 3.5 PB3 (and PA7) speed on SPI1 (recommended if D1/D6 point at it; no CPU cost)

Write OSPEEDR explicitly in `dout_init()`, so the setting no longer depends on
the reset value or on whatever the bootloader leaves behind:

```c
/* PB3 = SCK, PA7 = MOSI: medium speed (01). 13.5 MHz needs more than low speed. */
OSPEEDR(GPIOB_BASE) = (OSPEEDR(GPIOB_BASE) & ~(3UL << 6))  | (1UL << 6);
OSPEEDR(GPIOA_BASE) = (OSPEEDR(GPIOA_BASE) & ~(3UL << 14)) | (1UL << 14);
```

`dout.c` currently has macros for MODER, OTYPER, PUPDR and AFRL only, so it
needs an `OSPEEDR(b)` macro at offset `0x08`.

- **Why medium, not low:** low speed is rated for only a few MHz into a cable
  load (see the STM32F765 datasheet table "I/O AC characteristics"), and SCK
  runs at 13.5 MHz.
- **The alternative is costly:** low speed with a slower SPI clock (BR /32,
  3.4 MHz) would make each pass about 12 µs of foreground busy-waiting, 500 times
  a second. That costs about 0.5 % of main-loop time, which matters because
  render time is at the ceiling while playing.
- **Test:** every button, every LED, and SW43, while the sequencer is playing.

### 3.6 Panel drive (source B)

The firmware can change only what is lit and whether the panel is on:

- **Display off** (`lcd_turnOff()`) stops the row drive. The screensaver already
  does this for one phase in four. **Note:** with the screensaver off,
  `screensaver_check()` does nothing, so the display is never switched off.
- **Internal converter off** (command `0x13`, the `0x17` power bit cleared,
  according to the WS0010 instruction set) stops the internal converter. The
  screen goes dark unless the module has an external OLED supply. **Use this
  only as a diagnostic (D5b)**, and confirm the bit against the WS0010
  datasheet, which is not in the tree.
- **Brightness or scan rate:** the driver has no command for either. As far as
  I know the WS0010 has none in character mode, but check the datasheet.

If B is the source, the fix is hardware (§3.7).

### 3.7 Hardware options (for reference)

- **Series resistors** of 220 Ω–1 kΩ on DB4–DB7, RS and E at the MCU end. They
  form an RC with the cable and input capacitance and slow both edges. This is
  the standard edge-rate fix.
- **Filter the display module's supply:** a ferrite or a 10 Ω series resistor,
  with 47–100 µF plus 100 nF at the module.
- **Return currents:** check that the display's ground return does not share
  copper with the audio ground (DAC or op-amp reference) before the star point.
- **A ground line next to E** in the ribbon, if a spare conductor is available.

---

## 4. Diagnostics (do these first)

**Method:**
1. Record OUT1 into a computer at a fixed, normal gain, with every voice muted
   or the Scene silent.
2. Keep the master volume where you normally hear the noise.
3. Look at a spectrogram (Audacity's view is enough) as well as listening.

| # | Setup | Bus A | Panel B | SPI1 C | What it tells you |
|---|-------|-------|---------|--------|-------------------|
| D1 | Sequencer stopped, page without the CPU widget, hands off | idle | on | on | Noise here is **not** from the display comms. A 500 Hz comb points to C; a steady tone or hash points to B. |
| D2 | Same as D1, comparing a page full of characters with a sparse page | idle | content varies | on | A level change that follows the amount of lit text points to B. |
| D3 | Screensaver on, wait 2 minutes | ~4 bytes + CLEAR per 412 ms | mostly dark, or 1 character | on | **Done (user): quietest state.** A and B both drop, so this is consistent with either; D1 separates them (§0.1). |
| D4 | Stopped; turn one endless pot steadily on a VOICE page | bursts every 20 ms | on | on | A 1–5 kHz buzz that starts and stops with the knob points to A. |
| D5 | Temporary DEV build hooks, one at a time: **(a)** hold the display off; **(b)** send `0x13`; **(c)** write the *same* character to the *same* cell in a loop, so the bus is busy but the screen does not change; **(d)** skip `din_dout_exchange()` (buttons stop working) | varies | varies | (d) off | Each hook isolates one source: (c) gives bus edges without any panel change, (d) removes SPI1. |
| D6 | DEV build: show GPIOB_OSPEEDR on the display with `lcd_diagDisplayInt()` | — | — | — | Confirms whether PB3 is really at `11` after the bootloader. |
| D7 | Scope on DB4 and on PB3, at the display connector | — | — | — | Measured rise and fall times, before and after §3.1, §3.2 and §3.5. |

D5 hooks are throwaway code and are not meant to stay in the tree.

---

## 5. Suggested order

1. **D1–D4.** No code needed. These decide whether A, B or C is the source you
   are hearing.
2. **If C (500 Hz comb):** §3.5, confirmed with D6 and D7.
3. **If A (buzz only during repaints):**
   - §3.1 first;
   - then §3.2, with the precondition checks;
   - then try §3.3 at 25 kHz as an A/B listen.
4. **If B (steady, follows lit content):** the firmware cannot fix this. Go
   straight to the supply filter and return path in §3.7, and use D5a/D5b to
   confirm it before changing hardware.
5. **In any case:** §3.1 and §3.5 can go in on their own. Both are harmless and
   remove switching that does nothing useful.

---

## 6. Statements this makes out of date (for you to act on)

These were found while reading the code. They are not edited here.

| Location | Statement | Actual |
|----------|-----------|--------|
| `knowledge_files/hardware_archive/HARDWARE_MAP.md`, LCD section | "Non-blocking TIM7-driven async queue (128 entries, 10kHz drain rate)" | TIM7 runs at 5 kHz (ARR 199 on a 1 MHz tick) |
| `knowledge_files/hardware_archive/HARDWARE_MAP.md`, SPI1 section | "SPI exchange happens in TIM6_DAC_IRQHandler at 1kHz" | TIM6 sets a flag every 2 ms; the exchange runs in foreground `timebase_serviceFrontPanel()` at 500 Hz |
| [lcd.c:496](Core/Hardware/frontPanel/lcd.c#L496) comment | "1 tick normal, 20 ticks for CLEAR/HOME" | `LCD_TICKS_CLEAR_WAIT` is 10 |
| [lcd.h:89](Core/Hardware/frontPanel/lcd.h#L89) comment | "64 ops" | `LCD_QUEUE_SIZE` is 128 |
| [lcd.c:247-248](Core/Hardware/frontPanel/lcd.c#L247-L248) comment | "at LCD_QUEUE_SIZE=64" | 128 |

If §3.2, §3.3 or §3.5 are adopted, the pin table in §1.1 and the HARDWARE_MAP
LCD and SPI1 rows will also need updating.

---

## 7. Open checks (need the device, the schematic or the datasheet)

- Display module supply voltage, and whether the DB inputs have internal
  pull-ups (§3.2).
- PE7–PE10 FT status (STM32F765 datasheet pin table), needed only if the module
  pulls up to 5 V.
- The `0x13` and `0x17` power bit, and the absence of brightness or scan-rate
  commands (WS0010 datasheet).
- PB3 OSPEEDR after the bootloader (D6).
- Measured edge times (D7), and by ear and spectrogram, which source is
  audible (D1–D5).
