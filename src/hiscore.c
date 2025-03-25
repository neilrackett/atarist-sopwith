//
// Copyright(C) 2025 Simon Howard
//
// You can redistribute and/or modify this program under the terms of the
// GNU General Public License version 2 as published by the Free Software
// Foundation, or any later version. This program is distributed WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.
//
//
// High score table
//

#include <stdio.h>

#include "sw.h"

#include "hiscore.h"
#include "swtext.h"
#include "video.h"

#define MAX_HIGH_SCORES 10

#define TABLE_X 9
#define TABLE_Y 1

struct high_score {
	char name[4];
	score_t score;
};

struct high_score high_scores[MAX_HIGH_SCORES] = {
	{"DLC", {6500}},  // David L. Clark
	{"DG",  {6000}},  // Dave Growden (Ox)
	{"JHC", {5500}},  // Jack Cole
	{"JS",  {5000}},  // Jesse Smith
	{"JH",  {4500}},  // Josh Horowitz
	{"CR",  {4000}},  // Christoph Reichenbach
	{"AMJ", {3500}},  // Andrew Jenner
	{"HJM", {3000}},  // Harry Mason
	{"BMB", {2500}},
	{"SDH", {2000}},  // Simon Howard
};

void DrawHighScoreTable(void)
{
	const struct high_score *hs;
	char buf[20];
	int i, x, y, m;

#ifdef HISCORE_DEBUG
	for (i = 0; i < MAX_HIGH_SCORES; ++i) {
		high_scores[i].score.medals_nr = 2;
		high_scores[i].score.medals[0] = MEDAL_COMPETENCE;
		high_scores[i].score.medals[1] = MEDAL_VALOUR;
		for (m = 0; m < 6; ++m) {
			high_scores[i].score.ribbons[m] = m;
		}
		high_scores[i].score.ribbons_nr = 6;
	}
#endif

	swcolor(2);
	swposcur(TABLE_X + 3, TABLE_Y);
	swputs("TOP PILOTS");

	swcolor(3);

	// We draw the table bottom up. See comment below for why.
	for (i = MAX_HIGH_SCORES - 1; i >= 0; i--) {
		hs = &high_scores[i];
		swposcur(TABLE_X, TABLE_Y + 2 + i);
		snprintf(buf, sizeof(buf), "%-3.3s ....... %-4d",
		         hs->name, hs->score.score);
		swputs(buf);

		x = (TABLE_X + 16) * 8 + 4;
		y = (SCR_HGHT - 1) - (TABLE_Y + 2 + i) * 8 + 5;

		for (m = 0; m < hs->score.medals_nr; ++m) {
			symset_t *ss = &symbol_medal[hs->score.medals[m]];
			Vid_DispSymbol(x + m * 8, y, &ss->sym[0],
			               FACTION_PLAYER1);
		}

		// Medal symbols are 12 characters tall, but each line is
		// only 8 characters tall. The top part of the medal symbol
		// gets cut off so that we can pack all the medals in; we do
		// this by drawing a black box over the top of it.
		Vid_Box(x, y, 32, 4, 0);

		for (m = 0; m < hs->score.ribbons_nr; ++m) {
			symset_t *ss = &symbol_ribbon[hs->score.ribbons[m]];
			int rx = (m / 2) * 8, ry = (m % 2) * 4 + 6;
			Vid_DispSymbol(x + 18 + rx, y - ry, &ss->sym[0],
			               FACTION_PLAYER1);
		}
	}
}
