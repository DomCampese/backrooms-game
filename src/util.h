#pragma once
// Hashes, RNG, value noise, small helpers — shared by every module.
#include "raylib.h"
#include <cstdint>

inline uint64_t hash64(uint64_t x) {
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27; x *= 0x94D049BB133111EBULL;
    x ^= x >> 31; return x;
}
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() { s += 0x9E3779B97F4A7C15ULL; return hash64(s); }
    float f01() { return (float)(next() >> 40) / 16777216.0f; }
    int ri(int lo, int hi) { return lo + (int)(next() % (uint64_t)(hi - lo + 1)); }
};
uint32_t ih(int x, int y, uint32_t s);
float lat(int x, int y, uint32_t s);
float vnoise2(float x, float y, uint32_t s);
float fbm2(float x, float y, uint32_t s, int oct);
inline unsigned char cl8(float v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// LEVEL FUN =) — the party decorations share one faded palette
extern const Color PARTY[5];
