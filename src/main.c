#include <stdlib.h>

#include "./util.h"
#include "./game.h"

#if defined(HOTRELOAD) && defined(__linux__)
    #include <string.h>

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
        if (fd == -1)
            EXIT_PRINT("Failed to initialize inotify: %s\n", strerror(errno));

        if (inotify_add_watch(fd, "src", IN_MODIFY) == -1)
            EXIT_PRINT("Failed to add watch: %s\n", strerror(errno));

        char buf[1024];
        while (true) {
            ssize_t len = read(fd, buf, sizeof(buf)); // blocks until file is modified
            if (len == -1)
                EXIT_PRINT("Failed to read inotify: %s\n", strerror(errno));
            *modified = true;
        }
    }

    // Returns a pointer to the game_update function dynamically loaded from game.so.
    // If a file depended on by game.so is modified, game.so is rebuild and reloaded.
    // (Uses static variables)
    static game_update_t *debug_reload(Gamestate *const state) {
        static void *game_so = NULL; // Shared object handle to game.so
        static game_update_t *game_update = NULL;
        static pthread_t watcher_id;
        static bool modified = false;

        if (game_so == NULL) { // On first invocation
            if (pthread_create(&watcher_id, NULL, watcher, &modified))
                EXIT_PRINT("Failed to create watcher thread: %s\n", strerror(errno));
            goto do_reload;
        }

        if (modified) {
            modified = false;
#ifndef DEBUG
            if (system("make _game.so")) {
#else
            if (system("make _game.so-debug")) {
#endif
                // If compilation fails, the game will continue to run the old code.
                fprintf(stderr, "Failed to build game.so: %s\n", strerror(errno));
                modified = false;
                return game_update;
            }
            if (dlclose(game_so))
                EXIT_PRINT("Failed to close game.so: %s\n", dlerror());
            goto do_reload;
        }

        if (game_should_debug_reload(state)) {
            if (dlclose(game_so))
                EXIT_PRINT("Failed to close game.so: %s\n", dlerror());
            goto do_reload;
        }

        return game_update;

    do_reload:
        printf("Loading game.so...\n");

        char const *error;

        game_so = dlopen("game.so", RTLD_NOW);
        error = dlerror();
        if (error != NULL) 
            EXIT_PRINT("Failed to load game.so: %s\n", error);

        game_update = dlsym(game_so, "game_update");
        error = dlerror();
        if (error != NULL)
            EXIT_PRINT("Failed to load game_update: %s\n", error);

        printf("Loaded game.so (game_update = %p)\n", game_update);

        return game_update;
    }
#endif // defined(HOTRELOAD) && defined(__linux__)

int main(int const argc, char const *const *const argv) {

    Gamestate *const state = game_init();

#if defined(HOTRELOAD) && defined(__linux__)
    game_update_t *game_update = debug_reload(state);
#endif

    while (!game_should_close(state)) {
        game_update(state);

#if defined(HOTRELOAD) && defined(__linux__)
        game_update = debug_reload(state);
#endif
    }

    game_close(state);

    return EXIT_SUCCESS;
}

