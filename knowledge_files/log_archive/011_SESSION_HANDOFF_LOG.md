# Session 011 Handoff Log — LXR-02 Firmware Port

## How to start the next session

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Session goal**: Implement non-blocking SD card subsystem using asyncfatfs library (first session: sdcard shim + asyncfatfs integration + presetManager rewrite)
**Last session summary**: See END OF SESSION block below.
**Current tarball**: `lxr02-037_port.tar.gz` — no code was written this session, tarball is unchanged from Session 9.
**Constraints**: Do not write any SD or FatFS code that blocks the main loop. DSP render must never be starved. 1ms blocking is not acceptable under any circumstances.

Key files to be aware of:
- Original LXR source is at `/tmp/LXR-master/` on the server (AVR: `front/LxrAvr/`, STM32F4: `mainboard/LxrStm32/src/`)
- Current port lives in the working tarball, extracted to `/lxr02/`
- Knowledge files: HARDWARE_MAP.md, AVR_TO_F765_MIGRATION.md, ENHANCED_FEATURES.md
- **NEW**: asyncfatfs library source in uploaded `asyncfatfs-master.zip` — `lib/asyncfatfs.c`, `lib/asyncfatfs.h`, `lib/fat_standard.c`, `lib/fat_standard.h`, `lib/sdcard.h`
- **NEW**: NB_FatFS reference in uploaded `NB_FatFS-main.zip` — examined but NOT adopted, see analysis below
- Original LXR source in uploaded `LXR-master_2_.zip`

---

## End of session block

```
DATE: 2026-05-05
SESSION GOAL: Design and plan non-blocking SD card subsystem. Evaluate
              reference libraries (asyncfatfs, NB_FatFS). Determine
              whether to fork FatFS or adopt an alternative.
COMPLETED:
  - Full analysis of how original LXR handles SD/DSP timing (two-MCU
    architecture: AVR owns SD, trickle-feeds params over UART)
  - Full trace of FatFS ff.c blocking call graph (28 disk_read/disk_write
    sites, ~20 functions needing async conversion)
  - Evaluation of NB_FatFS (C++, callback chains, heap alloc — rejected)
  - Evaluation of asyncfatfs (C, polling FSM, sector cache — adopted)
  - Complete implementation plan with pseudocode for every changed file
  - Design decisions locked: main-loop polling first, ISR migration later
VERIFIED ON HARDWARE: No — no code changes this session.

CHANGES THIS SESSION:
- No files changed. Tarball remains lxr02-037_port.tar.gz from Session 9.

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED: None (planning session only).

NEXT SESSION RECOMMENDED GOAL: Implement asyncfatfs integration.
  Step 1: sdcard_lxr02.c shim (sdcard_readBlock/writeBlock/poll on
          top of existing spi_sd.c bit-bang)
  Step 2: Drop asyncfatfs.c/h, fat_standard.c/h into Core/Hardware/SD/
          with modifications (#undef AFATFS_USE_FREEFILE, strip
          introspective logging, adjust AFATFS_NUM_CACHE_SECTORS and
          AFATFS_MAX_OPEN_FILES)
  Step 3: Rewrite presetManager.c against asyncfatfs API
  Step 4: Rewrite kitBrowser.c against asyncfatfs API
  Step 5: sd_fsm.c — thin state machine driving asyncfatfs operations
  Step 6: Wire afatfs_poll() into main loop
  Step 7: Boot path — synchronous polling until filesystem ready
  Step 8: Remove old ff.c, ff.h, ffconf.h, diskio.c, sd_routines.c
          from build
  Step 9: Makefile update
  Step 10: Build and verify

BLOCKERS:
  - asyncfatfs source must be uploaded alongside the tarball at session start
  - Confirm SDHC block addressing vs byte addressing in sdcard shim
    (current sd_routines.c uses block addresses for SDHC — asyncfatfs
    blockIndex is also block-addressed, should be compatible)
  - kitBrowser_init() does 128× f_open to scan for kits — this must
    become an async scan driven by sd_fsm.c, which means the kit browser
    cannot be fully populated at boot. Need to decide: (a) scan
    asynchronously after boot while audio runs, or (b) scan synchronously
    before audio starts (blocking, but only at boot).

CRITICAL REMINDERS FOR NEXT SESSION:
- 1ms blocking anywhere in the main loop or any ISR at priority <= 4
  is completely unacceptable. There are no exceptions.
- SD bit-bang SPI (PC12/PD2/PC8/PD0) is NOT re-entrant. Must only
  be called from one context at a time.
- asyncfatfs afatfs_poll() must not be called from both main loop AND
  an ISR. One context only. Start with main loop.
- Boot-time preset load (before audio starts) CAN block — audio ISR
  is not running. Use synchronous polling loop for boot only.
- EXTI_IMR = 0 must remain first in main().
- GetRngValue() calls must mask: & 0x7FFF.
- Do NOT enable internal DAC on PA4/PA5.
```

---

## Session 11 — Full Design Notes

### How the Original LXR Solved SD/DSP Timing

The original LXR is a two-MCU design. The STM32F4 (mainboard) runs DSP only — its main loop is: `uart_processMidi()`, `uart_processFront()`, `usb_tick()`, `calcNextSampleBlock()`, `seq_tick()`, `trigger_tick()`. No SD card access for presets.

The ATmega644 AVR (front panel) owns the SD card, LCD, buttons, encoders, pots, and all FatFS operations. When it loads a kit, it reads bytes from the .SND file and sends each parameter to the STM32 via UART using `frontPanel_sendData()` — one parameter at a time, interleaved with `din_readNextInput()` and `dout_updateOutputs()` to keep the UI responsive during the morph loop.

