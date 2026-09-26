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
    // Where on the damp-patch field the patches start (the field is 0..1, see
    // carpetWetCPU). 0.60 wets about a quarter of Level 0's carpet; Level 1's
    // concrete only pools "in inconsistent areas", about 3% at 0.78.
    float wetFrom = 0.60f;
    // Floor-to-floor pitch, metres; 0 for a level that is one floorplan. Level
    // 0's is its 3 m room, the metre of dark "cramped space" the Threshold
    // article puts above the ceiling tiles, and a slab: 4.32 m, which is 24
    // risers of 180 mm — a building's stair, not a ladder. See World::storeyH.
    float storeyH = 0.0f;
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
float carpetWetCPU(float x, float z, float from = 0.60f);

// The Manila Room's panel mask (x0,z0,x1,z1) and chandelier (x,y,z,output),
// for lightAtCPU — the same values Game hands the shader as uRoomMask/uLamp.
void setLightExtrasCPU(const float mask[4], const float lamp[4]);

// Storeys, for lightAtCPU: the pitch, the storey you are on, and the same
// occupancy snapshot the shader marches (four bytes a cell: this storey, the
// one below, the one above). A sprite on a flight is lit by the tubes of the
// floor it is nearest, a panel over an opening is not there to light it, and a
// sprite in a double-height hall gets the floor above's tubes as well — exactly
// as the shader does it, or sprites and rooms stop agreeing (see AGENTS.md).
struct StoreyLightCPU {
    float storeyH = 0.0f;
    int storey = 0;
    const unsigned char *occ = nullptr;
    int occN = 0, originI = 0, originK = 0;
};
void setStoreyLightCPU(const StoreyLightCPU &s);
// The per-storey offset into the tube hash — the shader's uStorey term.
float storeyHashOffset(int s);
