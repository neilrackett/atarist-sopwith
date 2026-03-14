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
// Status bar code
//

#include "video.h"

#include "swinit.h"
#include "swmain.h"
#include "swtext.h"

#ifdef PLATFORM_ATARI_TOS
/* Division by 13 via multiply-shift: floor(x / 13) = (x * 5042) >> 16
   Exact for 0 <= x <= 200 (our max ground height). */
static inline int div13(int x)
{
	return (int)(((unsigned int)x * 5042U) >> 16);
}
#endif

/* --- Cached minimap heights ------------------------------------------ */
/* The minimap scans 3000 ground samples per frame to find 158 column maxima.
   Since ground rarely changes (only on bomb craters), cache the downsampled
   heights and invalidate when ground is modified. */

#define MINIMAP_MAX_COLS 320  /* max possible map columns */
static uint8_t minimap_cache[MINIMAP_MAX_COLS];
static bool minimap_dirty = true;
static int minimap_num_cols;   /* actual number of columns (= max_x/wrld_rsx) */
static int minimap_wrld_rsx;   /* wrld_rsx when cache was built */

void minimap_invalidate(void)
{
	minimap_dirty = true;
}

static void minimap_rebuild(void)
{
	const int wrld_rsx = WRLD_RSX;
	const int max_x = currgame->gm_max_x;
	const GRNDTYPE *gnd = ground;
	int num_cols = max_x / wrld_rsx;
	int x, i;

	if (num_cols > MINIMAP_MAX_COLS)
		num_cols = MINIMAP_MAX_COLS;

	for (x = 0; x < num_cols; ++x) {
		unsigned int maxh = gnd[0];
		for (i = 1; i < wrld_rsx; ++i) {
			unsigned int g = gnd[i];
			if (g > maxh) maxh = g;
		}
		gnd += wrld_rsx;
#ifdef PLATFORM_ATARI_TOS
		minimap_cache[x] = (uint8_t)div13((int)maxh);
#else
		minimap_cache[x] = (uint8_t)(maxh / (unsigned)WRLD_RSY);
#endif
	}

	minimap_num_cols = num_cols;
	minimap_wrld_rsx = wrld_rsx;
	minimap_dirty = false;
}

#ifdef PLATFORM_ATARI_TOS
/*
 * Fast minimap pixel plotting - avoids Vid_PlotPixel overhead.
 * The minimap area is small and always in-bounds, so we skip all
 * bounds checks and compute addresses incrementally.
 *
 * Atari ST low-res: 4 bitplanes, 16 pixels per word group (8 bytes).
 * Screen stride = 160 bytes per row. Row 0 in screen memory = top.
 * Game Y=0 is bottom of screen, so game Y maps to screen row (199 - Y).
 */
#define MAP_SCREEN_STRIDE 160

/* Plot a single pixel at known-good minimap coordinates.
   clr is 0-15. x,y are in game coords (y=0 is bottom). */
static inline void map_plot(uint8_t *screen_base, int x, int y, int clr)
{
	uint16_t *words;
	uint16_t mask;

	words = (uint16_t *)(screen_base + (199 - y) * MAP_SCREEN_STRIDE
	                     + (x >> 4) * 8);
	mask = 0x8000U >> (x & 15);

	if (clr & 1) words[0] |= mask; else words[0] &= ~mask;
	if (clr & 2) words[1] |= mask; else words[1] &= ~mask;
	if (clr & 4) words[2] |= mask;
	if (clr & 8) words[3] |= mask;
}

/* Plot pixel using incremental offset from previous column.
   'words' points to the current word group. Returns updated pointer
   if we cross a 16-pixel word boundary. */
static inline void map_plot_same_col(uint16_t *words, uint16_t mask, int clr)
{
	if (clr & 1) words[0] |= mask; else words[0] &= ~mask;
	if (clr & 2) words[1] |= mask; else words[1] &= ~mask;
	if (clr & 4) words[2] |= mask;
	if (clr & 8) words[3] |= mask;
}

