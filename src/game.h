#pragma once

#include <stdbool.h>

typedef struct State Gamestate;

Gamestate *game_init();

bool game_should_close(Gamestate *state);

void game_close(Gamestate *state);

#ifndef DEBUG
void game_draw(Gamestate *state);
#endif

#ifdef DEBUG
bool game_should_debug_reload(Gamestate *state);

typedef void game_draw_t(Gamestate *state);
#endif
