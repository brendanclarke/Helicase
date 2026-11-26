/*
 * menuPages.h — LXR-02 menu page table.
 * Ported from original LXR AVR menuPages.h by Julian Schmidt.
 * PROGMEM removed. Since S097 P1 only the static pages have rows.
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef MENUPAGES_H_
#define MENUPAGES_H_
#include "menu.h"

/*
 * Static menu pages (S097 P1 item 1).
 *
 * What: only the six pages whose cells come from this table have rows here,
 * each with all NUM_SUB_PAGES sub-pages; menuStaticPageSlot[] maps a page id
 * to its block (1-based, 0 = not a table page). Every other page (VOICE1-7,
 * LOAD, SAVE, SOM, EFFECT) resolves its cells elsewhere and reads the shared
 * all-zero row in menu_pageRow() (menu.c), exactly as it read its zero rows
 * of the former menuPages[NUM_PAGES][NUM_SUB_PAGES].
 * Why: 88 of the 136 rows of the former table could never hold a cell
 * (-2,112 B). The six table pages keep all eight sub-pages as room to grow;
 * a new static page needs a block here and a slot entry.
 * Accessors: menu_pageRow() only.
 */
#define MENU_STATIC_PAGE_COUNT 6u

static const uint8_t menuStaticPageSlot[NUM_PAGES] = {
    [MENU_MIDI_PAGE]        = 1u,
    [PERFORMANCE_PAGE]      = 2u,
    [SEQ_PAGE]              = 3u,
    [EUKLID_PAGE]           = 4u,
    [PATTERN_SETTINGS_PAGE] = 5u,
    [RECORDING_PAGE]        = 6u,
};

