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
	return (int) (get_sysvar(_hz_200) * 5UL);
}

void Timer_Sleep(int msec)
{
	/* The game loop is already paced by Vsync() in Vid_Update().
	   All Timer_Sleep calls are SDL-era throttling that are redundant
	   on the ST and harmful to sound timing — skip them entirely. */
	(void)msec;
}
