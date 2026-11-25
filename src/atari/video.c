/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mint/osbind.h>
#include <mint/ostruct.h>

#include "atari_keys.h"
#include "font.h"
#include "planar.h"
#include "sw.h"
#include "swinit.h"
#include "swmain.h"
#include "swsymbol.h"
#include "timer.h"
#include "video.h"

#define INPUT_BUFFER_LEN 32
#define SCREEN_BYTES 32000
#define SCREEN_STRIDE 160
#define SCREEN_REZ_LOW 0
#define KEY_HOLD_MS 120
#define SBAR_HGHT 19

/* Both buffers must be in ST-RAM for blitter DMA access. */
#define IS_ST_RAM(p) ((unsigned long)(p) < 0x01000000UL)

/* --- Blitter acceleration (optional, runtime detected) -------------- */

static int has_blitter = -1;

static void detect_blitter(void)
{
	long mode = Blitmode(-1);
	has_blitter = (mode & 1) != 0;
}

/* Blitter zero-fill: clears 'n' bytes at dst (must be word-aligned).
   Uses HOP=1 (all-ones source), OP=0 (zero dest), single-row mode so
   the full 32000-byte screen fits in one pass (16000 words < 65535). */
static void blitter_clear(void *dst, size_t n)
{
	volatile char *ctrl = (volatile char *)0xFF8A3CL;

	*(volatile short *)0xFF8A20 = 0;     /* Src X Inc (unused for HOP=1) */
	*(volatile short *)0xFF8A22 = 0;     /* Src Y Inc (unused for HOP=1) */
	*(volatile short *)0xFF8A2E = 2;     /* Dst X Inc */
	*(volatile short *)0xFF8A30 = 0;     /* Dst Y Inc (single row, no wrap) */

	*(volatile short *)0xFF8A28 = 0xFFFF;
	*(volatile short *)0xFF8A2A = 0xFFFF;
	*(volatile short *)0xFF8A2C = 0xFFFF;

	*(volatile long *)0xFF8A32 = (long)dst;

	*(volatile char *)0xFF8A3A = 1;      /* HOP=1 (all-ones) */
	*(volatile char *)0xFF8A3B = 0;      /* OP=0 (zero dest) */

	*(volatile short *)0xFF8A36 = (short)(n >> 1); /* X count in words */
	*(volatile short *)0xFF8A38 = 1;               /* Y count = 1 row */

	*ctrl = 0xC0;
	while (*ctrl & 0x80)
		;
}

/* -------------------------------------------------------------------- */

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

uint8_t *vid_vram;
unsigned int vid_pitch = SCREEN_STRIDE;

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

static struct input_event input_buffer[INPUT_BUFFER_LEN];
static int input_head;
static int input_tail;
static bool ctrlbreak;
static bool initted;
static bool text_input_mode;
static uint32_t key_expiry[NUM_KEYS];

static int active_palette;
static short saved_palette[16];
static short original_rez;
static void *original_logbase;
static void *original_physbase;
static uint8_t *screen_alloc;
static uint8_t *draw_page;
static uint8_t *page[2];
static int draw_page_idx;

/* Pre-computed pixel masks for each x position (16 pixels per word) */
static const uint16_t pixel_mask_table[16] = {
	0x8000, 0x4000, 0x2000, 0x1000,
	0x0800, 0x0400, 0x0200, 0x0100,
	0x0080, 0x0040, 0x0020, 0x0010,
	0x0008, 0x0004, 0x0002, 0x0001,
};

extern bool isNetworkGame(void);
extern void PollJoystick(void);

/* --- Screen rows and the page flip ----------------------------------- */

/* Byte offset of screen row 'row' (0 = top), without a multiply. Game y
   counts up from the bottom, so game y is screen row SCR_HGHT - 1 - y. */
#define RowOffset(row) PlanarRowOffset(row)

/* _vbclock: counted by the VBL handler when it does its work, which
   includes handing Setscreen's new physbase to the video hardware. */
#define VBCLOCK (*(volatile uint32_t *)0x462L)

static bool flip_pending;
static uint32_t flip_vbclock;

/* The page about to be drawn on was the one on screen until the VBL
   after the last Vid_Update. Vid_Update used to wait for that VBL
   itself; waiting here instead, as drawing starts, lets the game's
   logic run in the meantime, and the wait is often over by then. */