static const Page menuStaticPages[MENU_STATIC_PAGE_COUNT][NUM_SUB_PAGES] = {
/* MENU_MIDI_PAGE (globals)
** Session 025 warning: this table's empty cells are not harmless decoration.
** The menu traversal treats TEXT_EMPTY/PAR_NONE as page terminators in some
** paths. When globals are reordered, manually verify the table; inserting an
** early TEXT_EMPTY/PAR_NONE pair can block access to later globals pages. */
{
  /*
   * S104 P1: `omn` (the MIDI Omni channel) sits beside G (`ch`) in column 4;
   * the screensaver cell moves to the Scene-change screen (row 2, column 3).
   */
  {TEXT_BPM,TEXT_EXT_SYNC,TEXT_QUANTISATION,TEXT_MIDI_CHAN_GLOBAL,TEXT_MIDI_CHAN_OMNI,TEXT_MIDI_FILT_TX,TEXT_MIDI_FILT_RX,TEXT_MIDI_ROUTING,
    PAR_BPM,PAR_EXT_SYNC, PAR_QUANTISATION, PAR_MIDI_CHAN_GLOBAL, PAR_MIDI_CHAN_OMNI, PAR_MIDI_FILT_TX, PAR_MIDI_FILT_RX, PAR_MIDI_ROUTING},
  /* The final two cells expose AutoSave and the retained Pattern pool-use
   * reading before the traversal terminator. Pattern is read-only and uses a
   * virtual parameter sentinel; AutoSave remains the real Global parameter. */
  {TEXT_BAR_RESET_MODE,TEXT_TRIGGER_IN_PPQ,   TEXT_TRIGGER_OUT1_PPQ,   TEXT_FOLLOW,TEXT_OSC_INTERP,    TEXT_CPU_USE,        TEXT_AUTOSAVE, TEXT_PAT_STORE_USE,
    PAR_BAR_RESET_MODE, PAR_PRESCALER_CLOCK_IN,PAR_PRESCALER_CLOCK_OUT1,PAR_FOLLOW, PAR_OSC_WAVE_INTERP,PAR_RUNTIME_CPU_USE, PAR_AUTOSAVE_ENABLED, PAR_PAT_STORE_USE},
  /*
   * Scene change / queue globals (S080 F3, P2).
   *
   * What: scd, qcd, qac in columns 0-2; `scr` follows qac (S104 P1); no
   * parameter follows `scr` on this screen, and it is the last Global screen.
   * Its scroll cue is '^' (a Scene-settings page follows). Why: the
   * compressor cells are Scene settings and must sit together on their own
   * page, marked '+' (user, S080 P2 follow-up). Encoder traversal crosses
   * from qac to the compressor page through the static cross-sub-page step
   * (menu_moveToMenuItem(), extended to MENU_MIDI_PAGE in S080 P2); the
   * MODE MENU re-press cycle reaches it through menu_switchSubPage().
   * Affiliates: checkScrollSign() ('^' cue), MENU_GLOBAL_SCENE_SUBPAGE.
   */
  {TEXT_SCENE_CHANGE_DIV,TEXT_SCENE_QUEUE_DIV,TEXT_SCENE_QUEUE_ACTION,TEXT_SCREENSAVER_ON_OFF,
    TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
    PAR_SCENE_CHANGE_DIV,PAR_SCENE_QUEUE_DIV,PAR_SCENE_QUEUE_ACTION,PAR_SCREENSAVER_ON_OFF,
    PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  /*
   * Scene-owned bus compressor page (MENU_GLOBAL_SCENE_SUBPAGE, S074 /
   * S080 P2).
   *
   * What: cmp, cam, ctm, csc together in columns 0-3; S095 P4 C5 adds the
   * read-only `cgr` readout in column 4 (the first cell of the second
   * screen), with the rest of the second half empty. Why: all four are Scene
   * settings (active-Scene mirrors fanned out through the VOICE edit mask),
   * so they are shown together on the last settings page with the '+'
   * Scene-ownership cue.
   * New global pages go before this one and move MENU_GLOBAL_SCENE_SUBPAGE.
   * Accessors: global encoder traversal, checkScrollSign() ('+'). Affiliates:
   * menu_commitBusCompParam(), menu_devCheckGlobalSceneSubPage() (diagnostic
   * layout check).
   */
  {TEXT_BUS_COMP_MODE,TEXT_BUS_COMP_AMOUNT,TEXT_BUS_COMP_TIME,TEXT_BUS_COMP_SIDECHAIN,
    TEXT_BUS_COMP_GR,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
    PAR_BUS_COMP_MODE,PAR_BUS_COMP_AMOUNT,PAR_BUS_COMP_TIME,PAR_BUS_COMP_SIDECHAIN,
    PAR_BUS_COMP_GR,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
},
/* PERFORMANCE_PAGE
 * Per-track shuffle moved to the STEP track-settings front page below.
 *
 * Why: shuffle is now PatternData-owned timing data, so PERF should not expose
 * a global-looking value that edits whichever track happens to be active.
 * Clients: menu_repaintGeneric() and edit navigation consume this static table.
 * Output: PERF exposes overall Scene Morph, six per-voice Morph amounts, and
 * the Scene's Effect Morph amount `fxm` (S075; it replaced the retired global
 * `srt` decimation cell). Roll is intentionally absent from PERF; later
 * performance/recording work will re-home roll behavior with the new pattern
 * stack instead of keeping it beside Morph.
 *
 * S080 A1: split into two subpages, four active cells each.
 *
 * What: subpage 0 = mrp (overall Morph), fxm (Effect Morph), 1VM, 2VM;
 * subpage 1 = 3VM, 4VM, 5VM, 6VM. Cells 4-7 on both subpages are
 * TEXT_EMPTY/PAR_NONE terminators.
 * Why: the prior 8-cell single page reached the per-voice Morph cells only by
 * scrolling the within-subpage second half, which also caused the PERF
 * page-entry cursor bug (see buttonHandler.c A2). Splitting into subpages
 * groups the two global controls (mrp, fxm) on the landing subpage and puts
 * the six per-voice Morph controls on a second subpage.
 * Inputs: static compile-time table consumed by menu_repaintGeneric() and
 * encoder/subpage traversal.
 * Outputs: the generic page renderer reads cells 0-3 of the active subpage;
 * the TEXT_EMPTY terminator signals the end of the subpage.
 * Accessors: menu_resolveCell(), menu_switchSubPage(), menu_getSubPage(),
 * menu_moveToMenuItem() (S080 cross-subpage step).
 * Affiliates: menu_applyPerfMarkers() (A3), buttonHandler handleModeButtons
 * (A2), the PERF automation scan (va_scanService).
 */
/* PERFORMANCE_PAGE */ {
  /* Subpage 0: mrp, fxm, 1VM, 2VM. Cells 4-7 are terminators. */
  {TEXT_X_FADE,TEXT_EFFECT_MORPH,TEXT_VOICE1_MORPH,TEXT_VOICE2_MORPH,
   TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
   PAR_MORPH,PAR_EFFECT_MORPH,PAR_VOICE1_MORPH,PAR_VOICE2_MORPH,
   PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  /* Subpage 1: 3VM, 4VM, 5VM, 6VM. Cells 4-7 are terminators. */
  {TEXT_VOICE3_MORPH,TEXT_VOICE4_MORPH,TEXT_VOICE5_MORPH,TEXT_VOICE6_MORPH,
   TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
   PAR_VOICE3_MORPH,PAR_VOICE4_MORPH,PAR_VOICE5_MORPH,PAR_VOICE6_MORPH,
   PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
},
/* SEQ_PAGE
 * Subpage 0 is the track-settings front page. S078 §7 groups the three
 * morphable parameters (len, scl, shf) on the left, followed by the discrete
 * play mode (run), MIDI channel, MIDI note, the S080 P2 looper opt-out
 * (lop, Defect 8a) and the S081 P2 automation ramp mode (rmp). All eight
 * cells are populated; the generic traversal stops at column 8. Clients:
 * STEP-mode VOICE re-press toggles menuIndex through Menu helpers, and encoder
 * scrolling reaches every cell through normal traversal.
 */
/* SEQ_PAGE */ {
  /* S081 P2: cell 7 = automation ramp mode (`rmp`, was `asm`), after lop.
   * S083 follow-up: cell 5 (`nte`) uses TEXT_TRACK_NOTE (category Track). */
  {TEXT_PAT_LENGTH,TEXT_TRACK_SCALE,TEXT_SHUFFLE,TEXT_TRACK_PLAY_MODE,TEXT_MIDI_CHANNEL,TEXT_TRACK_NOTE,TEXT_TRACK_LOOPABLE,TEXT_TRACK_AUTO_SMOOTH, PAR_TRACK_LENGTH,PAR_TRACK_SCALE,PAR_SHUFFLE,PAR_TRACK_PLAY_MODE,PAR_TRACK_MIDI_CHAN,PAR_TRACK_MIDI_NOTE,PAR_TRACK_LOOPABLE,PAR_TRACK_AUTO_SMOOTH},
  /*
   * Step specials subpage 1 (S079; S099 P3, user 2026-11-19): vel, note,
   * micro-timing, fill (`fil`, "Step Fill", was the roll page's `rfl`: the
   * same value, which truncates a roll and, on some instruments, shortens the
   * step). The four fixed cells end at fil; scrolling past it enters the
   * variable automation list rendered by menu.c from PatternData's block list.
   */
  {TEXT_STEP_VELOCITY,TEXT_NOTE,TEXT_MICROTIMING,TEXT_ROLL_FILL,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_STEP_VOLUME,PAR_STEP_NOTE,PAR_STEP_TIMING,PAR_STEP_FILL,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  /* Step specials subpage 2 (S079; S099 P3): condition, roll rate, roll
   * rhythm (fill moved to subpage 1). */
  {TEXT_CONDITION,TEXT_ROLL_RATE,TEXT_ROLL_RHYTHM,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_STEP_CONDITION,PAR_STEP_ROLL,PAR_STEP_RHYTHM,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
},
/* EUKLID_PAGE */ {
  /* PAR_ROLL retired in place (S081 A3-5, Q14): Euklid roll-speed cell cleared. */
  {TEXT_PAT_LENGTH,TEXT_NUM_STEPS,TEXT_ROTATION,TEXT_X_FADE,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_EUKLID_LENGTH,PAR_EUKLID_STEPS,PAR_EUKLID_ROTATION,PAR_MORPH,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
},
/* PATTERN_SETTINGS_PAGE */ {
  /*
   * S080 G8: sub-page 0 hosts the SHIFT+PLAY+SEQ scene chain editor: cn1,
   * nx1, cn2, nx2 (category Chain) of the Scene in menu_chainMenuScene. The
   * cells are PAR_CHAIN_* mirrors committed by menu_commitChainParam(). Pattern
   * repeat/next switching stays retired (the legacy ENABLE_EUKLID_PAGE SHIFT
   * path would also land here; it is compiled out).
   */
  {TEXT_CHAIN_COND1,TEXT_CHAIN_NEXT1,TEXT_CHAIN_COND2,TEXT_CHAIN_NEXT2,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_CHAIN_COND1,PAR_CHAIN_NEXT1,PAR_CHAIN_COND2,PAR_CHAIN_NEXT2,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
},
/* RECORDING_PAGE — repurposed as the SHIFT+REC performance settings menu (S080 E3).
 *
 * What: sub-page 0 holds the PERF BAR overlay settings (pb</pn</pb>/pn>,
 * category Prfrmnc); sub-page 1 (S083 follow-up, user 2026-10-22) holds the
 * overlay roll velocity rhythms pv</pv> (category Prfrmnc), quantisation
 * `qnt` and the recording track `rtk` on one page (qnt and rtk were on
 * sub-pages 1 and 2). Sub-pages 2-7 are reserved. Why: SHIFT+REC now opens a single performance/settings surface; the
 * overlay controls are shown first so they are reachable immediately. Inputs:
 * static table read by menu_repaintGeneric(). Accessors: menu_switchPage(
 * RECORDING_PAGE), the S080 cross-sub-page encoder step, buttonHandler's
 * SHIFT+REC enter/exit (E7). Affiliates: PAR_PERF_BTN_LT/RT (E1),
 * TEXT_PERF_BTN_LT/RT (E2).
 */
/* RECORDING_PAGE */ {
  /* Sub-page 0: BAR overlay trigger types and notes. */
  {TEXT_PERF_BTN_LT,TEXT_PERF_NOTE_LT,TEXT_PERF_BTN_RT,TEXT_PERF_NOTE_RT,
   TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
   PAR_PERF_BTN_LT,PAR_PERF_NOTE_LT,PAR_PERF_BTN_RT,PAR_PERF_NOTE_RT,
   PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  /* Sub-page 1: overlay roll rhythms, quantisation, recording track. */
  {TEXT_PERF_VEL_LT,TEXT_PERF_VEL_RT,TEXT_QUANTISATION,TEXT_AUTOMATION_TRACK,
   TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
   PAR_PERF_VEL_LT,PAR_PERF_VEL_RT,PAR_QUANTISATION,PAR_AUTOM_TRACK,
   PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  /* Sub-pages 2-7 reserved. */
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
},
};

#endif /* MENUPAGES_H_ */