#endif /* PLATFORM_ATARI_TOS */

static void dispribbonrow (int *ribbonid, int ribbons_nr, int y)
{
	int i;
	int offset = 50 + ((3 - ribbons_nr) * 4);

	for (i = 0; i < ribbons_nr; i++) {
		int ribbon_id = ribbonid[i];

		Vid_DispSymbol(offset, y, &symbol_ribbon[ribbon_id].sym[0],
		               FACTION_PLAYER1);
		offset += 8;
	}
}

static void dispmedals(OBJECTS *ob)
{
	static const int medal_widths[3] = {10, 8, 8};
	static const int medal_offsets[3] = {0, -1, -1};
	int medal_offset = 50;
	int i;

	for (i = 0; i < ob->ob_score.medals_nr; i++) {
		int medal_id = ob->ob_score.medals[i];

		Vid_DispSymbol(medal_offset + medal_offsets[medal_id],
		               11, &symbol_medal[medal_id].sym[0],
		               FACTION_PLAYER1);
		medal_offset += medal_widths[medal_id];
	}

	if (ob->ob_score.ribbons_nr <= 3) {
		dispribbonrow(ob->ob_score.ribbons,
		              ob->ob_score.ribbons_nr, 15);
	} else {
		dispribbonrow(ob->ob_score.ribbons, 3, 16);
		dispribbonrow(ob->ob_score.ribbons + 3,
		              ob->ob_score.ribbons_nr - 3, 14);
	}
}

/* Fast integer-to-string for score display, avoids heavyweight snprintf */
static void itoa_score(int val, char *buf, int bufsz)
{
	char tmp[12];
	int i = 0, neg = 0;

	if (bufsz <= 0) return;

	if (val < 0) {
		neg = 1;
		val = -val;
	}
	do {
		tmp[i++] = '0' + (val % 10);
		val /= 10;
	} while (val > 0 && i < (int)sizeof(tmp));

	if (neg && i < (int)sizeof(tmp)) tmp[i++] = '-';

	/* Reverse into buf */
	{
		int j = 0;
		while (i > 0 && j < bufsz - 1) {
			buf[j++] = tmp[--i];
		}
		buf[j] = '\0';
	}
}

static void dispscore(OBJECTS * ob)
{
	char buf[10];
	int x;

	Vid_Box(0, 16, 48 + 32, 16, 0);

	// We adjust position for large scores to not overwrite the medals.
	if (!in_range(-9999, ob->ob_score.score, 99999)) {
		// Wow?
		x = 0;
	} else if (!in_range(-999, ob->ob_score.score, 9999)) {
		x = 1;
	} else {
		x = 2;
	}
	swposcur(x, 24);
	swcolor(ob->ob_clr);
	itoa_score(ob->ob_score.score, buf, sizeof(buf));
	swputs(buf);
}

static void dispgge(int x, int cury, int maxy, int clr)
{
	int y;

	cury = clamp_max(9, cury * 10 / maxy - 1);
	for (y = 0; y <= cury; ++y) {
		Vid_PlotPixel(x, y, clr);
	}
	for (; y <= 9; ++y) {
		Vid_PlotPixel(x, y, 0);
	}
}

static void dispgauges(OBJECTS *ob)
{
	int x = GAUGEX;
	int sep = conf_missiles ? 3 : 5;

	// crashes/lives
	dispgge(x += sep, maxcrash - ob->ob_crashcnt, maxcrash, 1);

	// fuel
	dispgge(x += sep, ob->ob_life >> 4, MAXFUEL >> 4, 1);

	// bombs
	dispgge(x += sep, ob->ob_bombs, MAXBOMBS, 2);

	// bullets
	dispgge(x += sep, ob->ob_rounds, MAXROUNDS, 3);

	if (conf_missiles) {

		// missiles
		dispgge(x += sep, ob->ob_missiles, MAXMISSILES, 1);

		// starburst (flares)
		dispgge(x += sep, ob->ob_bursts, MAXBURSTS, 2);
	}
}

