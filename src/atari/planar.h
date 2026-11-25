/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// Small helpers for writing straight into an ST Low page (four
// interleaved planes, 160 bytes a row), shared by both Atari backends.

#ifndef __PLANAR_H__
#define __PLANAR_H__

#include <stdint.h>

#define PLANAR_STRIDE 160

// Byte offset of screen row 'row' (0 = top): row * 160 as two shifts,
// as gcc -Os would otherwise call __mulsi3 for the multiply.
static inline unsigned int PlanarRowOffset(int row)
{
	return ((unsigned int)row << 7) + ((unsigned int)row << 5);
}

// An 8x8 glyph in colour 0-3 at p, the byte of plane 0 for its column
// (planes 0 and 1 are a word apart, so plane 1's byte is at p + 2), on
// screen rows first_row to first_row + 7 (0 = top). Glyph pixels take
// the colour; the others are left alone. A colour sets or clears each
// plane's bits outright, so each row is an OR or an AND per plane.
static inline void PlanarGlyph(uint8_t *p, const uint8_t *glyph, int color,
                               int first_row, int rows)
{
	int gy;

	if (first_row < 0 || first_row + 8 > rows)
	{
		// Partly off the screen: a row at a time, checked.
		for (gy = 0; gy < 8; ++gy, p += PLANAR_STRIDE)
		{
			int sy = first_row + gy;
			uint8_t m = glyph[gy];

			if (sy < 0 || sy >= rows || m == 0)
			{
				continue;
			}
			if (color & 1) p[0] |= m; else p[0] &= (uint8_t)~m;
			if (color & 2) p[2] |= m; else p[2] &= (uint8_t)~m;
		}
		return;
	}

	switch (color & 3)
	{
	case 0:
		for (gy = 0; gy < 8; ++gy, p += PLANAR_STRIDE)
		{
			uint8_t m = (uint8_t)~glyph[gy];
			p[0] &= m;
			p[2] &= m;
		}
		break;
	case 1:
		for (gy = 0; gy < 8; ++gy, p += PLANAR_STRIDE)
		{
			uint8_t m = glyph[gy];
			p[0] |= m;
			p[2] &= (uint8_t)~m;
		}
		break;
	case 2:
		for (gy = 0; gy < 8; ++gy, p += PLANAR_STRIDE)
		{
			uint8_t m = glyph[gy];
			p[0] &= (uint8_t)~m;
			p[2] |= m;
		}
		break;
	default:
		for (gy = 0; gy < 8; ++gy, p += PLANAR_STRIDE)
		{
			uint8_t m = glyph[gy];
			p[0] |= m;
			p[2] |= m;
		}
		break;
	}
}

#endif /* __PLANAR_H__ */
