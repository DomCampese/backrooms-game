// THE BACKROOMS — Level 0
// raylib + OpenGL 3.3 natively; WebGL 2 (GLSL ES 3.00) under Emscripten.
// Procedural worlds plus embedded CC0 materials and revolver.
//
// Module map:
//   util      hashes, RNG, value noise, shared palette
//   shaders   world + post-process GLSL
//   textures  every surface, synthesized at startup
//   sfx       one-shot sounds (footsteps, gunshot, ...)
//   audio     streaming ambience synth (hum, drone, water, music box)
//   levels    per-level look/feel tables
//   world     infinite maze: chunk gen, meshing, collision, line of sight
//   entity    PIRATE CLARK's state
//   game      run state + per-frame update logic
//   render    3D scene pass, viewmodel, HUD, overlays
#include "game.h"

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>

// A browser tab owns its own event loop: a `while (!WindowShouldClose())` here
// never yields, so the page hangs before the first frame is ever presented.
// The game is driven by requestAnimationFrame instead, which is what the 0 is.
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
