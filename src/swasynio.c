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
//        swasynio -      SW asynchrounous communications I/O
//

#include <stdarg.h>
#include <string.h>

#include "tcpcomm.h"
#include "timer.h"
#include "video.h"

#include "sw.h"
#include "swasynio.h"
#include "swend.h"
#include "swgames.h"
#include "swinit.h"
#include "swmain.h"
#include "swsound.h"
#include "swtext.h"
#include "swtitle.h"

#define ABORTMSG "\n\n         (Ctrl-C to abort)"

int asynport = DEFAULT_PORT;
enum asyn_mode asynmode;
char *asynhost;

#define SYNC_IM_PLAYER0 '?'
#define SYNC_IM_PLAYER1 '!'
#define TIMEOUT_LEN_MS 5000 	/* time out after 5 seconds */

static int timeout_time;

static void SetTimeout(void)
{
	timeout_time = Timer_GetMS() + TIMEOUT_LEN_MS;
}

static bool TimedOut(void)
{
	return Timer_GetMS() >= timeout_time;
}

#ifdef PLATFORM_ATARI_TOS
// Whether the game itself has started: until it has, consoleplayer is
// still the last game's plane.
static bool in_game;
#endif

// The other player has closed the connection. That is to be expected at
// the end of a game - the loser's ends first - but not before.
static void OtherPlayerLeft(void)
{
#ifdef PLATFORM_ATARI_TOS
	if (in_game && consoleplayer != NULL
	 && consoleplayer->ob_endsts != PLAYING) {
		asynend(NULL);
	}
#endif
	asynerror("  The other player has left");
}

// Waiting on the other player, who might have gone, or be too slow.
static void CheckWaiting(void)
{
	if (commclosed()) {
		OtherPlayerLeft();
	} else if (TimedOut()) {
		asynerror("  The other player stopped answering");
	}
}

static inline void SendShort(int s)
{
	commout(s & 0xff);
	commout((s >> 8) & 0xff);
}

static inline int TryReadShort(void)
{
	int s, t;

	s = commin();

	if (s < 0) {
		return -1;
	}

	SetTimeout();

	while ((t = commin()) < 0) {
		CheckWaiting();
	}

	return (t << 8) + s;
}

static int ReadShort(void)
{
	int i;

	SetTimeout();

	for (i=-1; i < 0; i = TryReadShort()) {
		CheckWaiting();
	}

	return i;
}

void asynput(int movekey)
{
	SendShort(movekey);
}

char *asynclos(void)
{
	commterm();
	return NULL;
}

#ifdef PLATFORM_ATARI_TOS

// How long the game may wait for the other player's next move before
// the player can give up with Ctrl-C, and before it gives up by itself.
// A game waits a little for every move, so pressing Ctrl-C normally
// quits through the game, in step, as on the desktop: only a game held
// up for longer than this just stops.
#define STALL_ABORT_MS   2000
#define STALL_GIVEUP_MS  30000

static int stall_start;
static bool stalled;

// The game cannot move on until the other player's next move arrives:
// make sure it is still coming.
static void CheckStalled(void)
{
	int waited;

	if (latest_player_time[!player] > countmove) {
		stalled = false;
		return;
	}
	if (commclosed()) {
		OtherPlayerLeft();
	}
	if (!stalled) {
		stalled = true;
		stall_start = Timer_GetMS();
		return;
	}

	waited = Timer_GetMS() - stall_start;
	if (waited > STALL_GIVEUP_MS) {
		asynerror("  The other player stopped answering");
	} else if (waited > STALL_ABORT_MS && ctlbreak()) {
		asynerror(NULL);
	}
}

#endif

void asynupdate(void)
{
	int i, ticnum;

	i = TryReadShort();

	if (i >= 0) {
		int netplayer;

		/* we have read a short from the other player. add
		 * to the queue */
		netplayer = !player;

		ticnum = latest_player_time[netplayer] % MAX_NET_LAG;
		latest_player_commands[netplayer][ticnum] = i;
		++latest_player_time[netplayer];
#ifdef PLATFORM_ATARI_TOS
		// A move came: whatever waiting there was is over.
		stalled = false;
#endif
	}
#ifdef PLATFORM_ATARI_TOS
	else if (playmode == PLAYMODE_ASYNCH) {
		CheckStalled();
	}
#endif
}

