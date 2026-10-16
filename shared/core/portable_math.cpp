#include "fp_strict.h"
#include "portable_math.h"
#include <cmath>
#include <limits>

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

// Reduces x to r in [-pi/4, pi/4] and the quadrant of x - r, as a multiple of
// pi/2, modulo 4.
static double reduce(double x, int &quadrant) {
    double n = std::floor(x * INV_PIO2 + 0.5);
    double r = x - n * PIO2_1;
    r -= n * PIO2_2;
    r -= n * PIO2_3;
    r -= n * PIO2_4;
    quadrant = (int)(n - 4.0 * std::floor(n * 0.25));
    return r;
}
// sin(r + quadrant * pi/2).
static double sinQuadrant(double r, int quadrant) {
    double v = (quadrant & 1) ? cosPoly(r) : sinPoly(r);
    return quadrant & 2 ? -v : v;
}

float portableSin(float xf) {
    double x = xf;
    if (!std::isfinite(x)) return (float)(x - x);
    if (x == 0.0) return xf;   // keeps the sign of -0
    int quadrant;
    double r = reduce(x, quadrant);
    return (float)sinQuadrant(r, quadrant);
}

float portableCos(float xf) {
    double x = xf;
    if (!std::isfinite(x)) return (float)(x - x);
    int quadrant;
    double r = reduce(x, quadrant);
    return (float)sinQuadrant(r, (quadrant + 1) & 3);
}

// ln 2 in two parts. The first ends in 21 zero bits, so k * LN2_HI is exact for
// every k the clamp below allows.
static const double LN2_HI = 0x1.62e42fee00000p-1, LN2_LO = 0x1.a39ef35793c76p-33;
static const double INV_LN2 = 1.4426950408889634;

float portableExp(float xf) {
    double x = xf;
    if (x != x) return xf;
    // Past these the float result is already infinity or zero.
    x = std::fmin(std::fmax(x, -200.0), 200.0);
    double k = std::floor(x * INV_LN2 + 0.5);
    double r = x - k * LN2_HI;
    r -= k * LN2_LO;
    // Taylor series on |r| <= ln2/2, carried far enough that truncation is
    // below a double's rounding.
    double p = 1.6059043836821613e-10;
    p = p * r + 2.08767569878681e-09;
    p = p * r + 2.505210838544172e-08;
    p = p * r + 2.755731922398589e-07;
    p = p * r + 2.7557319223985893e-06;
    p = p * r + 2.48015873015873e-05;
    p = p * r + 0.0001984126984126984;
    p = p * r + 0.001388888888888889;
    p = p * r + 0.008333333333333333;
    p = p * r + 0.041666666666666664;
    p = p * r + 0.16666666666666666;
    p = p * r + 0.5;
    p = p * r + 1.0;
    p = p * r + 1.0;
    return (float)std::ldexp(p, (int)k);   // scaling by a power of two is exact
}

static const double PI = 3.141592653589793, PI_2 = 1.5707963267948966,
                    PI_4 = 0.7853981633974483, PI_6 = 0.5235987755982988;
static const double TAN_PI_12 = 0.2679491924311228, SQRT3 = 1.7320508075688772;

// atan(t) for t in [0, 1]. Above tan(pi/12), atan(t) = pi/6 + atan(u) with
// u = (t sqrt3 - 1) / (t + sqrt3), which brings the argument under 0.268, where
// the series converges to a double's rounding in 15 terms.
static double atanUnit(double t) {
    double base = 0.0;
    if (t > TAN_PI_12) {
        t = (t * SQRT3 - 1.0) / (t + SQRT3);
        base = PI_6;
    }
    double t2 = t * t, p = 0.0;
    for (int n = 29; n >= 3; n -= 2) p = (n & 2 ? -1.0 : 1.0) / n + p * t2;
    return base + (t + t * t2 * p);
}

float portableAtan2(float yf, float xf) {
    double y = yf, x = xf;
    // One NaN: xf + yf keeps whichever payload the optimiser puts first.
    if (x != x || y != y) return std::numeric_limits<float>::quiet_NaN();
    double a;
    if (std::isinf(y) && std::isinf(x)) a = std::signbit(x) ? 3.0 * PI_4 : PI_4;
    else if (std::isinf(x) || y == 0.0) a = std::signbit(x) ? PI : 0.0;
    else if (std::isinf(y) || x == 0.0) a = PI_2;
    else {
        double ax = std::fabs(x), ay = std::fabs(y);
        a = ay <= ax ? atanUnit(ay / ax) : PI_2 - atanUnit(ax / ay);
        if (x < 0.0) a = PI - a;
    }
    return (float)std::copysign(a, y);
}

float portableHypot(float x, float y) {
    // The squares of floats are exact in double, so this rounds twice at most
    // and the same way everywhere.
    return (float)std::sqrt((double)x * x + (double)y * y);
}
