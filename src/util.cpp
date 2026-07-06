#include "util.h"
#include <cmath>

uint32_t ih(int x, int y, uint32_t s) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16);
}
float lat(int x, int y, uint32_t s) { return (float)(ih(x, y, s) & 0xFFFFFF) / 16777216.0f; }
float vnoise2(float x, float y, uint32_t s) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = lat(xi, yi, s), b = lat(xi + 1, yi, s), c = lat(xi, yi + 1, s), d = lat(xi + 1, yi + 1, s);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
float fbm2(float x, float y, uint32_t s, int oct) {
    float v = 0, amp = 0.5f, f = 1;
    for (int i = 0; i < oct; i++) { v += vnoise2(x * f, y * f, s + (uint32_t)i * 101u) * amp; amp *= 0.5f; f *= 2; }
    return v;
}

// LEVEL FUN =) — the party decorations share one faded palette
const Color PARTY[5] = {
    { 206, 64, 58, 255 }, { 222, 172, 62, 255 }, { 84, 142, 198, 255 },
    { 106, 178, 92, 255 }, { 182, 96, 178, 255 },
};