// The protocol header names the game both players must be running. The
// ST's game is SDL Sopwith's, move for move, so it goes by SDL Sopwith's
// name: an ST can play a computer running the same version.
#ifdef PLATFORM_ATARI_TOS
#define PROTOCOL_NAME "SDL Sopwith " PACKAGE_VERSION
#else
#define PROTOCOL_NAME PACKAGE_STRING
#endif

#define PROTOHEADER_FMT (PROTOCOL_NAME ", player %d")

static void Synchronize(void)
{
	char buf[sizeof(PROTOHEADER_FMT) + 5], *p;

	// send the header ourselves first
	snprintf(buf, sizeof(buf), PROTOHEADER_FMT, player);

	for (p = buf; *p; ++p) {
		commout(*p);
	}

	// now listen for response
	snprintf(buf, sizeof(buf), PROTOHEADER_FMT, !player);
	SetTimeout();

	for (p = buf; *p;) {
		int c;

		CheckWaiting();

		c = commin();

		if (c >= 0) {
			if (c == *p) {
				++p;
			} else if (c != SYNC_IM_PLAYER0
			        && c != SYNC_IM_PLAYER1) {
				asynerror("  The other player is running a\n"
				          "  different version of Sopwith");
			}
		}
	}

	// (The ST's console is underneath the game's screen.)
	if (player) {
		explseed = ReadShort();

#ifndef PLATFORM_ATARI_TOS
		printf("random seed: %i\n", explseed);
#endif
		conf_missiles = ReadShort() != 0;
		conf_wounded = ReadShort() != 0;
		conf_animals = ReadShort() != 0;
		conf_big_explosions = ReadShort() != 0;
		starting_level = ReadShort();
	} else {
		// send settings
		SendShort(explseed);

#ifndef PLATFORM_ATARI_TOS
		printf("random seed: %i\n", explseed);
#endif
		SendShort(conf_missiles);
		SendShort(conf_wounded);
		SendShort(conf_animals);
		SendShort(conf_big_explosions);
		SendShort(starting_level);
	}
	gamenum = starting_level;
}

// setup tcp loop
static void AssignPlayers(bool server_side)
{
	int starttime, lastsendtime, now;

	clrprmpt();
	swputs("  Connected, waiting for other player..." ABORTMSG);
	Vid_Update();

	starttime = Timer_GetMS();
	lastsendtime = 0;

	// for the first 5 seconds, listen to see if theres another player
	// there
	for (;;) {
		int c;

		if (ctlbreak()) {
			asynerror(NULL);
		}
		
		c = commin();
		if (c >= 0) {
			if (server_side && c == SYNC_IM_PLAYER0) {
				commout(SYNC_IM_PLAYER1);
				player = 1;
				return;
			} else if (!server_side && c == SYNC_IM_PLAYER1) {
				player = 0;
				return;
			} else if (server_side || c != SYNC_IM_PLAYER0) {
				asynerror("  That isn't a Sopwith game\n"
				          "  (it sent %02x)", c);
			}
		} else if (commclosed()) {
			OtherPlayerLeft();
		}

		now = Timer_GetMS();
		if (!server_side) {
			// After five seconds we switch to server mode, so
			// that two clients connected to a loop synchronize:
			if ((now - starttime) > 5000) {
				server_side = true;
			} else if ((now - lastsendtime) > 500) {
				commout(SYNC_IM_PLAYER0);
				lastsendtime = now;
			}
		}
	}
}

// setup connection

static void asyninit(void)
{
	if (asynmode == ASYN_LISTEN) {
		swtitln();
		clrprmpt();
		swputs("  Listening for connection...");
#ifdef PLATFORM_ATARI_TOS
		// The other player needs to know where to find this ST:
		{
			const char *addr;
			char buf[40];

			addr = commlocaladdr();
			if (addr != NULL) {
				snprintf(buf, sizeof(buf), "\n  at %s port %d",
				         addr, asynport);
			} else {
				snprintf(buf, sizeof(buf), "\n  on port %d",
				         asynport);
			}
			swputs(buf);
		}
#endif
		swputs(ABORTMSG);
		Vid_Update();
		commlisten();
		AssignPlayers(true);
	} else if (asynmode == ASYN_CONNECT) {
		swtitln();
		clrprmpt();
		swputs("  Attempting to connect to\n  ");
		swputs(asynhost);
		swputs(" ..." ABORTMSG);
		Vid_Update();
		commconnect(asynhost);
		AssignPlayers(false);
	} else {
		ErrorExit("asynmode: unknown asynmode %d", asynmode);
	}
}

