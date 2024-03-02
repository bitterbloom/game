#pragma once

#include <stdbool.h>

typedef struct State Gamestate;

Gamestate *game_init();

#ifndef DEBUG
void game_draw(Gamestate *state);
#endif

#ifdef DEBUG
typedef void game_draw_t(Gamestate *state);
#endif

bool game_should_close(Gamestate *state);

void game_close(Gamestate *state);

#ifdef DEBUG
bool game_should_debug_reload(Gamestate *state);
#endif

