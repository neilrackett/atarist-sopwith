/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <mint/sysvars.h>

#include <stdl/stdl.h>

#include "swsound.h"
#include "timer.h"

/* --- VBL-driven sound update ----------------------------------------- */

/* Same arrangement as the native backend: swsndupdate() runs from the
   VBL queue at a rock-steady 50Hz so note timing does not depend on
   how long drawing takes. STDL has no "call me every VBL" API - its
   own YM tick claims a slot privately - so this uses the TOS VBL
   queue directly. We are already in supervisor mode (STDL_Init), and
   STDL picks a different free slot for its own tick.  */

#define VBL_SLOT 0   /* _vblqueue slot to use (0 to nvbls-1, default 8 slots) */

static void (*old_vbl)(void);

void Timer_Init(void)
{
	void (**vblq)(void) = *_vblqueue;
	old_vbl = vblq[VBL_SLOT];
	vblq[VBL_SLOT] = swsndupdate;
}

void Timer_Exit(void)
{
	void (**vblq)(void) = *_vblqueue;
	vblq[VBL_SLOT] = old_vbl;
}

int Timer_GetMS(void)
{
	return (int)STDL_GetTicks();
}

void Timer_Sleep(int msec)
{
	/* The game loop is already paced by the VBL-synced page flip in
	   Vid_Update(). All Timer_Sleep calls are SDL-era throttling that
	   are redundant on the ST and harmful to sound timing - skip them
	   entirely. */
	(void)msec;
}
