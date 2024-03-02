CC = clang
CFLAGS = -Wall -lraylib

help:
	@echo
	@echo -e "Usage:"
	@echo -e "  make build\t- Build the code"
	@echo -e "  make run\t- Run the code"
	@echo -e "  make debug\t- Build and run the code with debug mode"
	@echo -e "  make clean\t- Clean the object files and bin directory"

bin/game.so: src/game.c
	@echo -e "Building game.so ..."
	@mkdir -p bin
	@$(CC) src/game.c -shared -o bin/game.so $(CFLAGS)

build: src/* bin/game.so
	@echo -e "Building executable ..."
	@mkdir -p bin
	@$(CC) src/main.c src/game.c -o bin/multiplayer-game $(CFLAGS) -Wl,-rpath,./bin

run: build
	@echo -e "Running executable ..."
	@bin/multiplayer-game

debug: src/* bin/game.so
	@echo -e "Building executable with debug mode ..."
	@mkdir -p bin
	@$(CC) src/main.c src/game.c -o bin/multiplayer-game $(CFLAGS) -DDEBUG -Wl,-rpath,./bin
	@echo -e "Copying shared object ..."
	@echo -e "Running executable ..."
	@bin/multiplayer-game

clean:
	@echo -e "Deleting object files and bin directory ..."
	@rm -f obj/*.o -r

.PHONY: help build run debug clean
