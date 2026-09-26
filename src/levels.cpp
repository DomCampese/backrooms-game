#include "levels.h"
#include "util.h"
#include <cmath>

// One row per level. The columns are in LevelCfg's declaration order:
//     wallH   ls   dead  lightMul fogDen gloss   lightCol            amb                     fogCol                  name
const LevelCfg LEVELS[NLEVELS] = {
    // Level 0 — "Threshold". The 2002 photograph and every render made from it
    // have a drop ceiling crowded with fittings, a few of them out: fluorescent
    // trays every other ceiling tile, not one lonely panel per 8 m square, which
    // is what this row used to be and why the level's ceiling read as dark
    // board with the occasional light in it. A 4 m grid is four times the
    // fittings; a fifth of them are dead and the rest vary in output, so the
    // pools of light are uneven the way the lore's "inconsistently placed
    // fluorescent lighting" asks for. lightMul comes down because a point
    // under the grid is now summing four near panels instead of one. The
    // carpet's wet patches are the last column (see uWet in shaders.cpp).
    // The last column is the storey pitch: Level 0 is floors stacked on floors
    // (see storeyH in levels.h).
    { 3.0f,  4.0f, 0.20f, 0.36f, 0.050f, 0.06f, {1.00f,0.95f,0.76f}, {0.045f,0.042f,0.030f}, {0.140f,0.125f,0.070f}, "LEVEL 0 · THRESHOLD",
      0.42f, 0.16f, 0.0f, 0.60f, 4.32f },
    // Level 1 — "Habitable Zone": "a large, sprawling warehouse" of concrete
    // floors and walls under "dim fluorescent lights" that "are prone to
    // flicker and fail at inconsistent intervals", in "a low-hanging fog with
    // no discernable source" that "coalesces into condensation, forming
    // puddles on the floor in inconsistent areas". It used to be lit in cold
    // blue at a 12 m pitch, which read as a night-time car park rather than a
    // fluorescent warehouse, and its floor gloss (0.22) put a sheen on every
    // square metre of slab — puddles everywhere, which is the opposite of
    // "inconsistent areas". Now: dim, faintly green-white tube battens, three
    // in ten of them out and a fifth stuttering, grey-green fog for the mist
    // to be made of, matte concrete, and real puddles on a few percent of the
    // floor through the same world-space field as Level 0's carpet. The grid
    // stays 12 m: at 8 m nearly all nine summed fittings fall inside shadow
    // range and the frame cost 58% more on the software rasteriser.
    // lightMul is up to carry the sparser fittings.
    { 4.2f, 12.0f, 0.30f, 1.05f, 0.070f, 0.07f, {0.93f,0.97f,0.86f}, {0.054f,0.057f,0.052f}, {0.070f,0.075f,0.068f}, "LEVEL 1 · HABITABLE ZONE",
      0.35f, 0.20f, 0.0f, 0.78f },
    // Tall vaulted bathing halls: warm diffuse light over pristine ceramic, with
    // restrained exposure so white grout and underwater steps stay readable.
    // The panels sit 7.4 m up, so lightMul carries the extra throw.
    { 7.5f,  8.0f, 0.06f, 1.25f, 0.026f, 0.55f, {1.00f,0.98f,0.89f}, {0.16f,0.18f,0.18f},    {0.16f,0.23f,0.22f},    "THE POOLROOMS" },
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
// uRoomMask and uLamp, mirrored: Game sets both every frame alongside the
// shader uniforms, so a sprite in the Manila Room is lit by its chandelier
// and not by the tubes that aren't there.
static float gMaskCPU[4] = { 0, 0, 0, 0 }, gLampCPU[4] = { 0, 0, 0, 0 };
void setLightExtrasCPU(const float mask[4], const float lamp[4]) {
    for (int i = 0; i < 4; i++) { gMaskCPU[i] = mask[i]; gLampCPU[i] = lamp[i]; }
}
static float lhashCPU(float gx, float gz) {
    float v = sinf(gx * 127.1f + gz * 311.7f) * 43758.5453f;
    return v - floorf(v);
}
static StoreyLightCPU gStoreyCPU;
void setStoreyLightCPU(const StoreyLightCPU &s) { gStoreyCPU = s; }
// One byte of the occupancy snapshot: `ch` 0 = your storey, 1 = below, 2 = above.
static int occCPU(int ci, int ck, int ch) {
    const StoreyLightCPU &s = gStoreyCPU;
    if (!s.occ || ch < 0 || ch > 2) return 0;
    int x = ci - s.originI, z = ck - s.originK;
    if (x < 0 || z < 0 || x >= s.occN || z >= s.occN) return 0;
    return s.occ[(z * s.occN + x) * 4 + ch];
}
static int chanOf(int rel) { return rel == 0 ? 0 : rel == -1 ? 1 : rel == 1 ? 2 : -1; }
// Each storey's tubes fail in their own places: the panel hash is offset per
// storey. Bounded (a golden-ratio fraction of 97 cells) rather than growing
// with the storey number, because the shader's sin() loses the hash at large
// arguments. Storey 0 is offset 0, so its tubes are the ones it always had.
float storeyHashOffset(int s) {
    float t = (float)s * 0.6180339f;
    return (t - floorf(t)) * 97.0f;
}
float lightAtCPU(float x, float y, float z, float blackout,
                        float ls, float ly, float dead, float mul, float ambLum,
                        float entX, float entZ, float entDark, float vary) {
    float bx = floorf((x - ls * 0.5f) / ls + 0.5f), bz = floorf((z - ls * 0.5f) / ls + 0.5f);
    float sum = 0;
    // Storeys, exactly as roomLight() takes them: which floor this point is
    // on, its own tubes (less any over an opening), and — standing where there
    // is no ceiling — the tubes of the floor above that hang over the hole.
    const float SH = gStoreyCPU.storeyH;
    int rel = SH > 0.0f ? (int)floorf((y + 0.3f) / SH) : 0;
    int ci0 = (int)floorf(x / 2.0f), ck0 = (int)floorf(z / 2.0f);
    int layers = (SH > 0.0f && rel < 1 && (occCPU(ci0, ck0, chanOf(rel)) & 16)) ? 2 : 1;
    for (int layer = 0; layer < layers; layer++) {
    int lrel = rel + layer;
    float lly = ly + lrel * SH, so = storeyHashOffset(gStoreyCPU.storey + lrel);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        float gx = bx + dx, gz = bz + dz;
        float lx = gx * ls + ls * 0.5f, lz = gz * ls + ls * 0.5f;
        if (SH > 0.0f) {
            int po = occCPU((int)floorf(lx * 0.5f + 0.5f), (int)floorf(lz * 0.5f + 0.5f), chanOf(lrel));
            if (po & 8) continue;                        // no fitting: it would hang in an opening
            if (layer == 1 && !(po & 32)) continue;      // above you, but over floor, not the hole
        }
        float h = lhashCPU(gx + so, gz + so * 1.7f);
        if (h < dead) continue;
        float out = 1.0f - vary * (h * 53.7f - floorf(h * 53.7f));   // same spread as lightState()
        if (lx >= gMaskCPU[0] && lx <= gMaskCPU[2] && lz >= gMaskCPU[1] && lz <= gMaskCPU[3]) continue;
        // the panel is a rectangle, and the shader shades from the point on it
        // closest to the surface — mirror that, or a sprite standing under a
        // fitting comes out dimmer than the floor it is standing on
        float cx = clampf(x, lx - PANEL_HALF_CPU, lx + PANEL_HALF_CPU);
        float cz = clampf(z, lz - PANEL_HALF_CPU, lz + PANEL_HALF_CPU);
        float d2 = (cx - x) * (cx - x) + (lly - y) * (lly - y) + (cz - z) * (cz - z);
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
        sum += out * st / (1.0f + 0.22f * d2) * 5.4f * mul * ndl * wx * wz;
    }
    }
    float ambT = ambLum * 1.4f;
    ambT *= 1.0f + 5.5f / (1.0f + 40.0f * ambT);   // same toe compensation as the shader
    if (gLampCPU[3] > 0.01f) {   // the chandelier (no occlusion here: sprites near it are in the room)
        float dx = gLampCPU[0] - x, dy = gLampCPU[1] - y, dz = gLampCPU[2] - z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < 90.0f) sum += gLampCPU[3] * 3.2f / (1.0f + 0.26f * d2) * 0.6f;
    }
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

// The shader's damp-patch field (uWet in WORLD_FS), on the CPU: the same
// vnoise over the same hash at the same two scales and the same threshold, so
// anything asking the CPU "is it wet here" gets the patch the eye sees. GPU and CPU sin() differ
// in the last bits at large arguments; for a footstep that is nothing.
static float vnoiseCPU(float x, float z) {
    float ix = floorf(x), iz = floorf(z), fx = x - ix, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
    float a = lhashCPU(ix, iz), b = lhashCPU(ix + 1, iz), c = lhashCPU(ix, iz + 1), d = lhashCPU(ix + 1, iz + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz;
}
float carpetWetCPU(float x, float z, float from) {
    float wn = vnoiseCPU(x * 0.42f, z * 0.42f) * 0.62f + vnoiseCPU(x * 1.35f + 17.0f, z * 1.35f + 17.0f) * 0.38f;
    float t = clampf((wn - from) / 0.12f, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
