# Native (desktop) build for the Chip-8 emulator using MSYS2/MinGW-w64 + SDL2.
# The web build lives separately in build_wasm.bat (Emscripten) and is untouched.
#
# Requires the MSYS2 UCRT64 toolchain on PATH (gcc, gdb) and SDL2 installed via
#   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gdb mingw-w64-ucrt-x86_64-SDL2
# Run with `mingw32-make` (or `make` if the make package is installed):
#   make            -> debug build
#   make release    -> optimised build
#   make run        -> build (debug) and launch with a default ROM
#   make clean      -> remove build artifacts

CC      := gcc
SRC     := main.c chip8.c
DEBUG_BIN   := chip8_debug.exe
RELEASE_BIN := chip8.exe

# SDL2 headers live in the UCRT64 include/SDL2 subdir; main.c uses <SDL.h>.
# Link order matters on Windows: mingw32 -> SDL2main -> SDL2.
SDL_INCLUDE := -IC:/msys64/ucrt64/include/SDL2
SDL_LIBS    := -lmingw32 -lSDL2main -lSDL2

WARNINGS := -Wall -Wextra
CSTD     := -std=c11

# Unit tests (greatest, single-header). The core (chip8.c) is SDL-free, so the
# test binary links it directly -- no SDL, no main.c.
TEST_SRC := tests/test_chip8.c chip8.c
TEST_BIN := test_chip8.exe

# Default ROM for `make run` (relative to this dir; the /roms absolute default
# in main.c only works inside the Emscripten virtual filesystem).
ROM ?= roms/Space_Invaders.ch8

.PHONY: all debug release run test clean

all: debug

debug: $(SRC)
	$(CC) -g -O0 $(WARNINGS) $(CSTD) $(SDL_INCLUDE) $(SRC) -o $(DEBUG_BIN) $(SDL_LIBS)

release: $(SRC)
	$(CC) -O2 $(WARNINGS) $(CSTD) $(SDL_INCLUDE) $(SRC) -o $(RELEASE_BIN) $(SDL_LIBS)

run: debug
	./$(DEBUG_BIN) $(ROM)

# Build and run the unit tests. Pass extra greatest flags via ARGS,
# e.g. `make test ARGS=-v` for verbose per-test output.
test: $(TEST_SRC)
	$(CC) -g -O0 $(WARNINGS) $(CSTD) $(TEST_SRC) -o $(TEST_BIN)
	./$(TEST_BIN) $(ARGS)

clean:
	rm -f $(DEBUG_BIN) $(RELEASE_BIN) $(TEST_BIN)
