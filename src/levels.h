#pragma once
// Per-level look and feel: wall height, light grid, fog, palette. The level
// index doubles as the world generator's mode switch (see world.cpp).
#include "raylib.h"

struct LevelCfg {
    float wallH, ls, dead, lightMul, fogDen, gloss;
    Vector3 lightCol, amb, fogCol;
    const char *name;
};
constexpr int NLEVELS = 5;
extern const LevelCfg LEVELS[NLEVELS];
// where each level's exit door leads; the Red Halls and the party both dump you back at the start
extern const int EXIT_NEXT[NLEVELS];

// CPU-side estimate of the shader's room lighting, for tinting billboards
float lightAtCPU(float x, float y, float z, float blackout,
                 float ls, float ly, float dead, float mul, float ambLum);
