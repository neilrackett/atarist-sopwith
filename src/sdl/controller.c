//
// Copyright(C) 2024 Simon Howard
//
// You can redistribute and/or modify this program under the terms of the
// GNU General Public License version 2 as published by the Free Software
// Foundation, or any later version. This program is distributed WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.
//
//
// SDL game controller
//

#include <SDL.h>

#include "sw.h"
#include "video.h"

int controller_bindings[NUM_KEYS] = {
	SDL_CONTROLLER_BUTTON_INVALID,        // KEY_UNKNOWN
	SDL_CONTROLLER_BUTTON_DPAD_DOWN,      // KEY_PULLUP
	SDL_CONTROLLER_BUTTON_DPAD_UP,        // KEY_PULLDOWN
	SDL_CONTROLLER_BUTTON_A,              // KEY_FLIP
	SDL_CONTROLLER_BUTTON_LEFTSHOULDER,   // KEY_BOMB
	SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,  // KEY_FIRE
	SDL_CONTROLLER_BUTTON_START,          // KEY_HOME
	SDL_CONTROLLER_BUTTON_INVALID,        // KEY_MISSILE
	SDL_CONTROLLER_BUTTON_INVALID,        // KEY_STARBURST
	SDL_CONTROLLER_BUTTON_Y,              // KEY_ACCEL
	SDL_CONTROLLER_BUTTON_B,              // KEY_DECEL
	SDL_CONTROLLER_BUTTON_INVALID,        // KEY_SOUND
};

static int button_to_key[SDL_CONTROLLER_BUTTON_MAX];

static SDL_GameController *controller = NULL;
static SDL_JoystickID controller_id;

static void RecalculateButtonMapping(void)
{
	int i;

	for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
		button_to_key[i] = KEY_UNKNOWN;
	}
	for (i = 0; i < NUM_KEYS; i++) {
		int button = controller_bindings[i];
		if (button != SDL_CONTROLLER_BUTTON_INVALID) {
			button_to_key[button] = i;
		}
	}
}

void Vid_ControllerButtonDown(SDL_ControllerButtonEvent *event)
{
	int gamekey;

	if (controller != NULL && event->which != controller_id) {
		return;
	}
	if (event->button >= SDL_CONTROLLER_BUTTON_MAX) {
		return;
	}
	gamekey = button_to_key[event->button];
	if (gamekey != KEY_UNKNOWN) {
		keysdown[gamekey] |= KEYDOWN_GAMEPAD | KEYDOWN_WAS_PRESSED;
	}
}

void Vid_ControllerButtonUp(SDL_ControllerButtonEvent *event)
{
	int gamekey;

	if (controller != NULL && event->which != controller_id) {
		return;
	}
	if (event->button >= SDL_CONTROLLER_BUTTON_MAX) {
		return;
	}
	gamekey = button_to_key[event->button];
	if (gamekey != KEY_UNKNOWN) {
		keysdown[gamekey] &= ~KEYDOWN_GAMEPAD;
	}
}

void Vid_ControllerAdded(SDL_ControllerDeviceEvent *event)
{
	if (controller != NULL) {
		return;
	}
	controller = SDL_GameControllerOpen(event->which);
	controller_id = SDL_JoystickInstanceID(
		SDL_GameControllerGetJoystick(controller));
}

void Vid_ControllerRemoved(SDL_ControllerDeviceEvent *event)
{
	if (controller == NULL || event->which != controller_id) {
		return;
	}
	SDL_GameControllerClose(controller);
	controller = NULL;
}

void Vid_ControllerInit(void)
{
	SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
	// SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
	RecalculateButtonMapping();
}
