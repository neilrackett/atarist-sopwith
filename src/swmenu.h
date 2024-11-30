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

struct menuitem {
	char key;
	char *label;
	void (*callback)(const struct menuitem *item);
	const void *user_data;
};

struct menu {
	void (*draw_background)(void *);
	void *draw_background_data;
	const struct menuitem *items;
};

void RunMenu(const struct menu *menu);
void SubMenu(const struct menuitem *item);
void FullscreenBackground(void *title);
void ToggleConfigOption(const struct menuitem *item);

#define CONFIG_OPTION(label, config_name) \
	{'1', label, ToggleConfigOption, config_name}