The STM32 receives parameter updates in `uart_processFront()` between audio render cycles. Parameters change while audio is playing — some voices have new values while others still have old ones mid-load. This is intentional and works fine.

The STM32 mainboard DOES have SD access — but only for sample upload (`SD_Manager.c` / `SampleMemory.c`). This reads .WAV files from SD and burns them into flash. It calls `seq_setRunning(0)` first — the sequencer stops, audio output is not expected to continue during sample upload. The SD SPI pins are on the same SPI1 bus as the shift registers; `spi_init()` reconfigures SPI1 for SD, then `spi_deInit()` releases it back afterward.

**Key insight**: the original never ran SD and audio concurrently. Preset load/save is on the AVR; sample upload halts audio. The LXR-02 port must do something the original never had to do — concurrent SD and audio on one MCU.

The asyncfatfs polling model is the functional equivalent of the AVR: it reads the SD card incrementally and writes results into shared memory that the main loop consumes passively.

### FatFS Blocking Analysis

Before evaluating alternatives, we traced every blocking point in our current FatFS (R0.06502, `_FS_TINY=1, _FS_READONLY=0`).

**Call graph from callers to disk I/O:**

```
presetManager.c / kitBrowser.c
  → f_open / f_read / f_write / f_close / f_mount
    → ff.c internal functions
      → disk_read / disk_write (diskio.c)
        → SD_readSingleBlockCustomBuffer / SD_writeSingleBlockCustomBuffer (sd_routines.c)
          → SPI_transmit / SPI_receive loops with busy-waits (spi_sd.c)
```

**Disk I/O sites in ff.c (our config, active code paths only):**

| Function | disk_read | disk_write | Via move_window | Via get_fat/put_fat |
|----------|-----------|------------|-----------------|---------------------|
| move_window | 1 (load sector) | 2 (dirty writeback + FAT mirror) | — | — |
| check_fs | 1 (boot record) | — | — | — |
| chk_mounted | 1 (FSInfo) | — | calls check_fs | — |
| sync | — | 1 (FSInfo) | calls move_window | — |
| get_fat | — | — | 1 call (FAT16/32) or 2 (FAT12) | — |
| put_fat | — | — | 1 call (FAT16/32) or 2 (FAT12) | — |
| create_chain | — | — | — | calls get_fat + put_fat |
| remove_chain | — | — | — | calls get_fat + put_fat |
| dir_sdi | — | — | — | calls get_fat |
| dir_next | — | — | calls move_window | calls get_fat |
| dir_find | — | — | calls move_window | via dir_next |
| dir_register | — | — | calls move_window | via dir_find + dir_next |
| dir_read | — | — | calls move_window | via dir_next |
| follow_path | — | — | — | via dir_find |
| f_open | — | — | calls move_window | via follow_path, dir_register, remove_chain |
| f_read | 1 (direct multi-sector) | — | calls move_window | via get_fat |
| f_write | — | 1 (direct multi-sector) | calls move_window | via create_chain |
| f_sync/f_close | — | 1 (dir entry) | calls move_window | — |

With `_FS_TINY=1`, f_read and f_write use the shared `fs->win[]` buffer via `move_window()` instead of per-file `fp->buf`. This simplifies the blocking surface but means `move_window` is the central chokepoint — almost all disk I/O flows through it.

**To make FatFS non-blocking**, every function in this table would need conversion to a resumable state machine with yield points at every disk I/O call. That's ~20 functions, ~28 yield points, with up to 6 levels of call nesting (f_open → follow_path → dir_find → dir_next → get_fat → move_window). Each level needs a context struct to save local variables across yields.

### Reference Library Evaluation

#### NB_FatFS (C++, STM32H7)

**Source**: `NB_FatFS-main.zip` — 9521 lines in `nb_ff.cpp`
**Based on**: FatFS R0.12c (newer than our R0.06502)
**Pattern**: Callback chains with heap-allocated continuation structs

Each FatFS function is decomposed into named sub-functions (`f_read_a`, `f_read_b`, `f_read_c`, `f_read_d`, `f_read_loop`, `f_read_loop_cont`). Each sub-function represents the code between two disk I/O calls. When disk_read/write is needed, it initiates the operation with a lambda callback that on completion calls the next sub-function. Local variables are carried in a heap-allocated struct (`f_read_strut`, `move_window_strut`, etc.) passed through the chain.

Example — `move_window` in NB_FatFS:
```
move_window(fs, sector, callback, data)
  → allocates move_window_strut on heap (new)
  → calls sync_window(fs, lambda)
    → lambda: calls disk_read(fs->drv, fs->win, sector, 1, lambda)
      → lambda: updates fs->winSector, deletes strut, calls callNextCallback()
```

A global callback stack (`callbackFunctions[10]`, `callbackCounter`) manages the return chain. `pollingModeCall()` pops and executes the next callback. `voidPtr` is a single global used to pass context through disk_read/write callbacks.

**Rejected because:**
- C++ lambdas and `new`/`delete` — we're bare-metal C, no heap
- Global `voidPtr` for context passing — fragile, single-operation
- `_FS_TINY` is explicitly `#error` — not supported
- FAT12 and FAT16 not supported
- Based on FatFS R0.12c — different structure names and internals from our R0.06502
- 9500 lines, heavy
- Designed for hardware DMA with interrupt-driven completion callbacks from SD driver — our bit-bang SPI has no hardware completion event

#### asyncfatfs (Betaflight/Cleanflight)

**Source**: `asyncfatfs-master.zip` — 3661 lines in `asyncfatfs.c`, 108 lines in `fat_standard.c`
**Pattern**: Sector cache + polling state machines
**NOT a FatFS fork** — ground-up FAT16/FAT32 reimplementation

**Architecture:**

