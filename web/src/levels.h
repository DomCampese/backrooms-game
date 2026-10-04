#pragma once
// How each level looks: light colour and output, fog, gloss, damp. The numbers
// the generator and the rules read (wall height, light pitch, storey pitch,
// name, exits, which tubes work) are core's LEVEL_RULES; LevelCfg extends that
// table, so LEVELS[lv].wallH and LEVEL_RULES[lv].wallH are one definition.
#include "raylib.h"
#include "core/level_rules.h"
#include "core/layout.h"

struct LevelCfg : LevelRules {
    float lightMul;   // brightness of the ones that work
    float fogDen;     // exponential fog density
    float gloss;      // floor specular; the shader skips specular below 0.10
    Vector3 lightCol; // colour of the fluorescents
    Vector3 amb;      // ambient floor, so unlit corners are not pure black
    Vector3 fogCol;   // what the fog fades to at range
    // World-space damp patches in the shader (uWet), and where on the 0..1
    // patch field they start (uWetFrom, carpetWetCPU): 0.60 wets about a
    // quarter of the floor, 0.78 about 1-3%.
    float wet = 0.0f;
    float wetFrom = 0.60f;
};
extern const LevelCfg LEVELS[NLEVELS];
// World-space variation the shader lays over the tiled surfaces (uMacro, uBoard
// in shaders.cpp). SURF_MACRO is how far the tone wanders, +/- as a fraction:
// a texture repeats every 2-3 m and its grime repeats with it, and a real floor
// does not. CEIL_BOARD is the pitch of a suspended ceiling's boards, metres,
// for per-board tone and water stains (0 = not a board ceiling). It must be the
// pitch surfaces.cpp draws the grid at, or the stains straddle the T-bars.
extern const float SURF_MACRO[NLEVELS];
extern const float CEIL_BOARD[NLEVELS];

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
