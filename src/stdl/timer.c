/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdl/stdl.h>

#include "swsound.h"
#include "timer.h"

/* --- VBL-driven sound update ----------------------------------------- */

/* Same arrangement as the native backend: swsndupdate() runs from the
   VBL queue at a rock-steady 50Hz so note timing does not depend on
   how long drawing takes. STDL_AddVBL gives it a free slot, ignores
   the second call (main() and swinit() both call Timer_Init), and
   takes it out again on every way out, abort() included - a slot left
   behind is called in freed memory, which crashes TOS 1.04. */

void Timer_Init(void)
{
	(void) STDL_AddVBL(swsndupdate);
}

void Timer_Exit(void)
{
	STDL_RemoveVBL(swsndupdate);
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
