/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Sopwith video backend built on STDL (Atari ST DirectMedia Layer).
 *
 * Deliberate rule for this file: it may only use the *public* STDL
 * API. There is no raw planar arithmetic, no inline asm and nothing
 * lifted from src/atari/video.c, so a benchmark against the native
 * backend measures the library rather than a differently-named copy
 * of the same hand-written code.
 *
 * Two coordinate conventions meet here:
 *   - Sopwith draws with y = 0 at the BOTTOM of the screen (CGA
 *     origin), except Vid_DrawChar which is given text cells in
 *     top-down screen order.
 *   - STDL surfaces are top-down.
 * SY() converts; everything else is straight STDL calls.
 *
 * Symbols are byte-per-pixel 4-colour images recoloured per faction
 * at draw time. STDL has no blit-time colour remap, so each
 * (symbol, faction) pair is converted once into an STDL sprite and
 * cached (see SymSprite below).
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdl/stdl.h>

#include "atari_keys.h"
#include "font.h"
#include "sw.h"
#include "swinit.h"
#include "swmain.h"
#include "timer.h"
#include "video.h"

#define INPUT_BUFFER_LEN 32
#define SBAR_HGHT        19

/* Sopwith y = 0 is the bottom row; STDL surfaces are top-down. */
#define SY(y) (SCR_HGHT - 1 - (y))

/*
 * Cap on RAM handed to cached symbol sprites. Sopwith already
 * spends a 156KB arena on the byte-per-pixel symbol masters, so the
 * planar copies have to stay bounded to keep the stock 520ST fit.
 * Past the cap Vid_DispSymbol falls back to a per-pixel draw.
 */
#define SPRITE_BUDGET  49152
#define SPRITE_CACHE_SIZE 257    /* prime: open addressing */

struct input_event
{
	int key;
	int ch;
};

struct palette
{
	const char *name;
	short colors[16];
};

/* Identical to the native backend's tables: ST hardware colour
   words, written straight to the shifter by STDL_SetColour. */
