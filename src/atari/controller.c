#include <stdint.h>

#include <mint/osbind.h>
#include <mint/ostruct.h>

#include "sw.h"
#include "video.h"

/* Joystick packet bit definitions (byte 1 of IKBD joystick event):
   Bit 0 = up, bit 1 = down, bit 2 = left, bit 3 = right, bit 7 = fire. */
#define JOY_UP    0x01
#define JOY_DOWN  0x02
#define JOY_LEFT  0x04
#define JOY_RIGHT 0x08
#define JOY_FIRE  0x80

/* Current joystick state, updated by IKBD interrupt handler. */
static volatile uint8_t joy_state = 0;

/* Last menu key event detected by the interrupt handler. */
static volatile enum menukey pending_menukey = MENUKEY_NONE;

/* Previous joyvec, restored on exit. */
static void (*old_joyvec)(void *) = NULL;

/* IKBD joystick event handler. Called at interrupt level with a pointer
   to a 2-byte packet: byte 0 = 0xFE (joystick 0) or 0xFF (joystick 1),
   byte 1 = direction/fire state. We only care about joystick 1.
   Since IKBD sends a packet on every state change, transitions are
   edge-triggered naturally — we queue one menu key per movement. */
static void joy_handler(void *packet)
{
	uint8_t *p = (uint8_t *)packet;
	uint8_t prev, cur;

	if (p[0] != 0xFF)
	{
		return;
	}

	prev = joy_state;
	cur  = p[1];
	joy_state = cur;

	/* Only queue a menu key on the leading edge (bit newly set). */
	if ((cur & ~prev) & JOY_UP)
		pending_menukey = MENUKEY_UP;
	else if ((cur & ~prev) & JOY_DOWN)
		pending_menukey = MENUKEY_DOWN;
	else if ((cur & ~prev) & JOY_FIRE)
		pending_menukey = MENUKEY_START;
}

/* Enable joystick reporting and install our handler.
   Called once at startup (already in supervisor mode). */
void JoystickInit(void)
{
	static const uint8_t joy_on[] = {0x14}; /* IKBD cmd: enable joystick event reporting */
	_KBDVECS *vecs = (_KBDVECS *)Kbdvbase();
	old_joyvec = vecs->joyvec;
	vecs->joyvec = joy_handler;
	Ikbdws(0, (void *)joy_on);
}

/* Restore the original joyvec and disable joystick reporting.
   Called at exit (already in supervisor mode via atexit while still super). */
void JoystickExit(void)
{
	static const uint8_t joy_off[] = {0x15}; /* IKBD cmd: disable joystick event reporting */
	_KBDVECS *vecs;

	Ikbdws(0, (void *)joy_off);
	vecs = (_KBDVECS *)Kbdvbase();
	if (old_joyvec != NULL)
	{
		vecs->joyvec = old_joyvec;
		old_joyvec = NULL;
	}
}

/* Called each game tick from PollInput(). Translates current joystick
   state into game key flags using KEYDOWN_GAMEPAD (level-triggered,
   independent of the keyboard key-expiry system). */
void PollJoystick(void)
{
	uint8_t joy = joy_state;

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
