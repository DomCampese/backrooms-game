// World surfaces: the floors, walls and ceilings of every level.
//
// Each surface is painted as three fields over the same pixels: colour, height
// in metres, and a gloss mask. finishSurface turns the height into the packed
// slopes the shader reads. Rules for everything in this file:
//
//  * Relief is authored as height, never derived from colour. The old detail
//    maps came from the albedo's luminance, so printed chevrons were embossed
//    and every stain was a dent. Heights are physical: a groove is -0.7 mm, a
//    skirting stands 14 mm proud, mortar is raked back 6 mm.
//
//  * Everything wraps at the tile edge, or every repeat shows a seam. The old
//    generators sampled fbm2 across 0..W, so Level 1's tide line jumped every
//    3 m. tnoise/tfbm below wrap on a whole number of lattice cells, and
//    scattered features use wrapped coordinates.
//
//  * Real sizes. Floors and ceilings map 2 m to one repeat, walls 3 m across
//    and tileV down (WallBuilder in chunk_mesh.cpp), so ceiling boards, bricks
//    and pool tiles are their real size. The old ones were not (metre-square
//    ceiling boards, 0.75 m bricks, pool tiles of different sizes on floor and
//    wall).
//
// Every generator ends by matching its mean luma to the surface it replaced
// (Canvas::matchLuma), because the ambients, light multipliers and
// tools/sweep.sh's bands were tuned against those. tools/texdump prints the
// means.
#include "textures.h"
#include "util.h"
#include "object_materials.generated.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

inline float sat(float v) { return clampf(v, 0.0f, 1.0f); }
inline float sstep(float a, float b, float v) { float t = sat((v - a) / (b - a)); return t * t * (3 - 2 * t); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Value noise on a lattice that wraps every px by py cells, so a texture that
// spans a whole number of cells tiles without a seam. Callers stay within one
// period (Canvas::nz maps the tile onto [0, px) and each octave doubles both),
// so the wrap is a compare rather than a modulo: eight integer divisions per
// sample were most of this file's startup cost.
inline float tlat(int x, int y, int px, int py, uint32_t s) {
    if (x >= px) x -= px; else if (x < 0) x += px;
    if (y >= py) y -= py; else if (y < 0) y += py;
    return lat(x, y, s);
}
float tnoise(float x, float y, int px, int py, uint32_t s) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = tlat(xi, yi, px, py, s), b = tlat(xi + 1, yi, px, py, s);
    float c = tlat(xi, yi + 1, px, py, s), d = tlat(xi + 1, yi + 1, px, py, s);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
// fBm, normalised to 0..1 with its mean at 0.5 (unlike fbm2, whose range
// shrinks with the octave count). Each octave doubles the period with the
// frequency, so it still wraps.
float tfbm(float x, float y, int px, int py, uint32_t s, int oct) {
    float v = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < oct; i++) {
        v += tnoise(x, y, px, py, s + (uint32_t)i * 101u) * amp;
        norm += amp; amp *= 0.5f; x *= 2; y *= 2; px *= 2; py *= 2;
    }
    return v / norm;
}

struct Canvas {
    int w, h;
    float wm, hm;                    // metres the tile spans across and down
    std::vector<float> r, g, b;      // albedo, 0..255
    std::vector<float> ht;           // height off the surface, metres, + toward the viewer
    std::vector<float> gl;           // gloss mask, 0..1, relative to the level's uGloss
    Canvas(int w_, int h_, float wm_, float hm_)
        : w(w_), h(h_), wm(wm_), hm(hm_), r(w_ * h_), g(w_ * h_), b(w_ * h_),
          ht(w_ * h_, 0.0f), gl(w_ * h_, 1.0f) {}
    float mx() const { return wm / w; }          // metres per pixel across
    float my() const { return hm / h; }          // ...and down
    // Tileable fBm at about fx by fy lattice cells per metre, rounded to a whole
    // number of cells across the tile. `perY` (pixels) repeats it sooner down
    // the tile than the tile itself, for Level 0's paper (see the wallpaper).
    //
    // Most fields are smooth at pixel scale (a 3-cells-per-metre field is 14 px
    // across at its finest octave), so each is evaluated once on a coarse grid
    // (up to every 8th pixel, with three grid steps inside its finest cell) and
    // read back bilinearly. That cut startup from seconds to under one. A field
    // is keyed by seed, octaves, lattice size and period, so calls with the
    // same key share it. Fine fields are evaluated directly.
    struct Field {
        uint64_t key = 0;
        int cx = 1, cy = 1, ph = 1, sx = 1, sy = 1, gw = 0, gh = 0;
        std::vector<float> v;                    // empty: evaluated directly
    };
    // A generator uses a dozen fields at most and looks one up for every pixel,
    // so the lookup is a short scan on the call's own parameters; everything
    // derived from them is worked out once, when the field is first asked for.
    mutable std::vector<Field> cache;
    // The coarsest grid step that keeps three steps inside the finest cell,
    // chosen per axis: grain along a board is fine across it and coarse along.
    static int gridStep(float finest, int len) {
        int st = 1;
        while (st < 8 && st * 2 * 3 <= finest && len % (st * 2) == 0) st *= 2;
        return st;
    }
    const Field &field(float fx, float fy, uint32_t s, int oct, int perY) const {
        uint32_t bx, by;
        memcpy(&bx, &fx, 4); memcpy(&by, &fy, 4);
        uint64_t key = (((uint64_t)bx << 32) | by) ^ ((uint64_t)s * 0x9E3779B97F4A7C15ULL)
                     ^ ((uint64_t)oct << 58) ^ ((uint64_t)perY << 44);
        for (const Field &f : cache) if (f.key == key) return f;
        Field f;
        f.key = key;
        f.ph = perY ? perY : h;
        f.cx = std::max(1, (int)lroundf(fx * wm));
        f.cy = std::max(1, (int)lroundf(fy * hm * f.ph / h));
        f.sx = gridStep((float)w / (f.cx << (oct - 1)), w);
        f.sy = gridStep((float)f.ph / (f.cy << (oct - 1)), f.ph);
        if (f.sx * f.sy > 1) {
            f.gw = w / f.sx; f.gh = f.ph / f.sy;
            f.v.resize((size_t)f.gw * f.gh);
            for (int j = 0; j < f.gh; j++) for (int i = 0; i < f.gw; i++)
                f.v[(size_t)j * f.gw + i] = tfbm((float)(i * f.sx) * f.cx / w, (float)(j * f.sy) * f.cy / f.ph,
                                                 f.cx, f.cy, s, oct);
        }
        cache.push_back(std::move(f));
        return cache.back();
    }
    float nz(float x, float y, float fx, float fy, uint32_t s, int oct = 1, int perY = 0) const {
        const Field &f = field(fx, fy, s, oct, perY);
        float X = x + 0.5f, Y = (perY || y >= h ? fmodf(y, (float)f.ph) : y) + 0.5f;
        if (f.v.empty()) return tfbm(X * f.cx / w, Y * f.cy / f.ph, f.cx, f.cy, s, oct);
        float gx = X / f.sx, gy = Y / f.sy;
        int i0 = (int)gx, j0 = (int)gy;
        float tx = gx - i0, ty = gy - j0;
        if (i0 >= f.gw) i0 -= f.gw;
        if (j0 >= f.gh) j0 -= f.gh;
        int i1 = i0 + 1 == f.gw ? 0 : i0 + 1, j1 = j0 + 1 == f.gh ? 0 : j0 + 1;
        const float *v = f.v.data();
        float a = v[(size_t)j0 * f.gw + i0], b = v[(size_t)j0 * f.gw + i1];
        float c2 = v[(size_t)j1 * f.gw + i0], d = v[(size_t)j1 * f.gw + i1];
        return a + (b - a) * tx + (c2 - a) * ty + (a - b - c2 + d) * tx * ty;
    }
    void set(int i, float cr, float cg, float cb) { r[i] = cr; g[i] = cg; b[i] = cb; }
    void mul(int i, float k) { r[i] *= k; g[i] *= k; b[i] *= k; }
    void blend(int i, float cr, float cg, float cb, float t) {
        r[i] += (cr - r[i]) * t; g[i] += (cg - g[i]) * t; b[i] += (cb - b[i]) * t;
    }
    // Scale the colours so the mean luma is `target` (see the top of the file).
    // `light` weights the channels by the level's light colour: under the Red
    // Halls' almost pure red light, two bricks with the same luma can differ
    // in brightness by a fifth.
    void matchLuma(float target, Vector3 light = { 1, 1, 1 }) {
        double s = 0;
        float wr = 0.299f * light.x, wg = 0.587f * light.y, wb = 0.114f * light.z;
        for (int i = 0; i < w * h; i++) s += wr * r[i] + wg * g[i] + wb * b[i];
        float k = target / (float)(s / (w * h));
        for (int i = 0; i < w * h; i++) mul(i, k);
    }
};

