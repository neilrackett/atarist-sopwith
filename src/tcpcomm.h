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
//
// TCP/IP Communications
//

#ifndef __TCPCOMM_H__
#define __TCPCOMM_H__

#include "std.h"

extern void commconnect(char *host);
extern void commlisten(void);

extern int commin(void);
extern void commout(unsigned char c);

extern void commterm(void);

// True once the other end has closed the connection (or it has failed)
// and everything it sent has been read.
extern bool commclosed(void);

#ifdef PLATFORM_ATARI_TOS
// This machine's address, for the other player to connect to, or NULL
// if it has none. (Fails with asynerror() if there is no TCP/IP stack.)
extern const char *commlocaladdr(void);
#endif

#endif /* __TCPCOMM_H__ */
