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
	unsigned long start = get_sysvar(_hz_200);
	unsigned long ticks = (msec + 4) / 5;

	if (ticks == 0) {
		ticks = 1;
	}

	while ((get_sysvar(_hz_200) - start) < ticks) {
	}
}
