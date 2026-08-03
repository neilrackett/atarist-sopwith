/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>

#include <stdl/stdl.h>

#include "sw.h"
#include "video.h"

/* STDL_GetJoyState() reports the IKBD joystick 1 byte directly, with
   the same bit layout the native backend's IKBD handler used, so the
   whole interrupt handler from src/atari/controller.c disappears. */
#define JOY_UP    0x01
#define JOY_DOWN  0x02
#define JOY_LEFT  0x04
#define JOY_RIGHT 0x08
#define JOY_FIRE  0x80

static uint8_t prev_joy;
static enum menukey pending_menukey = MENUKEY_NONE;

void JoystickInit(void)
{
	/* STDL installs the IKBD handler in STDL_SetVideoMode and
	   reports joystick packets through the same queue as keys, and
	   takes the vector back in STDL_Quit - so there is nothing to
	   install here and no JoystickExit counterpart. */
	prev_joy = STDL_GetJoyState();
}

/* Called each game tick from PollInput(). Translates the current
   joystick state into game key flags using KEYDOWN_GAMEPAD
   (level-triggered), and menu keys on the leading edge. */
void PollJoystick(void)
{
	uint8_t joy = STDL_GetJoyState();
	uint8_t newly = (uint8_t)(joy & ~prev_joy);

	prev_joy = joy;

	if (newly & JOY_UP)
		pending_menukey = MENUKEY_UP;
	else if (newly & JOY_DOWN)
		pending_menukey = MENUKEY_DOWN;
	else if (newly & JOY_FIRE)
		pending_menukey = MENUKEY_START;

	if (joy & JOY_UP)
		keysdown[KEY_PULLUP] |= KEYDOWN_GAMEPAD;
	else
		keysdown[KEY_PULLUP] &= ~KEYDOWN_GAMEPAD;

	if (joy & JOY_DOWN)
		keysdown[KEY_PULLDOWN] |= KEYDOWN_GAMEPAD;
	else
		keysdown[KEY_PULLDOWN] &= ~KEYDOWN_GAMEPAD;

	if (joy & JOY_LEFT)
		keysdown[KEY_DECEL] |= KEYDOWN_GAMEPAD;
	else
		keysdown[KEY_DECEL] &= ~KEYDOWN_GAMEPAD;

	if (joy & JOY_RIGHT)
		keysdown[KEY_ACCEL] |= KEYDOWN_GAMEPAD;
	else
		keysdown[KEY_ACCEL] &= ~KEYDOWN_GAMEPAD;

	if (joy & JOY_FIRE)
		keysdown[KEY_FIRE] |= KEYDOWN_GAMEPAD;
	else
		keysdown[KEY_FIRE] &= ~KEYDOWN_GAMEPAD;
}

enum menukey Vid_ControllerMenuKey(void)
{
	enum menukey result = pending_menukey;
	pending_menukey = MENUKEY_NONE;
	return result;
}

bool Vid_HaveController(void)
{
	return true;
}

const char *Vid_ControllerButtonName(enum gamekey key)
{
	switch (key)
	{
	case KEY_PULLUP:   return "Up";
	case KEY_PULLDOWN: return "Down";
	case KEY_ACCEL:    return "Right";
	case KEY_DECEL:    return "Left";
	case KEY_FIRE:     return "Fire";
	default:           return NULL;
	}
}
