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
    // Level 0's lighting is "inconsistently placed" in every version of the
    // lore, so its fittings are not all equally good: `vary` is how much dimmer
    // the worst working tube is than the best (0 = all identical), and
    // `faulty` the share of tubes that stutter. Both are read by the shader's
    // lightState() and mirrored in lightAtCPU — change one, change the other.
    float vary = 0.0f;
    float faulty = 0.07f;
    // "Old moist carpet": 1 turns on the shader's world-space damp patches,
    // which darken the pile and give it the gloss to mirror the tubes. World
    // space rather than baked into the 2 m carpet tile, because a puddle that
    // repeats every two metres is a pattern, not a leak.
    float wet = 0.0f;
};
constexpr int NLEVELS = 5;
extern const LevelCfg LEVELS[NLEVELS];
// where each level's exit door leads; the Red Halls and the party both dump you back at the start
extern const int EXIT_NEXT[NLEVELS];

// CPU-side estimate of the shader's room lighting, for tinting billboards.
// entX/entZ/entDark reproduce the hunter's pool of dead light (0 = no effect).
float lightAtCPU(float x, float y, float z, float blackout,
                 float ls, float ly, float dead, float mul, float ambLum,
                 float entX = 0, float entZ = 0, float entDark = 0, float vary = 0);

// How wet Level 0's carpet is at this point on the floor, 0..1 — the CPU twin
// of the shader's uWet patches, for the footsteps.
float carpetWetCPU(float x, float z);

// The Manila Room's panel mask (x0,z0,x1,z1) and chandelier (x,y,z,output),
// for lightAtCPU — the same values Game hands the shader as uRoomMask/uLamp.
void setLightExtrasCPU(const float mask[4], const float lamp[4]);
