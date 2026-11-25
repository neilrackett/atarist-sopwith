/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

long _stksize = 32768L;

#include "swmain.h"
#include "timer.h"

#include <osbind.h>
#include <stdint.h>

extern void JoystickInit(void);
extern void JoystickExit(void);

/* libcmini: the size of the blocks malloc takes from GEMDOS (no header
   declares it). */
extern void __mallocChunkSize(long size);

int main(int argc, char *argv[])
{
	/* libcmini's malloc asks GEMDOS for blocks that grow from 8KB to
	   32KB. Once the game has loaded, a 512KB ST has only 17-23KB of
	   GEMDOS memory left, too little for a 32KB block, so the heap
	   would stop short and leave it unused. 8KB blocks reach most of
	   it: room for up to sixty more objects in a busy scene. */
	__mallocChunkSize(8192);

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
