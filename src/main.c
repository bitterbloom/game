#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "./game.h"

#ifdef DEBUG
#include <dlfcn.h>

game_draw_t *debug_reload() {
    printf("Loading game.so...\n");

    static void *game_so = NULL;
    if (game_so != NULL) {
        dlclose(game_so);
        if (system("make bin/game.so")) {
            fprintf(stderr, "Failed to build game.so: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    game_so = dlopen("game.so", RTLD_NOW);
    if (game_so == NULL) {
        fprintf(stderr, "Failed to load game.so: %s\n", dlerror());
        exit(EXIT_FAILURE);
    }

    game_draw_t *game_draw = dlsym(game_so, "game_draw");
    if (game_draw == NULL) {
        fprintf(stderr, "Failed to load game_draw: %s\n", dlerror());
        exit(EXIT_FAILURE);
    }

    printf("Loaded game.so\n");

    return game_draw;
}
#endif

int main(int const argc, char const *const *const argv) {

#ifdef DEBUG
    game_draw_t *game_draw = debug_reload();
#endif

    Gamestate *state = game_init();

    while (!game_should_close(state)) {
        game_draw(state);

#ifdef DEBUG
        if (game_should_debug_reload(state)) {
            game_draw = debug_reload();
        }
#endif
    }

    game_close(state);

    return EXIT_SUCCESS;
}

