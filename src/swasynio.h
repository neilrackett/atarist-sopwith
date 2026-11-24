/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

//
// Copyright(C) 1984-2000 David L. Clark
// Copyright(C) 2001-2005 Simon Howard
//
// You can redistribute and/or modify this program under the terms of the
// GNU General Public License version 2 as published by the Free Software
// Foundation, or any later version. This program is distributed WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.
//

#ifndef __SWASYNIO_H__
#define __SWASYNIO_H__

#include <setjmp.h>

#include "std.h"

enum asyn_mode {
	ASYN_LISTEN,
	ASYN_CONNECT,
};

extern enum asyn_mode asynmode;
extern char *asynhost;
extern int asynport;

extern void asynput(int movekey);
extern char *asynclos(void);
extern void init1asy(void);
extern void asynupdate(void);

// Ends a network game that cannot go on, saying why (a printf format,
// or NULL when the player gave up): on the desktop by exiting, as ever;
// on the ST by going back to the title screen.
extern void asynerror(const char *fmt, ...);

#ifdef PLATFORM_ATARI_TOS
// Where a network game on the ST goes when it is over: swmain() sets it
// up, to start again from the title screen, and asynend() goes there,
// first showing msg (unless it is NULL) until a key is pressed.
extern jmp_buf asynreturn;
extern void asynend(const char *msg) __attribute__((noreturn));
#endif

#endif