#ifdef PLATFORM_ATARI_TOS

jmp_buf asynreturn;

// The settings the player who connects sends to the one listening, as
// they were before: the game is over, and they are this player's again.
static struct {
	bool missiles, wounded, animals, big_explosions;
	int starting_level;
} own_settings;

static void SaveSettings(void)
{
	own_settings.missiles = conf_missiles;
	own_settings.wounded = conf_wounded;
	own_settings.animals = conf_animals;
	own_settings.big_explosions = conf_big_explosions;
	own_settings.starting_level = starting_level;
}

static void RestoreSettings(void)
{
	conf_missiles = own_settings.missiles;
	conf_wounded = own_settings.wounded;
	conf_animals = own_settings.animals;
	conf_big_explosions = own_settings.big_explosions;
	starting_level = own_settings.starting_level;
	gamenum = starting_level;
}

// On the ST a network game ends at the title screen, where the reason
// can be read, rather than the desktop: the game, and the connection,
// are left where they are, and swmain() starts again from the menu.
void asynend(const char *msg)
{
	commterm();
	RestoreSettings();
	in_game = false;

	// No longer a network game: R and Q are not game keys at the
	// prompt, and C is a key like any other.
	playmode = PLAYMODE_UNSET;

	sound(0, 0, NULL);
	swsound();

	if (msg != NULL) {
		int start;

		swtitln();
		clrprmpt();
		swputs(msg);
		swputs("\n\n         (press a key)");
		Vid_Update();

		// A player still flying when the game stopped is still
		// pressing keys and moving the stick: give them a second to
		// see the message before one of those can dismiss it.
		start = Timer_GetMS();
		while (Timer_GetMS() - start < 1000) {
			Vid_GetKey();
			Vid_ControllerMenuKey();
		}
		while (Vid_GetKey() == 0
		    && Vid_ControllerMenuKey() == MENUKEY_NONE) {
		}
	}

	// Ctrl-C, if it was pressed, has done its work.
	Vid_ClearCtrlBreak();
	longjmp(asynreturn, 1);
}

#endif

void asynerror(const char *fmt, ...)
{
	char msg[100];
	va_list args;

	if (fmt == NULL) {
#ifdef PLATFORM_ATARI_TOS
		asynend(NULL);
#else
		ErrorExit("asyninit: user aborted");
#endif
	}

	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

#ifdef PLATFORM_ATARI_TOS
	asynend(msg);
#else
	ErrorExit("%s", msg);
#endif
}

void init1asy(void)
{
#ifndef TCPIP
	ErrorExit("TCP/IP support not compiled into binary!");
#else
#ifdef PLATFORM_ATARI_TOS
	SaveSettings();
	stalled = false;
	in_game = false;
#endif
	asyninit();
	clrprmpt();
	swputs("  Waiting for other player...");
	Synchronize();
#ifdef PLATFORM_ATARI_TOS
	in_game = true;
#endif
#endif
}

//
// 2003-02-14: Code was checked into version control; no further entries
// will be added to this log.
//
// sdh 14/2/2003: change license header to GPL
// sdh 26/03/2002: changed CGA_ to Vid_
// sdh 16/11/2001: TCPIP #define to disable TCP/IP support
// sdh 29/10/2001: harrykeys
// sdh 21/10/2001: rearranged file headers, added cvs tags
// sdh 21/10/2001: reformatted with indent, adjusted some code by hand
//                 to make more readable
// sdh 20/10/2001: rearranged netgame syncronisation, take game settings
//                 from the first (listening) player
// sdh 19/10/2001: removed all externs, this is done with headers now
// sdh 18/10/2001: converted all functions to ansi-style arguments
//
// 96-12-26        Remove IMAGINET network card address in "multaddr".
// 87-03-12        Allow asynch loopback for debugging.
// 87-03-09        Microsoft compiler.
// 85-04-03        Development
//

