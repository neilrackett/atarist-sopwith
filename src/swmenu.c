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
// Menu system for main menu and options menus
//

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "timer.h"
#include "pcsound.h"
#include "video.h"

#include "sw.h"
#include "swconf.h"
#include "swend.h"
#include "swmenu.h"
#include "swsound.h"
#include "swtext.h"
#include "swtitle.h"
#include "swmain.h"

static const char menukeys[] = "1234567890ABCDEFGHIJKL";

static void ChangeKeyBinding(const struct menuitem *item)
{
	const struct conf_option *opt;
	int key;

	Vid_ClearBuf();

	swcolor(3);
	swposcur(10, 5);
	swputs("Press the new key for: ");

	swcolor(2);
	swposcur(14, 7);
	swputs(item->description);

	swcolor(1);
	swposcur(1, 22);
	swputs("   ESC - Cancel");

	Vid_Update();

	while ((key = Vid_GetKey()) == 0) {
		Timer_Sleep(50);

		swsndupdate();
		if (ctlbreak()) {
			return;
		}
	}

	if (!strcasecmp(Vid_KeyName(key), "Escape")) {
		return;
	}

	opt = ConfOptionByName(item->config_name);
	if (opt == NULL) {
		return;
	}
	*opt->value.i = key;
}

static void DrawMenu(const char *title, const struct menuitem *menu)
{
	int i, y, keynum, said_key = 0;
	int title_len = strlen(title), x = 19 - title_len / 2;
	char buttons[32];
	int num_buttons = 0;

	Vid_ClearBuf();

	// Planes on the menus look decorative but serve a real
	// purpose: they give you an impression of what the game will
	// look like under the different palettes.
	Vid_DispSymbol(x * 8 - 32, SCR_HGHT - 10,
	               &symbol_plane[0].sym[0], FACTION_PLAYER1);
	Vid_DispSymbol((x + title_len) * 8 + 16, SCR_HGHT - 10,
	               &symbol_plane[0].sym[6], FACTION_PLAYER2);

	swcolor(2);
	swposcur(x, 2);
	swputs(title);

	swcolor(3);

	for (i=0, y=0, keynum=0; menu[i].config_name != NULL; ++i, ++y) {
		const struct conf_option *opt;
		char *suffix;
		char buf[40];
		int key;

		if (strlen(menu[i].config_name) == 0) {
			continue;
		}

		if (menu[i].config_name[0] == '>') {
			key = menu[i].config_name[1];
			swcolor(1);
			suffix = " >>>";
		} else {
			key = menukeys[keynum];
			++keynum;
			suffix = ":";

			if (!said_key) {
				swposcur(0, 5+y);
				swputs("Key:");
				said_key = 1;
			}
		}
		snprintf(buf, sizeof(buf), "%c - %s%s",
		         key, menu[i].description, suffix);

		swposcur(5, 5+y);
		swputs(buf);
		swcolor(3);

		if (strlen(buf) > 22) {
			++y;
		}

		if (num_buttons < sizeof(buttons) - 1) {
			buttons[num_buttons] = key;
			++num_buttons;
		}

		swposcur(28, 5+y);
		opt = ConfOptionByName(menu[i].config_name);
		if (opt == NULL) {
			continue;
		}
		switch (opt->type) {
		case CONF_BOOL:
			swputs(*opt->value.b ? "on" : "off");
			break;
		case CONF_INT:
			if(!strcasecmp(opt->name, "conf_video_palette")) {
				swputs(Vid_GetVideoPaletteName(*opt->value.i));
			}
			break;
		case CONF_KEY:
			swputs(Vid_KeyName(*opt->value.i));
			break;
		default:
			break;
		}
	}

	buttons[num_buttons] = '\0';
	Vid_ShowTouchKeys(buttons);

	swcolor(1);

	swposcur(1, 22);
	swputs("   ESC - Exit Menu");

	Vid_Update();
}

static const struct menuitem *MenuItemForKey(const struct menuitem *menu,
                                             int key)
{
	int i, keynum;

	for (i=0, keynum=0; menu[i].config_name != NULL; ++i) {
		int itemkey;
		if (strlen(menu[i].config_name) == 0) {
			continue;
		} else if (menu[i].config_name[0] == '>') {
			itemkey = menu[i].config_name[1];
		} else {
			itemkey = menukeys[keynum];
			++keynum;
		}
		if (key == itemkey) {
			return &menu[i];
		}
	}

	return NULL;
}

// Present the given menu to the user. Returns zero if escape was pushed
// to exit the menu, or if a >jump item was chosen, it returns the key
// binding associated with it.
int RunMenu(const char *title, const struct menuitem *menu)
{
	const struct menuitem *pressed;
	const struct conf_option *opt;
	int key;

	for (;;) {
		DrawMenu(title, menu);

		if (ctlbreak()) {
			swend(NULL, false);
		}

		key = toupper(swgetc() & 0xff);
		if (key == 27) {
			return 0;
		}

		// check if a number has been pressed for a menu option
		pressed = MenuItemForKey(menu, key);
		if (pressed == NULL) {
			continue;
		}

		if (pressed->config_name[0] == '>') {
			return pressed->config_name[1];
		}

		opt = ConfOptionByName(pressed->config_name);
		if (opt == NULL) {
			continue;
		}
		switch (opt->type) {
		case CONF_BOOL:
			*opt->value.b = !*opt->value.b;
			break;
		case CONF_INT:
			if(!strcasecmp(opt->name, "conf_video_palette")) {
				*opt->value.i = (*opt->value.i + 1) % Vid_GetNumVideoPalettes();
				Vid_SetVideoPalette(*opt->value.i);
			}
			break;
		case CONF_KEY:
			ChangeKeyBinding(pressed);
			break;
		default:
			break;
		}

		// reset the screen if we need to
		if (opt->value.b == &vid_fullscreen) {
			Vid_Reset();
		}

		swsaveconf();
	}
}
