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
//        swtitle  -      SW perform animation on the title screen
//

#include <ctype.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#endif

#include "video.h"

#include "sw.h"
#include "swasynio.h"
#include "swconf.h"
#include "swend.h"
#include "swgames.h"
#include "swgrpha.h"
#include "swinit.h"
#include "swmain.h"
#include "swmenu.h"
#include "swtext.h"
#include "swsound.h"
#include "swsymbol.h"
#include "swtitle.h"

#define X_OFFSET ((SCR_WDTH/2)-160)

void swtitln(void)
{
	const char *version_string;
	GRNDTYPE *orground = original_level.gm_ground;

	int i, h;

	sound(S_TITLE, 0, NULL);

	// clear the screen
	Vid_ClearBuf();

	swcolor(2);
	swposcur(18+X_OFFSET/8, 2);
	swputs("SDL");

	swcolor(3);
	swposcur(13+X_OFFSET/8, 4);
	swputs("S O P W I T H");

	version_string = "Version " PACKAGE_VERSION;
	swposcur((40 - strlen(version_string)) / 2 + X_OFFSET / 8, 6);
	swputs(version_string);

	swcolor(3);
	swposcur(0+X_OFFSET/8, 9);
	swputs("(c) 1984, 1985, 1987 ");

	swcolor(1);
	swputs("BMB ");
	swcolor(3);
	swputs("Compuscience");

	swcolor(3);
	swposcur(0+X_OFFSET/8, 10);
	swputs("(c) 1984-2000 David L. Clark");

	swcolor(3);
	swposcur(0+X_OFFSET/8, 11);
	swputs("(c) 2001-2024 Simon Howard, Jesse Smith");

	swcolor(3);
	swposcur(0+X_OFFSET/8, 12);
	swputs("    Distributed under the ");
	swcolor(1);
	swputs("GNU");
	swcolor(3);
	swputs(" GPL");

	swground(orground, 507 - X_OFFSET);

	Vid_DispSymbol(40+X_OFFSET, 180, &symbol_plane[0].sym[0],
	               FACTION_PLAYER1);
	Vid_DispSymbol(130+X_OFFSET, 80, &symbol_plane[1].sym[6],
	               FACTION_PLAYER2);
	Vid_DispSymbol(23+X_OFFSET, orground[530] + 16,
	               &symbol_targets[3].sym[0], FACTION_PLAYER2);
	Vid_DispSymbol(213+X_OFFSET, orground[720] + 16, &symbol_ox[0].sym[0],
	               FACTION_PLAYER1);
	Vid_DispSymbol(270+X_OFFSET, 160, &symbol_plane_hit[0].sym[0],
	               FACTION_PLAYER2);

	for (i = 6, h=165; i; --i, h += 5) {
		Vid_PlotPixel(280+X_OFFSET, h, 2);
	}
}

void swtitlf(void)
{
	if (titleflg) {
		return;
	}

	sound(0, 0, NULL);
	swsound();
}

bool ctlbreak(void)
{
	return Vid_GetCtrlBreak();
}

// clear bottom of screen
void clrprmpt(void)
{
	int x, y;

	for (y = 0; y <= 43; ++y)
		for (x = 0; x < SCR_WDTH; ++x) {
			Vid_PlotPixel(x, y, 0);
		}

	swposcur(0, 20);
}

static void TitleScreenBackground(void *unused)
{
	swtitln();
	clrprmpt();
}

static bool gethost(void)
{
	static char addrbuf[50];

	clrprmpt();

	swputs("Enter Remote Hostname/IP:\n");
	swgets(addrbuf, sizeof(addrbuf) - 3);
	asynhost = addrbuf;

	return strcmp(addrbuf, "") != 0;
}