static void dispmapobjects(int wrld_rsx
#ifndef PLATFORM_ATARI_TOS
                           , int wrld_rsy
#else
                           , int wrld_rsy __attribute__((unused))
#endif
                           )
{
	int mapx, mapy, x, y, groundy;
	OBJECTS *ob;
	int max_x = currgame->gm_max_x;
#ifdef PLATFORM_ATARI_TOS
	extern uint8_t *vid_vram;
	uint8_t *scr = vid_vram;
#endif

	for (ob=objtop; ob; ob=ob->ob_next) {
		if (!ob->ob_onmap
		 || ob->ob_x < 0 || ob->ob_x >= max_x) {
			continue;
		}

		mapx = ob->ob_x + (ob->ob_symbol->w / 2);

		groundy = ground[ob->ob_x];
		if (ob->ob_y - ob->ob_symbol->h <= groundy) {
			mapy = ob->ob_y - ob->ob_symbol->h + 8;
		} else {
			mapy = ob->ob_y - (ob->ob_symbol->h / 2);
		}
		x = SCR_CENTR + mapx / wrld_rsx;
#ifdef PLATFORM_ATARI_TOS
		y = div13(mapy);
#else
		y = mapy / wrld_rsy;
#endif

		if (y < SCR_MNSH-1) {
#ifdef PLATFORM_ATARI_TOS
			map_plot(scr, x, y, Vid_FuselageColor(ob->ob_clr));
#else
			Vid_PlotPixel(x, y,
				      Vid_FuselageColor(ob->ob_clr));
#endif
		}
	}
}