```
Caller (preset manager, kit browser)
  → asyncfatfs API (afatfs_fopen, afatfs_fread, afatfs_fwrite, afatfs_fclose)
    → sector cache (afatfs_cacheSector)
      → sdcard driver shim (sdcard_readBlock, sdcard_writeBlock, sdcard_poll)
        → bit-bang SPI (spi_sd.c, unchanged)
```

The key abstraction is an **8-sector LRU cache** between the filesystem logic and the SD card. Every operation that needs a sector calls `afatfs_cacheSector(physicalSectorIndex, &buffer, flags, eraseCount)`, which returns:
- `AFATFS_OPERATION_SUCCESS` — sector is in cache, `*buffer` points to it
- `AFATFS_OPERATION_IN_PROGRESS` — read has been queued, call again later

The caller just returns and gets called again on the next `afatfs_poll()` tick. When the cache has the data, the operation proceeds. FAT table lookups that hit the same sector on repeated calls are free (cache hit). Dirty sectors are flushed to disk in the background during `afatfs_poll()`.

Each open file has an `operation` field with the current pending operation's state (phase enum + operation-specific state struct). `afatfs_poll()` calls `afatfs_fileOperationContinue()` on each open file, which switches on operation type → calls `*Continue()` function → switches on phase.

Example — file open (createFile) state machine:
```
PHASE_INITIAL → findFirst(currentDirectory)
PHASE_FIND_FILE → findNext() loop
  → IN_PROGRESS: return (cache miss on directory sector)
  → SUCCESS + match: PHASE_SUCCESS
  → SUCCESS + terminator: PHASE_CREATE_NEW_FILE (if create mode)
  → FAILURE: PHASE_FAILURE
PHASE_CREATE_NEW_FILE → allocateDirectoryEntry()
  → IN_PROGRESS: return
  → SUCCESS: PHASE_SUCCESS
PHASE_SUCCESS → callback(file)
PHASE_FAILURE → callback(NULL)
```

`afatfs_fread()` returns a byte count (how many bytes could be read right now). If the needed sector isn't cached, it returns 0 and the caller retries. Same for `afatfs_fwrite()`. This makes the API inherently retry-based.

**asyncfatfs directory structure (from `asyncfatfs-master.zip`):**

```
asyncfatfs-master/
├── Readme.md              ← Usage guide, explains polling model
├── LICENSE                ← MIT license
├── Makefile               ← Test build (Linux, not for us)
├── lib/
│   ├── asyncfatfs.c       ← Core: sector cache, file operations, polling FSM (3661 lines)
│   ├── asyncfatfs.h       ← Public API: afatfs_fopen/fread/fwrite/fclose/poll/init (75 lines)
│   ├── fat_standard.c     ← FAT structure parsing helpers (108 lines)
│   ├── fat_standard.h     ← FAT types: directory entry, volume ID, MBR, etc. (124 lines)
│   └── sdcard.h           ← Interface we must implement: readBlock/writeBlock/poll (105 lines)
├── tests/                 ← Unit tests against simulated SD card (not for us)
│   ├── common.c/h
│   ├── sdcard_sim.c/h     ← SD card simulator for testing
│   ├── test_file_delete.c
│   ├── test_file_modes.c
│   ├── test_file_size.c
│   ├── test_file_size_powerloss.c
│   ├── test_logging_workload.c
│   ├── test_root_fill.c
│   ├── test_subdir_fill.c
│   └── test_volume_fill.c
├── tools/
│   └── profile_decode.c   ← Log decoder for introspective profiling (not for us)
└── images/                ← Test disk images (not for us)
```

**File-by-file notes for implementation session:**

`asyncfatfs.c` (3661 lines) — the bulk of the library:
- Lines 1-200: types, enums, state machine phase definitions, cache and file structs
- Lines 200-550: internal constants, super-cluster math, cluster-to-sector conversions
- Lines 550-700: sector cache memory management and initialization
- Lines 700-960: `afatfs_cacheSector()` — the core non-blocking read/write mechanism
- Lines 960-1100: MBR and volume ID parsing
- Lines 1089-1180: `afatfs_FATGetNextCluster()` and `afatfs_FATSetNextCluster()` — FAT chain read/write via cache
- Lines 1180-1400: cluster search, FAT pattern writing, directory entry save
- Lines 1400-1650: freefile operations (strip with `#undef AFATFS_USE_FREEFILE`)
- Lines 1650-1950: file sector access (retain/lock cursor sector for read/write)
- Lines 1950-2200: seek implementation (FSM with phases)
- Lines 2200-2540: directory scanning (findFirst/findNext/allocateDirectoryEntry)
- Lines 2540-2710: `afatfs_createFileContinue()` — file open state machine
- Lines 2710-2870: `afatfs_fcloseContinue()` — file close state machine
- Lines 2870-2960: file handle init, funlink
- Lines 2960-3040: `afatfs_fopen()` — public API, starts createFile operation
- Lines 3040-3170: `afatfs_fwrite()`, `afatfs_fread()` — return partial byte counts
- Lines 3170-3230: `afatfs_fileOperationContinue()`, `afatfs_fileOperationsPoll()`
- Lines 3230-3500: filesystem init state machine (read MBR → read VBR → init freefile)
- Lines 3500-3530: `afatfs_poll()` — the single entry point for all background work
- Lines 3530-3660: destroy, getFreeBufferSpace, init

