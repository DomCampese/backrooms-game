#include "fp_strict.h"
#include "hash.h"
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

// pi/2 in four parts. The first has 31 significant bits, so n * PIO2_1 is exact
// for n below 2^22 (|x| under about 6.5e6) and the reduction stays accurate
// there. Beyond that it loses accuracy but stays the same on every platform.
static const double PIO2_1 = 0x1.921fb544p+0, PIO2_2 = 0x1.0b4611a6p-34,
                    PIO2_3 = 0x1.3198a2ep-69, PIO2_4 = 0x1.b839a252049c1p-104;
static const double INV_PIO2 = 0x1.45f306dc9c883p-1;

// Taylor series on |r| <= pi/4, carried far enough that truncation is below a
// double's rounding.
static double sinPoly(double r) {
    double r2 = r * r;
    double p = 2.8114572543455206e-15;
    p = p * r2 - 7.647163731819816e-13;
    p = p * r2 + 1.6059043836821613e-10;
    p = p * r2 - 2.505210838544172e-08;
    p = p * r2 + 2.7557319223985893e-06;
    p = p * r2 - 0.0001984126984126984;
    p = p * r2 + 0.008333333333333333;
    p = p * r2 - 0.16666666666666666;
    return r + r * r2 * p;
}
static double cosPoly(double r) {
    double r2 = r * r;
    double p = -1.5619206968586225e-16;
    p = p * r2 + 4.779477332387385e-14;
    p = p * r2 - 1.1470745597729725e-11;
    p = p * r2 + 2.08767569878681e-09;
    p = p * r2 - 2.755731922398589e-07;
    p = p * r2 + 2.48015873015873e-05;
    p = p * r2 - 0.001388888888888889;
    p = p * r2 + 0.041666666666666664;
    p = p * r2 - 0.5;
    return 1.0 + r2 * p;
}

float portableSin(float xf) {
    double x = xf;
    if (!std::isfinite(x)) return (float)(x - x);
    if (x == 0.0) return xf;   // keeps the sign of -0
    double n = std::floor(x * INV_PIO2 + 0.5);
    double r = x - n * PIO2_1;
    r -= n * PIO2_2;
    r -= n * PIO2_3;
    r -= n * PIO2_4;
    int quadrant = (int)(n - 4.0 * std::floor(n * 0.25));
    double v = (quadrant & 1) ? cosPoly(r) : sinPoly(r);
    return (float)(quadrant & 2 ? -v : v);
}
