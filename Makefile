CC ?= clang
CFLAGS ?= -Wall

ifeq ($(OS),Windows_NT)
	_WINDOWS = set
	ifeq ($(RAYLIB_PATH),)
	else
		_CFLAGS = $(CFLAGS) -I$(RAYLIB_PATH)/include -L$(RAYLIB_PATH)/lib -lraylib -lgdi32 -lwinmm
	endif
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
		_LINUX = set
		_CFLAGS = $(CFLAGS) -lraylib -Wl,-rpath,./bin
		_HOTRELOAD = set
	endif
endif

help:
	@echo
	@echo -e "Usage:"
	@echo -e "  make build\t- Build the code"
	@echo -e "  make run  \t- Run the code"
	@echo -e "  make debug\t- Build and run with debug mode"
ifeq ($(_HOTRELOAD),set)
	@echo -e "  make watch\t- Build and run with hot reload mode"
	@echo -e "  make dev  \t- Build and run with debug and hot reload mode"
endif
	@echo -e "  make clean\t- Clean the object files and bin directory"

build: src/*
	$(if $(_CFLAGS),$(),$(if $(_WINDOWS),$(error Please set RAYLIB_PATH to the path of your Raylib installation either by setting it as an environment variable using `set RAYLIB_PATH=path/to/your/raylib/raylib-5.0_win64_mingw-w64` or by using `make RAYLIB_PATH=path/to/your/raylib/raylib-5.0_win64_mingw-w64`),$()))
	@echo -e "Building executable ..."
	@mkdir -p bin
	$(CC) src/main.c src/game.c src/net.c -o bin/main-build $(_CFLAGS) -Wl,-rpath,./bin

run: build
	@echo -e "Running executable ..."
	@bin/main-build

debug: src/*
	$(if $(_CFLAGS),$(),$(if $(_WINDOWS),$(error Please set RAYLIB_PATH to the path of your Raylib installation either by setting it as an environment variable using `set RAYLIB_PATH=path/to/your/raylib/raylib-5.0_win64_mingw-w64` or by using `make RAYLIB_PATH=path/to/your/raylib/raylib-5.0_win64_mingw-w64`),$()))
	@echo -e "Building executable with debug mode ..."
	@mkdir -p bin
	$(CC) src/main.c src/game.c src/net.c -o bin/main-debug $(_CFLAGS) -DDEBUG -Wl,-rpath,./bin
	@echo -e "Running executable ..."
	@bin/main-debug

ifeq ($(_HOTRELOAD),set)
_game.so: src/game.c src/net.c
	@echo -e "Building game.so ..."
	@mkdir -p bin
	$(CC) src/game.c -fpic -shared -o bin/game.so $(_CFLAGS) -DHOTRELOADING -Wl,-rpath,./bin

watch: src/* _game.so
	@echo -e "Building executable with hot reload mode ..."
	@mkdir -p bin
	$(CC) src/net.c -fpic -shared -o bin/net.so $(_CFLAGS)
	$(CC) src/main.c src/game.c -o bin/main $(_CFLAGS) -DHOTRELOAD -Wl,-rpath,./bin
	@echo -e "Running executable ..."
	@bin/main

_game.so-debug: src/game.c src/net.c
	@echo -e "Building game.so with debug mode ..."
	@mkdir -p bin
	$(CC) src/game.c -fpic -shared -o bin/game.so $(_CFLAGS) -DHOTRELOADING -DDEBUG -Wl,-rpath,./bin

dev: src/* _game.so-debug
	@echo -e "Building executable with debug and hot reload mode ..."
	@mkdir -p bin
	$(CC) src/net.c -fpic -shared -o bin/net.so $(_CFLAGS)
	$(CC) src/main.c src/game.c -o bin/main $(_CFLAGS) -DDEBUG -DHOTRELOAD -Wl,-rpath,./bin
	@echo -e "Running executable ..."
	@bin/main
else

endif

clean:
	@echo -e "Deleting build files ..."
	@rm -f bin/ -r
	@rm -f result

.PHONY: help build run _game.so _game.so-debug debug watch dev clean
