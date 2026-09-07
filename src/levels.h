#pragma once
// Per-level look and feel: wall height, light grid, fog, palette. The level
// index doubles as the world generator's mode switch (see world.cpp).
#include "raylib.h"

struct LevelCfg {
    float wallH;      // floor-to-ceiling height, metres
    float ls;         // light spacing: ceiling panels sit on a grid this many metres apart
    float dead;       // fraction of panels that are simply out (0 = all lit)
    float lightMul;   // overall brightness of the ones that work
    float fogDen;     // exponential fog density — how soon the corridor disappears
    float gloss;      // specular sheen on the floor: dry carpet ~0, wet tile high
    Vector3 lightCol; // colour of the fluorescents
    Vector3 amb;      // ambient floor, so unlit corners are not pure black
    Vector3 fogCol;   // what the fog fades to at range
    const char *name; // shown on the intro card and in the window title
};
constexpr int NLEVELS = 5;
extern const LevelCfg LEVELS[NLEVELS];
// where each level's exit door leads; the Red Halls and the party both dump you back at the start
extern const int EXIT_NEXT[NLEVELS];

// CPU-side estimate of the shader's room lighting, for tinting billboards.
// entX/entZ/entDark reproduce the hunter's pool of dead light (0 = no effect).
float lightAtCPU(float x, float y, float z, float blackout,
                 float ls, float ly, float dead, float mul, float ambLum,
                 float entX = 0, float entZ = 0, float entDark = 0);
