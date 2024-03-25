#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

typedef struct State Gamestate;

Gamestate *game_init();

bool game_should_close(Gamestate const *state);

void game_close(Gamestate *state);

#ifndef DEBUG
void game_update(Gamestate *state);
#endif

#ifdef DEBUG
bool game_should_debug_reload(Gamestate const *state);

typedef void game_update_t(Gamestate *state);
#endif

typedef struct {
    uint32_t x;
    uint32_t y;
} point;

typedef struct {
    uint32_t id;
    point pos;
} Player;