static void dispmap(void)
{
	int x, y, dx, maxh, sx;
	const int wrld_rsx = WRLD_RSX;
	const int wrld_rsy = WRLD_RSY;
	const int max_x = currgame->gm_max_x;

	/* Rebuild minimap cache if dirty or parameters changed */
	if (minimap_dirty || minimap_wrld_rsx != wrld_rsx) {
		minimap_rebuild();
	}

#ifdef PLATFORM_ATARI_TOS
	/* Fast Atari path: iterate cached heights, inline pixel plotting */
	extern uint8_t *vid_vram;
	uint8_t *scr = vid_vram;
	uint16_t *col_words;
	uint16_t col_mask;
	int num_cols = minimap_num_cols;

	(void)dx; (void)max_x; (void)wrld_rsx; (void)wrld_rsy;

	sx = SCR_CENTR;
	col_words = (uint16_t *)(scr + 199 * MAP_SCREEN_STRIDE
	                         + (sx >> 4) * 8);
	col_mask = 0x8000U >> (sx & 15);
	y = 0;

	for (x = 0; x < num_cols; ++x) {
		maxh = minimap_cache[x];

		/* Draw ground trace - only the pixels that changed */
		if (maxh != y) {
			uint16_t *w;
			if (maxh > y) {
				int yy;
				for (yy = y + 1; yy <= maxh; ++yy) {
					w = (uint16_t *)((uint8_t *)col_words
					    - yy * MAP_SCREEN_STRIDE);
					w[0] |= col_mask;
					w[1] |= col_mask;
					w[2] |= col_mask;
				}
			} else {
				int yy;
				for (yy = y - 1; yy >= maxh; --yy) {
					w = (uint16_t *)((uint8_t *)col_words
					    - yy * MAP_SCREEN_STRIDE);
					w[0] |= col_mask;
					w[1] |= col_mask;
					w[2] |= col_mask;
				}
			}
			y = maxh;
		}
		/* Single pixel at current height */
		{
			uint16_t *w = (uint16_t *)((uint8_t *)col_words
			              - y * MAP_SCREEN_STRIDE);
			w[0] |= col_mask;
			w[1] |= col_mask;
			w[2] |= col_mask;
		}

		/* Plot sky marker at y=0, color 11 = planes 0,1,3 */
		col_words[0] |= col_mask;
		col_words[1] |= col_mask;
		col_words[3] |= col_mask;

		/* Advance to next map column */
		++sx;
		col_mask >>= 1;
		if (col_mask == 0) {
			col_mask = 0x8000U;
			col_words = (uint16_t *)((uint8_t *)col_words + 8);
		}
	}

	/* Map border - color 11 */
	{
		int border_h = div13(MAX_Y);
		uint16_t *left_words = (uint16_t *)(scr + 199 * MAP_SCREEN_STRIDE
		                       + (SCR_CENTR >> 4) * 8);
		uint16_t left_mask = 0x8000U >> (SCR_CENTR & 15);
		uint16_t *right_words = (uint16_t *)(scr + 199 * MAP_SCREEN_STRIDE
		                        + (sx >> 4) * 8);
		uint16_t right_mask = 0x8000U >> (sx & 15);

		for (y = 0; y <= border_h; ++y) {
			uint16_t *lw = (uint16_t *)((uint8_t *)left_words
			               - y * MAP_SCREEN_STRIDE);
			uint16_t *rw = (uint16_t *)((uint8_t *)right_words
			               - y * MAP_SCREEN_STRIDE);
			lw[0] |= left_mask;
			lw[1] |= left_mask;
			lw[3] |= left_mask;
			rw[0] |= right_mask;
			rw[1] |= right_mask;
			rw[3] |= right_mask;
		}
	}

	dispmapobjects(wrld_rsx, wrld_rsy);

	Vid_HLine(SCR_MNSH + 2, 7);

#else
	/* Generic path using cached heights */
	{
	int num_cols = minimap_num_cols;
	dx = 0;
	sx = SCR_CENTR;
	y = 0;

	for (x = 0; x < num_cols; ++x) {
		maxh = minimap_cache[x];
		if (maxh == y) {
			Vid_PlotPixel(sx, maxh, 7);
		} else if (maxh > y) {
			for (++y; y <= maxh; ++y) {
				Vid_PlotPixel(sx, y, 7);
			}
		} else {
			for (--y; y >= maxh; --y) {
				Vid_PlotPixel(sx, y, 7);
			}
		}
		y = maxh;
		Vid_PlotPixel(sx, 0, 11);
		++sx;
	}

	maxh = MAX_Y / wrld_rsy;
	for (y = 0; y <= maxh; ++y) {
		Vid_PlotPixel(SCR_CENTR, y, 11);
		Vid_PlotPixel(sx, y, 11);
	}

	dispmapobjects(wrld_rsx, wrld_rsy);
	}

	for (x = 0; x < SCR_WDTH; ++x) {
		Vid_PlotPixel(x, (SCR_MNSH + 2), 7);
	}
#endif
}

#ifdef PLATFORM_ATARI_TOS
#include "timer.h"
int _prof_map, _prof_score, _prof_gauge, _prof_medal;
#endif

void dispstatusbar(void)
{
#ifdef PLATFORM_ATARI_TOS
	int _t0 = Timer_GetMS();
#endif
	dispmap();
#ifdef PLATFORM_ATARI_TOS
	int _t1 = Timer_GetMS();
#endif
	dispscore(consoleplayer);
#ifdef PLATFORM_ATARI_TOS
	int _t2 = Timer_GetMS();
#endif
	dispgauges(consoleplayer);
#ifdef PLATFORM_ATARI_TOS
	int _t3 = Timer_GetMS();
#endif

	if (conf_medals) {
		dispmedals(consoleplayer);
	}
#ifdef PLATFORM_ATARI_TOS
	_prof_map = _t1 - _t0;
	_prof_score = _t2 - _t1;
	_prof_gauge = _t3 - _t2;
	_prof_medal = Timer_GetMS() - _t3;
#endif
}