// A field of 0..1 marks the size of the canvas: grooves, cracks and pits are
// stamped into one of these first, and the per-pixel pass reads it back. Marks
// take the maximum rather than summing, so a crossing is no deeper than a run.
struct Marks {
    int w, h;
    std::vector<float> v;
    Marks(int w_, int h_) : w(w_), h(h_), v(w_ * h_, 0.0f) {}
    float operator[](int i) const { return v[i]; }
    void dab(float cx, float cy, float rad) {
        int x0 = (int)floorf(cx - rad - 1), x1 = (int)ceilf(cx + rad + 1);
        int y0 = (int)floorf(cy - rad - 1), y1 = (int)ceilf(cy + rad + 1);
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float t = 1.0f - sqrtf(dx * dx + dy * dy) / (rad + 0.5f);
            if (t <= 0) continue;
            t = t * t * (3 - 2 * t);
            int xx = x % w, yy = y % h;
            if (xx < 0) xx += w;
            if (yy < 0) yy += h;
            float &m = v[yy * w + xx];
            if (t > m) m = t;
        }
    }
    // A wandering line: `turn` is how much it steers per step, `kink` the chance
    // of an occasional sharp change of heading.
    void walk(Rng &r, float x, float y, float ang, int steps, float stepLen, float turn, float rad,
              float kink = 0.0f, float taper = 0.0f) {
        for (int s = 0; s < steps; s++) {
            ang += (r.f01() - 0.5f) * turn;
            if (kink > 0 && r.f01() < kink) ang += (r.f01() - 0.5f) * 1.6f;
            x += cosf(ang) * stepLen; y += sinf(ang) * stepLen;
            float k = taper > 0 ? 1.0f - taper * (float)s / steps : 1.0f;
            dab(x, y, rad * k);
        }
    }
};

// atan2 to about a tenth of a degree, for masks that only need to know roughly
// which way round a circle a pixel is; the library call was a third of the
// concrete floor's generation time.
inline float fastAtan2(float y, float x) {
    float ax = fabsf(x), ay = fabsf(y);
    float a = std::min(ax, ay) / (std::max(ax, ay) + 1e-12f), q = a * a;
    float r = ((-0.0464964749f * q + 0.15931422f) * q - 0.327622764f) * q * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    return y < 0 ? -r : r;
}

// The shortest signed distance from a to b on a wrapping axis of length n.
inline float wrapd(float a, float b, float n) { float d = a - b; return d - n * floorf(d / n + 0.5f); }

Surface finishSurface(const Canvas &c, float gain) {
    Image img = GenImageColor(c.w, c.h, BLANK), det = GenImageColor(c.w, c.h, BLANK);
    Color *p = (Color *)img.data, *q = (Color *)det.data;
    // Central differences in metres: the slope is height per metre along the
    // surface, which is what detailNormal() in the shader takes it to be.
    float kx = gain / (2 * c.mx()), ky = gain / (2 * c.my());
    for (int y = 0; y < c.h; y++) for (int x = 0; x < c.w; x++) {
        int i = y * c.w + x;
        p[i] = { cl8(c.r[i]), cl8(c.g[i]), cl8(c.b[i]), 255 };
        int xl = x ? x - 1 : c.w - 1, xr = x + 1 < c.w ? x + 1 : 0;
        int yu = y ? y - 1 : c.h - 1, yd = y + 1 < c.h ? y + 1 : 0;
        float sx = (c.ht[y * c.w + xr] - c.ht[y * c.w + xl]) * kx;
        float sy = (c.ht[yd * c.w + x] - c.ht[yu * c.w + x]) * ky;
        q[i] = { cl8(128 + 127 * clampf(sx, -1, 1)), cl8(128 + 127 * clampf(sy, -1, 1)), cl8(255 * sat(c.gl[i])), 255 };
    }
    return { finishTexture(img, true), finishTexture(det, true) };
}

// ------------------------------------------------------------ paper walls
//
// Level 0 and LEVEL FUN are both papered: 0.75 m drops with slightly lifted
// seams, a fine stipple emboss, above a skirting board. SKIRT is the board's
// height (it was a 27 cm painted band; real skirting is 100-150 mm). The
// outlets at 0.32 m sit above it.
const float SKIRT = 0.12f;

// The paper itself, for a pixel `sx` pixels into its drop: relief (seam, lifted
// edge, emboss) into the height field, and the seam's hairline into `tone`.
void paperRelief(Canvas &c, int i, int x, int y, int sx, float &tone, int perY) {
    const float mm = 0.001f;
    float h = 0.0f;
    // The emboss: a fine stipple, 0.1 mm deep.
    h += (c.nz((float)x, (float)y, 170, 170, 0x77A1u, 2, perY) - 0.5f) * 0.20f * mm;
    // The seam: a hairline gap, and the edge of the next drop lifted off the wall
    // for a centimetre.
    if (sx < 2) { h -= 0.15f * mm; tone *= 0.93f; }
    else if (sx < 14) { float t = 1.0f - (sx - 2) / 12.0f; h += 0.22f * mm * t * t; }
    c.ht[i] = h;
}

// The skirting, for a pixel `hgt` metres above the floor: stained timber with
// the grain along the board, 14 mm proud of the wall, with a bullnose on top.
void skirting(Canvas &c, int i, int x, int y, float hgt, float cr, float cg, float cb) {
    const float PROUD = 0.014f, NOSE = 0.012f;
    float grain = c.nz((float)x, (float)y, 2.0f, 260.0f, 0x5B1u, 3);
    float fleck = c.nz((float)x, (float)y, 60.0f, 400.0f, 0x5B2u, 1);
    float v = 0.86f + 0.26f * grain + 0.06f * (fleck - 0.5f);
    float h = PROUD;
    if (hgt > SKIRT - NOSE) {                           // the bullnose, a quarter round back to the wall
        float t = (hgt - (SKIRT - NOSE)) / NOSE;
        h = PROUD * sqrtf(sat(1.0f - t * t));
        v *= 1.0f + 0.10f * t;                           // worn paler
    }
    if (hgt < 0.006f) v *= 0.80f;                        // the shadow where it meets the floor
    // black scuffs, low on the board
    float scuff = c.nz((float)x, (float)y, 9.0f, 60.0f, 0x5B3u, 2);
    if (scuff > 0.70f && hgt < SKIRT * 0.7f) v *= 1.0f - (scuff - 0.70f) * 1.3f;
    c.set(i, cr * v, cg * v, cb * v);
    c.ht[i] = h;
    c.gl[i] = 1.0f;                                      // satin varnish, against the paper's matte
}

}   // namespace

