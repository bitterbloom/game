#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "raylib.h"

#include "./game.h"

typedef unsigned int uint;

typedef enum {
    TITLE_SCREEN,
    GAME_SCREEN,
} Screen;

struct State {
    Screen screen_kind;
    uint frames;
};

Gamestate *game_init() {
    InitWindow(800, 450, "Title");
    SetTargetFPS(60);

    Gamestate *state = malloc(sizeof(Gamestate));
    state->screen_kind = TITLE_SCREEN;
    state->frames = 0;

    return state;
}

#ifndef DEBUG
static void draw_title_screen(Gamestate *state, int const screen_width, int const screen_height) {
    char *const str = malloc(100);
    snprintf(str, 100, "frames: %u", state->frames);

    int i = state->frames * 2;
    int const maxY = screen_height / 2;
    if (i >= maxY) i = maxY;

    int const font_size = 128 - (i/5);
    Vector2 const font_dim = MeasureTextEx(GetFontDefault(), str, font_size, 0);
    int const posX = screen_width / 2.0 - font_dim.x / 1.75;
    int const posY = i - font_dim.y;

    DrawText(str, posX, posY, font_size, BLACK);
}

static void draw_game_screen() {
    DrawText("Some cool text!", 190, 200, 20, LIGHTGRAY);
}

void game_draw(Gamestate *state) {
    state->frames++;

    BeginDrawing();

    ClearBackground(ColorFromHSV(state->frames, 1.0, 1.0));

    switch (state->screen_kind) { 
        case TITLE_SCREEN:
            draw_title_screen(state, GetScreenWidth(), GetScreenHeight());
            break;
        case GAME_SCREEN:
            draw_game_screen();
            break;
    }

    EndDrawing();
}
#endif

bool game_should_close(Gamestate *state) {
    return WindowShouldClose();
}

void game_close(Gamestate *state) {
    free(state);
    CloseWindow();
}

bool game_should_debug_reload(Gamestate *state) {
    return IsKeyPressed(KEY_R);
}

