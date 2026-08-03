/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>

#include <stdl/stdl.h>

#include "pcsound.h"

bool snd_tinnyfilter = false;

/* Sopwith speaks in PC ISA timer divisors; STDL_SpeakerOn takes Hz
   and owns voice A (the PC-speaker idiom the library provides for
   exactly this). Volume 6 of 15 matches the native backend - a lower
   level takes the edge off the YM square wave. */
#define SPEAKER_VOLUME 6

void Speaker_Init(void)
{
	STDL_SpeakerOff();
}

void Speaker_Off(void)
{
	STDL_SpeakerOff();
}

void Speaker_Output(unsigned short count)
{
	long hz;

	if (count == 0)
	{
		STDL_SpeakerOff();
		return;
	}

	hz = 1193280L / (long)count;

	/* Below ~30 Hz is inaudible on the ST speaker - silence. This
	   covers the 0xF000 engine-idle tone (~19 Hz) from the original
	   code. */
	if (hz < 30)
	{
		STDL_SpeakerOff();
		return;
	}

	STDL_SpeakerOn((int)hz, SPEAKER_VOLUME);
}
