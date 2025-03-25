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

#define MAX_HIGH_SCORES 10

#define TABLE_X 11
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
	char buf[20];
	int i;

	swcolor(2);
	swposcur(TABLE_X + 2, TABLE_Y);
	swputs("HIGH SCORES");

	swcolor(3);

	for (i = 0; i < MAX_HIGH_SCORES; i++) {
		swposcur(TABLE_X, TABLE_Y + 2 + i);
		snprintf(buf, sizeof(buf), "%-3.3s ....... %-4d",
		         high_scores[i].name,
		         high_scores[i].score.score);
		swputs(buf);
	}
}