// ---------------------------------------------------------------- Level 0
//
// The wallpaper from the photograph: vertical pinstripe bands, a narrow scroll
// strip between them, and pairs of chevrons pointing up (the wiki's "90s
// chevron-styled" paper). The motif is Amini Allight's CC0
// texture (assets/materials/README.md), embedded as luminance and printed in
// ochre ink over the game's mono-yellow ground.
//
// 1024 px for the 3 m the wall UVs span, and the 256 px motif repeats exactly
// four times across it: a period that does not divide the texture puts half a
// chevron at every seam (AGENTS.md). The roll seams are on the motif's repeat,
// 0.75 m apart.
//
// Vertically the paper has a second constraint: on a storeyed level a wall
// taller than one tile carries on from the tile's 0.75-2.25 m band, repeating
// it (wallV/voidFace in chunk_mesh.cpp). So everything on the paper repeats every
// 1.5 m (512 px) down the tile; the noise uses that period, and anything
// that depends on height (the damp, the dust line under the ceiling, the
// skirting) stays outside the band.
Surface makeWallpaperSurface() {
    const int W = 1024, H = 1024, M = 256, BAND = 512;
    Canvas c(W, H, 3.0f, 3.0f);
    Image motif = LoadImageFromMemory(".jpg", object_wallpaper, (int)sizeof(object_wallpaper));
    ImageFormat(&motif, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
    ImageResize(&motif, M, M);
    const unsigned char *mp = (const unsigned char *)motif.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int i = y * W + x;
        float hgt = (H - 0.5f - y) / H * 3.0f;                 // metres above the floor
        if (hgt < SKIRT) { skirting(c, i, x, y, hgt, 98, 74, 45); continue; }
        float fx = (float)x, fy = (float)y;
        float ink = 1.0f - mp[(y % M) * M + (x % M)] / 255.0f;
        float grime = c.nz(fx, fy, 3.0f, 3.0f, 7u, 4, BAND);
        float stain = c.nz(fx, fy, 1.3f, 1.3f, 12u, 4, BAND);
        float base = 1.0f - 0.16f * grime;
        if (stain > 0.64f) base *= 1.0f - (stain - 0.64f) * 0.9f;
        int strip = x / M, sx = x % M;
        base *= 1.0f + (lat(strip, 0, 15u) - 0.5f) * 0.030f;   // roll-to-roll tone drift
        float tone = 1.0f;
        paperRelief(c, i, x, y, sx, tone, BAND);
        base *= tone;
        // Yellow ground, ochre-olive ink; the ink fades where the paper is most
        // yellowed, so it looks old.
        float r = 232 * base, g = 216 * base, b = 160 * base;
        float k = ink * (0.50f - 0.18f * grime);
        r = r * (1 - k) + 128 * base * k; g = g * (1 - k) + 112 * base * k; b = b * (1 - k) + 52 * base * k;
        c.set(i, r, g, b);
        // Damp wicking up from the floor, worst at the foot, gone by 0.7 m.
        float reach = sstep(0.72f, 0.12f, hgt);
        if (reach > 0) {
            float damp = c.nz(fx, fy, 5.0f, 2.0f, 16u, 3) * (0.55f + 0.75f * reach);
            if (damp > 0.52f) {
                float t = std::min(0.50f, (damp - 0.52f) * 1.8f) * reach;
                c.blend(i, 104, 96, 60, t);
                c.ht[i] -= 0.00012f * t;                           // the paper ripples where it was wet
            }
        }
        // Just above the board: the caulk line and the shade under the nosing.
        if (hgt < SKIRT + 0.02f) c.mul(i, 0.90f + 0.10f * (hgt - SKIRT) / 0.02f);
        // Under the ceiling: a grey dust band, heaviest in the top 30 cm.
        float dust = sstep(2.55f, 3.0f, hgt);
        if (dust > 0) c.blend(i, 150, 142, 112, 0.22f * dust * (0.6f + 0.8f * grime));
        c.gl[i] = 0.55f;
    }
    UnloadImage(motif);
    c.matchLuma(165.8f);
    return finishSurface(c, 1.0f);
}

// Contract carpet: level loop, tufted in rows along one direction, in a heather
// of four yarns. At 2 mm a pixel the loops themselves are resolved — two pixels
// a loop, alternate rows offset half a loop, so close up the floor has a weave
// and at a distance it mips to the old flat colour. Each loop is one yarn at
// its own height, and the rows show as faint stripes along the run. The yarns
// are close in colour so the fleck does not form a pattern.
//
// Clean, as requested: no wear paths or stains. The rotten cells get their dark
// tint from the mesher (chunk_mesh.cpp).
Surface makeCarpetSurface() {
    const int N = 1024, LOOPS = N / 2;
    Canvas c(N, N, 2.0f, 2.0f);
    struct Yarn { float r, g, b, w; };
    static const Yarn Y[] = {
        { 168, 153, 117, 0.44f }, { 181, 165, 121, 0.22f }, { 149, 134, 101, 0.24f }, { 162, 154, 130, 0.10f },
    };
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int i = y * N + x;
        int row = y >> 1, sh = row & 1;
        int col = ((x + sh) >> 1) % LOOPS;
        uint32_t hs = ih(col, row, 0xCA41u);
        float u = (hs & 0xFFFF) / 65536.0f;
        int k = 0;
        for (float acc = Y[0].w; k < 3 && u > acc; acc += Y[++k].w) {}
        float r = Y[k].r, g = Y[k].g, b = Y[k].b;
        // an occasional fleck yarn, slightly different in colour
        uint32_t fl = hs >> 24;
        if (fl < 3) { r *= 0.80f; g *= 0.78f; b *= 0.76f; }
        else if (fl > 252) { r *= 1.10f; g *= 1.10f; b *= 1.06f; }
        float shade = 0.96f + 0.08f * c.nz((float)x, (float)y, 7, 7, 0xCA42u, 3);   // pile direction variation
        float rowing = 1.0f + (lat(row % (N / 2), 0, 0xCA43u) - 0.5f) * 0.035f;
        float v = shade * rowing * ((y & 1) ? 0.93f : 1.0f);   // darker between rows
        c.set(i, r * v, g * v, b * v);
        // One loop is two pixels square: the top of the loop, and the row gap
        // between it and the next row down.
        float loopH = 0.0012f + 0.0006f * (((hs >> 16) & 255) / 255.0f);
        c.ht[i] = (y & 1) ? loopH * 0.55f : loopH;
        c.gl[i] = 0.35f;
    }
    c.matchLuma(147.8f);
    return finishSurface(c, 1.0f);
}

