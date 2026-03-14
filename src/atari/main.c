long _stksize = 65536L;

#include "swmain.h"

#include <osbind.h>
#include <stdint.h>

extern void JoystickInit(void);
extern void JoystickExit(void);

int main(int argc, char *argv[])
{
	printf("Loading ST Sopwith...");

	/* Enter supervisor mode for the lifetime of the program.
	   This allows direct access to low-memory system variables
	   (joystick state, hardware registers, etc.) without per-call
	   Supexec overhead. Super(0) switches to supervisor mode and
	   returns the old SSP; we don't need to restore it since the
	   program will exit normally via TOS. */
	Super(0L);

	/* Disable keyboard click (bit 0 of conterm at 0x0484) */
	*((volatile uint8_t *)0x0484) &= ~0x01;

	JoystickInit();
	atexit(JoystickExit);

	return swmain(argc, argv);
}
