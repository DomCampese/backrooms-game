#include "levels.h"
#include "util.h"
#include <cmath>

const LevelCfg LEVELS[NLEVELS] = {
    { 3.0f,  8.0f, 0.06f, 1.00f, 0.055f, 0.06f, {1.00f,0.94f,0.74f}, {0.045f,0.042f,0.030f}, {0.140f,0.125f,0.070f}, "LEVEL 0" },
    { 4.2f, 12.0f, 0.30f, 0.85f, 0.075f, 0.22f, {0.72f,0.80f,0.95f}, {0.016f,0.017f,0.022f}, {0.018f,0.020f,0.026f}, "LEVEL 1" },
    // Poolrooms lightMul is down from 0.72: white tile at 0.87 albedo under a
    // grid of fluorescents was landing the whole floor above 240 and taking the
    // tile pattern with it. The level is meant to be bright, not blank.
    { 3.6f,  8.0f, 0.06f, 0.55f, 0.045f, 0.55f, {1.00f,1.00f,0.97f}, {0.16f,0.18f,0.20f},    {0.19f,0.23f,0.27f},    "THE POOLROOMS" },
    // The Red Halls' ambient is up from 0.030 with the move to a filmic tone
    // curve. It is not a brightening: the curve's toe eats small values, and at
    // the old figure the level fell from "supposed to look almost black" to
    // nothing readable at all — mean luma 8 out of 255, where it had been 16.
    // It sits near 12 now rather than the old 16: the rest of the drop is the
    // fog, which no longer glows at a fixed brightness down an unlit corridor.
    { 3.0f,  8.0f, 0.45f, 0.80f, 0.095f, 0.10f, {1.00f,0.22f,0.15f}, {0.052f,0.014f,0.011f}, {0.055f,0.010f,0.008f}, "THE RED HALLS" },
    { 3.0f,  8.0f, 0.10f, 1.05f, 0.055f, 0.06f, {1.00f,0.82f,0.76f}, {0.050f,0.040f,0.036f}, {0.150f,0.100f,0.085f}, "LEVEL FUN =)" },
};
// where each level's exit door leads; the Red Halls and the party both dump you back at the start
const int EXIT_NEXT[NLEVELS] = { 1, 2, 4, 0, 0 };

// -------------------------------------------------- CPU-side light estimate
// mirrors the shader's hash so billboard tinting matches the room lighting.
// Keep this in step with roomLight()/tonemap() in shaders.cpp: the two are one
// lighting model with two implementations, and when they drift what you see is
// sprites lit for a different room than the one they are standing in.
static const float PANEL_HALF_CPU = 0.62f;   // same panel rectangle as the shader
static float lhashCPU(float gx, float gz) {
    float v = sinf(gx * 127.1f + gz * 311.7f) * 43758.5453f;
    return v - floorf(v);
}
float lightAtCPU(float x, float y, float z, float blackout,
                        float ls, float ly, float dead, float mul, float ambLum,
                        float entX, float entZ, float entDark) {
    float bx = floorf((x - ls * 0.5f) / ls + 0.5f), bz = floorf((z - ls * 0.5f) / ls + 0.5f);
    float sum = 0;
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        float gx = bx + dx, gz = bz + dz;
        float h = lhashCPU(gx, gz);
        if (h < dead) continue;
        float lx = gx * ls + ls * 0.5f, lz = gz * ls + ls * 0.5f;
        // the panel is a rectangle, and the shader shades from the point on it
        // closest to the surface — mirror that, or a sprite standing under a
        // fitting comes out dimmer than the floor it is standing on
        float cx = clampf(x, lx - PANEL_HALF_CPU, lx + PANEL_HALF_CPU);
        float cz = clampf(z, lz - PANEL_HALF_CPU, lz + PANEL_HALF_CPU);
        float d2 = (cx - x) * (cx - x) + (ly - y) * (ly - y) + (cz - z) * (cz - z);
        float st = 1.0f;
        if (entDark > 0.01f) {   // mirror the shader: the fluorescents near it die
            float ed = sqrtf((lx - entX) * (lx - entX) + (lz - entZ) * (lz - entZ));
            float t = clampf((ed - 2.0f) / 7.0f, 0.0f, 1.0f); t = t * t * (3 - 2 * t);
            st = (1.0f - entDark) + entDark * t;
        }
        // mirror the shader's window, so billboard tinting doesn't step at the
        // light-cell boundaries either
        float wx = clampf((1.5f * ls - fabsf(lx - x)) / (0.20f * 1.5f * ls), 0.0f, 1.0f);
        float wz = clampf((1.5f * ls - fabsf(lz - z)) / (0.20f * 1.5f * ls), 0.0f, 1.0f);
        wx = wx * wx * (3 - 2 * wx); wz = wz * wz * (3 - 2 * wz);
        // A sprite is a billboard with no normal worth speaking of, so take the
        // shader's wrap at its softest — the value a surface facing the light
        // edge-on would get — rather than a lambert against a made-up normal.
        float w = clampf(PANEL_HALF_CPU / sqrtf(d2 + PANEL_HALF_CPU * PANEL_HALF_CPU), 0.10f, 0.80f);
        float ndl = (0.55f + w) / (1.0f + w);
        sum += st / (1.0f + 0.22f * d2) * 5.4f * mul * ndl * wx * wz;
    }
    float ambT = ambLum * 1.4f;
    ambT *= 1.0f + 5.5f / (1.0f + 40.0f * ambT);   // same toe compensation as the shader
    float lit = sum * blackout + ambT;
    if (entDark > 0.01f) {   // and the surface pool of shadow around it
        float fd = sqrtf((x - entX) * (x - entX) + (z - entZ) * (z - entZ));
        float t = clampf((fd - 0.8f) / 5.7f, 0.0f, 1.0f); t = t * t * (3 - 2 * t);
        lit *= (1.0f - entDark) + entDark * t;
    }
    // The world pass tone-maps before anything reaches the screen; props and
    // billboards are drawn with raylib's unlit shader and never go through it,
    // so the estimate has to come out the far side of the same curve or every
    // sprite in the game sits at a different exposure from the room around it.
    lit *= 0.70f;
    lit = (lit * (2.51f * lit + 0.03f)) / (lit * (2.43f * lit + 0.59f) + 0.14f);
    return clampf(lit, 0.0f, 1.0f);
}