namespace {

// A suspended ceiling: square boards on an exposed T-bar grid. The grid is
// painted steel, 24 mm wide, lighter than the boards and standing proud of them
// (the old ceiling drew it as a dark groove, like floor grout). A hairline
// shadow runs where each board edge sits on the flange.
//
// The boards are fissured mineral fibre. The fissures are short strokes stamped
// one by one (a noise level set gave long wandering lines that looked like
// water trails), with pinholes and a speckle in the fibre.
Surface boardCeiling(int N, int boards, float tileM, float br, float bg, float bb,
                     float gr, float gg, float gb, int worms, uint32_t seed, float targetLuma) {
    Canvas c(N, N, tileM, tileM);
    const int B = N / boards;
    const float mpp = tileM / N;
    Marks fis(N, N), pin(N, N);
    Rng rng(seed);
    for (int by = 0; by < boards; by++) for (int bx = 0; bx < boards; bx++)
        for (int k = 0; k < worms; k++) {
            float x = bx * B + 6 + rng.f01() * (B - 12), y = by * B + 6 + rng.f01() * (B - 12);
            int len = 4 + rng.ri(0, 14);
            fis.walk(rng, x, y, rng.f01() * TAU, len, 0.6f, 0.8f, 0.45f + rng.f01() * 0.45f, 0.0f, 0.5f);
        }
    for (int k = 0; k < N * N / 260; k++) pin.dab(rng.f01() * N, rng.f01() * N, 0.35f + rng.f01() * 0.4f);
    const float BAR = 0.012f, GAP = 0.0032f;               // half the T-bar's face; the shadow line
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int i = y * N + x;
        int lx = x % B, ly = y % B;
        float ex = std::min(lx + 0.5f, B - lx - 0.5f), ey = std::min(ly + 0.5f, B - ly - 0.5f);
        float ed = std::min(ex, ey) * mpp;                  // metres to the nearest grid line
        if (ed < BAR) {                                     // the bar: flat painted steel
            float v = 0.985f + 0.03f * c.nz((float)x, (float)y, 30, 30, seed + 7, 2);
            float roll = sstep(BAR - 0.0025f, BAR, ed);     // its rolled edge turns away
            c.set(i, gr * v * (1 - 0.18f * roll), gg * v * (1 - 0.18f * roll), gb * v * (1 - 0.18f * roll));
            c.ht[i] = 0.0010f - 0.0010f * roll;
            c.gl[i] = 1.0f;
            continue;
        }
        if (ed < BAR + GAP) {                               // the board's cut edge, in shadow
            float t = (ed - BAR) / GAP;
            float v = 0.40f + 0.45f * t;
            c.set(i, br * v, bg * v, bb * v);
            c.ht[i] = -0.0025f * (1 - t);
            c.gl[i] = 0.2f;
            continue;
        }
        int board = (y / B) * boards + (x / B);
        float v = 1.0f + (lat(board, 0, seed + 3) - 0.5f) * 0.035f;   // boards replaced at different times
        float sp = lat(x, y, seed + 4);                      // fibre speckle
        if (sp > 0.93f) v *= 0.90f;
        if (sp > 0.99f) v *= 0.72f;
        v *= 0.97f + 0.06f * c.nz((float)x, (float)y, 25, 25, seed + 5, 2);
        // Dust settles along the grid, where air leaks round the board edges.
        v *= 0.965f + 0.035f * sstep(BAR + GAP, BAR + GAP + 0.03f, ed);
        float f = fis[i], p = pin[i];
        v *= 1.0f - 0.26f * f - 0.50f * p;
        c.set(i, br * v, bg * v, bb * v);
        c.ht[i] = -0.0007f * f - 0.0010f * p;
        c.gl[i] = 0.3f;
    }
    c.matchLuma(targetLuma);
    return finishSurface(c, 1.0f);
}

}   // namespace

// Level 0's ceiling: 2 ft boards, as in the photograph. A ceiling maps 2 m to
// one repeat, so three boards per repeat makes each 667 mm (the nearest whole
// number of boards to 600 mm), with grid lines every 2/3 m in world space. That
// puts a line through the centre of every light fitting (cell corners, at
// 2 + 4k m) and the tray's 0.69 m half-width on the next line, so a troffer
// fills a 2 x 2 board bay. The old boards were a metre square.
Surface makeCeilingSurface() {
    return boardCeiling(768, 3, 2.0f, 208, 202, 181, 222, 220, 210, 240, 0xCE11u, 187.6f);
}

