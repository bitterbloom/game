#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

//#define DEBUG

#include "./game.h"

#ifdef DEBUG
    #include <dlfcn.h>
    #include <sys/inotify.h>
    #include <pthread.h>
    #include <unistd.h>
    #include <errno.h>
    #include <stdnoreturn.h>

    // Input is just a pointer to a bool, which is set to true when the file is modified.
    // Does not return.
    // (Uses static variables)
    static noreturn void *watcher(void *args) {
        static int fd;
        bool *modified = args;

        fd = inotify_init();
        if (fd == -1) {
            fprintf(stderr, "Failed to initialize inotify: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (inotify_add_watch(fd, "src", IN_MODIFY) == -1) {
            fprintf(stderr, "Failed to add watch: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        char buf[1024];
        while (true) {
            ssize_t len = read(fd, buf, sizeof(buf)); // blocks until file is modified
            if (len == -1) {
                fprintf(stderr, "Failed to read inotify: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            *modified = true;
        }
    }

    // Returns a pointer to the game_draw function dynamically loaded from game.so.
    // If a file depended on by game.so is modified, game.so is rebuild and reloaded.
    // (Uses static variables)
    static game_draw_t *debug_reload(Gamestate *const state) {
        static void *game_so = NULL; // Shared object handle to game.so
        static game_draw_t *game_draw = NULL;
        static pthread_t watcher_id;
        static bool modified = false;

        if (game_so == NULL) { // On first invocation
            if (pthread_create(&watcher_id, NULL, watcher, &modified)) {
                fprintf(stderr, "Failed to create watcher thread: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            goto do_reload;
        }

        if (modified) {
            modified = false
            if (system("make bin/game.so")) {
                // If compilation fails, the game will continue to run the old code.
                fprintf(stderr, "Failed to build game.so: %s\n", strerror(errno));
                modified = false;
                return game_draw;
            }
            dlclose(game_so);
            goto do_reload;
        }

        if (game_should_debug_reload(state))
            goto do_reload;

        return game_draw;

    do_reload:
        printf("Loading game.so...\n");

        game_so = dlopen("game.so", RTLD_NOW);
        if (game_so == NULL) {
            fprintf(stderr, "Failed to load game.so: %s\n", dlerror());
            exit(EXIT_FAILURE);
        }

        game_draw = dlsym(game_so, "game_draw");
        if (game_draw == NULL) {
            fprintf(stderr, "Failed to load game_draw: %s\n", dlerror());
            exit(EXIT_FAILURE);
        }

        printf("Loaded game.so\n");

        return game_draw;
    }
#endif // DEBUG

int main(int const argc, char const *const *const argv) {

    Gamestate *const state = game_init();

#ifdef DEBUG
    game_draw_t *game_draw = debug_reload(state);
#endif

    while (!game_should_close(state)) {
        game_draw(state);

#ifdef DEBUG
        game_draw = debug_reload(state);
#endif
    }

    game_close(state);

    return EXIT_SUCCESS;
}

