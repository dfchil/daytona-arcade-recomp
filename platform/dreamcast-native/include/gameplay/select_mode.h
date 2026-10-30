#ifndef DAYTONA_NATIVE_GAMEPLAY_SELECT_MODE_H
#define DAYTONA_NATIVE_GAMEPLAY_SELECT_MODE_H
#include <stdint.h>
#include "frontend_pool.h"

/* The game start and the course/transmission select (include/select.h), between the attract and the race.
 * The mode, its pad and the select logic are compiled outside the LTO unit (gameplay/select_mode.c); the
 * game start's and the race entry's gameplay (include/frontend.h, the cinematic's and the race's own code)
 * run inside it, in code/practice.c (nm_practice_game_start, practice_race_load), so no race function
 * gets a caller outside the unit.
 *   from_attract  1: the cinematic just ran the credit's iteration (2818): its next two iterations (phases
 *                 12 and 17) leave the pool the race inherits; 0 from the title (the original's title runs
 *                 after the V.R. demo, which is not native: a cold pool).
 *   counter       the frame counter 5010b0 of the credit's iteration. */
int nm_select_mode_start(int from_attract, uint32_t counter);

/* The attract's START or A (code/practice.c's pad pass): 1 when it opened the settings screen (both
 * triggers and START; the attract's scene is released), else 0: a credit. */
int nm_select_attract_button(unsigned start, unsigned ltrigger, unsigned rtrigger);

/* The credit's iteration has run (code/practice.c's attract tick): release the attract's scene and start
 * the game with counter = 5010b0 of that iteration. 0 on failure. */
int nm_select_credit(uint32_t counter);

/* The start state the race entry applies (practice_race_load): 0 when the race did not come through the
 * select, else 1 with *pool (NULL: a cold pool) and the counter base of the phase20 tick. Consumed. */
int nm_select_race_state(const nm_entity_pool_t **pool, uint32_t *counter_base);

/* The game start's iterations on the live cinematic (code/practice.c, inside the LTO unit). */
int nm_practice_game_start(nm_entity_pool_t *pool);

/* The frame counter 5010b0 the front end keeps across scenes (main-loop iterations since power-on). */
uint32_t nm_frontend_counter(void);
void nm_frontend_counter_set(uint32_t counter);

/* Courses whose data this build carries (NATIVE_COURSES, a bit per course: 0 Beginner, 1 Expert,
 * 2 Advanced; default 5u, Beginner and Advanced). A course the select chose without data races course 0. */
unsigned nm_select_course_raced(unsigned chosen);

#endif