// ---------------------------------------------------------------- Level 1
//
// Poured concrete walls, cast against plywood. Level 1's walls are one tile
// floor to ceiling (3 m across, 4.2 m up; WallBuilder::tileV in chunk_mesh.cpp),
// so features are at real heights. Three 1.4 m lifts, each against two 1.5 m
// form sheets, which leave:
//  * each sheet's own tone and a ghost of its wood grain, with a fin of grout
//    where two sheets met;
//  * the lift joints, a hairline with a lip;
//  * four form-tie holes per sheet — most patched with grout a shade lighter,
//    some left open, and those bleeding rust down the wall;
//  * bugholes (trapped air), more toward the top of every lift.
// Then damp and a tide line at the foot (the wiki's "bland, discolored walls"),
// wet streaks down the face, and a few cracks, drawn as jagged branching
// strokes (the old noise level set formed closed loops like a contour map).
Surface makeConcreteWallSurface() {
    const int W = 1024, H = 1024;
    const float WM = 3.0f, HM = 4.2f, LIFT = 1.4f, SHEET = 1.5f;
    Canvas c(W, H, WM, HM);
    const float mx = c.mx(), my = c.my();
    Marks bug(W, H), crack(W, H);
    Rng rng(0xC0C0ULL);
    for (int k = 0; k < 2600; k++) {
        float x = rng.f01() * W, y = rng.f01() * H;
        float hgt = (H - y) * my, u = fmodf(hgt, LIFT) / LIFT;   // how far up its lift
        if (rng.f01() > 0.15f + 0.85f * u * u) continue;
        bug.dab(x, y, 0.4f + rng.f01() * rng.f01() * 2.2f);
    }
    for (int k = 0; k < 6; k++) {                               // cracks, down and across from a tie
        float x = rng.f01() * W, y = rng.f01() * H;
        float ang = 1.2f + rng.f01() * 0.8f;
        crack.walk(rng, x, y, ang, 90 + rng.ri(0, 180), 1.0f, 0.35f, 0.55f, 0.06f, 0.6f);
        if (rng.f01() < 0.6f) crack.walk(rng, x, y, ang + 1.5f, 40 + rng.ri(0, 60), 1.0f, 0.35f, 0.45f, 0.06f, 0.8f);
    }
    // Terms that vary only along the wall: the tide line and the wander of the
    // lift joints. Worked out once per column rather than once per pixel.
    std::vector<float> tideCol(W), jointCol(W);
    for (int x = 0; x < W; x++) {
        tideCol[x] = 0.95f + (c.nz((float)x, 0, 1.4f, 1, 91u, 3) - 0.5f) * 0.45f + c.nz((float)x, 7, 11, 1, 92u, 2) * 0.10f;
        jointCol[x] = (c.nz((float)x, 14, 6, 1, 90u, 2) - 0.5f) * 0.012f;
    }
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int i = y * W + x;
        float fx = (float)x, fy = (float)y;
        float wx = (x + 0.5f) * mx, hgt = (H - 0.5f - y) * my;   // metres across, metres up
        int lift = std::min(2, (int)(hgt / LIFT)), sheet = (int)(wx / SHEET);
        float v = 1.0f + (c.nz(fx, fy, 1.2f, 1.2f, 81u, 4) - 0.5f) * 0.22f;
        // the paste's grain, in tone and in relief: one octave the field cache can
        // grid, and the finest scale as a per-pixel hash
        float paste = c.nz(fx, fy, 70, 70, 86u, 1) * 0.72f + lat(x, y, 86u) * 0.28f;
        v += (paste - 0.5f) * 0.07f;
        v *= 1.0f + (lat(sheet, lift, 84u) - 0.5f) * 0.09f;      // each form sheet cures its own shade
        v *= 1.0f + (c.nz(fx, fy, 2.0f, 38.0f, 85u, 3) - 0.5f) * 0.05f;   // ...and prints its grain
        float h = (paste - 0.5f) * 0.0004f;
        float gloss = 0.6f;
        // sheet joints: a dark hairline with a fin of grout squeezed out of it
        float dj = fabsf(wrapd(wx, 0.0f, SHEET)) / mx;
        if (dj < 1.0f) v *= 0.80f;
        else if (dj < 3.0f) h += 0.0005f * (1.0f - (dj - 1.0f) / 2.0f);
        // lift joints at 1.4 and 2.8 m, wandering a few mm
        for (float jy : { LIFT, 2 * LIFT }) {
            float d = (hgt - jy - jointCol[x]) / my;
            if (fabsf(d) < 0.8f) { v *= 0.70f; h -= 0.0008f; }
            else if (d > 0 && d < 3.0f) { v *= 1.05f; h += 0.0006f * (1 - (d - 0.8f) / 2.2f); }
        }
        // Form ties: at a quarter and three quarters of each sheet, 0.35 m
        // and 1.05 m up each lift.
        float tx = wx - sheet * SHEET, ty = hgt - lift * LIFT;
        float qx = tx < SHEET * 0.5f ? SHEET * 0.25f : SHEET * 0.75f, qy = ty < LIFT * 0.5f ? 0.35f : 1.05f;
        float dx = tx - qx, dy = ty - qy, dr = sqrtf(dx * dx + dy * dy);
        uint32_t tie = ih(sheet * 2 + (tx >= SHEET * 0.5f), lift * 2 + (ty >= LIFT * 0.5f), 0x71Eu);
        bool open = (tie & 7) < 3;
        float rust = 0;
        if (open) {
            if (dr < 0.011f) { float t = dr / 0.011f; v *= 0.30f + 0.45f * t * t; h -= 0.010f * (1 - t * t); gloss = 0.2f; }
            else if (dy < 0 && dy > -0.45f) {                     // the rust run beneath it
                float spread = 0.012f + 0.09f * (-dy);
                float fall = 1.0f + dy / 0.45f;
                rust = sat(1.0f - fabsf(dx) / spread) * fall * fall * 0.60f;
                rust *= 0.45f + c.nz(fx, fy, 30, 4, 0x71Fu, 2);
            }
        } else if (dr < 0.019f) {                                   // patched: grout, lighter, set back
            float t = dr / 0.019f;
            v *= 1.13f - 0.26f * sstep(0.78f, 0.95f, t) + 0.13f * sstep(0.95f, 1.0f, t);
            h -= 0.0015f * (1.0f - sstep(0.75f, 1.0f, t));
        }
        float bh = bug[i], cr = crack[i];
        v *= 1.0f - 0.42f * bh - 0.38f * cr;
        h -= 0.0020f * bh + 0.0010f * cr;
        // runs of wet down the face, from the lift joints
        float drip = c.nz(fx, fy, 14.0f, 0.9f, 82u, 3);
        if (drip > 0.62f) v *= 1.0f - (drip - 0.62f) * 0.38f;
        v *= 1.0f - 0.14f * (1.0f - hgt / HM);                     // darker toward the floor
        float r = 149 * v, g = 147 * v, b = 141 * v;
        if (rust > 0) { r = lerpf(r, 128 * v, rust); g = lerpf(g, 88 * v, rust); b = lerpf(b, 58 * v, rust); }
        // The damp band: darker and greener to about a metre, an uneven tide
        // line, and occasional wet streaks above it.
        float tide = tideCol[x];
        float climb = c.nz(fx, fy, 6.0f, 1.2f, 93u, 3);
        if (climb > 0.62f) tide += (climb - 0.62f) * 1.6f;
        if (hgt < tide) {
            float depth = sat((tide - hgt) / 0.35f);
            float k = 0.86f - 0.20f * depth;
            r *= k * 0.94f; g *= k; b *= k * 0.92f;
            float mould = c.nz(fx, fy, 3.5f, 1.2f, 94u, 3);
            if (mould > 0.60f && hgt < tide - 0.1f) { float t = (mould - 0.60f) * 1.5f; r *= 1 - t * 0.5f; g *= 1 - t * 0.4f; b *= 1 - t * 0.5f; }
            gloss = 1.0f;                                           // still wet
        } else if (hgt < tide + 0.03f) {                            // the tide line, salts left behind
            r *= 1.10f; g *= 1.10f; b *= 1.08f;
        }
        c.set(i, r, g, b);
        c.ht[i] = h;
        c.gl[i] = gloss;
    }
    c.matchLuma(123.4f);
    return finishSurface(c, 1.0f);
}