static inline void WaitFlip(void)
{
	if (flip_pending)
	{
		while (VBCLOCK == flip_vbclock)
		{
		}
		flip_pending = false;
	}
}

static void PollInput(void);
static int NormalizeKeycode(int scan, int ch);

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

static inline uint8_t *Align256(uint8_t *p)
{
	return (uint8_t *)((((unsigned long)p) + 255UL) & ~255UL);
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

static void ExpireGameKeys(uint32_t now)
{
	enum gamekey key;

	for (key = 0; key < NUM_KEYS; ++key)
	{
		if ((keysdown[key] & KEYDOWN_KEYBOARD) != 0 && (int32_t)(now - key_expiry[key]) >= 0)
		{
			keysdown[key] &= ~KEYDOWN_KEYBOARD;
		}
	}
}

static void HandleGameKey(int keycode, uint32_t now)
{
	enum gamekey key = TranslateKeycode(keycode);

	if (key == KEY_UNKNOWN)
	{
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

	key_expiry[key] = now + KEY_HOLD_MS;
}

static void PollInput(void)
{
	while (Cconis() != 0)
	{
		long raw = Crawcin();
		int ch;
		int scan;
		int keycode;
		uint32_t now;

		if (raw == MINT_EOF)
		{
			break;
		}

		ch = (int)(raw & 0xff);
		scan = (int)((raw >> 16) & 0xff);
		now = (uint32_t)Timer_GetMS();
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
		HandleGameKey(keycode, now);
	}

	ExpireGameKeys((uint32_t)Timer_GetMS());
	PollJoystick();
}

static void ApplyPalette(void)
{
	Setpalette((void *)video_palettes[active_palette].colors);
}

static void RestoreVideo(void)
{
	int i;

	if (!initted)
	{
		return;
	}

	(void)Setscreen(original_logbase, original_physbase, original_rez);
	for (i = 0; i < 16; ++i)
	{
		(void)Setcolor(i, saved_palette[i]);
	}
	free(screen_alloc);
	screen_alloc = NULL;
	draw_page = NULL;
	vid_vram = NULL;
	initted = false;
}

static void SaveCurrentPalette(void)
{
	int i;

	for (i = 0; i < 16; ++i)
	{
		saved_palette[i] = Setcolor(i, -1);
	}
}

void Vid_Init(void)
{
	if (initted)
	{
		return;
	}

	detect_blitter();
	original_rez = Getrez();
	original_logbase = Logbase();
	original_physbase = Physbase();
	SaveCurrentPalette();
	/* Allocate two screen buffers for double buffering (page flipping).
	   Both must be 256-byte aligned for Setscreen. SCREEN_BYTES (32000)
	   is already a multiple of 256, so aligning the base is sufficient. */
	screen_alloc = malloc(SCREEN_BYTES * 2 + 255);
	if (screen_alloc == NULL)
	{
		ErrorExit("Failed to allocate screen buffers");
	}

	page[0] = Align256(screen_alloc);
	page[1] = page[0] + SCREEN_BYTES;
	memset(page[0], 0, SCREEN_BYTES * 2);
	draw_page_idx = 0;
	draw_page = page[0];

	(void)Setscreen(original_logbase, original_physbase, SCREEN_REZ_LOW);
	vid_vram = draw_page;
	active_palette = 0;
	ApplyPalette();

	/* Fast repeat helps the first-pass polling input path feel sane. */
	(void)Kbrate(0, 0);

	atexit(RestoreVideo);
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

	/* Page flip: set the hardware to display the page we just drew,
	   then switch to drawing on the other page. Setscreen with -1
	   means "don't change that parameter"; the new page is shown from
	   the next vertical blank, which WaitFlip waits for before the
	   other page is drawn on. */
	Setscreen(-1L, draw_page, -1);
	flip_vbclock = VBCLOCK;
	flip_pending = true;

	draw_page_idx ^= 1;
	draw_page = page[draw_page_idx];
	vid_vram = draw_page;
}

bool Vid_GetCtrlBreak(void)
{
	PollInput();
	return ctrlbreak;
}

void Vid_ClearCtrlBreak(void)
{
	ctrlbreak = false;
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

void Vid_InvalidateGroundCache(void)
{
}

/* Compute a contiguous bit mask from bit position 'from' to 'to' (inclusive).
   Bit 15 = leftmost pixel, bit 0 = rightmost. */
static inline uint16_t range_mask(int from, int to)
{
	return (uint16_t)((0xFFFFu >> from) & (0xFFFFu << (15 - to)));
}

/* Box drawing - processes 16 pixels at a time, only planes 0-1 */
void Vid_Box(int x, int y, int w, int h, int c)
{
	int y1;
	uint16_t fill0, fill1;
	uint8_t *row;

	if (w <= 0 || h <= 0)
	{
		return;
	}

	/* Use slow path for out-of-bounds */
	if (!in_range(0, x, SCR_WDTH - 1) || !in_range(0, y, SCR_HGHT - 1))
	{
		int x1;
		for (y1 = 0; y1 <= h; ++y1)
		{
			for (x1 = 0; x1 < w; ++x1)
			{
				Vid_PlotPixel(x + x1, y - y1, c);
			}
		}
		return;
	}

	WaitFlip();
	fill0 = (c & 1) ? 0xFFFF : 0;
	fill1 = (c & 2) ? 0xFFFF : 0;

	/* Rows go down the screen from y, so up through memory a row at a
	   time from game row y. */
	row = draw_page + RowOffset(SCR_HGHT - 1 - y);
	for (y1 = 0; y1 <= h; ++y1, row += SCREEN_STRIDE)
	{
		int py = y - y1;
		if (py < 0)
		{
			break;
		}

		int end_x = x + w - 1;
		int start_word = x >> 4;
		int end_word = end_x >> 4;

		if (start_word == end_word)
		{
			uint16_t *words = (uint16_t *)(row + start_word * 8);
			uint16_t m = range_mask(x & 15, end_x & 15);
			uint16_t nm = ~m;
			words[0] = (words[0] & nm) | (fill0 & m);
			words[1] = (words[1] & nm) | (fill1 & m);
		}
		else
		{
			/* First partial word */
			uint16_t *words = (uint16_t *)(row + start_word * 8);
			uint16_t m = range_mask(x & 15, 15);
			uint16_t nm = ~m;
			words[0] = (words[0] & nm) | (fill0 & m);
			words[1] = (words[1] & nm) | (fill1 & m);

			/* Full middle words */
			int w_idx;
			for (w_idx = start_word + 1; w_idx < end_word; ++w_idx)
			{
				uint16_t *mw = (uint16_t *)(row + w_idx * 8);
				mw[0] = fill0;
				mw[1] = fill1;
			}

			/* Last partial word */
			uint16_t *lw = (uint16_t *)(row + end_word * 8);
			m = range_mask(0, end_x & 15);
			nm = ~m;
			lw[0] = (lw[0] & nm) | (fill0 & m);
			lw[1] = (lw[1] & nm) | (fill1 & m);
		}
	}
}

/* The ground's outline: in each column, XOR from the lowest of it and
   its neighbours up to its own height, in planes 0 and 1 at once (they
   are adjacent words, so one eor.l covers both). 320 columns a frame,
   each usually a pixel or two, so the work is in the bookkeeping: rows
   come from a table, heights are 16-bit, and the columns go 16 to a
   group with each one's mask from a table, rather than computing each
   address and shifting and testing the mask every column. */
static uint16_t *ground_rows;     /* game y * 160, SCR_HGHT of them */
static bool ground_rows_failed;
static const uint32_t ground_masks[16] = {
	0x80008000UL, 0x40004000UL, 0x20002000UL, 0x10001000UL,
	0x08000800UL, 0x04000400UL, 0x02000200UL, 0x01000100UL,
	0x00800080UL, 0x00400040UL, 0x00200020UL, 0x00100010UL,
	0x00080008UL, 0x00040004UL, 0x00020002UL, 0x00010001UL,
};

void Vid_DispGround(GRNDTYPE *gptr)
{
	uint8_t *group;
	int16_t hl, hc, hr;
	int g, k;

	WaitFlip();
	if (ground_rows == NULL && !ground_rows_failed)
	{
		ground_rows = RenderCacheAlloc(SCR_HGHT * sizeof(uint16_t), RENDER_CACHE_ARENA);
		if (ground_rows == NULL)
		{
			ground_rows_failed = true;
		}
		else
		{
			for (k = 0; k < SCR_HGHT; ++k)
			{
				ground_rows[k] = (uint16_t)RowOffset(k);
			}
		}
	}
	group = draw_page + RowOffset(SCR_HGHT - 1); /* game y = 0 */

	hc = *gptr++;
	if (hc > SCR_HGHT - 1)
	{
		hc = SCR_HGHT - 1;
	}
	hl = hc;

	for (g = 0; g < SCR_WDTH / 16; ++g, group += 8)
	{
		for (k = 0; k < 16; ++k)
		{
			uint32_t lmask = ground_masks[k];
			uint32_t *p;
			int16_t y0, n;

			hr = *gptr++;
			if (hr > SCR_HGHT - 1)
			{
				hr = SCR_HGHT - 1;
			}
			y0 = hl < hc ? hl : hc;
			if (hr < y0)
			{
				y0 = hr;
			}

			p = (uint32_t *)(group - (ground_rows != NULL
			                          ? ground_rows[y0]
			                          : RowOffset(y0)));
			n = hc - y0;
			do
			{
				*p ^= lmask;
				p = (uint32_t *)((uint8_t *)p - SCREEN_STRIDE);
			} while (--n >= 0);

			hl = hc;
			hc = hr;
		}
	}
}

void Vid_DispGround_Solid(GRNDTYPE *gptr)
{
	/* Process 16 columns at a time (one word group).
	   Build a height_to_mask[] lookup (indexed by row height) so each row
	   just ORs one precomputed value — no per-row inner loop of comparisons.
	   Then sweep top-down accumulating the mask with a single word write per row. */
	int grp;
	uint16_t height_to_mask[SCR_HGHT];

	WaitFlip();

	for (grp = 0; grp < SCR_WDTH / 16; ++grp)
	{
		const GRNDTYPE *col = gptr + grp * 16;
		int group_max = SBAR_HGHT - 1;
		int i, row;

		for (i = 0; i < 16; ++i)
		{
			int h = (int)col[i];
			if (h >= SCR_HGHT) h = SCR_HGHT - 1;
			if (h > group_max) group_max = h;
		}

		if (group_max < SBAR_HGHT - 1)
			continue;

		for (row = SBAR_HGHT - 1; row <= group_max; ++row)
			height_to_mask[row] = 0;

		{
			uint16_t bit = 0x8000U;
			for (i = 0; i < 16; ++i, bit >>= 1)
			{
				int h = (int)col[i];
				if (h >= SCR_HGHT) h = SCR_HGHT - 1;
				if (h >= SBAR_HGHT - 1)
					height_to_mask[h] |= bit;
			}
		}

		{
			uint16_t mask = 0;
			uint8_t *p = draw_page + RowOffset(SCR_HGHT - 1 - group_max) + grp * 8;

			for (row = group_max; row >= SBAR_HGHT - 1; --row)
			{
				mask |= height_to_mask[row];
				((uint16_t *)p)[0] |= mask;
				((uint16_t *)p)[1] |= mask;
				p += SCREEN_STRIDE;
			}
		}
	}
}

void Vid_PlotPixel(int x, int y, int clr)
{
	uint16_t *words;
	uint16_t mask, nm;

	if ((unsigned)x >= SCR_WDTH || (unsigned)y >= SCR_HGHT)
	{
		return;
	}

	WaitFlip();
	words = (uint16_t *)(draw_page + RowOffset(SCR_HGHT - 1 - y) + (x >> 4) * 8);
	mask = pixel_mask_table[x & 15];
	nm = ~mask;

	if (clr & 1) words[0] |= mask; else words[0] &= nm;
	if (clr & 2) words[1] |= mask; else words[1] &= nm;
	if (clr & 4) words[2] |= mask;
	if (clr & 8) words[3] |= mask;
}

void Vid_XorPixel(int x, int y, int clr)
{
	uint16_t *words;
	uint16_t mask;

	if ((unsigned)x >= SCR_WDTH || (unsigned)y >= SCR_HGHT)
	{
		return;
	}

	WaitFlip();
	words = (uint16_t *)(draw_page + RowOffset(SCR_HGHT - 1 - y) + (x >> 4) * 8);
	mask = pixel_mask_table[x & 15];

	if (clr & 1) words[0] ^= mask;
	if (clr & 2) words[1] ^= mask;
}

/* --- Symbols ---------------------------------------------------------- */

/*
 * Symbols are byte-per-pixel images in colours 0-3, recoloured for each
 * faction through color_mappings at draw time. Drawn a pixel at a time
 * they took a third of a frame on a plain ST, so each symbol is turned,
 * the first time it is drawn, into two bit planes a row: the pixels whose
 * colour has bit 0 set, and those with bit 1. Drawing one is then a few
 * logic operations a row for the faction's colours, and a masked write a
 * screen word, 16 pixels at a time.
 *
 * The cache is keyed on the pixel data, which identical symbols share
 * (swsymbol.c). Its memory comes from RenderCacheAlloc, so it costs a
 * 512KB machine nothing it had: a symbol it has no room for, or one wider
 * than 32 pixels, is drawn a pixel at a time as before.
 */
#define SYM_CACHE_SIZE 251      /* prime: open addressing */
#define SYM_BUDGET     32768    /* bytes of converted rows, at most */

struct sym_planes
{
	const uint8_t *data;    /* the symbol's pixels; NULL = free slot */
	uint32_t *rows;         /* h pairs, bit 0 then bit 1; NULL = slow */
};

static struct sym_planes *sym_cache;   /* SYM_CACHE_SIZE, once there */
static bool sym_cache_failed;
static unsigned long sym_bytes;

static uint32_t *BuildSymPlanes(const sopsym_t *sym)
{
	uint32_t *rows, *r;
	const uint8_t *src = sym->data;
	unsigned long cost = (unsigned long)sym->h * 8;
	int x, y;

	if (sym->w > 32 || sym_bytes + cost > SYM_BUDGET)
	{
		return NULL;
	}
	rows = RenderCacheAlloc(cost, RENDER_CACHE_ARENA);
	if (rows == NULL)
	{
		return NULL;
	}
	sym_bytes += cost;

	for (y = 0, r = rows; y < sym->h; ++y, r += 2)
	{
		uint32_t b0 = 0, b1 = 0, bit = 0x80000000UL;

		for (x = 0; x < sym->w; ++x, bit >>= 1)
		{
			int c = *src++;
			if (c & 1) b0 |= bit;
			if (c & 2) b1 |= bit;
		}
		r[0] = b0;
		r[1] = b1;
	}
	return rows;
}

static uint32_t *SymPlanes(const sopsym_t *sym)
{
	unsigned int slot;
	int probes;

	if (sym_cache == NULL)
	{
		if (sym_cache_failed)
		{
			return NULL;
		}
		sym_cache = RenderCacheAlloc(SYM_CACHE_SIZE * sizeof(*sym_cache),
		                             RENDER_CACHE_ARENA);
		if (sym_cache == NULL)
		{
			sym_cache_failed = true;
			return NULL;
		}
		memset(sym_cache, 0, SYM_CACHE_SIZE * sizeof(*sym_cache));
	}

	slot = (unsigned int)(((uintptr_t)sym->data >> 1) % SYM_CACHE_SIZE);

	for (probes = 0; probes < SYM_CACHE_SIZE; ++probes)
	{
		struct sym_planes *e = &sym_cache[slot];

		if (e->data == sym->data)
		{
			return e->rows;
		}
		if (e->data == NULL)
		{
			e->data = sym->data;
			e->rows = BuildSymPlanes(sym);
			return e->rows;
		}
		if (++slot == SYM_CACHE_SIZE)
		{
			slot = 0;
		}
	}
	return NULL;
}

/* One screen word group: the masked pixels take colours p0 and p1. */
static inline void PutGroup(uint16_t *words, uint16_t m, uint16_t p0,
                            uint16_t p1)
{
	if (m != 0)
	{
		uint16_t nm = ~m;
		words[0] = (words[0] & nm) | p0;
		words[1] = (words[1] & nm) | p1;
	}
}

/* A pixel at a time, from the byte-per-pixel master. */
static void DispSymbolSlow(int x, int y, const sopsym_t *symbol, int w,
                           int h, int left_skip, const uint8_t *color_mapping)
{
	int dst_x = x + left_skip;
	int x1, y1;
	uint8_t *row = draw_page + RowOffset(SCR_HGHT - 1 - y);
	const uint8_t *src_row = symbol->data + left_skip;

	for (y1 = 0; y1 < h; ++y1, row += SCREEN_STRIDE, src_row += symbol->w)
	{
		const uint8_t *src = src_row;

		for (x1 = left_skip; x1 < w; ++x1, ++src)
		{
			int ci = *src;
			if (ci)
			{
				int color = color_mapping[ci];
				int sx = dst_x + (x1 - left_skip);
				uint16_t *words = (uint16_t *)(row + (sx >> 4) * 8);
				uint16_t mask = pixel_mask_table[sx & 15];
				uint16_t nm = ~mask;
				if (color & 1) words[0] |= mask; else words[0] &= nm;
				if (color & 2) words[1] |= mask; else words[1] &= nm;
			}
		}
	}
}

void Vid_DispSymbol(int x, int y, sopsym_t *symbol, faction_t clr)
{
	int left_skip = x < 0 ? -x : 0;
	int dst_x = x + left_skip; /* screen x of first visible pixel, always >= 0 */
	int w = symbol->w, h = symbol->h;
	const uint8_t *color_mapping;
	const uint32_t *rows;
	uint32_t a0, b0, c0, a1, b1, c1, keep;
	uint8_t *row;
	int shift, group, y1, visible;
	bool second, third;

	if (x >= SCR_WDTH || y < 0 || y >= SCR_HGHT)
	{
		return;
	}

	if (w == 1 && h == 1)
	{
		Vid_XorPixel(x, y, clr);
		return;
	}

	if (x + w > SCR_WDTH)
	{
		w = SCR_WDTH - x;
	}

	if (h > y + 1)
	{
		h = y + 1;
	}

	if (left_skip >= w)
	{
		return;
	}

	WaitFlip();
	assert(clr < (int)(sizeof(color_mappings) / sizeof(color_mappings[0])));
	color_mapping = color_mappings[clr];

	rows = SymPlanes(symbol);
	if (rows == NULL)
	{
		DispSymbolSlow(x, y, symbol, w, h, left_skip, color_mapping);
		return;
	}

	/* Each plane's bit for a pixel of colour q1q0 (1, 2 or 3) is the
	   faction's colour's bit for it. Any such function with f(0,0) = 0
	   is a XOR of q0, q1 and q0 AND q1, so for plane n it is
	   (q0 & an) ^ (q1 & bn) ^ (q0 & q1 & cn), the masks all ones or all
	   zeros: an the bit for colour 1, bn for colour 2, and cn the XOR of
	   those two with colour 3's. */
	{
		uint32_t v1 = color_mapping[1], v2 = color_mapping[2];
		uint32_t v3 = color_mapping[3];

		a0 = (v1 & 1) ? ~0UL : 0;
		b0 = (v2 & 1) ? ~0UL : 0;
		c0 = ((v1 ^ v2 ^ v3) & 1) ? ~0UL : 0;
		a1 = (v1 & 2) ? ~0UL : 0;
		b1 = (v2 & 2) ? ~0UL : 0;
		c1 = ((v1 ^ v2 ^ v3) & 2) ? ~0UL : 0;
	}

	shift = dst_x & 15;
	group = dst_x >> 4;
	row = draw_page + RowOffset(SCR_HGHT - 1 - y) + group * 8;
	visible = w - left_skip;
	keep = visible < 32 ? ~0UL << (32 - visible) : ~0UL;
	/* Wider than 16 pixels, the row can reach a third group. */
	third = visible > 16 && shift != 0 && group + 2 < SCR_WDTH / 16;
	second = group + 1 < SCR_WDTH / 16;

	for (y1 = 0; y1 < h; ++y1, rows += 2, row += SCREEN_STRIDE)
	{
		uint32_t q0 = rows[0], q1 = rows[1];
		uint32_t q01, p0, p1, m;

		if (left_skip != 0)
		{
			q0 <<= left_skip;
			q1 <<= left_skip;
		}
		/* Columns past the right edge of the screen (w was cut) */
		q0 &= keep;
		q1 &= keep;

		m = q0 | q1;
		if (m == 0)
		{
			continue;
		}
		q01 = q0 & q1;
		p0 = (q0 & a0) ^ (q1 & b0) ^ (q01 & c0);
		p1 = (q0 & a1) ^ (q1 & b1) ^ (q01 & c1);

		/* The bits for a third group, before the shift loses them */
		if (third)
		{
			PutGroup((uint16_t *)(row + 16),
			         (uint16_t)(m << (16 - shift)),
			         (uint16_t)(p0 << (16 - shift)),
			         (uint16_t)(p1 << (16 - shift)));
		}

		/* 'shift' pixels into the first group: its word is the high
		   half, the next group's the low half. */
		m >>= shift;
		p0 >>= shift;
		p1 >>= shift;
		PutGroup((uint16_t *)row, (uint16_t)(m >> 16),
		         (uint16_t)(p0 >> 16), (uint16_t)(p1 >> 16));
		if (second)
		{
			PutGroup((uint16_t *)(row + 8), (uint16_t)m,
			         (uint16_t)p0, (uint16_t)p1);
		}
	}
}

int Vid_FuselageColor(faction_t f)
{
	assert(f < (int)(sizeof(color_mappings) / sizeof(color_mappings[0])));
	return color_mappings[f][1];
}

/* Zeroes a page, 48 bytes a movem.l: 666 of them and 32 bytes more. */
static void ClearPage(uint8_t *p)
{
	register uint8_t *end __asm__("a0") = p + SCREEN_BYTES;

	__asm__ volatile(
		"moveq	#0,%%d1\n\t"
		"move.l	%%d1,%%d2\n\t"
		"move.l	%%d1,%%d3\n\t"
		"move.l	%%d1,%%d4\n\t"
		"move.l	%%d1,%%d5\n\t"
		"move.l	%%d1,%%d6\n\t"
		"move.l	%%d1,%%d7\n\t"
		"move.l	%%d1,%%a1\n\t"
		"move.l	%%d1,%%a2\n\t"
		"move.l	%%d1,%%a3\n\t"
		"move.l	%%d1,%%a4\n\t"
		"move.l	%%d1,%%a5\n\t"
		"move.w	#665,%%d0\n"
		"1:\tmovem.l	%%d1-%%d7/%%a1-%%a5,-(%%a0)\n\t"
		"dbra	%%d0,1b\n\t"
		"movem.l	%%d1-%%d7/%%a1,-(%%a0)"
		: "+a"(end)
		:
		: "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
		  "a1", "a2", "a3", "a4", "a5", "cc", "memory");
}

void Vid_ClearBuf(void)
{
	if (!initted)
	{
		Vid_Init();
	}

	WaitFlip();
	if (has_blitter && IS_ST_RAM(draw_page))
	{
		blitter_clear(draw_page, SCREEN_BYTES);
	}
	else
	{
		ClearPage(draw_page);
	}
}

/* --- Fast text rendering directly to planar framebuffer -------------- */

/* x and y are text cells, top-down. A character is a byte of each plane
   word: the high byte for an even column, the low byte for an odd one. */
void Vid_DrawChar(int x, int y, int ch, int color)
{
	if (ch < 0 || ch > 255)
		return;

	WaitFlip();
	PlanarGlyph(draw_page + RowOffset(y * 8) + (x >> 1) * 8 + (x & 1),
	            font_data + ch * 8, color, y * 8, SCR_HGHT);
}

/* Fast horizontal line for status bar border (full-width lines) */
void Vid_HLine(int y, int color)
{
	uint8_t *row;
	uint16_t fill0, fill1, fill2, fill3;
	int i;

	if (y < 0 || y >= SCR_HGHT)
		return;

	WaitFlip();
	row = draw_page + RowOffset(SCR_HGHT - 1 - y);
	fill0 = (color & 1) ? 0xFFFF : 0;
	fill1 = (color & 2) ? 0xFFFF : 0;
	fill2 = (color & 4) ? 0xFFFF : 0;
	fill3 = (color & 8) ? 0xFFFF : 0;

	for (i = 0; i < 20; ++i)
	{
		uint16_t *words = (uint16_t *)(row + i * 8);
		words[0] = fill0;
		words[1] = fill1;
		words[2] = fill2;
		words[3] = fill3;
	}
}

/* Fast colorscreen: fill game area (above status bar) with solid color */
void Vid_ColorScreen(int color)
{
	int y;
	uint16_t fill0, fill1;
	uint8_t *row;

	WaitFlip();
	fill0 = (color & 1) ? 0xFFFF : 0;
	fill1 = (color & 2) ? 0xFFFF : 0;

	row = draw_page;
	for (y = SBAR_HGHT; y < SCR_HGHT; ++y, row += SCREEN_STRIDE)
	{
		int i;

		for (i = 0; i < 20; ++i)
		{
			uint16_t *words = (uint16_t *)(row + i * 8);
			words[0] = fill0;
			words[1] = fill1;
			words[2] = 0;
			words[3] = 0;
		}
	}
}

char *Vid_GetPrefPath(void)
{
	static char current_dir[] = "";

	return current_dir;
}

void ErrorExit(char *s, ...)
{
	va_list args;

	RestoreVideo();

	va_start(args, s);
	vfprintf(stderr, s, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}