static const struct palette video_palettes[] = {
	{"Atari",       /* Originally planned Atari port colors from swgrapha.c */
	 {0x000, 0x007, 0x700, 0x777,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"CGA 1",       /* CGA black, cyan, magenta, white */
	 {0x000, 0x077, 0x707, 0x777,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"CGA 2",       /* CGA black, green, red, yellow */
	 {0x000, 0x070, 0x700, 0x770,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"CGA 3",       /* CGA black, cyan, red, white (mode 5) */
	 {0x000, 0x077, 0x700, 0x777,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"Mono Amber",
	 {0x000, 0x750, 0x730, 0x771,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"Mono Green",
	 {0x000, 0x071, 0x061, 0x172,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"Mono Grey",
	 {0x000, 0x666, 0x555, 0x777,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"Tosh LCD 1",
	 {0x674, 0x454, 0x335, 0x006,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"Tosh LCD 2",
	 {0x006, 0x335, 0x454, 0x674,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"Tosh LCD 3",
	 {0x343, 0x233, 0x223, 0x123,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"IBM LCD",
	 {0x344, 0x233, 0x222, 0x111,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"Tandy LCD",
	 {0x253, 0x143, 0x132, 0x022,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"Gas Plasma",
	 {0x300, 0x620, 0x510, 0x720,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
	{"Muted",
	 {0x000, 0x366, 0x626, 0x666,
	  0x700, 0x070, 0x770, 0x555,
	  0x333, 0x007, 0x070, 0x077,
	  0x700, 0x707, 0x770, 0x777}},
};

/* symbol pixel value (0-3) -> palette index, per faction */
static const uint8_t color_mappings[][4] = {
	{0, 3, 3, 3},
	{0, 1, 2, 3},
	{0, 2, 1, 3},
	{0, 1, 3, 2},
	{0, 2, 3, 1},
	{0, 3, 1, 2},
	{0, 3, 2, 1},
	{0, 1, 1, 3},
	{0, 2, 2, 3},
};

static const char *const key_names[128] = {
	[ATARI_SCANCODE_ESCAPE] = "Escape",
	[ATARI_SCANCODE_1] = "1",
	[ATARI_SCANCODE_2] = "2",
	[ATARI_SCANCODE_3] = "3",
	[ATARI_SCANCODE_4] = "4",
	[ATARI_SCANCODE_5] = "5",
	[ATARI_SCANCODE_6] = "6",
	[ATARI_SCANCODE_7] = "7",
	[ATARI_SCANCODE_8] = "8",
	[ATARI_SCANCODE_9] = "9",
	[ATARI_SCANCODE_0] = "0",
	[ATARI_SCANCODE_MINUS] = "-",
	[ATARI_SCANCODE_EQUALS] = "=",
	[ATARI_SCANCODE_BACKSPACE] = "Backspace",
	[ATARI_SCANCODE_TAB] = "Tab",
	[ATARI_SCANCODE_Q] = "Q",
	[ATARI_SCANCODE_W] = "W",
	[ATARI_SCANCODE_E] = "E",
	[ATARI_SCANCODE_R] = "R",
	[ATARI_SCANCODE_T] = "T",
	[ATARI_SCANCODE_Y] = "Y",
	[ATARI_SCANCODE_U] = "U",
	[ATARI_SCANCODE_I] = "I",
	[ATARI_SCANCODE_O] = "O",
	[ATARI_SCANCODE_P] = "P",
	[ATARI_SCANCODE_LBRACKET] = "[",
	[ATARI_SCANCODE_RBRACKET] = "]",
	[ATARI_SCANCODE_RETURN] = "Return",
	[ATARI_SCANCODE_CTRL] = "Control",
	[ATARI_SCANCODE_A] = "A",
	[ATARI_SCANCODE_S] = "S",
	[ATARI_SCANCODE_D] = "D",
	[ATARI_SCANCODE_F] = "F",
	[ATARI_SCANCODE_G] = "G",
	[ATARI_SCANCODE_H] = "H",
	[ATARI_SCANCODE_J] = "J",
	[ATARI_SCANCODE_K] = "K",
	[ATARI_SCANCODE_L] = "L",
	[ATARI_SCANCODE_SEMICOLON] = ";",
	[ATARI_SCANCODE_APOSTROPHE] = "'",
	[ATARI_SCANCODE_GRAVE] = "`",
	[ATARI_SCANCODE_LSHIFT] = "Left Shift",
	[ATARI_SCANCODE_BACKSLASH] = "\\",
	[ATARI_SCANCODE_Z] = "Z",
	[ATARI_SCANCODE_X] = "X",
	[ATARI_SCANCODE_C] = "C",
	[ATARI_SCANCODE_V] = "V",
	[ATARI_SCANCODE_B] = "B",
	[ATARI_SCANCODE_N] = "N",
	[ATARI_SCANCODE_M] = "M",
	[ATARI_SCANCODE_COMMA] = ",",
	[ATARI_SCANCODE_PERIOD] = ".",
	[ATARI_SCANCODE_SLASH] = "/",
	[ATARI_SCANCODE_RSHIFT] = "Right Shift",
	[ATARI_SCANCODE_ALT] = "Alternate",
	[ATARI_SCANCODE_SPACE] = "Space",
	[ATARI_SCANCODE_CAPSLOCK] = "Caps Lock",
	[ATARI_SCANCODE_F1] = "F1",
	[ATARI_SCANCODE_F2] = "F2",
	[ATARI_SCANCODE_F3] = "F3",
	[ATARI_SCANCODE_F4] = "F4",
	[ATARI_SCANCODE_F5] = "F5",
	[ATARI_SCANCODE_F6] = "F6",
	[ATARI_SCANCODE_F7] = "F7",
	[ATARI_SCANCODE_F8] = "F8",
	[ATARI_SCANCODE_F9] = "F9",
	[ATARI_SCANCODE_F10] = "F10",
	[ATARI_SCANCODE_HOME] = "Home",
	[ATARI_SCANCODE_UP] = "Up",
	[ATARI_SCANCODE_LEFT] = "Left",
	[ATARI_SCANCODE_RIGHT] = "Right",
	[ATARI_SCANCODE_DOWN] = "Down",
	[ATARI_SCANCODE_INSERT] = "Insert",
	[ATARI_SCANCODE_DELETE] = "Delete",
	[ATARI_SCANCODE_HELP] = "Help",
	[ATARI_SCANCODE_UNDO] = "Undo",
};

/*
 * swstbar.c's minimap draws straight into the framebuffer under
 * PLATFORM_ATARI_TOS (one of the stock-520ST optimisations that
 * must not be degraded), so the backend still has to publish the
 * current draw page. STDL's screen surface has the same 320x200x4
 * layout and 160-byte stride, so the existing code works unchanged.
 */
uint8_t *vid_vram;
unsigned int vid_pitch = 160;

int keysdown[NUM_KEYS];
int controller_bindings[NUM_KEYS];
int keybindings[NUM_KEYS] = {
	0,
	',',
	'/',
	'.',
	'B',
	' ',
	'H',
	'V',
	'C',
	'X',
	'Z',
	'S',
};

bool vid_fullscreen = false;

static STDL_Surface *screen;
static STDL_Font game_font;

static struct input_event input_buffer[INPUT_BUFFER_LEN];
static int input_head;
static int input_tail;
static bool ctrlbreak;
static bool initted;
static bool text_input_mode;

static int active_palette;

extern bool isNetworkGame(void);
extern void PollJoystick(void);

static void PollInput(void);

/* --- symbol sprite cache -------------------------------------- */

/*
 * STDL sprites carry fixed colours, but Sopwith recolours every
 * symbol through color_mappings[faction] at draw time, so the cache
 * is keyed on (pixel data, faction). Symbols that share pixel data
 * (swsymbol.c dedupes identical source strings) share a sprite.
 */
struct sym_sprite
{
	const uint8_t *data;      /* sopsym_t::data - NULL = free slot */
	uint8_t        clr;
	uint8_t        failed;    /* conversion gave up; use slow path */
	STDL_Sprite   *spr;
};

static struct sym_sprite sprite_cache[SPRITE_CACHE_SIZE];
static unsigned long sprite_bytes;
static int sprite_count;

static STDL_Sprite *BuildSymSprite(const sopsym_t *sym, int clr)
{
	const uint8_t *map = color_mappings[clr];
	int padw = (sym->w + 15) & ~15;
	STDL_Surface *s;
	STDL_Sprite *spr;
	int y, gx;

	s = STDL_CreateSurface(padw, sym->h);
	if (s == NULL)
	{
		return NULL;
	}
	/* start fully transparent; only symbol pixels punch through */
	if (STDL_CreateMask(s, 1) < 0)
	{
		STDL_FreeSurface(s);
		return NULL;
	}

	for (y = 0; y < sym->h; ++y)
	{
		const uint8_t *src = sym->data + y * sym->w;

		for (gx = 0; gx < padw; gx += 8)
		{
			uint8_t planes[4] = {0, 0, 0, 0};
			uint8_t transmask = 0xFF;
			int i;

			for (i = 0; i < 8; ++i)
			{
				int px = gx + i;
				int ci, c;
				uint8_t bit;

				if (px >= sym->w)
				{
					break;
				}
				ci = src[px];
				if (ci == 0)
				{
					continue;
				}
				c = map[ci & 3];
				bit = (uint8_t)(0x80u >> i);
				transmask &= (uint8_t)~bit;
				if (c & 1) planes[0] |= bit;
				if (c & 2) planes[1] |= bit;
				if (c & 4) planes[2] |= bit;
				if (c & 8) planes[3] |= bit;
			}
			STDL_PutGroup8(s, gx, y, planes, transmask);
		}
	}

	/* key >= STDL_TRANSPARENT: keep the mask we just built */
	STDL_SetColourKey(s, 1, STDL_TRANSPARENT);
	spr = STDL_SpriteFromSurface(s, padw, 0);
	STDL_FreeSurface(s);
	return spr;
}

static STDL_Sprite *GetSymSprite(const sopsym_t *sym, int clr)
{
	unsigned long h = ((unsigned long)(size_t)sym->data >> 2)
	                  * 31u + (unsigned long)clr;
	int slot = (int)(h % SPRITE_CACHE_SIZE);
	int probes;

	for (probes = 0; probes < SPRITE_CACHE_SIZE; ++probes)
	{
		struct sym_sprite *e = &sprite_cache[slot];

		if (e->data == NULL)
		{
			unsigned long cost;

			/* Rows are (w+15)/16 groups of 5 words (mask + 4
			   planes); close enough for the budget check. */
			cost = (unsigned long)(((sym->w + 15) >> 4) * 10)
			       * (unsigned long)sym->h + 32u;
			e->data = sym->data;
			e->clr = (uint8_t)clr;
			if (sprite_bytes + cost > SPRITE_BUDGET)
			{
				e->failed = 1;
				return NULL;
			}
			e->spr = BuildSymSprite(sym, clr);
			if (e->spr == NULL)
			{
				e->failed = 1;
				return NULL;
			}
			sprite_bytes += cost;
			++sprite_count;
			return e->spr;
		}
		if (e->data == sym->data && e->clr == clr)
		{
			return e->failed ? NULL : e->spr;
		}
		slot = (slot + 1) % SPRITE_CACHE_SIZE;
	}
	return NULL;
}

/* --- input ---------------------------------------------------- */

int Vid_GetGameKeys(void)
{
	int i;
	int c = 0;

	while (Vid_GetKey() != 0)
	{
	}

	if (Vid_GetCtrlBreak())
	{
		c |= K_BREAK;
	}
	if (keysdown[KEY_FLIP])
	{
		keysdown[KEY_FLIP] = 0;
		c |= K_FLIP;
	}
	if (keysdown[KEY_PULLUP])
	{
		c |= K_FLAPU;
	}
	if (keysdown[KEY_PULLDOWN])
	{
		c |= K_FLAPD;
	}
	if (keysdown[KEY_ACCEL])
	{
		c |= K_ACCEL;
	}
	if (keysdown[KEY_DECEL])
	{
		c |= K_DEACC;
	}
	if (keysdown[KEY_SOUND])
	{
		keysdown[KEY_SOUND] = 0;
		c |= K_SOUND;
	}
	if (keysdown[KEY_BOMB])
	{
		c |= K_BOMB;
	}
	if (keysdown[KEY_FIRE])
	{
		c |= K_SHOT;
	}
	if (keysdown[KEY_HOME])
	{
		c |= K_HOME;
	}
	if (keysdown[KEY_MISSILE])
	{
		keysdown[KEY_MISSILE] = 0;
		c |= K_MISSILE;
	}
	if (keysdown[KEY_STARBURST])
	{
		keysdown[KEY_STARBURST] = 0;
		c |= K_STARBURST;
	}

	for (i = 0; i < NUM_KEYS; ++i)
	{
		keysdown[i] &= ~KEYDOWN_WAS_PRESSED;
	}

	return c;
}

static inline void PushInputEvent(int key, int ch)
{
	int next = (input_head + 1) % INPUT_BUFFER_LEN;

	if (next == input_tail)
	{
		input_tail = (input_tail + 1) % INPUT_BUFFER_LEN;
	}

	input_buffer[input_head].key = key;
	input_buffer[input_head].ch = ch;
	input_head = next;
}

static inline struct input_event PopInputEvent(void)
{
	struct input_event result = {0, 0};

	if (input_head == input_tail)
	{
		return result;
	}

	result = input_buffer[input_tail];
	input_tail = (input_tail + 1) % INPUT_BUFFER_LEN;
	return result;
}

static bool GameKeyIsEdgeTriggered(enum gamekey key)
{
	switch (key)
	{
	case KEY_FLIP:
	case KEY_SOUND:
	case KEY_MISSILE:
	case KEY_STARBURST:
		return true;

	default:
		return false;
	}
}

static enum gamekey TranslateKeycode(int keycode)
{
	enum gamekey key;

	for (key = 0; key < NUM_KEYS; ++key)
	{
		if (keybindings[key] == keycode)
		{
			return key;
		}
	}

	return KEY_UNKNOWN;
}

/* Named HandleCtrlChord to match the SDL version where these required Ctrl
   to be held. On Atari ST, GEMDOS swallows Ctrl+Q (XON) before it reaches
   the application, so these are triggered by the plain key instead. */
static bool HandleCtrlChord(int scan)
{
	switch (scan)
	{
#ifndef NO_EXIT
	case ATARI_SCANCODE_C:
		ctrlbreak = true;
		return true;
#endif
	case ATARI_SCANCODE_R:
		if (!isNetworkGame())
		{
			gamenum = starting_level;
			swinitlevel();
			return true;
		}
		break;

	case ATARI_SCANCODE_Q:
		if (!isNetworkGame())
		{
			swrestart();
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

static int NormalizeKeycode(int scan, int ch)
{
	if (ch == '\r')
	{
		ch = '\n';
	}
	if (ch >= 'a' && ch <= 'z')
	{
		return ch - 32;
	}
	if (ch != 0)
	{
		return ch;
	}
	return 0x100 | scan;
}

/*
 * STDL delivers real key-up events (the native backend polls
 * GEMDOS, which only ever sees makes, and has to expire held keys
 * on a 120ms timer). Track the true key state instead.
 */
static void HandleGameKey(int keycode, bool pressed)
{
	enum gamekey key = TranslateKeycode(keycode);

	if (key == KEY_UNKNOWN)
	{
		return;
	}

	if (!pressed)
	{
		keysdown[key] &= ~KEYDOWN_KEYBOARD;
		return;
	}

	if ((keysdown[key] & KEYDOWN_KEYBOARD) == 0)
	{
		keysdown[key] |= KEYDOWN_KEYBOARD | KEYDOWN_WAS_PRESSED;
	}
	else if (!GameKeyIsEdgeTriggered(key))
	{
		keysdown[key] |= KEYDOWN_WAS_PRESSED;
	}
}

static void PollInput(void)
{
	STDL_Event ev;

	while (STDL_PollEvent(&ev))
	{
		int ch, scan, keycode;

		switch (ev.type)
		{
		case STDL_KEYDOWN:
			scan = ev.key.keysym.scancode;
			ch = (int)(ev.key.keysym.unicode & 0xff);
			keycode = NormalizeKeycode(scan, ch);

			if (playmode != PLAYMODE_UNSET && HandleCtrlChord(scan))
			{
				continue;
			}
			if (ch == '\r')
			{
				ch = '\n';
			}
			PushInputEvent(keycode, ch);
			HandleGameKey(keycode, true);
			break;

		case STDL_KEYUP:
			scan = ev.key.keysym.scancode;
			ch = (int)(ev.key.keysym.unicode & 0xff);
			HandleGameKey(NormalizeKeycode(scan, ch), false);
			break;

		default:
			break;
		}
	}

	PollJoystick();
}

/* --- init / shutdown ------------------------------------------ */

static void ApplyPalette(void)
{
	int i;

	for (i = 0; i < 16; ++i)
	{
		STDL_SetColour(i, (uint16_t)video_palettes[active_palette].colors[i]);
	}
}

void Vid_Init(void)
{
	if (initted)
	{
		return;
	}

	if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_JOYSTICK) < 0)
	{
		ErrorExit("STDL_Init failed: %s", STDL_GetError());
	}

	screen = STDL_SetVideoMode(SCR_WDTH, SCR_HGHT, 4, STDL_DOUBLEBUF);
	if (screen == NULL)
	{
		ErrorExit("STDL_SetVideoMode failed: %s", STDL_GetError());
	}

	/*
	 * Sopwith draws in four colours. Every sprite, symbol, gauge,
	 * line and character goes through color_mappings[][] or a
	 * literal 0-3, so telling STDL to stop maintaining planes 2 and
	 * 3 halves the memory every primitive touches.
	 *
	 * Two things in the status bar are outside that range and are
	 * handled rather than ignored: swstbar.c's minimap pokes planes
	 * 0/1/2 (colour 7) and 0/1/3 (colour 11) straight into the
	 * framebuffer, which the budget does not affect because it is
	 * not STDL doing the writing - and Vid_ClearBuf zeroes all four
	 * planes every frame, so nothing accumulates; and Vid_HLine
	 * below draws the colour-7 separator raw for the same reason.
	 */
	STDL_SetPlaneBudget(SOPWITH_PLANE_BUDGET);

	/* Sopwith wants ASCII from keys; it does its own key-hold
	   tracking, so STDL's auto-repeat would double up presses. */
	STDL_EnableUNICODE(1);
	STDL_EnableKeyRepeat(0, 0);

	/* the game's 8x8 CGA font, used directly as an STDL font */
	game_font.cw = 8;
	game_font.ch = 8;
	game_font.first = 0;
	game_font.last = 255;
	game_font.bytes_per_row = 1;
	game_font.bits = font_data;

	active_palette = 0;
	ApplyPalette();

	vid_vram = screen->pixels;
	initted = true;
}

void Vid_Reset(void)
{
	if (!initted)
	{
		return;
	}

	ApplyPalette();
}

void Vid_Update(void)
{
	if (!initted)
	{
		Vid_Init();
	}

	/* Poll input immediately after the previous frame completes so events
	   are queued before the next frame starts rendering. This minimises
	   the latency between a keypress and the game acting on it. */
	PollInput();

	/* VBL-synced page flip; the back buffer moves, so republish it
	   for swstbar.c's direct minimap writes. */
	STDL_Flip();
	vid_vram = screen->pixels;
}

bool Vid_GetCtrlBreak(void)
{
	PollInput();
	return ctrlbreak;
}

void Vid_SetVideoPalette(int palette)
{
	if (palette < 0)
	{
		palette = 0;
	}
	active_palette = palette % (int)(sizeof(video_palettes) / sizeof(video_palettes[0]));
	if (initted)
	{
		ApplyPalette();
	}
}

const char *Vid_GetVideoPaletteName(int palette)
{
	if (palette < 0 || palette >= Vid_GetNumVideoPalettes())
	{
		return video_palettes[0].name;
	}
	return video_palettes[palette].name;
}

int Vid_GetNumVideoPalettes(void)
{
	return (int)(sizeof(video_palettes) / sizeof(video_palettes[0]));
}

int Vid_GetKey(void)
{
	struct input_event ev;

	PollInput();
	ev = PopInputEvent();
	return ev.key;
}

int Vid_GetChar(void)
{
	struct input_event ev;

	PollInput();
	ev = PopInputEvent();
	return ev.ch;
}

const char *Vid_KeyName(int key)
{
	static char unknown[16];
	static char printable[2];

	if (key >= 32 && key < 127)
	{
		printable[0] = (char)key;
		printable[1] = '\0';
		return printable;
	}

	if ((key & 0x100) != 0)
	{
		key &= 0xff;
	}

	if (key >= 0 && key < (int)(sizeof(key_names) / sizeof(key_names[0])) && key_names[key] != NULL)
	{
		return key_names[key];
	}

	snprintf(unknown, sizeof(unknown), "Scan %02X", key & 0xff);
	return unknown;
}

void Vid_StartTextInput(void)
{
	text_input_mode = true;
}

void Vid_StopTextInput(void)
{
	text_input_mode = false;
}

/* --- drawing --------------------------------------------------- */

void Vid_Box(int x, int y, int w, int h, int c)
{
	STDL_Rect r;

	if (w <= 0 || h < 0)
	{
		return;
	}

	/* Sopwith's box runs from y down to y - h inclusive, i.e. h + 1
	   rows, and grows upwards in game coordinates. */
	r.x = (int16_t)x;
	r.y = (int16_t)SY(y);
	r.w = (uint16_t)w;
	r.h = (uint16_t)(h + 1);
	STDL_FillRect(screen, &r, (uint8_t)c);
}

/*
 * Span lists for the ground outline. Worst case is one entry per
 * column in each list, which never both happen: a column is either
 * a step (vertical) or part of a flat run (horizontal).
 */
static STDL_Span ground_vspans[SCR_WDTH];
static STDL_Span ground_hspans[SCR_WDTH];

void Vid_DispGround(GRNDTYPE *gptr)
{
	int x;
	int hl, hc, hr, y0;
	int run_x = -1, run_end = 0, run_h = 0;
	int nv = 0, nh = 0;

	hc = clamp_max(*gptr, SCR_HGHT - 1);
	hl = hc;
	++gptr;

	/*
	 * The outline is one pixel tall wherever the profile is flat or
	 * at a local minimum, and a short vertical span only where it
	 * steps. Two things make that expensive one call at a time:
	 * there are up to 320 of them a frame, and each is one to three
	 * rows, so the call overhead dwarfs the pixels. So the whole
	 * frame is collected into two span lists and handed to the
	 * library in two calls - flat stretches as horizontal spans
	 * (each column contributes a distinct pixel, so a run XORs
	 * exactly the same set of pixels), steps as vertical ones.
	 */
	for (x = 0; x < SCR_WDTH; ++x)
	{
		hr = clamp_max(*gptr, SCR_HGHT - 1);
		y0 = imin(hl, imin(hc, hr));

		if (y0 == hc)
		{
			if (run_x >= 0 && run_h == hc)
			{
				run_end = x;
			}
			else
			{
				if (run_x >= 0)
				{
					ground_hspans[nh].x = (int16_t)run_x;
					ground_hspans[nh].y = (int16_t)SY(run_h);
					ground_hspans[nh].len =
						(int16_t)(run_end - run_x + 1);
					++nh;
				}
				run_x = run_end = x;
				run_h = hc;
			}
		}
		else
		{
			if (run_x >= 0)
			{
				ground_hspans[nh].x = (int16_t)run_x;
				ground_hspans[nh].y = (int16_t)SY(run_h);
				ground_hspans[nh].len =
					(int16_t)(run_end - run_x + 1);
				++nh;
				run_x = -1;
			}
			/* SY() flips the axis, so the span starts at the
			   higher ground level and runs down to the lower */
			ground_vspans[nv].x = (int16_t)x;
			ground_vspans[nv].y = (int16_t)SY(hc);
			ground_vspans[nv].len = (int16_t)(hc - y0 + 1);
			++nv;
		}

		hl = hc;
		hc = hr;
		++gptr;
	}

	if (run_x >= 0)
	{
		ground_hspans[nh].x = (int16_t)run_x;
		ground_hspans[nh].y = (int16_t)SY(run_h);
		ground_hspans[nh].len = (int16_t)(run_end - run_x + 1);
		++nh;
	}

	STDL_XorVSpans(screen, ground_vspans, nv, 3);
	STDL_XorHSpans(screen, ground_hspans, nh, 3);
}

void Vid_DispGround_Solid(GRNDTYPE *gptr)
{
	int x = 0;

	/* Run-length the height profile so flat stretches become one
	   wide STDL_FillRect instead of 16 vertical spans. Filling
	   colour 3 matches the native OR of planes 0+1, because planes
	   2 and 3 are always clear in the play area.

	   Deliberately NOT converted to STDL_VSpans: solid ground is
	   whole columns from the profile down to the status bar, so
	   the spans are ~150 rows and the per-call overhead the span
	   lists remove is a few percent of them - and a tall fill is
	   over the BLiTTER threshold, which STDL_FillRect can use and
	   the CPU-only span calls cannot. The outline path above is
	   the one whose spans are short enough for it to matter. */
	while (x < SCR_WDTH)
	{
		int h = clamp_max((int)gptr[x], SCR_HGHT - 1);
		int x2 = x + 1;

		while (x2 < SCR_WDTH
		       && clamp_max((int)gptr[x2], SCR_HGHT - 1) == h)
		{
			++x2;
		}

		if (h >= SBAR_HGHT - 1)
		{
			STDL_Rect r;
			r.x = (int16_t)x;
			r.y = (int16_t)SY(h);
			r.w = (uint16_t)(x2 - x);
			r.h = (uint16_t)(h - (SBAR_HGHT - 1) + 1);
			STDL_FillRect(screen, &r, 3);
		}

		x = x2;
	}
}

void Vid_PlotPixel(int x, int y, int clr)
{
	STDL_PutPixel(screen, x, SY(y), (uint8_t)clr);
}

void Vid_XorPixel(int x, int y, int clr)
{
	/* the native backend only ever XORs planes 0 and 1 */
	STDL_XorPixel(screen, x, SY(y), (uint8_t)(clr & 3));
}

/* Fallback when the sprite cache is full: one STDL_PutPixel per
   set pixel. Slow by design, and reported at exit. */
static void DispSymbolSlow(int x, int y, sopsym_t *symbol, int clr)
{
	const uint8_t *map = color_mappings[clr];
	int x1, y1;

	for (y1 = 0; y1 < symbol->h; ++y1)
	{
		const uint8_t *src = symbol->data + y1 * symbol->w;

		for (x1 = 0; x1 < symbol->w; ++x1)
		{
			int ci = src[x1];
			if (ci != 0)
			{
				STDL_PutPixel(screen, x + x1, SY(y - y1),
				              map[ci & 3]);
			}
		}
	}
}

void Vid_DispSymbol(int x, int y, sopsym_t *symbol, faction_t clr)
{
	STDL_Sprite *spr;

	if (x >= SCR_WDTH || y < 0 || y >= SCR_HGHT)
	{
		return;
	}

	if (symbol->w == 1 && symbol->h == 1)
	{
		Vid_XorPixel(x, y, clr);
		return;
	}

	spr = GetSymSprite(symbol, clr);
	if (spr != NULL)
	{
		/* the symbol's anchor is its BOTTOM-left pixel */
		STDL_BlitSprite(spr, 0, screen, x, SY(y));
	}
	else
	{
		DispSymbolSlow(x, y, symbol, clr);
	}
}

int Vid_FuselageColor(faction_t f)
{
	return color_mappings[f][1];
}

void Vid_ClearBuf(void)
{
	if (!initted)
	{
		Vid_Init();
	}

	STDL_FillRect(screen, NULL, 0);
}

void Vid_DrawChar(int x, int y, int ch, int color)
{
	char buf[2];

	if (ch < 0 || ch > 255)
	{
		return;
	}
	buf[0] = (char)ch;
	buf[1] = '\0';
	/* x and y are text cells, top-down (not game coordinates) */
	STDL_DrawText(screen, &game_font, x * 8, y * 8, buf,
	              (uint8_t)color);
}

/*
 * The status-bar separator is colour 7, which the plane budget
 * cannot express. It is full width, group aligned and drawn once a
 * frame, so writing the span raw costs nothing and keeps the line
 * the colour the native backend gives it - the same exception
 * swstbar.c's minimap already takes.
 */
void Vid_HLine(int y, int color)
{
	uint16_t *w;
	int g;

	if (color < (1 << SOPWITH_PLANE_BUDGET))
	{
		STDL_HLine(screen, 0, SCR_WDTH - 1, SY(y), (uint8_t)color);
		return;
	}
	w = (uint16_t *)(vid_vram + (uint32_t)SY(y) * vid_pitch);
	for (g = 0; g < SCR_WDTH / 16; ++g)
	{
		w[0] = (color & 1) ? 0xFFFFU : 0;
		w[1] = (color & 2) ? 0xFFFFU : 0;
		w[2] = (color & 4) ? 0xFFFFU : 0;
		w[3] = (color & 8) ? 0xFFFFU : 0;
		w += 4;
	}
}

void Vid_ColorScreen(int color)
{
	STDL_Rect r;

	/* game rows SBAR_HGHT..SCR_HGHT-1 = screen rows 0..180 */
	r.x = 0;
	r.y = 0;
	r.w = SCR_WDTH;
	r.h = (uint16_t)(SCR_HGHT - SBAR_HGHT);
	STDL_FillRect(screen, &r, (uint8_t)color);
}

char *Vid_GetPrefPath(void)
{
	static char current_dir[] = "";

	return current_dir;
}

void ErrorExit(char *s, ...)
{
	va_list args;

	STDL_Quit();

	va_start(args, s);
	vfprintf(stderr, s, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

/* Reported at exit so the sprite-cache budget can be tuned. */
void Vid_ReportSpriteCache(void)
{
	printf("STDL symbol sprites: %d cached, %lu bytes of %d\n",
	       sprite_count, sprite_bytes, SPRITE_BUDGET);
}