// A warehouse slab, power-trowelled: overlapping arcs, slightly darker and
// smoother than the surface between them. Under them a curing mottle, sand in the surface, the odd pop-out where a stone near the top
// spalled, two hairline shrinkage cracks, and black rubber scuffs where carts
// turned. Matte overall: the Level 1 lore pass took the sheen off this floor
// deliberately (its puddles come from the shader), and the burnish only moves
// the gloss mask inside a range that stays under the specular cut.
Surface makeConcreteFloorSurface() {
    const int N = 1024;
    Canvas c(N, N, 2.0f, 2.0f);
    Marks pop(N, N), crack(N, N), scuff(N, N);
    Rng rng(0xF100ULL);
    struct Arc { float x, y, r, w, a0, span, k, lo2, hi2; };
    std::vector<Arc> arcs;
    for (int k = 0; k < 22; k++)
        arcs.push_back({ rng.f01() * N, rng.f01() * N, (0.30f + rng.f01() * 0.55f) * N / 2.0f,
                         (0.09f + rng.f01() * 0.10f) * N / 2.0f, rng.f01() * TAU, 0.8f + rng.f01() * 2.2f,
                         0.5f + rng.f01() * 0.5f, 0, 0 });
    for (Arc &a : arcs) { a.lo2 = std::max(0.0f, a.r - a.w); a.lo2 *= a.lo2; a.hi2 = (a.r + a.w) * (a.r + a.w); }
    // Burnish, rasterised arc by arc over each one's own annulus rather than
    // testing every arc at every pixel.
    std::vector<float> burnMap((size_t)N * N, 0.0f);
    for (const Arc &a : arcs) {
        float R = a.r + a.w;
        for (int y = 0; y < N; y++) {
            float dy = wrapd(y + 0.5f, a.y, (float)N);
            if (fabsf(dy) > R) continue;
            float lo = sqrtf(std::max(0.0f, a.lo2 - dy * dy)), hi = sqrtf(std::max(0.0f, a.hi2 - dy * dy));
            for (int side = 0; side < 2; side++) {
                float d0 = side ? lo : -hi, d1 = side ? hi : -lo;
                for (int xi = (int)floorf(a.x + d0) - 1; xi <= (int)ceilf(a.x + d1); xi++) {
                    float dx = xi + 0.5f - a.x;
                    if (dx < d0 || dx > d1 || (side && dx <= -lo)) continue;
                    if (side && lo == 0.0f && dx <= 0.0f) continue;   // the middle column belongs to side 0
                    float d = sqrtf(dx * dx + dy * dy);
                    float band = 1.0f - fabsf(d - a.r) / a.w;
                    if (band <= 0) continue;
                    float ang = fastAtan2(dy, dx) - a.a0;
                    ang -= TAU * floorf(ang / TAU);
                    float along = sstep(0.0f, 0.4f, ang) * sstep(a.span, a.span - 0.4f, ang);
                    int x = ((xi % N) + N) % N;
                    burnMap[(size_t)y * N + x] += band * band * (3 - 2 * band) * along * a.k;
                }
            }
        }
    }
    for (int k = 0; k < 70; k++) pop.dab(rng.f01() * N, rng.f01() * N, 0.8f + rng.f01() * 1.6f);
    for (int k = 0; k < 2; k++)
        crack.walk(rng, rng.f01() * N, rng.f01() * N, rng.f01() * TAU, 300 + rng.ri(0, 250), 1.0f, 0.25f, 0.45f, 0.05f, 0.5f);
    for (int k = 0; k < 16; k++)
        scuff.walk(rng, rng.f01() * N, rng.f01() * N, rng.f01() * TAU, 20 + rng.ri(0, 50), 1.0f, 0.10f, 1.4f, 0.0f, 0.9f);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int i = y * N + x;
        float fx = (float)x, fy = (float)y;
        float burn = burnMap[i];
        burn = std::min(burn, 1.2f);
        float v = 1.0f + (c.nz(fx, fy, 1.1f, 1.1f, 85u, 4) - 0.5f) * 0.18f;   // curing mottle
        float paste = c.nz(fx, fy, 70, 70, 84u, 1) * 0.72f + lat(x, y, 84u) * 0.28f;   // grain, tone and relief
        v += (paste - 0.5f) * 0.06f;
        float sand = lat(x, y, 79u);
        if (sand > 0.975f) v *= 1.12f; else if (sand < 0.02f) v *= 0.86f;
        v *= 1.0f - 0.04f * burn;
        float p = pop[i], cr = crack[i], sc = scuff[i];
        v *= 1.0f - 0.30f * p - 0.28f * cr - 0.16f * sc;
        c.set(i, 93 * v, 91 * v, 87 * v);
        c.ht[i] = -0.0016f * p - 0.0006f * cr + (paste - 0.5f) * 0.00025f * (1.0f - 0.8f * std::min(burn, 1.0f));
        c.gl[i] = 0.62f + 0.30f * std::min(burn, 1.0f);
    }
    c.matchLuma(89.5f);
    return finishSurface(c, 1.0f);
}

// The slab's underside, cast on plywood decking: 1 x 2 m sheets, each its own
// shade and printing its grain along its length, a fin of grout at every joint.
Surface makeConcreteCeilSurface() {
    const int N = 512;
    Canvas c(N, N, 2.0f, 2.0f);
    const float mx = c.mx();
    Marks bug(N, N);
    Rng rng(0xCE1CULL);
    for (int k = 0; k < 900; k++) bug.dab(rng.f01() * N, rng.f01() * N, 0.3f + rng.f01() * rng.f01() * 1.5f);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int i = y * N + x;
        float fx = (float)x, fy = (float)y, wx = (x + 0.5f) * mx, wy = (y + 0.5f) * mx;
        int sheet = (int)(wx / 1.0f);
        float v = 1.0f + (c.nz(fx, fy, 1.5f, 1.5f, 87u, 4) - 0.5f) * 0.20f;
        v *= 1.0f + (lat(sheet, 0, 88u) - 0.5f) * 0.12f;
        v *= 1.0f + (c.nz(fx, fy, 34.0f, 1.5f, 89u, 3) - 0.5f) * 0.07f;   // grain along the sheet
        float h = 0;
        float dj = std::min(fabsf(wrapd(wx, 0.0f, 1.0f)), fabsf(wrapd(wy, 0.0f, 2.0f))) / mx;
        if (dj < 0.8f) v *= 0.78f;
        else if (dj < 2.5f) h += 0.0006f * (1.0f - (dj - 0.8f) / 1.7f);
        float b = bug[i];
        v *= 1.0f - 0.35f * b;
        h -= 0.0015f * b;
        c.set(i, 76 * v, 76 * v, 73 * v);
        c.ht[i] = h;
        c.gl[i] = 0.5f;
    }
    c.matchLuma(73.9f);
    return finishSurface(c, 1.0f);
}

// ---------------------------------------------------------------- Red Halls
//
// Real brick size: 215 x 65 mm with 10 mm joints, so a course is 75 mm and a
// 3 m wall has forty. A 3 m repeat holds thirteen along the wall, 231 mm on
// centre. The old brick was 0.75 m by 0.25 m.
//
// Stretcher bond, each brick with its own tone (from its index, with some dark
// headers and darker ends), a rounded edge, a pitted face, and mortar raked
// back 6 mm so joints cast shadows under the flashlight. Dirty grey mortar, with
// patches of white salt and dark rot.
Surface makeRedBrickSurface() {
    const int N = 1024, COURSES = 40, BRICKS = 13;
    Canvas c(N, N, 3.0f, 3.0f);
    const float ch = (float)N / COURSES, bp = (float)N / BRICKS;
    const float J = 0.010f / c.mx() * 0.5f;                 // half a joint, pixels
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int i = y * N + x;
        float fx = (float)x, fy = (float)y;
        int row = (int)((y + 0.5f) / ch);
        float by = y + 0.5f - row * ch;
        float xb = fmodf(x + 0.5f + ((row & 1) ? bp * 0.5f : 0.0f), (float)N);
        int bi = (int)(xb / bp);
        float bx = xb - bi * bp;
        float dBed = std::min(by, ch - by), dPerp = std::min(bx, bp - bx);
        float ed = std::min(dBed, dPerp);
        float eff = c.nz(fx, fy, 1.6f, 1.6f, 101u, 3);
        float rot = c.nz(fx, fy, 0.9f, 0.9f, 97u, 4);
        float r, g, b, h, gloss;
        if (ed < J) {                                        // the joint
            float m = 0.90f + 0.20f * c.nz(fx, fy, 200, 200, 100u, 2);
            float t = ed / J;
            m *= 0.72f + 0.28f * t;                          // shadowed at the back of the rake
            r = 96 * m; g = 86 * m; b = 78 * m;
            h = -0.006f + 0.0015f * t * t;
            gloss = 0.15f;
        } else {
            float bh = lat(bi, row, 99u);
            float v = 0.86f + (bh - 0.5f) * 0.26f;
            float grain = c.nz(fx, fy, 140, 140, 96u, 2);   // the face's texture, in tone and relief
            v *= 0.94f + 0.12f * grain;
            float ends = 1.0f - dPerp / (bp * 0.5f);
            v *= 1.0f - 0.16f * ends * ends * ends;           // flashed ends
            r = 150 * v; g = 44 * v; b = 34 * v;
            uint32_t bh2 = ih(bi, row, 0x9Au);
            if ((bh2 & 15) == 0) { r *= 0.72f; g *= 0.82f; b *= 0.96f; }   // a blue-burnt header
            else if ((bh2 & 15) == 1) { r *= 1.10f; g *= 1.18f; b *= 1.10f; }   // an underfired one, paler
            float arr = sat((ed - J) / 2.2f);                 // the rounded arris
            h = -0.0025f * (1.0f - arr) * (1.0f - arr);
            if (arr < 1) { float m = 0.88f + 0.12f * arr; r *= m; g *= m; b *= m; }
            float pit = lat(x, y, 0x9Bu);
            if (pit < 0.015f) { r *= 0.72f; g *= 0.72f; b *= 0.72f; h -= 0.0010f; }
            h += (grain - 0.5f) * 0.0006f;
            gloss = 0.35f;
        }
        if (rot > 0.62f) { float t = std::min(1.0f, (rot - 0.62f) * 2.2f); r *= 1 - t * 0.6f; g *= 1 - t * 0.45f; b *= 1 - t * 0.45f; }
        if (eff > 0.64f) {
            float t = std::min(0.45f, (eff - 0.64f) * 1.8f) * (ed < J ? 1.0f : 0.55f);
            r = lerpf(r, 158, t); g = lerpf(g, 148, t); b = lerpf(b, 138, t);
        }
        c.set(i, r, g, b);
        c.ht[i] = h;
        c.gl[i] = gloss;
    }
    // Matched on red alone. The Red Halls' light is (1, 0.22, 0.15) and their
    // ambient smaller still; at these levels the tone curve's toe removes green
    // and blue entirely, so only red is visible. Matched on luma (even luma
    // weighted by that light), the new brick (grey mortar, more green) rendered
    // a tenth darker than the old.
    c.matchLuma(0.299f * 106.9f, { 1.0f, 0.0f, 0.0f });
    return finishSurface(c, 1.0f);
}

