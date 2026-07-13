/*
 * Core/Hardware/SD/kitBrowser.c
 *
 *  Created on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

/*
 * kitBrowser.c - Kit file browser for LXR-02 (asyncfatfs version).
 *
 * Kit scan is driven by filesystem.c, which opens/closes
 * each P000.SND-P127.SND and records present slots in kb_map[].
 *
 * Kit name reads (on encoder scroll) are async via filesystem_requestLoadName().
 * The display shows "Loading..." until the async read completes.
 *
 * kb_map[] and kb_numKits are written directly by filesystem_scanKits_tick().
 */

#include "kitBrowser.h"
#include "filesystem.h"
#include "lcd.h"
#include <string.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
** State - kb_map and kb_numKits are extern'd by filesystem.c for scan writes
** ----------------------------------------------------------------------- */
uint16_t kb_map[KITBROWSER_MAX_KITS];
uint16_t kb_numKits   = 0;
static uint16_t kb_mapIndex  = 0;
static volatile uint8_t kb_dirty = 0;
static volatile uint8_t kb_name_pending = 0;

static char kb_kitName[9];

/* -----------------------------------------------------------------------
** Callback from filesystem_requestLoadName
** ----------------------------------------------------------------------- */
static void kb_onNameLoaded(void)
{
    const char *name = filesystem_loadedName();
    memcpy(kb_kitName, name, 8);
    kb_kitName[8] = '\0';
    kb_name_pending = 0;
    filesystem_ack();

    /* Repaint with loaded name */
    kb_dirty = 2; /* 2 = name ready, repaint now */
}

/* -----------------------------------------------------------------------
** kb_repaint
** ----------------------------------------------------------------------- */
static void kb_repaint(void)
{
    char buf[17];
    uint16_t kitNr = (uint16_t)(kb_map[kb_mapIndex] + 1u);

    /* Row 1 */
    buf[0]  = 'K'; buf[1]  = 'i'; buf[2]  = 't'; buf[3]  = ' ';
    buf[4]  = ' '; buf[5]  = ' '; buf[6]  = ' '; buf[7]  = ' ';
    buf[8]  = ' '; buf[9]  = ' '; buf[10] = ' '; buf[11] = ' ';
    buf[12] = ' ';
    buf[13] = (char)('0' + (kitNr / 100u));
    buf[14] = (char)('0' + ((kitNr / 10u) % 10u));
    buf[15] = (char)('0' + (kitNr % 10u));
    buf[16] = '\0';
    lcd_setcursor(0, 1);
    lcd_string(buf);

    /* Row 2: kit name or "Loading..." */
    memset(buf, ' ', 16);
    buf[16] = '\0';
    if (kb_name_pending) {
        memcpy(buf, "Loading...", 10);
    } else {
        uint8_t i;
        for (i = 0; i < 8 && kb_kitName[i]; i++)
            buf[i] = kb_kitName[i];
    }
    lcd_setcursor(0, 2);
    lcd_string(buf);
}

/* -----------------------------------------------------------------------
** kitBrowser_init - called after filesystem_requestScanKits completes.
** kb_map[] and kb_numKits are already populated by filesystem.c.
** ----------------------------------------------------------------------- */
uint8_t kitBrowser_init(void)
{
    if (kb_numKits == 0) {
        lcd_setcursor(0, 1);
        lcd_string("No kit files!   ");
        lcd_setcursor(0, 2);
        lcd_string("                ");
        return 0;
    }

    kb_mapIndex = 0;

    /* Request async name load for first kit */
    memcpy(kb_kitName, "Loading.", 8);
    kb_kitName[8] = '\0';
    kb_name_pending = 1;
    filesystem_ack(); /* clear any previous DONE state */
    filesystem_requestLoadName(FS_FILE_KIT, kb_map[0], kb_onNameLoaded);

    kb_repaint();
    return kb_numKits;
}

/* -----------------------------------------------------------------------
** kitBrowser_encoderDelta
** ----------------------------------------------------------------------- */
void kitBrowser_encoderDelta(int8_t delta)
{
    if (kb_numKits == 0) return;

    int16_t next = (int16_t)kb_mapIndex + delta;
    if (next < 0)              next = 0;
    if (next >= kb_numKits)    next = kb_numKits - 1;

    if ((uint16_t)next != kb_mapIndex) {
        kb_mapIndex = (uint16_t)next;
        kb_dirty = 1;
    }
}

/* -----------------------------------------------------------------------
** kitBrowser_tick - called from main loop
** ----------------------------------------------------------------------- */
void kitBrowser_tick(void)
{
    if (kb_dirty == 2) {
        /* Name loaded - just repaint */
        kb_dirty = 0;
        kb_repaint();
        return;
    }

    if (kb_dirty != 1) return;
    kb_dirty = 0;

    /* Start async name load */
    kb_name_pending = 1;
    filesystem_ack();
    if (!filesystem_requestLoadName(FS_FILE_KIT, kb_map[kb_mapIndex], kb_onNameLoaded)) {
        /* Filesystem busy (e.g. kit load in progress) - defer */
        kb_name_pending = 0;
        memcpy(kb_kitName, "Busy... ", 8);
        kb_kitName[8] = '\0';
    }
    kb_repaint();
}

uint16_t kitBrowser_getCurrentKit(void)
{
    return kb_map[kb_mapIndex];
}
