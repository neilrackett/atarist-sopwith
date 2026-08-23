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

/* No binding, matching what SDL_CONTROLLER_BUTTON_INVALID does for the
   SDL backend. Zero cannot serve: it is a real input. */
#define BIND_NONE (-1)

/* Which STDL input drives each game key. The first five exist on any
   ST joystick; the rest need a controller, and read as unheld without
   one, so this table is correct either way.

   Directions keep the port's existing feel - up is up, and left and
   right are the throttle - rather than following the SDL backend,
   which uses the d-pad for pitch because it puts the throttle on the
   face buttons. Fire stays on the fire button, so a plain joystick is
   unaffected by any of this.

   Overridable through the controller_* keys in the config file, the
   same as every other backend. */
int controller_bindings[NUM_KEYS] = {
	BIND_NONE,          /* KEY_UNKNOWN   */
	STDL_JOYKEY_UP,     /* KEY_PULLUP    */
	STDL_JOYKEY_DOWN,   /* KEY_PULLDOWN  */
	STDL_JOYKEY_TL,     /* KEY_FLIP      */
	STDL_JOYKEY_EAST,   /* KEY_BOMB      */
	STDL_JOYKEY_FIRE,   /* KEY_FIRE      */
	STDL_JOYKEY_START,  /* KEY_HOME      */
	STDL_JOYKEY_WEST,   /* KEY_MISSILE   */
	STDL_JOYKEY_NORTH,  /* KEY_STARBURST */
	STDL_JOYKEY_RIGHT,  /* KEY_ACCEL     */
	STDL_JOYKEY_LEFT,   /* KEY_DECEL     */
	STDL_JOYKEY_SELECT, /* KEY_SOUND     */
};

static uint8_t prev_joy;
static int prev_back;
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
	int back = STDL_JoyInputHeld(STDL_JOYKEY_EAST);
	int i;

	prev_joy = joy;

	/* Menu navigation, on the leading edge. The pad's B button backs
	   out, which is what a pad player will try first; in the air it
	   drops a bomb, but menus and flying are different moments. */
	if (newly & JOY_UP)
		pending_menukey = MENUKEY_UP;
	else if (newly & JOY_DOWN)
		pending_menukey = MENUKEY_DOWN;
	else if (newly & JOY_FIRE)
		pending_menukey = MENUKEY_START;
	else if (back && !prev_back)
		pending_menukey = MENUKEY_BACK;

	prev_back = back;

	/* Game keys, level triggered. Every binding is polled the same
	   way, so a joystick reaches the five it has and a controller
	   reaches the lot without either being a special case. */
	for (i = KEY_UNKNOWN + 1; i < NUM_KEYS; i++) {
		int input = controller_bindings[i];

		if (input < 0)
			continue;

		if (STDL_JoyInputHeld(input))
			keysdown[i] |= KEYDOWN_GAMEPAD;
		else
			keysdown[i] &= ~KEYDOWN_GAMEPAD;
	}
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
	/* Indexed by STDL input, so a rebind through the config file
	   renames itself. Face buttons carry their Xbox letters, which
	   are positions: STDL reports where a button sits, not what is
	   printed on it, so B is the right-hand one on any pad. */
	static const char *const names[STDL_JOYKEY_COUNT] = {
		"Up", "Down", "Left", "Right", "Fire",
		"B", "X", "Y", "L", "R", "LT", "RT",
		"Select", "Start", "Guide", "L3", "R3"
	};
	int input;

	if (key <= KEY_UNKNOWN || key >= NUM_KEYS)
		return NULL;

	input = controller_bindings[key];
	if (input < 0 || input >= STDL_JOYKEY_COUNT)
		return NULL;

	/* Without a controller only the joystick's five are reachable,
	   so do not offer the player a button they have not got. */
	if (input > STDL_JOYKEY_FIRE && !STDL_HavePad())
		return NULL;

	return names[input];
}