// ---------------------------------------------------------------- Poolrooms
//
// Glazed white tile at 1/6 m (a 6-inch tile), 64 px each: twelve to a 2 m floor
// repeat and eighteen to a 3 m wall repeat, so a wall and the floor it stands
// on carry the same grid and their grout lines meet at the foot of the wall.
// The joints are 8 mm and recessed 2.5 mm, and each tile has a small cushion
// edge (the old seven-pixel bevel looked like a chocolate bar). Each tile is
// tilted a few tenths of a degree, differently per tile, which is invisible
// face on but breaks up reflections tile by tile.
Surface makeTileSurface(bool wall) {
    const int T = wall ? 18 : 12, TP = 64, N = T * TP;
    const float M = wall ? 3.0f : 2.0f;
    Canvas c(N, N, M, M);
    const float mpp = c.mx(), G = 1.5f, R = 0.0025f;       // 8 mm joints: 5 mm vanished into the mips at 5 m
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int i = y * N + x;
        int tx = x / TP, ty = y / TP;
        float lx = x % TP + 0.5f, ly = y % TP + 0.5f;
        float ed = std::min(std::min(lx, TP - lx), std::min(ly, TP - ly));
        if (ed < G) {
            float v = 0.97f + 0.06f * c.nz((float)x, (float)y, 150, 150, 90u, 2);
            c.set(i, 164 * v, 171 * v, 167 * v);
            c.ht[i] = -0.0025f;
            c.gl[i] = 0.10f;
            continue;
        }
        uint32_t th = ih(tx, ty, wall ? 0x7A1Eu : 0x7A1Fu);
        float tv = 1.0f + (lat(tx, ty, 91u + wall) - 0.5f) * 0.016f;
        float em = (ed - G) * mpp, h = 0;
        if (em < R) { float q = 1.0f - em / R; h = -R * (1.0f - sqrtf(sat(1.0f - q * q))); }
        float sx = (((th >> 0) & 255) / 255.0f - 0.5f) * 0.012f, sy = (((th >> 8) & 255) / 255.0f - 0.5f) * 0.012f;
        h += sx * (lx - TP * 0.5f) * mpp + sy * (ly - TP * 0.5f) * mpp;
        h += (c.nz((float)x, (float)y, 70, 70, 0x7A20u, 2) - 0.5f) * 0.00005f;   // orange peel in the glaze
        float edgeTone = em < R ? 0.97f + 0.03f * em / R : 1.0f;
        c.set(i, 226 * tv * edgeTone, 229 * tv * edgeTone, 222 * tv * edgeTone);
        c.ht[i] = h;
        c.gl[i] = 1.0f;
    }
    c.matchLuma(220.9f);
    return finishSurface(c, 1.0f);
}

// ---------------------------------------------------------------- LEVEL FUN
//
// Children's-party paper hung in the same drops as Level 0's, over the same
// skirting: bunting printed along the top, a confetti print, crayon smileys
// somebody added later, and the same grime as everywhere else down here.
Surface makePartyWallSurface() {
    const int W = 1024, H = 1024;
    Canvas c(W, H, 3.0f, 3.0f);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int i = y * W + x;
        float hgt = (H - 0.5f - y) / H * 3.0f, fx = (float)x, fy = (float)y;
        if (hgt < SKIRT) { skirting(c, i, x, y, hgt, 96, 70, 42); continue; }
        float grime = c.nz(fx, fy, 3.0f, 3.0f, 207u, 4);
        float stain = c.nz(fx, fy, 1.3f, 1.3f, 212u, 4);
        float base = (1.0f - 0.14f * grime) * (1.0f - 0.05f * (1.0f - hgt / 3.0f));
        if (stain > 0.66f) base *= 1.0f - (stain - 0.66f) * 0.8f;
        float tone = 1.0f;
        paperRelief(c, i, x, y, x % 256, tone, 0);
        base *= tone * (1.0f + (lat(x / 256, 0, 208u) - 0.5f) * 0.03f);
        float r = 240 * base, g = 223 * base, b = 190 * base;
        // confetti print, gone dingy: round dots on a 16 px lattice
        uint32_t chh = ih((x >> 4) & 63, (y >> 4) & 63, 209u);
        if (chh % 6 == 0) {
            float cx = (x & ~15) + 4.0f + (chh >> 8) % 8, cy = (y & ~15) + 4.0f + (chh >> 12) % 8;
            float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
            float t = sstep(3.6f, 2.6f, d) * (0.75f - 0.2f * grime);
            Color pc = PARTY[(chh >> 6) % 5];
            r = lerpf(r, pc.r * base, t); g = lerpf(g, pc.g * base, t); b = lerpf(b, pc.b * base, t);
        }
        // crayon smileys, drawn on afterwards, mid-wall; waxy, so a little proud
        if (hgt > 0.6f && hgt < 2.4f) {
            uint32_t sh2 = ih(x >> 8, y >> 8, 214u);
            if (sh2 % 3 == 0) {
                float cx2 = (float)((x >> 8) << 8) + 80 + (sh2 % 96), cy2 = (float)((y >> 8) << 8) + 88 + ((sh2 >> 8) % 80);
                float dx2 = x - cx2, dy2 = y - cy2, d = sqrtf(dx2 * dx2 + dy2 * dy2);
                float wob = (c.nz(fx, fy, 60, 60, 215u, 1) - 0.5f) * 2.0f;
                bool ring = fabsf(d - 28.0f + wob) < 3.0f;
                bool eye = (fabsf(dx2 + 10) < 3.0f || fabsf(dx2 - 10) < 3.0f) && fabsf(dy2 + 8) < 3.5f;
                bool smile = fabsf(d - 16.0f + wob) < 3.0f && dy2 > 6.0f;
                if (ring || eye || smile) {
                    float wax = 0.75f + 0.25f * lat(x, y, 216u);   // crayon skips over the emboss
                    r = lerpf(r, 168 * base, wax); g = lerpf(g, 62 * base, wax); b = lerpf(b, 54 * base, wax);
                    c.ht[i] += 0.00008f * wax;
                }
            }
        }
        // bunting printed along the top of the wall, 6-36 cm down from it
        float down = 3.0f - hgt;
        if (down > 0.059f && down < 0.363f) {
            if (down < 0.082f) { r = 70 * base; g = 58 * base; b = 48 * base; }   // the printed string
            else {
                int seg = x / 128;
                float lx = (float)(x % 128), halfw = 48.0f * (1.0f - (down - 0.082f) / 0.281f);
                if (fabsf(lx - 64) < halfw) {
                    Color pc = PARTY[seg % 5];
                    float pv = base * (0.92f + 0.08f * sinf(x * 0.35f));
                    r = pc.r * pv; g = pc.g * pv; b = pc.b * pv;
                }
            }
        }
        c.set(i, r, g, b);
        if (hgt < SKIRT + 0.02f) c.mul(i, 0.90f + 0.10f * (hgt - SKIRT) / 0.02f);
        c.gl[i] = 0.55f;
    }
    c.matchLuma(179.1f);
    return finishSurface(c, 1.0f);
}