`asyncfatfs.h` (75 lines) — public API:
- `afatfs_init()`, `afatfs_poll()`, `afatfs_destroy()`
- `afatfs_fopen(filename, mode, callback)` — callback fires when file is ready (or NULL on failure)
- `afatfs_fread(file, buffer, len)` → returns bytes read (0 if cache miss, retry later)
- `afatfs_fwrite(file, buffer, len)` → returns bytes written (0 if cache miss, retry later)
- `afatfs_fclose(file, callback)`
- `afatfs_fseek(file, offset, whence)`
- `afatfs_findFirst/findNext/findLast` — directory scanning
- `afatfs_getFilesystemState()` — UNKNOWN / INITIALIZATION / READY / FATAL
- `afatfs_flush()`, `afatfs_isFull()`

`fat_standard.c/h` (108+124 lines) — FAT structure definitions and helpers:
- `fatDirectoryEntry_t` — 32-byte directory entry struct
- `fatVolumeID_t` — volume header parsing
- `mbrPartitionEntry_t` — MBR partition table
- `fat_convertFilenameToFATStyle()` / `fat_convertFATStyleToFilename()` — 8.3 name conversion
- `fat32_decodeClusterNumber()`, `fat16_isEndOfChainMarker()`, etc.

`sdcard.h` (105 lines) — interface we must implement:
- `sdcard_readBlock(blockIndex, buffer, callback, callbackData)` → bool (queued or busy)
- `sdcard_writeBlock(blockIndex, buffer, callback, callbackData)` → status enum
- `sdcard_poll()` → bool (card ready for new commands)
- `sdcard_beginWriteBlocks()` / `sdcard_endWriteBlocks()` — multi-block write (optional, tied to `AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT`)

