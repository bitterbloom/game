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
		_DEBUG = set
	endif
endif

help:
	@echo
	@echo -e "Usage:"
	@echo -e "  make build\t- Build the code"
	@echo -e "  make run\t- Run the code"
	$(if $(_DEBUG),$(@echo -e "  make debug\t- Build and run the code with debug mode"),$())
	@echo -e "  make clean\t- Clean the object files and bin directory"

build: src/*
	$(if $(_CFLAGS),$(),$(if $(_WINDOWS),$(error Please set RAYLIB_PATH to the path of your Raylib installation either by setting it as an environment variable using `set RAYLIB_PATH=path/to/your/raylib/raylib-5.0_win64_mingw-w64` or by using `make RAYLIB_PATH=path/to/your/raylib/raylib-5.0_win64_mingw-w64`),$()))
	@echo -e "Building executable ..."
	@mkdir -p bin
	$(CC) src/main.c src/game.c src/net.c -o bin/game $(_CFLAGS) -Wl,-rpath,./bin

run: build
	@echo -e "Running executable ..."
	@bin/game

ifeq ($(_DEBUG),set)
bin/game.so: src/game.c
	@echo -e "Building game.so ..."
	@mkdir -p bin
	$(CC) src/game.c src/net.c -fpic -shared -o bin/game.so $(_CFLAGS)

debug: src/* bin/game.so
	@echo -e "Building executable with debug mode ..."
	@mkdir -p bin
	$(CC) src/main.c src/game.c -o bin/game-debug $(_CFLAGS) -DDEBUG -Wl,-rpath,./bin
	@echo -e "Running executable ..."
	@bin/game-debug
endif

clean:
	@echo -e "Deleting build files ..."
	@rm -f bin/ -r
	@rm -f result

.PHONY: help build run debug clean
