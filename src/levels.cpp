#include "levels.h"
#include "util.h"
#include <cmath>

const LevelCfg LEVELS[NLEVELS] = {
    { 3.0f,  8.0f, 0.06f, 1.00f, 0.055f, 0.06f, {1.00f,0.94f,0.74f}, {0.045f,0.042f,0.030f}, {0.140f,0.125f,0.070f}, "LEVEL 0" },
    { 4.2f, 12.0f, 0.30f, 0.85f, 0.075f, 0.22f, {0.72f,0.80f,0.95f}, {0.016f,0.017f,0.022f}, {0.018f,0.020f,0.026f}, "LEVEL 1" },
    { 3.6f,  8.0f, 0.06f, 0.72f, 0.045f, 0.55f, {1.00f,1.00f,0.97f}, {0.16f,0.18f,0.20f},    {0.19f,0.23f,0.27f},    "THE POOLROOMS" },
    { 3.0f,  8.0f, 0.45f, 0.80f, 0.095f, 0.10f, {1.00f,0.22f,0.15f}, {0.030f,0.008f,0.006f}, {0.055f,0.010f,0.008f}, "THE RED HALLS" },
    { 3.0f,  8.0f, 0.10f, 1.05f, 0.055f, 0.06f, {1.00f,0.82f,0.76f}, {0.050f,0.040f,0.036f}, {0.150f,0.100f,0.085f}, "LEVEL FUN =)" },
};
// where each level's exit door leads; the Red Halls and the party both dump you back at the start
const int EXIT_NEXT[NLEVELS] = { 1, 2, 4, 0, 0 };

// -------------------------------------------------- CPU-side light estimate
// mirrors the shader's hash so billboard tinting matches the room lighting
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
        float d2 = (lx - x) * (lx - x) + (ly - y) * (ly - y) + (lz - z) * (lz - z);
        float st = 1.0f;
        if (entDark > 0.01f) {   // mirror the shader: the fluorescents near it die
            float ed = sqrtf((lx - entX) * (lx - entX) + (lz - entZ) * (lz - entZ));
            float t = clampf((ed - 2.0f) / 7.0f, 0.0f, 1.0f); t = t * t * (3 - 2 * t);
            st = (1.0f - entDark) + entDark * t;
        }
        sum += st / (1.0f + 0.055f * d2) * 1.8f * mul;
    }
    float lit = sum * blackout + ambLum * 2.2f;
    if (entDark > 0.01f) {   // and the surface pool of shadow around it
        float fd = sqrtf((x - entX) * (x - entX) + (z - entZ) * (z - entZ));
        float t = clampf((fd - 0.8f) / 5.7f, 0.0f, 1.0f); t = t * t * (3 - 2 * t);
        lit *= (1.0f - entDark) + entDark * t;
    }
    return clampf(lit, 0.0f, 1.0f);
}