**Adopted because:**
- Pure C, no heap allocation (fixed pool of file handles and cache entries)
- FAT16 and FAT32 supported (no FAT12 — fine, SDHC won't be FAT12)
- Polling model matches our needs exactly
- Battle-tested in Betaflight/Cleanflight blackbox logging on STM32F4/F7
- 3700 lines total — small, auditable
- Sector cache naturally absorbs repeated FAT lookups
- No dependency on specific SPI/DMA hardware — we provide the sdcard shim
- The polling API makes ISR migration trivial later (move `afatfs_poll()` call from main loop to timer ISR)

### ISR vs Main-Loop Polling — Design Decision

asyncfatfs does not use an ISR internally. `afatfs_poll()` is the single entry point, called from whatever context the application chooses.

**Main-loop polling (adopted for initial implementation):**

The main loop currently alternates between DSP render (when a queue slot is free) and idle (when queue is full, waiting for DMA to consume a slot). During idle time, `afatfs_poll()` runs. This is cooperative multitasking — DSP gets priority because the queue check runs first.

```c
while (1) {
    if (audioCodec_queueFreeSlots() > 0) {
        mixer_calcNextSampleBlock(...);
        audioCodec_commitRenderBuffer();
    }
    sd_fsm_tick();     // calls afatfs_poll() internally
    // ... button/menu/encoder processing
}
```

Advantages: simpler, no ISR priority concerns, no preemption issues with bit-bang SPI, no new timer. `afatfs_poll()` naturally yields to DSP.

Disadvantage: SD throughput is coupled to main loop slack time. If DSP render consistently takes close to 2.18ms, `afatfs_poll()` gets few calls and kit loads take longer. Worst case (DSP always takes exactly 2.18ms): `afatfs_poll()` never runs, kit load hangs.

**ISR migration path (future, when needed):**

Move `afatfs_poll()` into a TIM5 ISR at 10kHz, priority 6. Everything at priority ≤ 5 (audio, LCD, encoders, USB) preempts it. The bit-bang SPI inside `sdcard_poll()` gets interrupted mid-byte by audio DMA — SD cards tolerate arbitrary clock stretching on SPI, so this is safe.

To migrate: (1) init TIM5, (2) move `sd_fsm_tick()` call from main loop to `TIM5_IRQHandler`, (3) remove main-loop call. No code changes to asyncfatfs or the sdcard shim.

**Burst size within sdcard_poll():**

When `sdcard_poll()` is called (from either context), it clocks a burst of SPI bytes for the current pending read or write. The burst size is a tunable constant. At 16 bytes/call, each `afatfs_poll()` invocation takes ~9µs of SPI bit-bang time. A 512-byte sector read completes in 32 calls (3.2ms at 10kHz, or faster if called more frequently from the main loop).

Options documented for future tuning:
- (A) 10kHz ISR, 1 byte/tick — Session 10's original sketch. ~29ms per kit. Very low overhead but slow.
- (B) 10kHz ISR or main loop, 16 bytes/burst — adopted. ~2ms per sector. Good balance.
- (C) 40kHz ISR, 1 byte/tick — same throughput as B but more ISR entries. Not recommended.
- (D) Main loop, 512 bytes/burst (full sector per poll) — fastest but ~280µs per call, cuts into render slack. Viable if DSP is light.

### Implementation Plan — File by File

#### New Files

##### `Core/Hardware/SD/sdcard_lxr02.c` — SD card driver shim

Implements the `sdcard.h` interface on top of existing `spi_sd.c` bit-bang primitives. This is the adapter between asyncfatfs and our hardware.

```
State machine:
  SDCARD_STATE_IDLE
  SDCARD_STATE_READING_SEND_CMD
  SDCARD_STATE_READING_WAIT_TOKEN
  SDCARD_STATE_READING_DATA
  SDCARD_STATE_READING_CRC
  SDCARD_STATE_WRITING_SEND_CMD
  SDCARD_STATE_WRITING_TOKEN
  SDCARD_STATE_WRITING_DATA
  SDCARD_STATE_WRITING_CRC
  SDCARD_STATE_WRITING_WAIT_BUSY

Static state:
  sdcard_state_t state = IDLE
  uint8_t *xfer_buffer
  uint32_t xfer_block
  uint16_t xfer_offset        // byte position within 512-byte transfer
  sdcard_operationCompleteCallback_c xfer_callback
  uint32_t xfer_callbackData
  sdcardBlockOperation_e xfer_operation
  uint16_t retry_count        // for token wait / busy wait

Constants:
  SDCARD_BURST_SIZE = 16      // bytes per sdcard_poll() call
  SDCARD_TOKEN_TIMEOUT = 5000 // max polls waiting for 0xFE token
  SDCARD_BUSY_TIMEOUT = 50000 // max polls waiting for card ready
```

Pseudocode for `sdcard_readBlock`:
```c
bool sdcard_readBlock(uint32_t blockIndex, uint8_t *buffer,
                      sdcard_operationCompleteCallback_c callback,
                      uint32_t callbackData)
{
    if (state != SDCARD_STATE_IDLE) return false;  // busy

    xfer_buffer = buffer;
    xfer_block = blockIndex;
    xfer_callback = callback;
    xfer_callbackData = callbackData;
    xfer_operation = SDCARD_BLOCK_OPERATION_READ;

    // Send CMD17 (READ_SINGLE_BLOCK) — this is fast, do it inline
    SD_CS_ASSERT;
    SD_sendCommand(CMD17, blockIndex);  // SDHC uses block addressing
    state = SDCARD_STATE_READING_WAIT_TOKEN;
    xfer_offset = 0;
    retry_count = 0;
    return true;
}
```

Pseudocode for `sdcard_poll`:
```c
bool sdcard_poll(void)
{
    switch (state) {
    case SDCARD_STATE_IDLE:
        return true;  // ready for new commands

    case SDCARD_STATE_READING_WAIT_TOKEN:
        // Poll for data token 0xFE, one byte per call
        {
            uint8_t r = SPI_receive();
            if (r == 0xFE) {
                state = SDCARD_STATE_READING_DATA;
                xfer_offset = 0;
            } else if (++retry_count > SDCARD_TOKEN_TIMEOUT) {
                // Timeout — abort
                SD_CS_DEASSERT;
                state = SDCARD_STATE_IDLE;
                xfer_callback(SDCARD_BLOCK_OPERATION_READ,
                              xfer_block, NULL, xfer_callbackData);
                return true;
            }
        }
        return false;

    case SDCARD_STATE_READING_DATA:
        // Clock BURST_SIZE bytes
        {
            uint16_t end = xfer_offset + SDCARD_BURST_SIZE;
            if (end > 512) end = 512;
            while (xfer_offset < end)
                xfer_buffer[xfer_offset++] = SPI_receive();
            if (xfer_offset >= 512) {
                state = SDCARD_STATE_READING_CRC;
            }
        }
        return false;

    case SDCARD_STATE_READING_CRC:
        SPI_receive();  // CRC byte 1
        SPI_receive();  // CRC byte 2
        SD_CS_DEASSERT;
        SPI_transmit(0xFF);  // extra clocks
        state = SDCARD_STATE_IDLE;
        xfer_callback(SDCARD_BLOCK_OPERATION_READ,
                      xfer_block, xfer_buffer, xfer_callbackData);
        return true;

    case SDCARD_STATE_WRITING_SEND_CMD:
        // CMD24 already sent in sdcard_writeBlock
        // Send data token
        SPI_transmit(0xFE);
        state = SDCARD_STATE_WRITING_DATA;
        xfer_offset = 0;
        return false;

    case SDCARD_STATE_WRITING_DATA:
        {
            uint16_t end = xfer_offset + SDCARD_BURST_SIZE;
            if (end > 512) end = 512;
            while (xfer_offset < end)
                SPI_transmit(xfer_buffer[xfer_offset++]);
            if (xfer_offset >= 512) {
                state = SDCARD_STATE_WRITING_CRC;
            }
        }
        return false;

    case SDCARD_STATE_WRITING_CRC:
        SPI_transmit(0xFF);  // dummy CRC
        SPI_transmit(0xFF);
        // Read data response token
        {
            uint8_t resp = SPI_receive();
            if ((resp & 0x1F) != 0x05) {
                // Write rejected
                SD_CS_DEASSERT;
                state = SDCARD_STATE_IDLE;
                xfer_callback(SDCARD_BLOCK_OPERATION_WRITE,
                              xfer_block, NULL, xfer_callbackData);
                return true;
            }
        }
        state = SDCARD_STATE_WRITING_WAIT_BUSY;
        retry_count = 0;
        return false;

    case SDCARD_STATE_WRITING_WAIT_BUSY:
        // Poll for card ready (non-zero response)
        {
            uint8_t r = SPI_receive();
            if (r != 0x00) {
                SD_CS_DEASSERT;
                SPI_transmit(0xFF);
                state = SDCARD_STATE_IDLE;
                xfer_callback(SDCARD_BLOCK_OPERATION_WRITE,
                              xfer_block, xfer_buffer, xfer_callbackData);
                return true;
            }
            if (++retry_count > SDCARD_BUSY_TIMEOUT) {
                SD_CS_DEASSERT;
                state = SDCARD_STATE_IDLE;
                xfer_callback(SDCARD_BLOCK_OPERATION_WRITE,
                              xfer_block, NULL, xfer_callbackData);
                return true;
            }
        }
        return false;
    }
    return true;
}
```

Note on multi-block write: `sdcard_beginWriteBlocks()` and `sdcard_endWriteBlocks()` can be stubbed to return `SDCARD_OPERATION_SUCCESS` initially. If `AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT` is undefined in asyncfatfs.c, these are never called. Recommend undefining it for initial implementation — single-block writes are simpler and sufficient for kit save (one or two sectors).

Note on SD card init: `SD_init()` from `sd_routines.c` must still be called at boot (before `afatfs_init`) to bring the card to SPI mode. This is a one-time blocking operation before audio starts — acceptable. The existing `SD_init()` function stays in the build for this purpose. `sdcard_poll()` assumes the card is already initialized and in SPI mode.

##### `Core/Hardware/SD/sd_fsm.c/h` — High-level operation state machine

Thin state machine that sequences asyncfatfs calls for each operation type. Driven by `sd_fsm_tick()` called from the main loop.

```
Operations:
  SD_OP_NONE
  SD_OP_INIT           // filesystem mount
  SD_OP_LOAD_KIT       // f_open → f_read(name) → f_read(params) → f_close
  SD_OP_SAVE_KIT       // f_open → f_write(name) → f_write(params) → f_close
  SD_OP_LOAD_GLOBALS   // f_open → f_read → f_close
  SD_OP_SAVE_GLOBALS   // f_open → f_write → f_close
  SD_OP_SCAN_KITS      // iterate f_open/f_close on P000..P127
  SD_OP_READ_KIT_NAME  // f_open → f_read(8) → f_close

Status:
  SD_STATUS_IDLE
  SD_STATUS_BUSY
  SD_STATUS_DONE
  SD_STATUS_ERROR

Static state:
  sd_op_t       current_op
  sd_status_t   status
  uint8_t       op_phase        // sub-phase within operation
  uint8_t       op_kit_nr
  afatfsFilePtr_t op_file       // set by fopen callback
  bool          op_file_ready   // set by fopen callback
  uint32_t      op_bytes_done
  sd_completion_cb_t completion_callback

  // Staging buffer for bulk write (shared, only one op at a time)
  uint8_t       staging_buf[280]  // 8 name + NUM_PARAMS data, fits in SRAM
```

Pseudocode for `sd_fsm_tick` (called every main loop iteration):
```c
void sd_fsm_tick(void)
{
    afatfs_poll();  // advance all asyncfatfs background work

    if (status != SD_STATUS_BUSY) return;

    switch (current_op) {
    case SD_OP_LOAD_KIT:
        sd_fsm_loadKit_tick();
        break;
    case SD_OP_SAVE_KIT:
        sd_fsm_saveKit_tick();
        break;
    case SD_OP_LOAD_GLOBALS:
        sd_fsm_loadGlobals_tick();
        break;
    case SD_OP_SAVE_GLOBALS:
        sd_fsm_saveGlobals_tick();
        break;
    case SD_OP_SCAN_KITS:
        sd_fsm_scanKits_tick();
        break;
    case SD_OP_READ_KIT_NAME:
        sd_fsm_readKitName_tick();
        break;
    default:
        break;
    }
}
```

Pseudocode for loadKit (most important operation):
```c
// Phases: OPEN, READ_NAME, READ_PARAMS, CLOSE, APPLY, DONE

static void sd_fsm_loadKit_tick(void)
{
    switch (op_phase) {
    case 0: // OPEN
        // Build filename "Pnnn.SND"
        // Call afatfs_fopen(filename, "r", on_kit_open_complete)
        // If returns false (file pool full), stay in phase 0, retry next tick
        // If returns true, advance to phase 1
        break;

    case 1: // WAIT_OPEN
        // fopen is async — callback sets op_file_ready
        if (!op_file_ready) return;
        if (op_file == NULL) { status = SD_STATUS_ERROR; return; }
        op_phase = 2;
        op_bytes_done = 0;
        break;

    case 2: // READ_NAME
        {
            uint32_t n = afatfs_fread(op_file, preset_currentName + op_bytes_done,
                                       8 - op_bytes_done);
            op_bytes_done += n;
            if (op_bytes_done >= 8) {
                op_phase = 3;
                op_bytes_done = 0;
            }
        }
        break;

    case 3: // READ_PARAMS
        {
            uint32_t n = afatfs_fread(op_file,
                                       parameter_values + op_bytes_done,
                                       END_OF_SOUND_PARAMETERS - op_bytes_done);
            op_bytes_done += n;
            if (op_bytes_done >= END_OF_SOUND_PARAMETERS) {
                op_phase = 4;
            }
        }
        break;

    case 4: // CLOSE
        if (afatfs_fclose(op_file, on_kit_close_complete))
            op_phase = 5;
        // If fclose returns false (busy), retry next tick
        break;

    case 5: // WAIT_CLOSE
        // callback sets a flag
        // When done:
        op_phase = 6;
        break;

    case 6: // APPLY
        // Parameters are already in parameter_values[] from phase 3
        // This is the "trickle-feed" equivalent — params were written
        // directly into the live array as they were read.
        // Call menu_repaintAll() or set a flag for main loop to do it.
        status = SD_STATUS_DONE;
        if (completion_callback) completion_callback();
        break;
    }
}
```

Key design points:
- `afatfs_fread` writes directly into `parameter_values[]` — no intermediate buffer for reads. This means parameters update incrementally as sectors are read, exactly like the original LXR's UART trickle-feed.
- For writes, a staging buffer collects the data first, then `afatfs_fwrite` sends it in bulk.
- Only one SD operation at a time. Request functions check `status == SD_STATUS_IDLE`.
- Boot path: call `sd_request_load_kit(0)` then spin `while (sd_fsm_status() == SD_STATUS_BUSY) sd_fsm_tick();`

##### Request API (in `sd_fsm.h`):
```c
void sd_fsm_init(void);   // calls afatfs_init()
void sd_fsm_tick(void);   // called from main loop every iteration

sd_status_t sd_fsm_status(void);

bool sd_request_load_kit(uint8_t kit_nr, sd_completion_cb_t cb);
bool sd_request_save_kit(uint8_t kit_nr, sd_completion_cb_t cb);
bool sd_request_load_globals(sd_completion_cb_t cb);
bool sd_request_save_globals(sd_completion_cb_t cb);
bool sd_request_scan_kits(sd_completion_cb_t cb);
bool sd_request_read_kit_name(uint8_t kit_nr, char *name_out, sd_completion_cb_t cb);
```

#### Modified Files

##### `asyncfatfs.c` — Modifications from upstream

1. **Remove `AFATFS_USE_FREEFILE`**: `#undef` or remove the `#define` and all code within `#ifdef AFATFS_USE_FREEFILE` blocks. We don't need the pre-allocated contiguous file feature.

2. **Remove introspective logging**: Remove `#ifdef AFATFS_USE_INTROSPECTIVE_LOGGING` blocks.

3. **Adjust constants**:
   - `AFATFS_NUM_CACHE_SECTORS` — keep at 8 (4KB RAM, affordable on F765 with 384KB SRAM)
   - `AFATFS_MAX_OPEN_FILES` — keep at 3 (sufficient: one for kit read/write, one for globals, one for kit browser scan)

4. **Remove `#include <stdlib.h>` and `#include <signal.h>`** — bare-metal, no standard library beyond stdint/string.

5. **Add `#include "sdcard_lxr02.h"` instead of `#include "sdcard.h"`** (or just rename our implementation header to match).

6. **Wrap `assert()` calls**: Replace `afatfs_assert()` with a no-op or a debug LED blink in release builds. In debug builds, can halt or write to LCD.

7. **Check `__attribute__((packed))` compatibility** with arm-none-eabi-gcc — should work, but verify no padding surprises in `fatDirectoryEntry_t` etc.

##### `Core/Preset/presetManager.c` — Rewrite

Remove all direct FatFS calls. Replace with `sd_request_*` calls.

Current:
```c
void preset_loadDrumset(uint8_t presetNr, uint8_t isMorph) {
    char filename[9];
    preset_makeFilename(filename, presetNr);
    f_open(&preset_File, filename, FA_OPEN_EXISTING | FA_READ);
    f_read(&preset_File, preset_currentName, 8, &bytesRead);
    f_read(&preset_File, parameter_values, END_OF_SOUND_PARAMETERS, &bytesRead);
    f_close(&preset_File);
    preset_sendDrumsetParameters();
    menu_repaintAll();
}
```

New:
```c
void preset_loadDrumset(uint8_t presetNr, uint8_t isMorph) {
    // Just post a request — sd_fsm handles the async I/O
    sd_request_load_kit(presetNr, preset_onLoadComplete);
}

static void preset_onLoadComplete(void) {
    // Called from sd_fsm when load is done
    // Parameters are already in parameter_values[]
    preset_sendDrumsetParameters();
    menu_repaintAll();
}
```

Save path — current (byte-by-byte):
```c
void preset_writeDrumsetData(void) {
    for (int i = 0; i < END_OF_SOUND_PARAMETERS; i++)
        f_write(&preset_File, &parameter_values[i], 1, &bytesWritten);
}
```

New — bulk write via sd_fsm staging buffer:
```c
// sd_fsm internally does:
//   memcpy(staging_buf, preset_currentName, 8);
//   memcpy(staging_buf + 8, parameter_values, END_OF_SOUND_PARAMETERS);
//   afatfs_fwrite(file, staging_buf, 8 + END_OF_SOUND_PARAMETERS);
// The byte-by-byte loop is eliminated entirely.
```

The staging buffer for writes is `8 + END_OF_SOUND_PARAMETERS` bytes. `END_OF_SOUND_PARAMETERS` is the number of sound parameters (from ParameterArray.h, should be around 250). Total staging buffer ~260 bytes. Fits easily in SRAM.

Globals save is similar — `parameter_values[END_OF_SOUND_PARAMETERS..NUM_PARAMS-1]` written as a bulk f_write instead of byte-by-byte.

##### `Core/Hardware/SD/kitBrowser.c` — Rewrite

Current `kitBrowser_init()` does 128× f_open synchronously to scan for existing kits. This becomes an async scan operation.

Two options:
- **(a) Scan asynchronously after boot**: `sd_request_scan_kits(on_scan_complete)` — kit browser populates in the background while audio plays. User sees "Scanning..." until done. Better UX for fast boot.
- **(b) Scan synchronously at boot**: Before audio starts, poll `sd_fsm_tick()` in a loop until scan completes. Simpler but adds boot latency (~128 file existence checks × ~2ms each = ~256ms).

Recommend starting with (b) for simplicity — 256ms boot delay is acceptable and avoids complex UI state. Can move to (a) later.

New `kitBrowser_tick()` also needs async conversion — currently calls `kb_readKitName()` which does f_open/f_read/f_close synchronously on each encoder scroll step. Replace with `sd_request_read_kit_name()` and show "Loading..." until callback fires.

##### `main.c` — Boot path and main loop changes

Boot sequence changes:
```c
// Before (blocking):
preset_init();           // f_mount
preset_loadDrumset(0,0); // blocking f_open/read/close
preset_loadGlobals();    // blocking f_open/read/close
audioCodec_init();

// After:
SD_init();               // existing card init, blocking (pre-audio, OK)
sd_fsm_init();           // calls afatfs_init()

// Poll until filesystem is ready
while (afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY)
    sd_fsm_tick();

// Synchronous kit scan (blocking before audio, OK)
sd_request_scan_kits(NULL);
while (sd_fsm_status() == SD_STATUS_BUSY)
    sd_fsm_tick();

// Synchronous boot kit load
sd_request_load_kit(0, NULL);
while (sd_fsm_status() == SD_STATUS_BUSY)
    sd_fsm_tick();

// Synchronous globals load
sd_request_load_globals(NULL);
while (sd_fsm_status() == SD_STATUS_BUSY)
    sd_fsm_tick();

// NOW start audio — everything after this is non-blocking
audioCodec_init();
```

Main loop changes:
```c
while (1) {
    if (audioCodec_queueFreeSlots() > 0) {
        mixer_calcNextSampleBlock(
            audioCodec_getRenderBuffer(),
            audioCodec_getRenderBuffer2());
        audioCodec_commitRenderBuffer();
    }

    sd_fsm_tick();  // <-- NEW: advance SD operations (calls afatfs_poll)

    int8_t delta = encode_read4();
    // ... rest of UI processing unchanged ...
}
```

##### `Makefile` — Build changes

Remove from SRCS:
```
Core/Hardware/SD/ff.c
Core/Hardware/SD/diskio.c
Core/Hardware/SD/sd_routines.c
```

Add to SRCS:
```
Core/Hardware/SD/asyncfatfs.c
Core/Hardware/SD/fat_standard.c
Core/Hardware/SD/sdcard_lxr02.c
Core/Hardware/SD/sd_fsm.c
```

Keep in SRCS (still needed):
```
Core/Hardware/SD/spi_sd.c          # bit-bang SPI primitives
Core/Hardware/SD/kitBrowser.c      # rewritten against async API
Core/Hardware/SD/sdTest.c          # test harness (commented out)
```

Remove from build (no longer used after async conversion):
```
Core/Hardware/SD/ff.h
Core/Hardware/SD/ffconf.h
Core/Hardware/SD/diskio.h
Core/Hardware/SD/sd_routines.h
```

##### `config.h` — New constants

```c
#define SDCARD_BURST_SIZE        16   // bytes per sdcard_poll() call
#define SDCARD_TOKEN_TIMEOUT   5000   // max polls waiting for read token
#define SDCARD_BUSY_TIMEOUT   50000   // max polls waiting for write busy
```

#### Files NOT Changed

- `spi_sd.c/h` — bit-bang SPI primitives unchanged. `SPI_transmit()`, `SPI_receive()`, `SD_CS_ASSERT/DEASSERT` used directly by `sdcard_lxr02.c`.
- `AudioCodecManager.c/h` — untouched.
- `timebase.c/h` — untouched in this session. TIM5 ISR addition deferred to future session if polling throughput proves insufficient.
- `encoder.c`, `endlessPots.c`, `menu.c` — untouched.
- `sd_routines.c` — `SD_init()` function still needed for card initialization at boot. The rest of sd_routines.c (SD_readSingleBlock, SD_writeSingleBlock, busy-wait loops) is superseded by sdcard_lxr02.c. Options: (a) keep full file in build but only call SD_init, or (b) extract SD_init into sdcard_lxr02.c and remove sd_routines.c entirely. Recommend (a) for initial implementation to minimize changes.

### RAM Budget

asyncfatfs static allocation:
- 8 × 512-byte cache sectors = 4096 bytes
- 8 × `afatfsCacheBlockDescriptor_t` (~20 bytes each) = ~160 bytes
- 3 × `afatfsFile_t` (~200 bytes each) = ~600 bytes
- `afatfs_t` global state = ~300 bytes
- **Total: ~5.2KB**

sd_fsm.c:
- Staging buffer: ~280 bytes
- State variables: ~50 bytes
- **Total: ~330 bytes**

sdcard_lxr02.c:
- State variables: ~30 bytes

kitBrowser.c:
- Existing: kb_map[128] + kb_fs (unchanged, but kb_fs is now unused — remove)
- New: minimal

**Grand total new RAM: ~5.6KB** from SRAM1 (368KB available, current usage well under 100KB). Not a concern.

### Open Questions for Implementation Session

1. **SD card block addressing**: Our current `sd_routines.c` sends block addresses for SDHC (address = block number, not byte offset). asyncfatfs `blockIndex` is also a block index. Confirm they're compatible — should be, but verify against `SD_readSingleBlockCustomBuffer` in sd_routines.c.

2. **SD_init() dependency**: asyncfatfs assumes the card is already in SPI mode when `afatfs_init()` is called. Our `SD_init()` does CMD0/CMD1/CMD8/ACMD41 to bring the card to SPI mode and detect SDHC. This must run before `afatfs_init()`. The card type (SDHC vs standard) affects addressing — asyncfatfs always uses block addresses, which is correct for SDHC.

3. **kitBrowser scan timing**: 128 file existence checks at boot. Each requires `afatfs_fopen` → cache miss (directory sector read) → `afatfs_fclose`. With 8-sector cache, many directory reads will hit the cache after the first few. Estimated 10-20 unique sector reads × ~2ms each = 20-40ms total for the synchronous boot scan. Acceptable.

4. **f_mount equivalent**: asyncfatfs handles mount internally during `afatfs_init()` → `afatfs_poll()` initialization phases (READ_MBR → READ_VBR → READY). No separate mount call needed.

5. **Concurrent file access**: asyncfatfs allows up to `AFATFS_MAX_OPEN_FILES` (3) simultaneous open files. We only need one at a time (all operations are serialized through sd_fsm.c). The extra slots are insurance for future features.

6. **asyncfatfs file modes**: `afatfs_fopen(filename, "r", callback)` for read, `afatfs_fopen(filename, "a", callback)` for append-write, `afatfs_fopen(filename, "w", callback)` for create-write. Our kit save uses create-always semantics (`FA_CREATE_ALWAYS | FA_WRITE`), which maps to "w" mode.

7. **8.3 filename compatibility**: asyncfatfs uses 8.3 FAT-style filenames internally (`fat_convertFilenameToFATStyle`). Our filenames are already 8.3 (`P000.SND`, `GLO.CFG`). No LFN support needed. Confirm `afatfs_fopen("P000.SND", ...)` works — the library should convert to FAT-style internally.