// Banquet-hall carpet: a burgundy ground under a gold trellis, a rosette in each
// diamond, navy sprigs and quatrefoils, on a 2/3 m repeat. Muted, because bright
// gold on a regular grid looked like floor tiles at a distance. The old floor
// was flat red with confetti. Cut pile, so the shade varies in soft patches.
// Some flattened confetti is still in it.
Surface makePartyCarpetSurface() {
    const int N = 768, REP = 256;                        // three repeats of 2/3 m to the 2 m tile
    Canvas c(N, N, 2.0f, 2.0f);
    const float px = 1.0f / REP;                         // one pixel in motif units
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int i = y * N + x;
        float fx = (float)x, fy = (float)y;
        float u = (x % REP + 0.5f) / REP - 0.5f, v = (y % REP + 0.5f) / REP - 0.5f;
        float r = 124, g = 26, b = 34;                   // burgundy ground
        auto paint = [&](float cov, float cr, float cg, float cb) {
            r = lerpf(r, cr, cov); g = lerpf(g, cg, cov); b = lerpf(b, cb, cov);
        };
        // a darker damask ground-figure: the woven shadow of a leaf scroll
        float ang = atan2f(v, u), rad = sqrtf(u * u + v * v);
        float scroll = sinf(ang * 4.0f + rad * 22.0f) * sinf(ang * 2.0f - rad * 9.0f);
        paint(sstep(0.35f, 0.70f, scroll) * sstep(0.14f, 0.22f, rad) * 0.55f, 88, 15, 25);
        // the trellis: a diamond joining the cell's edge midpoints, old gold
        float dia = fabsf(fabsf(u) + fabsf(v) - 0.5f);
        paint(sstep(0.010f + px, 0.010f - px, dia) * 0.8f, 150, 108, 50);
        // sprigs on the diagonals, navy leaves pointing at the trellis corners
        for (int q = 0; q < 4; q++) {
            float ca = q & 1 ? 0.7071f : -0.7071f, sa = q & 2 ? 0.7071f : -0.7071f;
            float lu = u * ca + v * sa - 0.255f, lv = -u * sa + v * ca;
            float leaf = sqrtf(lu * lu / 0.0022f + lv * lv / 0.00035f);
            paint(sstep(1.0f + 8 * px, 1.0f - 8 * px, leaf) * 0.85f, 34, 36, 64);
        }
        // the rosette: eight petals in dull gold round a dark ring and a gold heart
        float petal = 0.115f + 0.032f * cosf(ang * 8.0f);
        paint(sstep(petal + px, petal - px, rad) * 0.9f, 140, 96, 44);
        paint(sstep(petal - 0.018f + px, petal - 0.018f - px, rad) * 0.5f, 120, 30, 32);
        paint(sstep(0.010f, 0.0f, fabsf(rad - 0.058f) - 0.005f), 64, 12, 20);
        paint(sstep(0.024f + px, 0.024f - px, rad) * 0.9f, 164, 120, 56);
        // quatrefoils where the trellis meets the cell edge
        for (int q = 0; q < 4; q++) {
            float qu = u - (q == 0 ? 0.5f : q == 1 ? -0.5f : 0.0f), qv = v - (q == 2 ? 0.5f : q == 3 ? -0.5f : 0.0f);
            float qr = sqrtf(qu * qu + qv * qv), qa = atan2f(qv, qu);
            float foil = 0.042f + 0.016f * cosf(qa * 4.0f);
            paint(sstep(foil + px, foil - px, qr) * 0.9f, 36, 38, 70);
            paint(sstep(0.012f + px, 0.012f - px, qr) * 0.8f, 150, 108, 50);
        }
        // cut pile: the tufts' own fleck, and the pile lying different ways
        float pile = 0.93f + 0.10f * lat(x, y, 205u) + 0.10f * (c.nz(fx, fy, 6, 6, 233u, 3) - 0.5f);
        r *= pile; g *= pile; b *= pile;
        float h = (lat(x, y, 206u) - 0.5f) * 0.0005f + (c.nz(fx, fy, 40, 40, 207u, 2) - 0.5f) * 0.0005f;
        float gloss = 0.3f;
        // confetti trodden in: paper, flat and a little shiny
        uint32_t fh = ih((x >> 2) % (N >> 2), (y >> 2) % (N >> 2), 231u);
        if (fh % 70 == 0) {
            Color pc = PARTY[(fh >> 7) % 5];
            float cv = 0.78f;
            r = pc.r * cv; g = pc.g * cv; b = pc.b * cv;
            h = 0.0004f; gloss = 1.0f;
        }
        c.set(i, r, g, b);
        c.ht[i] = h;
        c.gl[i] = gloss;
    }
    c.matchLuma(71.8f);
    return finishSurface(c, 1.0f);
}

// LEVEL FUN's ceiling: black acoustic board on a black grid. Same construction
// as Level 0's, with 0.5 m boards.
Surface makePartyCeilSurface() {
    return boardCeiling(512, 4, 2.0f, 22, 20, 25, 30, 29, 33, 120, 0xFA27u, 19.3f);
}

Surface makeSurface(SurfSlot slot) {
    switch (slot) {
        case SURF_CARPET:    return makeCarpetSurface();
        case SURF_BOARDS:    return makeCeilingSurface();
        case SURF_PAPER:     return makeWallpaperSurface();
        case SURF_SLAB:      return makeConcreteFloorSurface();
        case SURF_SOFFIT:    return makeConcreteCeilSurface();
        case SURF_CONCWALL:  return makeConcreteWallSurface();
        case SURF_POOLFLOOR: return makeTileSurface(false);
        case SURF_POOLWALL:  return makeTileSurface(true);
        case SURF_BRICK:     return makeRedBrickSurface();
        case SURF_BANQUET:   return makePartyCarpetSurface();
        case SURF_PARTYCEIL: return makePartyCeilSurface();
        case SURF_PARTYWALL: return makePartyWallSurface();
        case SURF_COUNT:     break;
    }
    return {};
}


