// THE BACKROOMS — Level 0
// Native game on raylib + OpenGL 3.3. All textures and sounds are procedural.
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

int main() {
    Game game;
    game.init();
    while (!WindowShouldClose())
        if (!game.tick()) break;
    game.shutdown();
    return 0;
}
