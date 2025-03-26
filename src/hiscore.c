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
#include <string.h>
#include <ctype.h>

#include "sw.h"

#include "hiscore.h"
#include "swgames.h"
#include "swmain.h"
#include "swsound.h"
#include "swtext.h"
#include "swtitle.h"
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

static void DrawHighScore(const struct high_score *hs, int x, int y)
{
	int px, py, m;
	char buf[20];

	swposcur(x, y);
	snprintf(buf, sizeof(buf), "%-3.3s ....... %-4d",
	         hs->name, hs->score.score);
	swputs(buf);

	if (!conf_medals) {
		return;
	}

	px = (x + 16) * 8 + 4;
	py = (SCR_HGHT - 1) - y * 8 + 5;

	for (m = 0; m < hs->score.medals_nr; ++m) {
		symset_t *ss = &symbol_medal[hs->score.medals[m]];
		Vid_DispSymbol(px + m * 8, py, &ss->sym[0],
		               FACTION_PLAYER1);
	}

	// Medal symbols are 12 characters tall, but each line is
	// only 8 characters tall. The top part of the medal symbol
	// gets cut off so that we can pack all the medals in; we do
	// this by drawing a black box over the top of it.
	Vid_Box(px, py, 32, 4, 0);

	for (m = 0; m < hs->score.ribbons_nr; ++m) {
		symset_t *ss = &symbol_ribbon[hs->score.ribbons[m]];
		int rx = (m / 2) * 8, ry = (m % 2) * 4 + 6;
		Vid_DispSymbol(px + 18 + rx, py - ry, &ss->sym[0],
		               FACTION_PLAYER1);
	}
}

void DrawHighScoreTable(void)
{
	int i;

#ifdef HISCORE_DEBUG
	for (i = 0; i < MAX_HIGH_SCORES; ++i) {
		int m;
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

	// We draw the table bottom up. See comment in DrawHighScore for why.
	for (i = MAX_HIGH_SCORES - 1; i >= 0; i--) {
		DrawHighScore(&high_scores[i], TABLE_X, TABLE_Y + 2 + i);
	}
}

static int CompareHighScores(const struct high_score *a,
                             const struct high_score *b)
{
	if (a->score.score != b->score.score) {
		return a->score.score - b->score.score;
	}
	if (a->score.medals_nr != b->score.medals_nr) {
		return a->score.medals_nr - b->score.medals_nr;
	}
	if (a->score.ribbons_nr != b->score.ribbons_nr) {
		return a->score.ribbons_nr - b->score.ribbons_nr;
	}
	return 0;
}

// NewHighScoreIndex returns the index in the high_scores[] array where the
// new high score should be inserted, or -1 if it is not a new high score.
static int NewHighScoreIndex(const struct high_score *hs)
{
	int i;

	for (i = 0; i < MAX_HIGH_SCORES; ++i) {
		if (CompareHighScores(hs, &high_scores[i]) > 0) {
			return i;
		}
	}

	return -1;
}

static bool IsNewHighScore(const struct high_score *hs)
{
	return NewHighScoreIndex(hs) >= 0;
}

static bool EnterHighScore(struct high_score *hs)
{
	char *p;

	// TODO: Add a new music track for the high score screen?
	soundoff();

	Vid_ClearBuf();
	DrawHighScoreTable();

	swcolor(2);
	swposcur(2, 15);
	swputs("NEW HIGH SCORE!");
	swcolor(3);
	swposcur(2, 17);
	DrawHighScore(hs, TABLE_X, 17);

	swposcur(TABLE_X - 1, 17);
	swputs("[    ]");
	clrprmpt();
	swposcur(TABLE_X, 17);
	swgets(hs->name, sizeof(hs->name) - 1);

	for (p = hs->name; *p != '\0'; ++p) {
		*p = toupper(*p);
	}

	return strlen(hs->name) > 0;
}

// NewHighScore checks if the given score is actually a new high score, and if
// so: prompts the user for their name and adds it.
bool NewHighScore(score_t *s)
{
	struct high_score new_hs = {"", *s};
	int idx;

	// High scores only apply to the default version of the game.
	if (playmode != PLAYMODE_COMPUTER
	 || currgame != &original_level
	 || conf_missiles || conf_wounded || !conf_animals
	 || !conf_big_explosions) {
		return false;
	}
	if (!IsNewHighScore(&new_hs)) {
		return false;
	}
	if (!EnterHighScore(&new_hs)) {
		return false;
	}

	idx = NewHighScoreIndex(&new_hs);
	if (idx < 0) {
		return false;
	}
	memmove(&high_scores[idx + 1], &high_scores[idx],
	        sizeof(struct high_score) * (MAX_HIGH_SCORES - 1 - idx));
	high_scores[idx] = new_hs;

	return true;
}
