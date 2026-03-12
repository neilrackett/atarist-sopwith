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
#include "sw.h"
#include "swinit.h"
#include "swmain.h"
#include "timer.h"
#include "video.h"

#define INPUT_BUFFER_LEN 32
#define SCREEN_BYTES 32000
#define SCREEN_STRIDE 160
#define SCREEN_REZ_LOW 0
#define KEY_HOLD_MS 120
#define SBAR_HGHT 19

/* --- Blitter acceleration (optional, runtime detected) -------------- */

static int has_blitter = -1;

static void detect_blitter(void)
{
	long mode = Blitmode(-1);
	has_blitter = (mode & 1) != 0;
}

/* Both buffers must be in ST-RAM for blitter DMA access. */
#define IS_ST_RAM(p) ((unsigned long)(p) < 0x01000000UL)

/* Blitter zero-fill: clears 'n' bytes at dst (must be word-aligned). */
static void blitter_clear(void *dst, size_t n)
{
	volatile char *ctrl = (volatile char *)0xFF8A3CL;

	/* Source X/Y inc don't matter for HOP=1 (all-ones); dest inc = 2 */
	*(volatile short *)0xFF8A20 = 0; /* Src X Inc (unused) */
	*(volatile short *)0xFF8A22 = 0; /* Src Y Inc (unused) */
	*(volatile short *)0xFF8A2E = 2; /* Dst X Inc */
	*(volatile short *)0xFF8A30 = 2; /* Dst Y Inc */

	/* No masking */
	*(volatile short *)0xFF8A28 = 0xFFFF;
	*(volatile short *)0xFF8A2A = 0xFFFF;
	*(volatile short *)0xFF8A2C = 0xFFFF;

	*(volatile long *)0xFF8A32 = (long)dst;

	/* HOP=1 (all-ones), OP=0 (zero / clear dest) */
	*(volatile char *)0xFF8A3A = 1;
	*(volatile char *)0xFF8A3B = 0;

	/* Blitter X count is 16-bit, max 65535 words = 128KB.
		 32000 bytes = 16000 words, fits in one pass. */
	*(volatile short *)0xFF8A36 = (short)(n >> 1);
	*(volatile short *)0xFF8A38 = 1;

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
		{"CGA",
		 {0x000, 0x077, 0x707, 0x777,
			0x700, 0x070, 0x770, 0x555,
			0x333, 0x007, 0x070, 0x077,
			0x700, 0x707, 0x770, 0x777}},
		{"Atari",
		 {0x000, 0x007, 0x700, 0x777,
			0x070, 0x707, 0x770, 0x555,
			0x333, 0x007, 0x070, 0x077,
			0x700, 0x707, 0x770, 0x777}},
		{"Green",
		 {0x000, 0x020, 0x040, 0x070,
			0x010, 0x030, 0x050, 0x060,
			0x010, 0x020, 0x030, 0x040,
			0x050, 0x060, 0x070, 0x070}},
		{"Amber",
		 {0x000, 0x210, 0x430, 0x760,
			0x110, 0x320, 0x540, 0x650,
			0x110, 0x210, 0x320, 0x430,
			0x540, 0x650, 0x760, 0x760}},
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

static void free_blit_sym_cache(void);

/* Pre-computed pixel masks for each x position (16 pixels per word) */
static const uint16_t pixel_mask_table[16] = {
		0x8000, 0x4000, 0x2000, 0x1000,
		0x0800, 0x0400, 0x0200, 0x0100,
		0x0080, 0x0040, 0x0020, 0x0010,
		0x0008, 0x0004, 0x0002, 0x0001};

extern bool isNetworkGame(void);

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

static inline uint16_t *PixelWordsForXY(int x, int y)
{
	int row = SCR_HGHT - 1 - y;
	return (uint16_t *)(draw_page + row * SCREEN_STRIDE + (x >> 4) * 8);
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

static void HandleCtrlChord(int scan)
{
	switch (scan)
	{
#ifndef NO_EXIT
	case ATARI_SCANCODE_C:
		ctrlbreak = true;
		break;
#endif
	case ATARI_SCANCODE_R:
		if (!isNetworkGame())
		{
			gamenum = starting_level;
			swinitlevel();
		}
		break;

	case ATARI_SCANCODE_Q:
		if (!isNetworkGame())
		{
			swrestart();
		}
		break;

	default:
		break;
	}
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
		scan = (int)((raw >> 8) & 0xff);
		now = (uint32_t)Timer_GetMS();
		keycode = NormalizeKeycode(scan, ch);

		if ((Kbshift(-1) & K_CTRL) != 0)
		{
			HandleCtrlChord(scan);
		}

		if (ch == '\r')
		{
			ch = '\n';
		}
		PushInputEvent(keycode, ch);
		HandleGameKey(keycode, now);
	}

	ExpireGameKeys((uint32_t)Timer_GetMS());
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
	free_blit_sym_cache();
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
	page[1] = page[0] + SCREEN_BYTES; /* 32000 is 256-aligned */
	memset(page[0], 0, SCREEN_BYTES * 2);
	draw_page_idx = 0;
	draw_page = page[0];

	(void)Setscreen(original_logbase, original_physbase, SCREEN_REZ_LOW);
	vid_vram = draw_page;
	active_palette = 1;
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

	/* Page flip: set the hardware to display the page we just drew,
		 then switch to drawing on the other page. Setscreen with -1
		 means "don't change that parameter". Vsync ensures the flip
		 takes effect on the next vertical blank. */
	Setscreen(-1L, draw_page, -1);
	Vsync();

	/* Swap draw page */
	draw_page_idx ^= 1;
	draw_page = page[draw_page_idx];
	vid_vram = draw_page;
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
	(void)text_input_mode;
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
	/* 0xFFFF >> from gives bits (15-from)..0, then mask off below 'to' */
	return (uint16_t)((0xFFFFu >> from) & (0xFFFFu << (15 - to)));
}

/* Box drawing - processes 16 pixels at a time, only planes 0-1 */
void Vid_Box(int x, int y, int w, int h, int c)
{
	int y1;
	uint16_t fill0, fill1;

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

	fill0 = (c & 1) ? 0xFFFF : 0;
	fill1 = (c & 2) ? 0xFFFF : 0;

	for (y1 = 0; y1 <= h; ++y1)
	{
		int py = y - y1;
		if (!in_range(0, py, SCR_HGHT - 1))
		{
			continue;
		}

		int end_x = x + w - 1;
		int start_word = x >> 4;
		int end_word = end_x >> 4;

		uint8_t *row = draw_page + (SCR_HGHT - 1 - py) * SCREEN_STRIDE;

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

/* Helper: XOR a vertical line into planes 0+1 using blitter.
   Blitter must already be set up with static registers (call
   ground_blitter_setup first). Only dst address, source mask,
   and Y count change per call. */
static uint16_t blitter_mask_source;

static void ground_blitter_setup(void)
{
	*(volatile short *)0xFF8A20 = 0;              /* Src X Inc */
	*(volatile short *)0xFF8A22 = 0;              /* Src Y Inc */
	*(volatile short *)0xFF8A2E = 0;              /* Dst X Inc */
	*(volatile short *)0xFF8A30 = -SCREEN_STRIDE; /* Dst Y Inc */
	*(volatile short *)0xFF8A28 = 0xFFFF;         /* Endmask 1 */
	*(volatile short *)0xFF8A2A = 0xFFFF;         /* Endmask 2 */
	*(volatile short *)0xFF8A2C = 0xFFFF;         /* Endmask 3 */
	*(volatile long  *)0xFF8A24 = (long)&blitter_mask_source;
	*(volatile char  *)0xFF8A3A = 0;              /* HOP=0 (source only) */
	*(volatile char  *)0xFF8A3B = 6;              /* OP=6 (XOR) */
	*(volatile short *)0xFF8A36 = 1;              /* X count = 1 word */
}

static inline void ground_blit_vline(uint8_t *dst, uint16_t mask, int height)
{
	volatile char *ctrl = (volatile char *)0xFF8A3CL;

	blitter_mask_source = mask;
	*(volatile short *)0xFF8A38 = (short)height;

	/* Blit plane 0 */
	*(volatile long *)0xFF8A32 = (long)dst;
	*ctrl = 0xC0;
	while (*ctrl & 0x80)
		;

	/* Blit plane 1 */
	*(volatile long *)0xFF8A32 = (long)(dst + 2);
	*ctrl = 0xC0;
	while (*ctrl & 0x80)
		;
}

static inline void cpu_xor_vline(uint8_t *row_base, uint16_t mask, int height)
{
	/* XOR both planes 0+1 simultaneously using a longword operation.
	   In ST low-res, planes 0+1 are adjacent words, so a single
	   eor.l hits both planes at once - saves ~30% vs two eor.w ops. */
	uint32_t lmask = ((uint32_t)mask << 16) | mask;
	uint32_t *lp = (uint32_t *)row_base;
	int yy;
	for (yy = 0; yy < height; yy++)
	{
		*lp ^= lmask;
		lp = (uint32_t *)((uint8_t *)lp - SCREEN_STRIDE);
	}
}

void Vid_DispGround(GRNDTYPE *gptr)
{
	int x;
	int hl, hc, hr, y0;
	int use_blitter = has_blitter && IS_ST_RAM(draw_page);

	if (use_blitter)
		ground_blitter_setup();

	hc = clamp_max(*gptr, SCR_HGHT - 1);
	hl = hc;
	++gptr;

	for (x = 0; x < SCR_WDTH; ++x)
	{
		hr = clamp_max(*gptr, SCR_HGHT - 1);
		y0 = imin(hl, imin(hc, hr));

		if (y0 <= hc)
		{
			int y_start = y0;
			int y_end = hc;
			int line_height;

			if (y_start < 0)
				y_start = 0;
			if (y_end >= SCR_HGHT)
				y_end = SCR_HGHT - 1;
			line_height = y_end - y_start + 1;

			if (line_height > 0)
			{
				uint16_t mask = pixel_mask_table[x & 15];
				uint8_t *row_base = draw_page + (SCR_HGHT - 1 - y_start) * SCREEN_STRIDE + (x >> 4) * 8;

				if (use_blitter && line_height >= 4)
					ground_blit_vline(row_base, mask, line_height);
				else
					cpu_xor_vline(row_base, mask, line_height);
			}
		}

		hl = hc;
		hc = hr;
		++gptr;
	}
}

void Vid_DispGround_Solid(GRNDTYPE *gptr)
{
	int x;
	int gc;
	int use_blitter = has_blitter && IS_ST_RAM(draw_page);

	if (use_blitter)
		ground_blitter_setup();

	for (x = 0; x < SCR_WDTH; ++x, ++gptr)
	{
		gc = clamp_max(*gptr, SCR_HGHT - 1);

		if (gc >= SBAR_HGHT - 1)
		{
			int y_start = SBAR_HGHT - 1;
			int line_height = gc - y_start + 1;
			uint16_t mask = pixel_mask_table[x & 15];
			uint8_t *row_base = draw_page + (SCR_HGHT - 1 - y_start) * SCREEN_STRIDE + (x >> 4) * 8;

			if (use_blitter && line_height >= 4)
				ground_blit_vline(row_base, mask, line_height);
			else
				cpu_xor_vline(row_base, mask, line_height);
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

	words = (uint16_t *)(draw_page + (SCR_HGHT - 1 - y) * SCREEN_STRIDE + (x >> 4) * 8);
	mask = pixel_mask_table[x & 15];
	nm = ~mask;

	/* Since we clear the entire buffer each frame, we only need to
	   SET bits for active planes - no need to clear them. */
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

	words = (uint16_t *)(draw_page + (SCR_HGHT - 1 - y) * SCREEN_STRIDE + (x >> 4) * 8);
	mask = pixel_mask_table[x & 15];

	if (clr & 1)
		words[0] ^= mask;
	if (clr & 2)
		words[1] ^= mask;
}

/* --- Blitter-accelerated symbol rendering --------------------------- */

typedef struct
{
	int w_words; /* width in 16-pixel words */
	int h;
	void *data; /* Interleaved bitplane data for P0, P1 */
} blit_sym_t;

typedef struct blit_sym_cache_entry_s
{
	sopsym_t *original_sym;
	faction_t faction;
	blit_sym_t *blit_sym;
	struct blit_sym_cache_entry_s *next;
} blit_sym_cache_entry_t;

static blit_sym_cache_entry_t *blit_sym_cache_head = NULL;

static void free_blit_sym_cache(void)
{
	blit_sym_cache_entry_t *entry = blit_sym_cache_head;
	while (entry)
	{
		blit_sym_cache_entry_t *next = entry->next;
		if (entry->blit_sym)
		{
			free(entry->blit_sym->data);
			free(entry->blit_sym);
		}
		free(entry);
		entry = next;
	}
	blit_sym_cache_head = NULL;
}

static __attribute__((noinline)) blit_sym_t *find_or_create_blit_sym(sopsym_t *symbol, faction_t clr)
{
	blit_sym_cache_entry_t *entry;
	blit_sym_t *bsym;
	int w_words, h;
	const uint8_t *color_mapping;
	int y1, x1;

	/* Search cache */
	for (entry = blit_sym_cache_head; entry; entry = entry->next)
	{
		if (entry->original_sym == symbol && entry->faction == clr)
		{
			return entry->blit_sym;
		}
	}

	/* Not found. Create a new one and render it. */
	w_words = (symbol->w + 15) >> 4;
	h = symbol->h;

	bsym = malloc(sizeof(blit_sym_t));
	if (!bsym)
		return NULL;
	/* 2 planes, 2 bytes per word */
	bsym->data = malloc(w_words * h * 2 * 2);
	if (!bsym->data)
	{
		free(bsym);
		return NULL;
	}

	bsym->w_words = w_words;
	bsym->h = h;
	memset(bsym->data, 0, w_words * h * 4);

	assert(clr < (int)(sizeof(color_mappings) / sizeof(color_mappings[0])));
	color_mapping = color_mappings[clr];

	for (y1 = 0; y1 < symbol->h; ++y1)
	{
		for (x1 = 0; x1 < symbol->w; ++x1)
		{
			int pixel = symbol->data[y1 * symbol->w + x1];
			if (pixel != 0)
			{
				int color = color_mapping[pixel];
				int word_idx = x1 >> 4;
				uint16_t mask = pixel_mask_table[x1 & 15];
				uint16_t *planes = (uint16_t *)((uint8_t *)bsym->data + y1 * w_words * 4 + word_idx * 4);

				if (color & 1)
					planes[0] |= mask;
				if (color & 2)
					planes[1] |= mask;
			}
		}
	}

	/* Add to cache */
	entry = malloc(sizeof(blit_sym_cache_entry_t));
	if (!entry)
	{
		free(bsym->data);
		free(bsym);
		return NULL;
	}
	entry->original_sym = symbol;
	entry->faction = clr;
	entry->blit_sym = bsym;
	entry->next = blit_sym_cache_head;
	blit_sym_cache_head = entry;

	return bsym;
}

/* Optimized symbol display - uses pre-rendered bitplane data.
   Blitter path when available, fast CPU word-XOR fallback otherwise. */
void Vid_DispSymbol(int x, int y, sopsym_t *symbol, faction_t clr)
{
	int w = symbol->w, h = symbol->h;
	int left_skip_pixels = 0;
	int left_skip_words = 0;
	int x_off;
	int dst_w_words;
	blit_sym_t *bsym;
	int y1;

	if (x >= SCR_WDTH || y < 0 || y >= SCR_HGHT)
	{
		return;
	}

	if (w == 1 && h == 1)
	{
		Vid_XorPixel(x, y, clr);
		return;
	}

	bsym = find_or_create_blit_sym(symbol, clr);
	if (!bsym)
		return;

	w = symbol->w;
	h = symbol->h;

	if (y - h + 1 < 0)
	{
		h = y + 1;
	}

	if (x < 0)
	{
		left_skip_pixels = -x;
		w += x;
		x = 0;
	}
	if (x + w > SCR_WDTH)
	{
		w = SCR_WDTH - x;
	}

	if (w <= 0 || h <= 0)
	{
		return;
	}

	x_off = x & 15;
	dst_w_words = (x_off + w + 15) >> 4;
	left_skip_words = left_skip_pixels >> 4;

	if (has_blitter && IS_ST_RAM(draw_page))
	{
		volatile char *ctrl = (volatile char *)0xFF8A3CL;
		uint8_t *dst_row;

		dst_row = draw_page + (SCR_HGHT - 1 - y) * SCREEN_STRIDE + (x >> 4) * 8;

		*(volatile short *)0xFF8A3A = (short)((x_off) << 8) | 0; /* SKEW, HOP=0 */
		*(volatile char *)0xFF8A3B = 6;															/* OP=6 (XOR) */

		*(volatile short *)0xFF8A20 = 4; /* Src X Inc */
		*(volatile short *)0xFF8A22 = (bsym->w_words * 4) - (dst_w_words * 4); /* Src Y Inc */

		*(volatile short *)0xFF8A2E = 8; /* Dst X Inc */
		*(volatile short *)0xFF8A30 = SCREEN_STRIDE - (dst_w_words * 8); /* Dst Y Inc */

		*(volatile long *)0xFF8A24 = (long)bsym->data + left_skip_words * 4;
		*(volatile long *)0xFF8A32 = (long)dst_row;

		*(volatile short *)0xFF8A28 = 0xFFFF >> x_off;
		*(volatile short *)0xFF8A2C = 0xFFFF << (15 - ((x + w - 1) & 15));

		*(volatile short *)0xFF8A36 = dst_w_words;
		*(volatile short *)0xFF8A38 = h;

		*ctrl = 0xC0;
		while (*ctrl & 0x80)
			;
		return;
	}

	/* CPU fallback using pre-rendered bitplane data.
	   The blit_sym_t stores interleaved plane0/plane1 words.
	   We shift and XOR them into the screen. */
	{
		int shift = x_off;
		int src_row_words = bsym->w_words; /* words per plane per row in source */

		for (y1 = 0; y1 < h; ++y1)
		{
			int py = y - y1;
			uint8_t *dst_row;
			const uint16_t *src_planes;
			int wi;

			if ((unsigned)py >= SCR_HGHT)
				continue;

			dst_row = draw_page + (SCR_HGHT - 1 - py) * SCREEN_STRIDE + (x >> 4) * 8;
			/* Source row: each row is w_words * 4 bytes (2 planes, 2 bytes each) */
			src_planes = (const uint16_t *)((const uint8_t *)bsym->data
				+ y1 * src_row_words * 4) + left_skip_words * 2;

			if (shift == 0)
			{
				/* Aligned case: direct XOR, no shifting needed */
				for (wi = 0; wi < dst_w_words; ++wi)
				{
					uint16_t s0 = src_planes[wi * 2];
					uint16_t s1 = src_planes[wi * 2 + 1];
					uint16_t *dst = (uint16_t *)(dst_row + wi * 8);
					dst[0] ^= s0;
					dst[1] ^= s1;
				}
			}
			else
			{
				/* Unaligned: shift source words right by 'shift' bits
				   and XOR into two destination word groups */
				uint32_t carry0 = 0, carry1 = 0;
				for (wi = 0; wi < src_row_words - left_skip_words; ++wi)
				{
					uint16_t s0 = src_planes[wi * 2];
					uint16_t s1 = src_planes[wi * 2 + 1];
					uint32_t wide0 = ((uint32_t)s0 << 16) >> shift;
					uint32_t wide1 = ((uint32_t)s1 << 16) >> shift;
					uint16_t *dst = (uint16_t *)(dst_row + wi * 8);

					dst[0] ^= (uint16_t)(wide0 >> 16) | (uint16_t)carry0;
					dst[1] ^= (uint16_t)(wide1 >> 16) | (uint16_t)carry1;
					carry0 = wide0 & 0xFFFF;
					carry1 = wide1 & 0xFFFF;
				}
				/* Flush remaining carry bits */
				if (carry0 | carry1)
				{
					uint16_t *dst = (uint16_t *)(dst_row + wi * 8);
					dst[0] ^= (uint16_t)carry0;
					dst[1] ^= (uint16_t)carry1;
				}
			}
		}
	}
}

int Vid_FuselageColor(faction_t f)
{
	assert(f < (int)(sizeof(color_mappings) / sizeof(color_mappings[0])));
	return color_mappings[f][1];
}

void Vid_ClearBuf(void)
{
	if (!initted)
	{
		Vid_Init();
	}

	if (has_blitter && IS_ST_RAM(draw_page))
	{
		blitter_clear(draw_page, SCREEN_BYTES);
	}
	else
	{
		memset(draw_page, 0, SCREEN_BYTES);
	}
}

/* --- Fast text rendering directly to planar framebuffer -------------- */

void Vid_DrawChar(int x, int y, int ch, int color)
{
	const uint8_t *glyph;
	uint8_t *row;
	int screen_y, gy;
	uint16_t fill0, fill1;

	if (ch < 0 || ch > 255)
		return;

	/* Screen Y of top pixel row of this character.
	   y is in character-cell coords (0 = top of screen).
	   Each cell is 8 pixels. Screen memory row 0 is the top. */
	screen_y = y * 8;

	glyph = font_data + ch * 8;
	fill0 = (color & 1) ? 0xFFFF : 0;
	fill1 = (color & 2) ? 0xFFFF : 0;

	/* Character is byte-aligned if x is a multiple of 8.
	   Since x is in character cells (each 8 pixels wide),
	   the pixel X = x*8. In low-res ST, 16 pixels per word,
	   so pixel X = x*8 means word index = x/2, bit offset = (x&1)*8. */

	if ((x & 1) == 0)
	{
		/* Even column: glyph byte goes into high byte of the word */
		int word_idx = x >> 1;
		int byte_off = word_idx * 8; /* offset in bytes to this word group */

		for (gy = 0; gy < 8; ++gy)
		{
			int sy = screen_y + gy;
			uint16_t mask;
			uint16_t nm;

			if (sy < 0 || sy >= SCR_HGHT)
				continue;

			row = draw_page + sy * SCREEN_STRIDE + byte_off;
			mask = (uint16_t)glyph[gy] << 8;
			nm = ~mask;

			/* Write planes 0 and 1 */
			*(uint16_t *)(row)     = (*(uint16_t *)(row)     & nm) | (fill0 & mask);
			*(uint16_t *)(row + 2) = (*(uint16_t *)(row + 2) & nm) | (fill1 & mask);
		}
	}
	else
	{
		/* Odd column: glyph byte goes into low byte of the word */
		int word_idx = x >> 1;
		int byte_off = word_idx * 8;

		for (gy = 0; gy < 8; ++gy)
		{
			int sy = screen_y + gy;
			uint16_t mask;
			uint16_t nm;

			if (sy < 0 || sy >= SCR_HGHT)
				continue;

			row = draw_page + sy * SCREEN_STRIDE + byte_off;
			mask = (uint16_t)glyph[gy]; /* low byte of word */
			nm = ~mask;

			*(uint16_t *)(row)     = (*(uint16_t *)(row)     & nm) | (fill0 & mask);
			*(uint16_t *)(row + 2) = (*(uint16_t *)(row + 2) & nm) | (fill1 & mask);
		}
	}
}

/* Fast horizontal line for status bar border (full-width lines) */
void Vid_HLine(int y, int color)
{
	uint8_t *row;
	uint16_t fill0, fill1, fill2, fill3;
	int i;

	if (y < 0 || y >= SCR_HGHT)
		return;

	/* y is in game coords (0=bottom), convert to screen row */
	row = draw_page + (SCR_HGHT - 1 - y) * SCREEN_STRIDE;
	fill0 = (color & 1) ? 0xFFFF : 0;
	fill1 = (color & 2) ? 0xFFFF : 0;
	fill2 = (color & 4) ? 0xFFFF : 0;
	fill3 = (color & 8) ? 0xFFFF : 0;

	/* 320 pixels = 20 word groups of 16 pixels each */
	for (i = 0; i < 20; ++i)
	{
		uint16_t *words = (uint16_t *)(row + i * 8);
		words[0] = fill0;
		words[1] = fill1;
		words[2] = fill2;
		words[3] = fill3;
	}
}

/* Fast colorscreen: fill screen area with solid color using word writes */
void Vid_ColorScreen(int color)
{
	int y;
	uint16_t fill0, fill1;

	fill0 = (color & 1) ? 0xFFFF : 0;
	fill1 = (color & 2) ? 0xFFFF : 0;

	/* Fill from row 19 to 199 (game coords), which is screen rows 0..180 */
	for (y = 0; y <= SCR_HGHT - SBAR_HGHT; ++y)
	{
		int screen_row = (SCR_HGHT - 1 - (y + SBAR_HGHT - 1));
		uint8_t *row = draw_page + screen_row * SCREEN_STRIDE;
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

enum menukey Vid_ControllerMenuKey(void)
{
	return MENUKEY_NONE;
}

bool Vid_HaveController(void)
{
	return false;
}

const char *Vid_ControllerButtonName(enum gamekey key)
{
	(void)key;
	return NULL;
}