#ifdef TCPIP
// network menu
static enum menu_action getnet(const struct menuitem *item)
{
	for (;;) {
		swtitln();
		clrprmpt();
		swputs("Key: L - listen for connection\n");
		swputs("     C - connect to remote host\n");

		Vid_ShowTouchKeys("LC");
		Vid_Update();

		swsndupdate();

		if (ctlbreak()) {
			swend(NULL, false);
		}

		switch (toupper(swgetc() & 0xff)) {
		case 'L':
			playmode = PLAYMODE_ASYNCH;
			asynmode = ASYN_LISTEN;
			return MENU_ACTION_RETURN;
		case 'C':
			if (gethost()) {
				playmode = PLAYMODE_ASYNCH;
				asynmode = ASYN_CONNECT;
				return MENU_ACTION_RETURN;
			} else {
				return MENU_ACTION_NONE;
			}
		case 27:
			return MENU_ACTION_NONE;
		}
	}
}
#endif

// sdh: get single player skill level
static enum menu_action getskill(const struct menuitem *item)
{
	for (;;) {
		swtitln();
		clrprmpt();
		swputs("Key: N - novice player\n");
		swputs("     E - expert player\n");

		Vid_ShowTouchKeys("NE");
		Vid_Update();

		swsndupdate();
		if (ctlbreak()) {
			swend(NULL, false);
		}
		switch (toupper(swgetc() & 0xff)) {
		case 'N':
			playmode = PLAYMODE_NOVICE;
			return MENU_ACTION_RETURN;
		case 'E':
			playmode = PLAYMODE_SINGLE;
			return MENU_ACTION_RETURN;
		case 27:
			return MENU_ACTION_NONE;
		}
	}
}

static enum menu_action StartVsComputer(const struct menuitem *item)
{
	playmode = PLAYMODE_COMPUTER;
	return MENU_ACTION_RETURN;
}

// TODO: This function is unnecessary.
static enum menu_action OpenOptionsMenu(const struct menuitem *item)
{
	setconfig();
	return MENU_ACTION_NONE;
}

#ifdef __EMSCRIPTEN__
static enum menu_action OpenManual(const struct menuitem *item)
{
	emscripten_run_script("openManual()");
	return MENU_ACTION_NONE;
}

static enum menu_action InstallApp(const struct menuitem *item)
{
	emscripten_run_script("promptForInstall()");
	return MENU_ACTION_NONE;
}
#endif

#ifndef NO_EXIT
static enum menu_action QuitGame(const struct menuitem *item)
{
	exit(0);
	return MENU_ACTION_NONE;
}
#endif

static const struct menuitem main_menu_items[] = {
	{'S', "single player", getskill},
	{'C', "single player versus computer", StartVsComputer},
#ifdef TCPIP
	{'N', "network game", getnet},
#endif
	{'O', "game options", OpenOptionsMenu},
#ifdef __EMSCRIPTEN__
	{'M', "open manual", OpenManual},
	{'I', "install as app", InstallApp},
#endif
#ifndef NO_EXIT
	{'Q', "quit game", QuitGame},
#endif
	{0, NULL},
};

static const struct menu main_menu = {
	TitleScreenBackground, NULL,
	main_menu_items,
};

void getgamemode(void)
{
	RunMenu(&main_menu);
}

//
// 2003-02-14: Code was checked into version control; no further entries
// will be added to this log.
//
// sdh 14/2/2003: change license header to GPL
// sdh 27/06/2002: move to new sopsym_t for symbols,
//                 remove references to symwdt, symhgt
// sdh 28/04/2002: Centering of title screen on non-320 pixel wide screens
// sdh 26/03/2002: change CGA_ to Vid_
// sdh 16/11/2001: TCPIP #define to disable TCP/IP support
// sdh 29/10/2001: moved options menu into swconf.c
// sdh 29/10/2001: harrykeys
// sdh 28/10/2001: rearranged title/menu stuff a bit
//                 added options menu
// sdh 21/10/2001: rearranged headers, added cvs tags
// sdh 19/10/2001: moved all the menus here
// sdh 19/10/2001: removed extern definitions, these are now in headers
// sdh 18/10/2001: converted all functions to ANSI-style arguments
// sdh ??/10/2001: added #define to control whether we use the classic
//                 title screen or the "network edition" one
//
// 2000-10-29      Copyright update.
// 99-01-24        1999 copyright.
// 96-12-27        New network version.
// 87-04-01        Version 7.F15
// 87-03-11        Title reformatting.
// 87-03-10        Microsoft compiler.
// 84-02-02        Development
//
