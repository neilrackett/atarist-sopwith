/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

long _stksize = 32768L;

#include <stdio.h>
#include <stdlib.h>

#include "swmain.h"
#include "timer.h"
#include "video.h"

extern void JoystickInit(void);
#ifdef SOPWITH_STATS
extern void Vid_ReportSpriteCache(void);
#endif

int main(int argc, char *argv[])
{
	printf("Loading ST Sopwith (STDL backend)...");

	/*
	 * libcmini runs atexit handlers in REGISTRATION order, not the
	 * LIFO order the C standard specifies. STDL_Init registers its
	 * own handler, which drops back out of supervisor mode - so
	 * Timer_Exit, which pokes the VBL queue at $456, has to be
	 * registered *before* Vid_Init or it runs in user mode and bus
	 * errors on the way out. (Registering it early is harmless:
	 * Timer_Init has always run by the time the program exits.)
	 */
	atexit(Timer_Exit);

	/*
	 * Unlike src/atari/main.c there is no Super() here: STDL_Init
	 * (reached through Vid_Init) takes supervisor mode, the IKBD
	 * vector and a VBL slot for the whole run, and two supervisor
	 * regimes entered at different stack depths crash on exit.
	 * The keyboard click is silenced by STDL's YM service.
	 */
	Vid_Init();

	JoystickInit();
	Timer_Init();

#ifdef SOPWITH_STATS
	/* registered last, so it runs last - after STDL has put the
	   video mode back and the console is visible again */
	atexit(Vid_ReportSpriteCache);
#endif

	return swmain(argc, argv);
}
