#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "raylib.h"

#include "./game.h"
#include "./net.h"

typedef enum {
    TITLE_SCREEN,
    GAME_SCREEN,
} Screen;

struct State {
    Screen screen_tag;
    union {
        struct { // Title Screen
            uint8_t ts_selected;
        };
        struct { // Game Screen
            bool gs_hosting;
            union {
                struct { // Server
                    ServerData *gss_serv_data;
                    uint16_t gss_player_count;
                    Player *gss_players;
                };
                struct { // Client
                    ClientData *gsc_clnt_data;
                    Player gsc_player;
                };
            };
            //char *gs_ip; Uses localhost for now
            uint16_t gs_net_port;
        };
    };
    uint32_t frames;
    int screen_width;
    int screen_height;
};

Gamestate *game_init() {
    Gamestate *const state = malloc(sizeof(Gamestate));
    *state = (Gamestate) {
        TITLE_SCREEN,
        .ts_selected = 0,
        .frames = 0,
        800, 450,
    };

    InitWindow(state->screen_width, state->screen_height, "Title");
    SetTargetFPS(60);

    return state;
}

bool game_should_close(Gamestate const *const state) {
    return WindowShouldClose();
}

void game_close(Gamestate *const state) {
    free(state);
    CloseWindow();
}

#ifndef DEBUG
static void goto_title_screen(Gamestate *const state) {
    state->screen_tag = TITLE_SCREEN;
    state->ts_selected = 0;
}

static void goto_game_screen(Gamestate *const state, bool const hosting) {
    state->screen_tag = GAME_SCREEN;
    state->gs_hosting = hosting;
    state->gs_net_port = 1234;
    if (hosting) {
        state->gss_players = malloc(10 * sizeof (Player));
        state->gss_player_count = 0;
        state->gss_serv_data = net_server_data_new(state->gss_players, &state->gss_player_count, 10);
        net_server_create(state->gs_net_port, state->gss_serv_data);
    }
    else {
        state->gsc_player.id = 0;
        state->gsc_player.x = 0;
        state->gsc_player.y = 0;
        state->gsc_clnt_data = net_client_data_new(&state->gsc_player);
        net_client_create(state->gs_net_port, state->gsc_clnt_data);
    }
}

static void update_title_screen(Gamestate *const state) {
    if (IsKeyPressed(KEY_SPACE)) state->frames = 0;

    if (IsKeyPressed(KEY_UP)   && state->ts_selected > 0) state->ts_selected--;
    if (IsKeyPressed(KEY_DOWN) && state->ts_selected < 1) state->ts_selected++;

    if (IsKeyPressed(KEY_ENTER)) switch (state->ts_selected) {
        case 0: goto_game_screen(state, true); break;
        case 1: goto_game_screen(state, false); break;
    }
}

static void update_game_screen(Gamestate *const state) {
    if (IsKeyDown(KEY_UP))    state->gsc_player.y -= 1;
    if (IsKeyDown(KEY_DOWN))  state->gsc_player.y += 1;
    if (IsKeyDown(KEY_LEFT))  state->gsc_player.x -= 1;
    if (IsKeyDown(KEY_RIGHT)) state->gsc_player.x += 1;
}

static void draw_title_screen(Gamestate const *const state) {
    ClearBackground(ColorFromHSV(150, 0.15, 1.0));

    /* draw title */ {
        char *const str = malloc(100);
        snprintf(str, 100, "frames: %u", state->frames);

        int i = state->frames * 5;
        int const maxY = state->screen_height / 2.0;
        if (i >= maxY) i = maxY;

        int const font_size = 128 - (i/5);
        Vector2 const text_size = MeasureTextEx(GetFontDefault(), str, font_size, 0);
        int const posX = state->screen_width / 2.0 - text_size.x / 1.75;
        int const posY = i - text_size.y;

        DrawText(str, posX, posY, font_size, BLACK);
        free(str);
    }

    /* draw menu */ {
        char const *const host_str_full = "* Host a game";
        char const *const host_str = state->ts_selected == 0 ? host_str_full : host_str_full + 2;
        char const *const join_str_full = "* Join a game";
        char const *const join_str = state->ts_selected == 1 ? join_str_full : join_str_full + 2;

        int const font_size = 32;

        Vector2 const host_size = MeasureTextEx(GetFontDefault(), host_str, font_size, 0);
        int const host_x = state->screen_width / 2.0 - host_size.x / 2.0;
        int const host_y = state->screen_height / 2.0;

        Vector2 const join_size = MeasureTextEx(GetFontDefault(), join_str, font_size, 0);
        int const join_x = state->screen_width / 2.0 - join_size.x / 2.0;
        int const join_y = state->screen_height / 2.0 + host_size.y;

        uint32_t const speed = 3;
        uint32_t const min = 50 * speed;
        uint32_t const time = state->frames * speed;
        uint8_t alpha;
        if (time < min) alpha = 0;
        else if (time > min + 255) alpha = 255;
        else alpha = time - min;

        DrawText(host_str, host_x, host_y, font_size, (Color) {0, 0, 0, alpha});
        DrawText(join_str, join_x, join_y, font_size, (Color) {0, 0, 0, alpha});
    }
}

static void draw_game_screen(Gamestate const *const state) {
    ClearBackground(ColorFromHSV(150, 0.15, 1.0));

    DrawText("Some cool text!", 190, 200, 20, BLACK);

    if (state->gs_hosting) {
        DrawText("Hosting a game", 190, 220, 20, BLACK);
    } else {
        DrawText("Joined a game", 190, 220, 20, BLACK);
    }

    char *const str = malloc(100);

    snprintf(str, 100, "Port: %u", state->gs_net_port);
    DrawText(str, 190, 240, 20, BLACK);

    if (state->gs_hosting) {
        for (uint16_t i = 0; i < state->gss_player_count; i++) {
            snprintf(str, 100, "X: %d, Y: %d", state->gss_players[i].x, state->gss_players[i].y);
            DrawText(str, 190, 260 + i * 20, 20, BLACK);

            DrawRectangle(state->gss_players[i].x, state->gss_players[i].y, 10, 10, ColorFromHSV(state->gss_players[i].id / 360.0, 1.0, 1.0));
        }
    }
    else {
        snprintf(str, 100, "X: %d, Y: %d", state->gsc_player.x, state->gsc_player.y);
        DrawText(str, 190, 260, 20, BLACK);

        snprintf(str, 100, "ID: %d", state->gsc_player.id);
        DrawText(str, 190, 280, 20, BLACK);

        DrawRectangle(state->gsc_player.x, state->gsc_player.y, 10, 10, ColorFromHSV(state->gsc_player.id / 360.0, 1.0, 1.0));
    }
}

void game_update(Gamestate *const state) {
    state->frames++;
    state->screen_width = GetScreenWidth();
    state->screen_height = GetScreenHeight();

    switch (state->screen_tag) {
        case TITLE_SCREEN:
            update_title_screen(state);
            break;
        case GAME_SCREEN:
            update_game_screen(state);
            break;
    }

    BeginDrawing();

    switch (state->screen_tag) { 
        case TITLE_SCREEN:
            draw_title_screen(state);
            break;
        case GAME_SCREEN:
            draw_game_screen(state);
            break;
    }

    EndDrawing();
}
#endif

bool game_should_debug_reload(Gamestate const *const state) {
    return IsKeyPressed(KEY_R);
}

