#pragma once
// Hashes, a seeded RNG and value noise. Everything random in core comes from
// these, so a seed reproduces the world exactly.
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
    // Inclusive. hi < lo takes a modulo of a negative count and crashes with a
    // floating point exception.
    int ri(int lo, int hi) { return lo + (int)(next() % (uint64_t)(hi - lo + 1)); }
};
uint32_t ih(int x, int y, uint32_t s);        // integer lattice hash
float lat(int x, int y, uint32_t s);          // lattice value, 0..1
float vnoise2(float x, float y, uint32_t s);  // value noise, 0..1 (not -1..1)
float fbm2(float x, float y, uint32_t s, int oct);   // octaves of vnoise2; not periodic
