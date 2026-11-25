/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <mint/sysvars.h>

#include "swsound.h"
#include "timer.h"

/* --- VBL-driven sound update ----------------------------------------- */

/* We install a VBL handler so swsndupdate() fires at a rock-steady 50Hz,
   completely independent of how long drawing takes each frame.  This
   eliminates the occasional note-skip caused by the menu loop catching up
   multiple sound ticks in a burst when drawing runs slow. */

#define VBL_SLOT 0   /* _vblqueue slot to use (0 to nvbls-1, default 8 slots) */

static void (*old_vbl)(void);
static int installed;

void Timer_Init(void)
{
	void (**vblq)(void) = *_vblqueue;

	// main() calls this and swinit() calls it again. A second
	// install would keep swsndupdate itself as the routine to put
	// back, and leave it in the queue after exit: TOS 1.04 then
	// calls into freed memory at the next VBL and the ST crashes.
	if (installed)
	{
		return;
	}
	old_vbl = vblq[VBL_SLOT];
	vblq[VBL_SLOT] = swsndupdate;
	installed = 1;
}

void Timer_Exit(void)
{
	void (**vblq)(void) = *_vblqueue;

	if (installed)
	{
		vblq[VBL_SLOT] = old_vbl;
		installed = 0;
	}
}

int Timer_GetMS(void)
{
	return (int) (*_hz_200 * 5UL);
}

void Timer_Sleep(int msec)
{
	/* The game loop is already paced by Vsync() in Vid_Update().
	   All Timer_Sleep calls are SDL-era throttling that are redundant
	   on the ST and harmful to sound timing — skip them entirely. */
	(void)msec;
}
