long _stksize = 32768L;

#include "swmain.h"
#include "timer.h"

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
		 Supexec overhead. We do not switch back to user mode at exit;
		 Pterm() handles cleanup regardless of the current mode.
		 Switching back via an atexit handler would corrupt the stack,
		 since Super() changes the active stack pointer mid-call-chain. */
	Super(0L);

	/* Disable keyboard click (bit 0 of conterm at 0x0484) */
	*((volatile uint8_t *)0x0484) &= ~0x01;

	JoystickInit();
	atexit(JoystickExit);
	Timer_Init();
	atexit(Timer_Exit);

	return swmain(argc, argv);
}
