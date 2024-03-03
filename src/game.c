#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#include "./game.h"

typedef enum {
    TITLE_SCREEN,
    GAME_SCREEN,
} Screen;

struct State {
    Screen screen_kind;
    uint32_t frames;
};

Gamestate *game_init() {
    InitWindow(800, 450, "Title");
    SetTargetFPS(60);

    Gamestate *state = malloc(sizeof(Gamestate));
    state->screen_kind = TITLE_SCREEN;
    state->frames = 0;

    return state;
}

bool game_should_close(Gamestate *state) {
    return WindowShouldClose();
}

void game_close(Gamestate *state) {
    free(state);
    CloseWindow();
}

#ifndef DEBUG
static void draw_title_screen(Gamestate *state, int const screen_width, int const screen_height) {
    ClearBackground(ColorFromHSV(0.0, 1.0, 1.0));

    /* draw title */ {
        char *const str = malloc(100);
        snprintf(str, 100, "frames: %u", state->frames);

        int i = state->frames * 2;
        int const maxY = screen_height / 2.0;
        if (i >= maxY) i = maxY;

        int const font_size = 128 - (i/5);
        Vector2 const text_size = MeasureTextEx(GetFontDefault(), str, font_size, 0);
        int const posX = screen_width / 2.0 - text_size.x / 1.75;
        int const posY = i - text_size.y;

        DrawText(str, posX, posY, font_size, BLACK);
    }

    if (IsKeyPressed(KEY_SPACE)) state->frames = 0;

    /* draw menu */ {
        char const *const str1 = "Play as Server";
        char const *const str2 = "Play as Client";
        int const font_size = 32;
        Vector2 const text_size = MeasureTextEx(GetFontDefault(), str1, font_size, 0);
        int const posX = screen_width / 2.0 - text_size.x / 2.0;
        int const posY = screen_height / 2.0;

        uint32_t min = 125;
        uint8_t alpha;
        if (state->frames < min) alpha = 0;
        else if (state->frames > min + 255) alpha = 255;
        else alpha = state->frames - min;

        DrawText(str1, posX, posY, font_size, (Color) {0, 0, 0, alpha});
        DrawText(str2, posX, posY + text_size.y, font_size, (Color) {0, 0, 0, alpha});
    }
}

static void draw_game_screen(Gamestate *state) {
    ClearBackground(ColorFromHSV(state->frames, 1.0, 1.0));

    DrawText("Some cool text!", 190, 200, 20, LIGHTGRAY);
}

void game_draw(Gamestate *state) {
    state->frames++;

    BeginDrawing();

    switch (state->screen_kind) { 
        case TITLE_SCREEN:
            draw_title_screen(state, GetScreenWidth(), GetScreenHeight());
            break;
        case GAME_SCREEN:
            draw_game_screen(state);
            break;
    }

    EndDrawing();
}
#endif

bool game_should_debug_reload(Gamestate *state) {
    return IsKeyPressed(KEY_R);
}

