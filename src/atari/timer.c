#include <mint/sysvars.h>

#include "timer.h"

void Timer_Init(void)
{
}

int Timer_GetMS(void)
{
	return (int) (get_sysvar(_hz_200) * 5UL);
}

void Timer_Sleep(int msec)
{
	/* Vid_Update() calls Vsync() which already paces at 50/60 Hz, so
	   short sleeps used for menu pacing add unnecessary latency. Only
	   sleep for requests longer than one video frame (~20ms). */
	if (msec <= 20) {
		return;
	}

	unsigned long start = get_sysvar(_hz_200);
	unsigned long ticks = (msec + 4) / 5;

	while ((get_sysvar(_hz_200) - start) < ticks) {
	}
}
