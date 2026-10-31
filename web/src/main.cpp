// The Web build's entry point: raylib and OpenGL 3.3 natively, WebGL 2 (GLSL ES
// 3.00) under Emscripten. AGENTS.md has the module map.
#include "game.h"

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>

// A browser tab owns its event loop: a `while (!WindowShouldClose())` here never
// yields, so the page hangs before the first frame. The 0 below means the loop
// runs on requestAnimationFrame.
//
// Game is heap-allocated and deliberately never deleted. With
// simulate_infinite_loop set, emscripten_set_main_loop unwinds out of main()
// rather than returning, so a stack object's destructor would run against a
// game the loop is still calling into.
static Game *gGame = nullptr;

static void webFrame() {
    if (!gGame->tick()) emscripten_cancel_main_loop();
}

int main() {
    gGame = new Game();
    gGame->init();
    emscripten_set_main_loop(webFrame, 0, 1);
    return 0;
}

#else

int main() {
    Game game;
    game.init();
    while (!WindowShouldClose())
        if (!game.tick()) break;
    game.shutdown();
    return 0;
}

#endif
