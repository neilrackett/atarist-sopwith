//
// Copyright(C) 2001-2005 Simon Howard
//
// You can redistribute and/or modify this program under the terms of the
// GNU General Public License version 2 as published by the Free Software
// Foundation, or any later version. This program is distributed WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.
//
//
// Functions for handling the "touch area" that contains controls for
// playing on a touch screen.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "video.h"
#include "sw.h"
#include "swinit.h"
#include "swmain.h"
#include "swtext.h"

#define MARGIN 4
#define BUTTON_WIDTH  87
#define BUTTON_HEIGHT 32

static const struct touch_button game_buttons[] = {
	{TOUCH_BUTTON_GAME_KEY, " \x18",     4,  2, KEY_PULLDOWN},
	{TOUCH_BUTTON_LABEL,    "Stick",     3,  5},
	{TOUCH_BUTTON_GAME_KEY, " \x19",     4,  8, KEY_PULLUP},
	{TOUCH_BUTTON_GAME_KEY, "Flip",      4, 12, KEY_FLIP},

	{TOUCH_BUTTON_GAME_KEY, " \x18",    18,  2, KEY_ACCEL},
	{TOUCH_BUTTON_LABEL,    "Throttle", 16,  5},
	{TOUCH_BUTTON_GAME_KEY, " \x19",    18,  8, KEY_DECEL},
	{TOUCH_BUTTON_GAME_KEY, "Home",     18, 12, KEY_HOME},

	/*
	{TOUCH_BUTTON_GAME_KEY, "Missile",  20,  7, KEY_MISSILE},
	{TOUCH_BUTTON_GAME_KEY, "Flare",    20, 10, KEY_STARBURST},
	*/

	{TOUCH_BUTTON_CLOSE,    "Close",    32,  2},
	{TOUCH_BUTTON_GAME_KEY, "Fire",     32,  8, KEY_FIRE},
	{TOUCH_BUTTON_GAME_KEY, "Bomb",     32, 12, KEY_BOMB},

	{TOUCH_BUTTON_END},
};

static void GetButtonPos(const struct touch_button *b, int *x, int *y)
{
	*x = b->x * 8 - 28;
	*y = b->y * 8 - 12;
}

static void DrawTouchArea(const struct touch_button *buttons)
{
	int i, cx;

	Vid_Box(0, SCR_HGHT - 3 - MARGIN,
	        SCR_WDTH, TOUCH_AREA_HEIGHT - MARGIN - 3, 0);

	for (i = 0; buttons[i].type != TOUCH_BUTTON_END; i++) {
		const struct touch_button *b = &buttons[i];

		cx = b->x;

		if (b->type == TOUCH_BUTTON_LABEL) {
			swcolor(3);
		} else {
			int x, y;
			bool pressed = i < arrlen(button_pressed)
			            && button_pressed[i];
			GetButtonPos(b, &x, &y);
			Vid_Box(x, SCR_HGHT - y,
			        BUTTON_WIDTH, BUTTON_HEIGHT, 3);
			Vid_Box(x + 1, SCR_HGHT - y - 1,
			        BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2,
			        pressed ? 2 : 1);
			swcolor(0);
		}

		swposcur(cx, b->y);
		swputs(b->label);
	}
}

const struct touch_button *Vid_GetTouchButton(int x, int y)
{
	int i, bx, by;

	for (i = 0; game_buttons[i].type != TOUCH_BUTTON_END; i++) {
		const struct touch_button *b = &game_buttons[i];
		GetButtonPos(b, &bx, &by);

		if (b->type != TOUCH_BUTTON_LABEL
		 && x >= bx && y >= by
		 && x < bx + BUTTON_WIDTH && y < by + BUTTON_HEIGHT) {
			return b;
		}
	}

	return NULL;
}

void Vid_TouchButtonPress(const struct touch_button *b, bool pressed)
{
	int idx = b - game_buttons;
	if (idx >= 0 && idx < arrlen(button_pressed)) {
		button_pressed[idx] = pressed;
	}
}

void Vid_DrawTouchControls(void)
{
	DrawTouchArea(game_buttons);
}
