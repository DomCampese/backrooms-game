#include "textures.h"
#include "util.h"
#include "object_materials.generated.h"
#include <cmath>
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------- textures
static Texture2D finishTexture(Image img, bool tiled) {
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    if (tiled) {
        GenTextureMipmaps(&t);
        // raylib's anisotropic filter only sets the anisotropy level. Without
        // trilinear first, min/mag stay GL_NEAREST from LoadTexture: the mips
        // go unused, and ANGLE draws the point/filtered switch as rings
        // around the camera on floors and walls.
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_8X);
        SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    } else SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

Texture2D makeWallpaperTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float vy = (float)y / H;
        float stripe = 0.97f + 0.03f * sinf(x * TAU / 42.0f);
        float lines = 0.985f + 0.015f * sinf(x * 0.9f);
        float grime = fbm2(x * 0.013f, y * 0.013f, 7u, 4);
        float stain = fbm2(x * 0.006f + 31.0f, y * 0.006f, 12u, 4);
        float base = stripe * lines * (1.0f - 0.16f * grime) * (1.0f - 0.10f * vy);
        if (stain > 0.62f) base *= 1.0f - (stain - 0.62f) * 0.8f;
        // Wallpaper arrives on a roll and gets hung in strips, so there is a seam
        // every so often — a hairline of shadow with a lifting edge beside it,
        // and the two strips never quite match in tone. Without them a wall is
        // one printed sheet a hundred metres long, which is the thing that most
        // gives away that a corridor is generated rather than decorated.
        int strip = x / 128, sx = x % 128;
        base *= 1.0f + (lat(strip, 0, 15u) - 0.5f) * 0.030f;   // roll-to-roll tone drift
        // A hairline, not a stripe. Anything stronger than this and the post
        // pass's chromatic aberration picks the seam up and draws a coloured
        // line down the wall at every one of them.
        if (sx < 2) base *= 0.93f;                             // the seam itself
        else if (sx < 9) base *= 0.985f + 0.015f * ((sx - 2) / 7.0f);
        // damp creeping up from the skirting, worst in the corners of the roll
        float damp = fbm2(x * 0.017f, y * 0.006f, 16u, 3) * (0.25f + 0.95f * vy * vy);
        float r = 199 * base, g = 178 * base, b = 104 * base;
        if (damp > 0.42f) {
            float t = std::min(0.55f, (damp - 0.42f) * 1.7f);
            r = r * (1 - t) + 96 * t; g = g * (1 - t) + 92 * t; b = b * (1 - t) + 58 * t;
        }
        if (y > H - 46) {                            // baseboard
            float t = fbm2(x * 0.02f, y * 0.1f, 99u, 3);
            r = 92 - 22 * t; g = 74 - 18 * t; b = 42 - 11 * t;
            if (y < H - 40) { r *= 0.45f; g *= 0.45f; b *= 0.45f; }
        }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    return finishTexture(img, true);
}

Texture2D makeCarpetTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float n = lat(x, y, 5u) * 0.16f - 0.08f;
        // Loop pile, not sand. Real contract carpet is rows of loops laid in one
        // direction, and the give-away is that it is anisotropic: stretched along
        // the run, tight across it. The old isotropic noise read as a flat dirty
        // colour from any distance, which is the one thing carpet never does.
        float fiber = (fbm2(x * 0.09f, y * 0.42f, 33u, 2) - 0.5f) * 0.17f;
        float loop = sinf(y * 1.55f + vnoise2(x * 0.30f, y * 0.05f, 34u) * 3.4f);
        float v = 1.0f + n + fiber + loop * 0.045f;
        // walked lanes: the pile lies flat and goes darker and slightly shinier
        float lane = fbm2(x * 0.004f, y * 0.010f, 35u, 3);
        if (lane > 0.55f) v *= 1.0f - (lane - 0.55f) * 0.55f;
        float blotch = fbm2(x * 0.008f, y * 0.008f, 21u, 4);
        if (blotch > 0.56f) v *= 1.0f - (blotch - 0.56f) * 0.9f;
        p[y * W + x] = { cl8(141 * v), cl8(124 * v), cl8(66 * v), 255 };
    }
    return finishTexture(img, true);
}

Texture2D makeCeilingTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float v = 1.0f;
        float sp = lat(x, y, 44u);
        if (sp > 0.90f) v *= 0.80f;
        if (sp > 0.985f) v *= 0.55f;
        // Mineral-fibre board is not a flat surface with speckle on it: it is
        // covered in wandering worm-track fissures, and it is the fissures your
        // eye reads as "suspended ceiling" before it reads anything else. A
        // narrow band of a warped noise field draws them; the pass either side
        // of the band puts a lip on the near edge, which is what stops them
        // looking like ink and starts them looking like grooves.
        // Keep them fine. At a first pass these ran at a fifth of this frequency
        // and four times this depth, and what came out was not fissured board but
        // a ceiling covered in dark wandering water-trails.
        float wob = fbm2(x * 0.14f, y * 0.14f, 61u, 2);
        float fis = fbm2(x * 0.075f + wob * 0.9f, y * 0.048f - wob * 0.7f, 60u, 3);
        float band = fabsf(fis - 0.5f);
        if (band < 0.030f) v *= 0.84f + 0.16f * (band / 0.030f);     // the groove
        else if (band < 0.055f) v *= 1.0f + 0.03f * (1.0f - band / 0.055f);   // its lit lip
        // pinholes, punched in a loose scatter the way the real board is
        uint32_t ph = ih(x >> 2, y >> 2, 62u);
        if ((ph & 63u) == 0u) {
            int cxp = (x >> 2 << 2) + 1 + (int)((ph >> 8) & 1u), cyp = (y >> 2 << 2) + 1 + (int)((ph >> 9) & 1u);
            float dd = (float)((x - cxp) * (x - cxp) + (y - cyp) * (y - cyp));
            if (dd < 2.2f) v *= 0.52f;
        }
        float r = 208 * v, g = 202 * v, b = 179 * v;
        float stain = fbm2(x * 0.01f, y * 0.01f, 55u, 3);
        if (stain > 0.64f) {
            float t = std::min(0.6f, (stain - 0.64f) * 2.2f);
            r = r * (1 - t) + 172 * t; g = g * (1 - t) + 150 * t; b = b * (1 - t) + 96 * t;
        }
        // Tile edges: a shadowed groove where two boards meet, then the chamfer
        // on each board catching light. A single flat dark line read as a grid
        // painted on one continuous sheet; the light side is what lifts each
        // board off its neighbour and makes the grid look laid-in.
        int bx = x % 256, by = y % 256;
        int ex = std::min(bx, 255 - bx), ey = std::min(by, 255 - by), ed = std::min(ex, ey);
        if (ed < 2) { r *= 0.50f; g *= 0.50f; b *= 0.50f; }
        else if (ed < 7) { float t = (ed - 2) / 5.0f; float m = 0.74f + 0.34f * t; r *= m; g *= m; b *= m; }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    return finishTexture(img, true);
}

// A SMILER, as the lore has it: no body you can pin down, just a shape of
// darker dark with a grin and two eyes floating in it. Two sheets on the same
// ENT_FRAMES x ENT_ROWS grid as the other residents. `glow` = false is the fog
// body, lit and fogged like anything else; `glow` = true is the eyes and
// teeth alone, drawn unlit on top, because the grin in an unlit corridor is
// the whole of the creature. Frames churn the fog rather than stride; the
// shared gait still drives them, so the smoke boils faster as it closes.
Texture2D makeSmilerTex(bool glow) {
    const int FW = 128, FH = 256, W = FW * ENT_FRAMES, H = FH * ENT_ROWS;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int hr = 0; hr < ENT_ROWS; hr++)
    for (int f = 0; f < ENT_FRAMES; f++) {
        int fx = f * FW, fy = hr * FH;
        float ph = (float)f / ENT_FRAMES * TAU;
        // 0 = turned away (no face at all), 1 = full on. The face slides and
        // narrows across the head as it comes round.
        float look = (hr < ENT_ROW_LEAN_L) ? (float)hr / ENT_ROW_FACE : 1.0f;
        float shear = (hr == ENT_ROW_LEAN_L) ? -14.0f : (hr == ENT_ROW_LEAN_R) ? 14.0f : 0.0f;
        auto off = [&](float y) { return shear * clampf((252.0f - y) / 250.0f, 0, 1); };
        for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++) {
            float cx = 64 + off((float)y), dx = x - cx;
            Color c = BLANK;
            if (!glow) {
                // Silhouette: a head, broad hunched shoulders, then smoke
                // thinning toward the floor. Ragged by drifting noise.
                float half = y < 70  ? 26.0f * sqrtf(fmaxf(0, 1 - powf((y - 42) / 30.0f, 2)))
                           : y < 120 ? 26.0f + (y - 70) * 0.55f
                                     : 53.0f - (y - 120) * 0.30f;
                float n = fbm2(x * 0.06f + sinf(ph) * 1.3f, y * 0.035f - ph * 0.45f, 91u, 4);
                float edge = half * (0.75f + 0.55f * n) - fabsf(dx);
                float fade = clampf((256.0f - y) / 110.0f, 0, 1);   // dissolves toward the floor
                float a = clampf(edge / 14.0f, 0, 1) * fade * (0.55f + 0.45f * n);
                if (a > 0.01f) {
                    unsigned char v = (unsigned char)(6 + 10 * n);
                    c = { v, (unsigned char)(v * 0.9f), v, (unsigned char)(235 * a) };
                }
            } else if (look > 0.2f) {
                float sx = 1.0f / (0.45f + 0.55f * look);           // narrower while turning
                float fcx = cx - (1.0f - look) * 12.0f;
                float u = (x - fcx) * sx, g = 0;
                // eyes: tilted slits, hot centres and a halo
                for (int side = -1; side <= 1; side += 2) {
                    float ex = u - side * 13.0f, ey = (y - 36) + side * ex * 0.18f;
                    float d = ex * ex / 36.0f + ey * ey / 6.0f;
                    g = fmaxf(g, clampf(1.6f - d, 0, 1));
                    g = fmaxf(g, 0.35f * expf(-d * 0.15f));
                }
                // the grin: a crescent far wider than any mouth, lined with teeth
                float mu = u / 34.0f;
                if (fabsf(mu) < 1.0f) {
                    float top = 52 + 6 * mu * mu, bot = 52 + 18 * (1 - mu * mu) * 0.9f + 4;
                    float yy = (float)y;
                    if (yy > top - 1 && yy < bot + 1) {
                        float mid = (top + bot) * 0.5f;
                        // alternating fangs from top and bottom; dark gaps between
                        float tp = fmodf(fabsf(u) + 100.0f, 7.0f) / 7.0f;
                        float fang = 1.0f - fabsf(tp - 0.5f) * 2.0f;          // 0..1 peak mid-tooth
                        bool upper = yy < mid;
                        float reach = upper ? (yy - top) / (mid - top + 0.01f) : (bot - yy) / (bot - mid + 0.01f);
                        float tooth = reach < fang * 1.05f ? 1.0f : 0.0f;
                        g = fmaxf(g, tooth * (0.85f + 0.15f * (1 - fabsf(mu))));
                    }
                    // faint glow bleeding off the lips
                    float lip = fminf(fabsf(y - top), fabsf(y - bot));
                    g = fmaxf(g, 0.25f * expf(-lip * 0.25f) * (1 - fabsf(mu)));
                }
                g *= clampf((look - 0.2f) / 0.4f, 0, 1);
                if (g > 0.01f)
                    c = { 250, 246, 226, (unsigned char)(255 * clampf(g, 0, 1)) };
            }
            p[(fy + y) * W + fx + x] = c;
        }
    }
    return finishTexture(img, false);
}

// Where one leg is at a given point in the gait, as a fraction of full stride.
//
// Not a sine. A leg spends about 60% of a cycle planted — sliding backwards
// under the body at the speed the body moves forward — and the other 40%
// swinging through, off the floor. A sine gets you a walk that skates, and
// worse, it is symmetric: sin(60 deg) and sin(120 deg) put the ankle in exactly
// the same place, so half the frames of an evenly sampled sheet come out
// duplicates. Measured on the first version of this sheet, frames 1 and 2
// differed by 129 pixels out of 1900 and frames 4 and 5 by 96.
//
// `x` runs -1 (trailing) to +1 (leading); `lift` is 0 planted, 1 at the top of
// the swing.
static void legPose(float ph, float &x, float &lift) {
    ph -= floorf(ph);
    if (ph < 0.6f) { float t = ph / 0.6f; x = 1.0f - 2.0f * t; lift = 0.0f; }
    else           { float t = (ph - 0.6f) / 0.4f; x = -1.0f + 2.0f * t; lift = sinf(t * 3.14159265f); }
}

// THE PARTYGOER =): pale yellow, painted-on smile, striped party hat. It was
// here before the bunting went up. It will be here after.
Texture2D makePartygoerTex() {
    const int FW = 128, FH = 256, W = FW * ENT_FRAMES, H = FH * ENT_ROWS;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    int fx = 0, fy = 0;
    auto put = [&](int x, int y, Color c) { x += fx; y += fy; if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; };
    auto hspan = [&](int y, float cx, float halfw, Color c) {
        for (int x = (int)(cx - halfw); x <= (int)(cx + halfw); x++) put(x, y, c);
    };
    Color skin = { 208, 182, 84, 255 };
    Color skin2 = { 176, 150, 62, 255 };
    // It walks like something wearing a body rather than living in one: the legs
    // do the work and the arms barely answer, which is most of why it is worse
    // to look at than Clark.
    for (int hr = 0; hr < ENT_ROWS; hr++)
    for (int f = 0; f < ENT_FRAMES; f++) {
    fx = f * FW; fy = hr * FH;
    // 0 = looking away over his shoulder, 1 = facing you. The head, hat, beard
    // and eye all ride it; the body does not turn, because the thing that makes
    // this read is the head moving independently of the walk.
    float look = (hr < ENT_ROW_LEAN_L) ? (float)hr / (ENT_ROW_FACE) : 1.0f;
    float headShift = -(1.0f - look) * 9.0f;
    // He tips into where he is going, from the feet up — they stay planted.
    float shear = (hr == ENT_ROW_LEAN_L) ? -12.0f : (hr == ENT_ROW_LEAN_R) ? 12.0f : 0.0f;
    float ph = (float)f / ENT_FRAMES;
    float lx0, ll0, rx0, rl0;
    legPose(ph, lx0, ll0);
    legPose(ph + 0.5f, rx0, rl0);
    float legL = lx0 * 6.0f, liftL = ll0 * 5.5f;
    float legR = rx0 * 6.0f, liftR = rl0 * 5.5f;
    float armSw = -lx0 * 2.2f;
    // Same rule as Clark: its face is painted on, so the smile and the eyes have
    // to travel with the head or they end up on the wall behind it.
    auto bodyOff = [&](float y) {
        float o = shear * clampf((252.0f - y) / 250.0f, 0, 1);
        if (y < 76) o += headShift * clampf((76.0f - y) / 54.0f, 0, 1);
        return o;
    };
    for (int y = 24; y < 252; y++) {
        float wob = (vnoise2(0.05f * y, 8.2f, 177u) - 0.5f) * 6.0f;
        float rag = (vnoise2(0.35f * y, 4.4f, 188u) - 0.5f) * 2.2f;
        float cx = 64 + wob * 0.3f;
        cx += bodyOff((float)y);
        if (y >= 24 && y <= 62) {   // round head
            float dy = (y - 43) / 20.0f;
            if (dy * dy < 1.0f) hspan(y, cx, 19.0f * sqrtf(1 - dy * dy) + rag * 0.5f, skin);
        }
        if (y > 58 && y <= 68) hspan(y, cx, 6 + rag, skin2);              // neck
        if (y > 64 && y <= 200) {   // soft drippy body, widening as it goes
            float t = (y - 64) / 136.0f;
            float halfw = 11 + 15 * t;
            float hem = (y > 190) ? (vnoise2(0.6f * y, 2.5f, 171u) - 0.5f) * 5 : 0;
            hspan(y, cx, halfw + rag + hem, skin);
        }
        if (y > 78 && y <= 178) {   // arms, hanging a little too still
            float t = (y - 78) / 100.0f;
            float off = 21 + 8 * t;
            hspan(y, cx - off - armSw * t, 3.4f + rag * 0.4f, skin2);
            hspan(y, cx + off + armSw * t, 3.4f + rag * 0.4f, skin2);
        }
        if (y > 200 && y < 252) {   // legs
            float t = (y - 200) / 52.0f;
            float lx = cx - 9 + wob * 0.2f + legL * t, rx = cx + 9 + wob * 0.2f + legR * t;
            if (y < 252 - liftL) {
                hspan(y, lx, 5.2f + rag * 0.4f, skin2);
                if (y > 246 - liftL) hspan(y, lx, 7, skin2);
            }
            if (y < 252 - liftR) {
                hspan(y, rx, 5.2f + rag * 0.4f, skin2);
                if (y > 246 - liftR) hspan(y, rx, 7, skin2);
            }
        }
    }
    // something sweet dripped down it once and never dried
    for (int x = fx; x < fx + FW; x++) {
        if (lat(x - fx, 7, 191u) < 0.82f) continue;
        int len = 30 + (int)(lat(x - fx, 9, 192u) * 90);
        for (int y = 70 + fy; y < 70 + fy + len && y < fy + 250; y++)
            if (p[y * W + x].a) {
                Color &c = p[y * W + x];
                c.r = cl8(c.r * 0.82f); c.g = cl8(c.g * 0.80f); c.b = cl8(c.b * 0.72f);
            }
    }
    // the face: two dot eyes and a smile that was painted on, not grown
    auto putIf = [&](int x, int y, Color c) {
        x += fx + (int)bodyOff((float)y); y += fy;
        if (x >= 0 && x < W && y >= 0 && y < H && p[y * W + x].a) p[y * W + x] = c;
    };
    Color ink = { 34, 26, 20, 255 };
    for (int dy = -12; dy <= 12; dy++) for (int dx = -14; dx <= 14; dx++) {
        float d = sqrtf((float)(dx * dx + dy * dy));
        if (fabsf(d - 11.0f) < 1.8f && dy > 3) putIf(64 + dx, 42 + dy, ink);   // wide smile
    }
    for (int s = -1; s <= 1; s += 2)
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++)
            if (dx * dx + dy * dy < 7) putIf(64 + s * 7 + dx, 36 + dy, ink);   // eyes
    {   // striped cone hat, slightly askew; nobody remembers putting it on
        Color ha = { 196, 60, 54, 255 }, hb = { 84, 138, 192, 255 };
        for (int y = 2; y <= 26; y++) {
            float t = (y - 2) / 24.0f;
            hspan(y, 60 + t * 4 + bodyOff((float)y), 1.0f + 11.0f * t, ((y / 5) & 1) ? ha : hb);
        }
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy < 5) put(60 + dx + (int)bodyOff(2.0f), 2 + dy, { 226, 218, 200, 255 });   // pompom
    }
    }
    return finishTexture(img, false);
}

// ---------------------------------------------------------------- wall scrawl
//
// What an earlier wanderer wrote on the wall, drawn stroke by stroke rather
// than blitted from a font. Everything else in this project is synthesized;
// this was the one surface that fell back to raylib's bitmap font, and it
// showed — blocky digital type on a wall that is supposed to carry somebody's
// handwriting.
//
// A glyph is a handful of control points in a unit box, which is far too few
// to look like writing on its own. Three things turn them into a hand:
// Catmull-Rom through the control points, so coarse points give smooth curves
// rather than a polygon; a wobble that pulls the path off course at low
// frequency, so no two letters are drawn quite alike; and a pen whose width
// varies along the stroke, so the line thins where the hand moved fast and
// pools where it slowed or turned.
//
// Points are two digits each, x then y, '0'-'9' over the glyph box with y
// running down. That resolution is deliberately coarse — the spline and the
// wobble carry the detail, and a table of exact coordinates would be both
// unreadable and a false promise of precision in something meant to be shaky.
struct Glyph { char ch; const char *stroke[3]; };
static const Glyph GLYPHS[] = {
    { 'A', { "0924406499", "2676", nullptr } },
    { 'B', { "0009", "0060722540", "0475876909" } },
    { 'C', { "91602215286998", nullptr, nullptr } },
    { 'D', { "0009", "0051845809", nullptr } },
    { 'E', { "90000999", "0565", nullptr } },
    { 'F', { "900009", "0555", nullptr } },
    { 'G', { "91602215286998", "985595", nullptr } },
    { 'H', { "0009", "9099", "0595" } },
    { 'I', { "5059", nullptr, nullptr } },
    { 'J', { "80876928", nullptr, nullptr } },
    { 'K', { "0009", "9005", "2599" } },
    { 'L', { "101999", nullptr, nullptr } },
    { 'M', { "0910569099", nullptr, nullptr } },
    { 'N', { "09009990", nullptr, nullptr } },
    { 'O', { "508194875927142150", nullptr, nullptr } },
    { 'P', { "0009", "0061735505", nullptr } },
    { 'Q', { "508194875927142150", "6799", nullptr } },
    { 'R', { "0009", "0061735505", "4599" } },
    { 'S', { "9160212375875918", nullptr, nullptr } },
    { 'T', { "0090", "5059", nullptr } },
    { 'U', { "0016498790", nullptr, nullptr } },
    { 'V', { "005990", nullptr, nullptr } },
    { 'W', { "0029548990", nullptr, nullptr } },
    { 'X', { "0099", "9009", nullptr } },
    { 'Y', { "005590", "5559", nullptr } },
    { 'Z', { "0090", "9009", "0999" } },
    { '0', { "508194875927142150", "8128", nullptr } },
    { '1', { "325059", nullptr, nullptr } },
    { '2', { "12306082751999", nullptr, nullptr } },
    { '3', { "11507244", "44857829", nullptr } },
    { '4', { "701696", "7279", nullptr } },
    { '5', { "90202464867829", nullptr, nullptr } },
    { '6', { "80332759875526", nullptr, nullptr } },
    { '7', { "0090", "9049", nullptr } },
    { '8', { "50213365774927457350", nullptr, nullptr } },
    { '9', { "798461323575", nullptr, nullptr } },
    { '.', { "4849", nullptr, nullptr } },
    { ',', { "5839", nullptr, nullptr } },
    { '\'', { "5052", nullptr, nullptr } },
    { '!', { "5056", "5859", nullptr } },
    { '?', { "114071735556", "5859", nullptr } },
    { '-', { "1585", nullptr, nullptr } },
    { '=', { "1484", "1686", nullptr } },
    { ')', { "30636639", nullptr, nullptr } },
    { '(', { "60333669", nullptr, nullptr } },
    { '/', { "8019", nullptr, nullptr } },
    { ':', { "4344", "4647", nullptr } },
};


// One dab of the pen. Ink pools rather than stacking: overlapping dabs take the
// darker alpha instead of summing, or every junction and turn would blow out to
// a solid blob while the straights stayed thin.
static void inkDab(Color *p, int W, int H, float cx, float cy, float rad, Color ink, float strength,
                   int cl, int ct, int cr, int cb) {
    int x0 = (int)(cx - rad) - 1, x1 = (int)(cx + rad) + 1;
    int y0 = (int)(cy - rad) - 1, y1 = (int)(cy + rad) + 1;
    if (x0 < cl) x0 = cl;
    if (y0 < ct) y0 = ct;
    if (x1 > cr) x1 = cr;
    if (y1 > cb) y1 = cb;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    float soft = rad * 0.62f < 0.7f ? 0.7f : rad * 0.62f;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > rad) continue;
        float a = strength * clampf((rad - d) / soft, 0, 1) * (ink.a / 255.0f);
        Color &c = p[y * W + x];
        float ca = c.a / 255.0f;
        if (a <= ca) continue;
        float t = (a - ca) / (1.0f - ca + 1e-4f);   // how much new ink shows here
        c.r = cl8(c.r + (ink.r - c.r) * t);
        c.g = cl8(c.g + (ink.g - c.g) * t);
        c.b = cl8(c.b + (ink.b - c.b) * t);
        c.a = cl8(a * 255.0f);
    }
}

// Catmull-Rom through the control points, so four coarse points make a curve
// and not a bent wire.
static void splineAt(const float *px, const float *py, int n, float t, float &ox, float &oy) {
    if (n == 1) { ox = px[0]; oy = py[0]; return; }
    float u = t * (n - 1);
    int i = (int)u; if (i > n - 2) i = n - 2;
    float f = u - i;
    int i0 = i - 1 < 0 ? 0 : i - 1, i1 = i, i2 = i + 1, i3 = i + 2 > n - 1 ? n - 1 : i + 2;
    float f2 = f * f, f3 = f2 * f;
    float b0 = -0.5f * f3 + f2 - 0.5f * f, b1 = 1.5f * f3 - 2.5f * f2 + 1.0f;
    float b2 = -1.5f * f3 + 2.0f * f2 + 0.5f * f, b3 = 0.5f * f3 - 0.5f * f2;
    ox = px[i0] * b0 + px[i1] * b1 + px[i2] * b2 + px[i3] * b3;
    oy = py[i0] * b0 + py[i1] * b1 + py[i2] * b2 + py[i3] * b3;
}

// One stroke of the pen, wobbling off course and varying its pressure. Returns
// the lowest point it reached, which is where a drip would start if this stroke
// is the one that gets one.
static void penStroke(Color *p, int W, int H, const float *cxs, const float *cys, int n,
                      Color ink, float wid, Rng &r, float &lowX, float &lowY,
                      int cl, int ct, int cr, int cb) {
    float ph1 = r.f01() * TAU, ph2 = r.f01() * TAU, ph3 = r.f01() * TAU;
    float wob = wid * (0.55f + r.f01() * 0.7f);     // how far this stroke strays
    float lean = (r.f01() - 0.5f) * 0.10f;          // and which way it drifts overall
    int steps = 12 + (int)(n * 9);
    lowY = -1e9f; lowX = 0;
    for (int s = 0; s <= steps; s++) {
        float t = (float)s / steps;
        float x, y; splineAt(cxs, cys, n, t, x, y);
        x += sinf(t * 5.3f + ph1) * wob + sinf(t * 11.7f + ph2) * wob * 0.35f + lean * wid * t * 3.0f;
        y += cosf(t * 4.1f + ph2) * wob * 0.8f + sinf(t * 13.1f + ph3) * wob * 0.25f;
        // pressure: thin where the hand ran, heavier at the ends and on the
        // slow parts, plus a little grain so no two strokes weigh the same
        float ends = 0.55f + 0.45f * sinf(t * 3.14159f);
        float press = ends * (0.72f + 0.5f * (0.5f + 0.5f * sinf(t * 7.9f + ph3)));
        float rad = wid * clampf(press, 0.30f, 1.5f);
        float a = clampf(0.55f + 0.45f * press, 0.25f, 1.0f);
        inkDab(p, W, H, x, y, rad, ink, a, cl, ct, cr, cb);
        if (y > lowY) { lowY = y; lowX = x; }
    }
}

// Paint ran. One or two per phrase, from the bottom of a stroke, thinning and
// fading as gravity takes it — the thing that most says "wet paint on a wall"
// rather than "text drawn on a wall".
static void inkDrip(Color *p, int W, int H, float x, float y, float wid, Color ink, Rng &r,
                    int cl, int ct, int cr, int cb) {
    int len = 6 + r.ri(0, 34);
    float drift = (r.f01() - 0.5f) * 0.6f;
    for (int i = 0; i < len; i++) {
        float t = (float)i / len;
        float yy = y + i * 0.9f;
        if (yy > cb) break;
        inkDab(p, W, H, x + drift * i, yy, wid * (0.62f - 0.42f * t), ink, (1.0f - t) * 0.75f, cl, ct, cr, cb);
    }
    if (y + len * 0.9f < cb)   // the bead that gathered at the end and dried there
        inkDab(p, W, H, x + drift * len, y + len * 0.9f, wid * 0.42f, ink, 0.5f, cl, ct, cr, cb);
}

// How much room each character takes. Letters are drawn at a jittered height,
// so the advance has to leave slack or a wide letter runs into its neighbour —
// "NO CLIP" came out as "NO QIP" before this. A space is nearly a full letter:
// below that the words stop reading as separate words.
static float glyphAdvance(char ch) {
    if (ch == ' ') return 0.92f;
    if (ch == '.' || ch == ',' || ch == '\'' || ch == ':' || ch == '!') return 0.52f;
    if (ch == 'I' || ch == '1') return 0.62f;
    return 1.10f;
}

// One phrase, laid out left to right on a baseline that is not quite level.
static void drawScrawl(Color *p, int W, int H, const char *text, float x, float y,
                       float h, Color ink, Rng &r, int cl, int ct, int cr, int cb) {
    float pen = x, tilt = (r.f01() - 0.5f) * 0.09f;   // the whole line runs slightly downhill
    float wid = h * (0.055f + r.f01() * 0.03f);
    int dripsLeft = r.ri(1, 2);
    int glyphs = 0;
    for (const char *c = text; *c; c++) if (*c != ' ') glyphs++;
    int dripEvery = glyphs > 0 ? glyphs / (dripsLeft + 1) + 1 : 1;
    int seen = 0;
    for (const char *c = text; *c; c++) {
        char ch = *c >= 'a' && *c <= 'z' ? (char)(*c - 32) : *c;   // one shaky case, as on a wall
        float adv = glyphAdvance(ch) * h * 0.62f;
        if (ch == ' ') { pen += adv; continue; }
        const Glyph *g = nullptr;
        for (const Glyph &cand : GLYPHS) if (cand.ch == ch) { g = &cand; break; }
        if (!g) { pen += adv; continue; }
        // every letter its own size and slant — a hand does not repeat itself
        float gh = h * (0.88f + r.f01() * 0.24f);
        float slant = (r.f01() - 0.5f) * 0.22f;
        float base = y + tilt * (pen - x) + (r.f01() - 0.5f) * h * 0.10f;
        float lowX = 0, lowY = 0, bestLowX = 0, bestLowY = -1e9f;
        for (int si = 0; si < 3 && g->stroke[si]; si++) {
            const char *sp = g->stroke[si];
            int n = (int)(strlen(sp) / 2); if (n < 1) continue;
            float cxs[16], cys[16];
            for (int k = 0; k < n && k < 16; k++) {
                float gx = (sp[k * 2] - '0') / 9.0f, gy = (sp[k * 2 + 1] - '0') / 9.0f;
                cys[k] = base + gy * gh;
                cxs[k] = pen + gx * gh * 0.55f - (gy - 0.5f) * slant * gh;   // slant leans the top
            }
            penStroke(p, W, H, cxs, cys, n < 16 ? n : 16, ink, wid, r, lowX, lowY, cl, ct, cr, cb);
            if (lowY > bestLowY) { bestLowY = lowY; bestLowX = lowX; }
        }
        seen++;
        if (dripsLeft > 0 && seen % dripEvery == 0 && r.f01() < 0.75f) {
            inkDrip(p, W, H, bestLowX, bestLowY, wid, ink, r, cl, ct, cr, cb);
            dripsLeft--;
        }
        pen += adv;
    }
}

static float scrawlWidth(const char *text, float h) {
    float w = 0;
    for (const char *c = text; *c; c++) {
        char ch = *c >= 'a' && *c <= 'z' ? (char)(*c - 32) : *c;
        w += glyphAdvance(ch) * h * 0.62f;
    }
    return w;
}

// wall scrawl atlas: 32 phrases in 32 different hands, 4 x 8 cells of 256x128
Texture2D makeScrawlTex() {
    const int W = 1024, H = 1024, CW = W / 4, CH = H / 8;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    // Thirty-two of them, so that seeing the same line twice in one run means
    // something rather than meaning the pool is small. They are all somebody
    // trying to leave a fact behind: a count, a warning, a rule they worked out.
    static const char *LINES[32] = {
        "NO CLIP",              "dont stare",           "day 407",              "the exit lies",
        "he hears the flares",  "keep walking",         "it hums at night",     "wrong door =)",
        "i counted 12 doors",   "none of them out",     "turn left. always",    "dont sleep here",
        "the lights know",      "day 1 again",          "it wears a coat",      "smells like almond",
        "i was here. was i",    "same room twice",      "no stairs go up",      "hold still :(",
        "water is a floor",     "dont say your name",   "fire moves it",        "it is taller today",
        "421 and counting",     "my watch stopped",     "listen for dogs",      "the party never ends",
        "i can hear the hum",   "there is no 13th",     "follow the pipes",     "help",
    };
    Rng r(0x5C12ULL);
    for (int i = 0; i < 32; i++) {
        int cx = (i % 4) * CW, cy = (i / 4) * CH;
        // whoever wrote it used whatever was to hand: dried blood-brown,
        // charcoal, marker, chalk, a rust-coloured smear
        static const Color INKS[5] = {
            { 104, 32, 26, 218 }, { 54, 46, 40, 208 }, { 32, 34, 52, 200 },
            { 122, 96, 44, 196 }, { 178, 170, 158, 176 },
        };
        Color ink = INKS[r.ri(0, 4)];
        float h = CH * (0.44f + r.f01() * 0.14f);
        float w = scrawlWidth(LINES[i], h);
        while (w > CW * 0.86f && h > CH * 0.085f) { h *= 0.92f; w = scrawlWidth(LINES[i], h); }
        float x0 = cx + (CW - w) * 0.5f + (r.f01() - 0.5f) * CW * 0.04f;
        float y0 = cy + (CH - h) * 0.5f + (r.f01() - 0.5f) * CH * 0.08f;
        if (x0 < cx + 3) x0 = cx + 3;
        drawScrawl(p, W, H, LINES[i], x0, y0, h, ink, r, cx + 2, cy + 2, cx + CW - 3, cy + CH - 3);
    }
    return finishTexture(img, false);
}

// ---------------------------------------------------------------- fixtures
// The fittings the building would actually have: outlets at skirting height
// (backrooms canon names them specifically), a switch, a return-air grille, a
// ceiling diffuser, and a fire-exit sign that points somewhere there is no
// exit. Plus one flat galvanised swatch, which is what the conduit runs and
// sprinkler heads sample — they are geometry rather than decals, and putting
// them in the fixtures mesh keeps them off the props mesh's 16-bit index
// budget.
//
// Each fitting gets an atlas rect of its own proportions rather than a slot in
// a uniform grid, so that its cell and the quad that carries it are the same
// shape and nothing is stretched. FIXTURES (textures.h) is that table; the
// pixel rects below are the same rectangles in atlas space.
static const int FIXPX[FIX_COUNT][4] = {   // x, y, w, h in the 512px atlas
    {   8,   8,  96, 152 },   // FIX_OUTLET        75 x 118 mm
    { 120,   8,  96, 152 },   // FIX_OUTLET_BROKEN
    { 232,   8,  92, 148 },   // FIX_SWITCH        72 x 115 mm
    {   8, 168, 224, 160 },   // FIX_GRILLE        560 x 400 mm
    { 296, 176, 192, 192 },   // FIX_DIFFUSER      600 x 600 mm
    { 280, 400, 224,  80 },   // FIX_SIGN          560 x 200 mm
};
const FixtureRect FIXTURES[FIX_COUNT] = {
    { 8/512.f,     8/512.f, 104/512.f, 160/512.f, 0.0375f, 0.059f  },
    { 120/512.f,   8/512.f, 216/512.f, 160/512.f, 0.0375f, 0.059f  },
    { 232/512.f,   8/512.f, 324/512.f, 156/512.f, 0.036f,  0.0575f },
    { 8/512.f,   168/512.f, 232/512.f, 328/512.f, 0.280f,  0.200f  },
    { 296/512.f, 176/512.f, 488/512.f, 368/512.f, 0.300f,  0.300f  },
    { 280/512.f, 400/512.f, 504/512.f, 480/512.f, 0.280f,  0.100f  },
};

Texture2D makeFixturesTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    auto box = [&](int x0, int y0, int x1, int y1, Color c) {
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > W - 1) x1 = W - 1;
        if (y1 > H - 1) y1 = H - 1;
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) p[y * W + x] = c;
    };
    auto frame = [&](int x0, int y0, int x1, int y1, int t, Color c) {
        box(x0, y0, x1, y0 + t - 1, c); box(x0, y1 - t + 1, x1, y1, c);
        box(x0, y0, x0 + t - 1, y1, c); box(x1 - t + 1, y0, x1, y1, c);
    };
    auto line = [&](int ax, int ay, int bx, int by, int t, Color c) {
        int n = (abs(bx - ax) > abs(by - ay) ? abs(bx - ax) : abs(by - ay)) + 1;
        for (int i = 0; i < n; i++) {
            int x = ax + (bx - ax) * i / (n - 1), y = ay + (by - ay) * i / (n - 1);
            box(x - t / 2, y - t / 2, x - t / 2 + t - 1, y - t / 2 + t - 1, c);
        }
    };
    // Nothing down here has been wiped in years. Dirt gathers in the corners of
    // a plate and along the underside of every louvre, which is most of what
    // makes moulded plastic read as moulded plastic and not a grey rectangle.
    auto grime = [&](int x0, int y0, int x1, int y1, uint32_t s, float amt) {
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            if (x < 0 || y < 0 || x > W - 1 || y > H - 1) continue;
            Color &c = p[y * W + x];
            if (!c.a) continue;
            float v = 1.0f - amt * fbm2(x * 0.075f, y * 0.075f, s, 3);
            c.r = cl8(c.r * v); c.g = cl8(c.g * v); c.b = cl8(c.b * v);
        }
    };
    const unsigned char OP = 254;   // textured, opaque, no relief bump — see AGENTS.md

    // --- a duplex outlet, intact and with the cover torn off. Both fill their
    // rect: the plate *is* the fitting, so there is no margin to leave.
    for (int variant = 0; variant < 2; variant++) {
        const int *r = FIXPX[variant];
        int x0 = r[0], y0 = r[1], x1 = r[0] + r[2] - 1, y1 = r[1] + r[3] - 1;
        const Color plate = { 226, 219, 198, OP }, lipHi = { 243, 237, 219, OP };
        const Color lipLo = { 170, 163, 144, OP }, slot = { 28, 26, 25, OP };
        box(x0, y0, x1, y1, plate);
        box(x0, y0, x1, y0 + 3, lipHi); box(x0, y0, x0 + 3, y1, lipHi);
        box(x0, y1 - 3, x1, y1, lipLo); box(x1 - 3, y0, x1, y1, lipLo);
        if (variant == 0) {
            for (int k = 0; k < 2; k++) {
                int cy = y0 + 38 + k * 76;
                box(x0 + 14, cy - 26, x1 - 14, cy + 26, { 213, 206, 186, OP });   // moulded recess
                frame(x0 + 14, cy - 26, x1 - 14, cy + 26, 2, { 182, 174, 154, OP });
                box(x0 + 30, cy - 19, x0 + 37, cy + 2, slot);                     // neutral
                box(x1 - 37, cy - 19, x1 - 30, cy + 2, slot);                     // live
                box(x0 + 43, cy + 9, x1 - 43, cy + 17, slot);                     // ground
            }
            box(x0 + 43, y0 + 71, x1 - 43, y0 + 79, { 146, 140, 124, OP });       // centre screw
        } else {
            // Half the cover is gone. What is left is the hole, the yoke still
            // screwed to the box, and two wire ends nobody made safe.
            box(x0 + 30, y0, x1, y1, { 24, 22, 21, OP });
            for (int y = y0; y <= y1; y++) {          // a torn edge, not a cut one
                int w = 6 + (int)(lat(y, 3, 0xF1u) * 14);
                box(x0 + 30, y, x0 + 30 + w, y, plate);
                box(x0 + 30 + w, y, x0 + 31 + w, y, lipLo);
            }
            box(x0 + 46, y0 + 30, x1 - 10, y0 + 44, { 96, 92, 84, OP });          // yoke
            box(x0 + 46, y1 - 44, x1 - 10, y1 - 30, { 96, 92, 84, OP });
            line(x0 + 54, y0 + 58, x1 - 18, y0 + 86, 6, { 172, 106, 48, OP });    // copper, bare
            line(x0 + 60, y1 - 22, x1 - 24, y1 - 52, 5, { 178, 174, 168, OP });
        }
        grime(x0, y0, x1, y1, 0xA1u + (unsigned)variant * 7u, 0.34f);
    }

    // --- a switch plate, its rocker resting off
    {
        const int *r = FIXPX[FIX_SWITCH];
        int x0 = r[0], y0 = r[1], x1 = r[0] + r[2] - 1, y1 = r[1] + r[3] - 1;
        box(x0, y0, x1, y1, { 228, 221, 200, OP });
        box(x0, y0, x1, y0 + 3, { 244, 238, 220, OP });
        box(x0, y1 - 3, x1, y1, { 170, 163, 144, OP });
        int rx0 = x0 + 30, rx1 = x1 - 30, ry0 = y0 + 36, ry1 = y1 - 36;
        box(rx0, ry0, rx1, ry1, { 236, 230, 212, OP });
        box(rx0, ry0, rx1, ry0 + (ry1 - ry0) / 2, { 212, 205, 187, OP });   // the pressed half, in shadow
        frame(rx0, ry0, rx1, ry1, 2, { 174, 167, 148, OP });
        box(x0 + 40, y0 + 12, x1 - 40, y0 + 20, { 146, 140, 124, OP });     // screws
        box(x0 + 40, y1 - 20, x1 - 40, y1 - 12, { 146, 140, 124, OP });
        grime(x0, y0, x1, y1, 0xB3u, 0.32f);
    }

    // --- return-air grille. Louvres are lit on top and dark underneath, which
    // is the whole read: without the dark line it is a striped rectangle.
    {
        const int *r = FIXPX[FIX_GRILLE];
        int x0 = r[0], y0 = r[1], x1 = r[0] + r[2] - 1, y1 = r[1] + r[3] - 1;
        box(x0, y0, x1, y1, { 150, 147, 138, OP });
        frame(x0, y0, x1, y1, 5, { 180, 176, 165, OP });
        box(x0 + 11, y0 + 11, x1 - 11, y1 - 11, { 24, 23, 22, OP });
        for (int i = 0; i < 9; i++) {
            int ly = y0 + 16 + i * 14;
            if (ly + 8 > y1 - 12) break;
            box(x0 + 12, ly, x1 - 12, ly + 5, { 168, 164, 152, OP });
            box(x0 + 12, ly + 6, x1 - 12, ly + 8, { 56, 54, 51, OP });
        }
        for (int sx = x0 + 7; sx <= x1 - 7; sx += (x1 - x0 - 14))
            for (int sy = y0 + 7; sy <= y1 - 7; sy += (y1 - y0 - 14))
                box(sx - 3, sy - 3, sx + 3, sy + 3, { 116, 112, 102, OP });
        grime(x0, y0, x1, y1, 0xC7u, 0.42f);
    }

    // --- ceiling supply diffuser, egg-crate core
    {
        const int *r = FIXPX[FIX_DIFFUSER];
        int x0 = r[0], y0 = r[1], x1 = r[0] + r[2] - 1, y1 = r[1] + r[3] - 1;
        box(x0, y0, x1, y1, { 158, 155, 146, OP });
        frame(x0, y0, x1, y1, 6, { 186, 182, 172, OP });
        box(x0 + 13, y0 + 13, x1 - 13, y1 - 13, { 22, 21, 20, OP });
        for (int i = 1; i < 7; i++) {   // the crate: thin bars, each catching light on one side
            int gx = x0 + 13 + i * (x1 - x0 - 26) / 7, gy = y0 + 13 + i * (y1 - y0 - 26) / 7;
            box(gx, y0 + 13, gx + 3, y1 - 13, { 128, 125, 117, OP });
            box(gx, y0 + 13, gx, y1 - 13, { 174, 170, 160, OP });
            box(x0 + 13, gy, x1 - 13, gy + 3, { 128, 125, 117, OP });
            box(x0 + 13, gy, x1 - 13, gy, { 174, 170, 160, OP });
        }
        grime(x0, y0, x1, y1, 0xD5u, 0.36f);
    }

    // --- the fire-exit sign. It points down a corridor like any other.
    {
        const int *r = FIXPX[FIX_SIGN];
        int x0 = r[0], y0 = r[1], x1 = r[0] + r[2] - 1, y1 = r[1] + r[3] - 1;
        box(x0, y0, x1, y1, { 26, 118, 62, OP });
        frame(x0, y0, x1, y1, 3, { 232, 232, 226, OP });
        const Color ink = { 238, 240, 234, OP };
        int ty = y0 + 22, th = 38, tx = x0 + 22, sw = 6;   // E X I T, in strokes
        box(tx, ty, tx + sw, ty + th, ink);
        box(tx, ty, tx + 22, ty + sw, ink);
        box(tx, ty + th / 2 - 3, tx + 18, ty + th / 2 + 3, ink);
        box(tx, ty + th - sw, tx + 22, ty + th, ink);
        tx += 34;
        line(tx, ty + 3, tx + 22, ty + th - 3, sw, ink);
        line(tx + 22, ty + 3, tx, ty + th - 3, sw, ink);
        tx += 34;
        box(tx, ty, tx + sw, ty + th, ink);
        tx += 20;
        box(tx, ty, tx + 26, ty + sw, ink);
        box(tx + 10, ty, tx + 16, ty + th, ink);
        int ax = x1 - 46, ay = (y0 + y1) / 2;            // and the arrow
        box(ax - 26, ay - 5, ax + 2, ay + 5, ink);
        for (int i = 0; i <= 18; i++) box(ax + 2 + i, ay - 19 + i, ax + 2 + i, ay + 19 - i, ink);
        grime(x0, y0, x1, y1, 0xE9u, 0.40f);
    }

    // --- plain galvanised metal, covering the pixel at UV (0.375, 0.75).
    //
    // That is not an arbitrary corner: `addSolidBox` hardcodes exactly that UV
    // for every face it emits, and the props atlas keeps its plain metal there
    // for the same reason. Two atlases agreeing on where "plain metal" lives is
    // what lets the conduit and sprinkler bodies — geometry, not decals — go
    // through the same helper as everything else. Move it and they sample a
    // transparent cell and vanish without a word.
    {
        int ox = 144, oy = 336;                       // 128 px square, contains (192, 384)
        for (int y = oy; y < oy + 128; y++) for (int x = ox; x < ox + 128; x++) {
            float v = 0.88f + 0.24f * fbm2(x * 0.11f, y * 0.11f, 0x5Eu, 3);
            p[y * W + x] = { cl8(150 * v), cl8(150 * v), cl8(146 * v), OP };
        }
    }
    return finishTexture(img, false);
}

// prop atlas: left half cardboard, right-top cabinet front (drawers), right-bottom plain metal
Texture2D makePropsTex() {
    const int W = 1024, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float r, g, b;
        if (x >= 512) {
            int xx = x-512;
            if (y < 256) { // lacquered veneer: long grain, pores, rubbed edges
                float warp = vnoise2(xx*0.018f,y*0.008f,621u)*13;
                float grain = sinf(xx*0.30f+warp) * 0.065f;
                float pore = lat(xx,y/4,622u) < 0.06f ? 0.12f : 0;
                float v=0.86f+grain-pore+(fbm2(xx*0.012f,y*0.04f,623u,2)-0.5f)*0.16f;
                if (xx<5 || xx>506 || y<5 || y>250) v*=0.78f;
                r=228*v;g=207*v;b=173*v;
            } else { // woven upholstery; neutral so each prop keeps its own tint
                int yy=y-256;
                float weave = ((xx&3)<2 ? 0.035f : -0.035f) + ((yy&3)<2 ? 0.025f : -0.025f);
                float stain=fbm2(xx*0.013f,yy*0.020f,624u,3);
                float v=0.88f+weave-std::max(0.0f,stain-0.5f)*0.35f;
                if (xx<7 || xx>504 || yy<7 || yy>248) v*=0.70f;
                r=232*v;g=228*v;b=215*v;
            }
        } else if (x < 256) {                               // cardboard
            float n = (fbm2(x * 0.03f, y * 0.03f, 61u, 3) - 0.5f) * 0.18f;
            float v = 1.0f + n + (lat(x,y,611u)-0.5f)*0.055f;
            if (x%64 < 2) v*=0.91f; // compressed fold fibres
            if (x < 18 || x > 238 || y < 18 || y > 494) v *= 0.80f;   // box edges
            r = 166 * v; g = 128 * v; b = 84 * v;
            if (y > 238 && y < 274) {                // packing tape
                float tv = 1.0f + (lat(x, y, 62u) - 0.5f) * 0.1f;
                r = 196 * tv; g = 188 * tv; b = 160 * tv;
            }
            if (x > 60 && x < 150 && y > 330 && y < 392) { r = 205; g = 198; b = 178; } // label
        } else {                                     // office metal
            float brush = (fbm2(x * 0.9f, y * 0.02f, 73u, 2) - 0.5f) * 0.10f;
            float v = 1.0f + brush;
            r = 138 * v; g = 144 * v; b = 134 * v;
            if (y < 256) {                           // cabinet front: 3 drawers
                int dy = y - 10;
                if (dy >= 0 && dy < 234) {
                    int drawer = dy % 78;
                    if (drawer < 5) { r *= 0.55f; g *= 0.55f; b *= 0.55f; }           // gaps
                    if (drawer > 30 && drawer < 42 && x > 352 && x < 416) { r *= 0.6f; g *= 0.6f; b *= 0.6f; } // handles
                }
            }
            if (x < 262 + 4 || x > 506 || (y % 256) < 6 || (y % 256) > 250) { r *= 0.78f; g *= 0.78f; b *= 0.78f; }
        }
        if (x>256 && x<512 && y<256) {
            // Enamel worn through at drawer corners, with tiny fastener heads.
            int dy=(y-10+78)%78;
            if ((x<279 || x>493) && (dy<12 || dy>70) && lat(x,y,612u)>0.70f) {
                r=112;g=96;b=72;
            }
            if ((abs(x-281)<3 || abs(x-487)<3) && abs(dy-14)<3) {
                r=185;g=188;b=181;
                if (dy==14) { r=65;g=67;b=65; }
            }
        }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    for (int x=72;x<136;++x) if ((ih(x/2,0,613u)&3)!=0)
        for (int y=352;y<377;++y) p[y*W+x]={72,65,52,255};
    // Scanned CC0 tiles replace only neutral material regions. Labels and
    // cabinet fronts retain their authored layout; existing prop tints survive.
    auto stamp = [&](const unsigned char *bytes,int size,int x0,int y0,int w,int h,float contrast) {
        Image source=LoadImageFromMemory(".jpg",bytes,size);
        Color *sample=LoadImageColors(source);
        for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
            const Color &c=sample[(y*source.height/h)*source.width+x*source.width/w];
            float lum=(c.r*0.30f+c.g*0.59f+c.b*0.11f)/255;
            float v=clampf(0.75f+(lum-0.45f)*contrast,0.35f,1);
            // Leave a padded edge so atlas mip levels do not borrow neighbours.
            float edge=(x<4 || x>=w-4 || y<4 || y>=h-4) ? 0.90f : 1;
            p[(y0+y)*W+x0+x]={cl8(245*v*edge),cl8(242*v*edge),cl8(235*v*edge),255};
        }
        UnloadImageColors(sample);UnloadImage(source);
    };
    stamp(object_wood,sizeof(object_wood),512,0,512,256,0.75f);
    stamp(object_fabric,sizeof(object_fabric),512,256,512,256,0.90f);
    stamp(object_metal,sizeof(object_metal),256,256,256,256,0.55f);
    return finishTexture(img, true);
}

// Level 1: bare concrete
Texture2D makeConcreteWallTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float vy = (float)y / H;
        float v = 1.0f + (fbm2(x * 0.02f, y * 0.02f, 81u, 4) - 0.5f) * 0.28f;
        float drip = fbm2(x * 0.06f, y * 0.006f, 82u, 3);
        if (drip > 0.60f) v *= 1.0f - (drip - 0.60f) * 0.9f;          // water streaks
        // Hairline cracks, as a level set of a noise field. Sparser and lighter
        // than they were: a level set closes on itself, so at the old width and
        // depth the wall came out ruled with dark loops that read as a contour map
        // rather than as cracking. Thin enough and they read as hairlines again.
        float crack = fbm2(x * 0.015f, y * 0.015f, 83u, 4);
        if (fabsf(crack - 0.5f) < 0.0026f) v *= 0.66f;
        // exposed aggregate — the stones in the mix, lighter and harder-edged
        // than the paste around them. Poured concrete without them is plaster.
        float agg = lat(x >> 1, y >> 1, 89u);
        if (agg > 0.965f) v *= 1.16f; else if (agg > 0.93f) v *= 1.07f;
        else if (agg < 0.035f) v *= 0.86f;                             // blowholes
        // Form-tie holes on the shutter grid, rust bleeding down from each.
        // The grid pitch has to divide the texture width or the pattern breaks at
        // the wrap and every tile boundary gets a row of half-holes; 128 does,
        // and staggering alternate rows keeps it from reading as a checkerboard.
        int hrow = y / 128;
        int hx = (x + 64 + (hrow & 1) * 64) % 128, hy = y % 128;
        float hd = sqrtf((float)((hx - 64) * (hx - 64) + (hy - 64) * (hy - 64)));
        float r = 119 * v, g = 117 * v, b = 111 * v;
        if (hd < 6.0f) { float t = 1.0f - hd / 6.0f; float m = 1.0f - 0.55f * t * t; r *= m; g *= m; b *= m; }
        else if (hd < 22.0f && hy > 64) {                              // the rust streak below it
            float t = (1.0f - (hd - 6.0f) / 16.0f) * 0.30f;
            r = r * (1 - t) + 120 * t; g = g * (1 - t) + 82 * t; b = b * (1 - t) + 52 * t;
        }
        float fall = 1.0f - 0.14f * vy;                                // darker toward the floor
        p[y * W + x] = { cl8(r * fall), cl8(g * fall), cl8(b * fall), 255 };
    }
    return finishTexture(img, true);
}

Texture2D makeConcreteFloorTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float v = 1.0f + (lat(x, y, 84u) - 0.5f) * 0.10f + (fbm2(x * 0.03f, y * 0.03f, 85u, 3) - 0.5f) * 0.2f;
        // power-float sweeps: long shallow arcs the trowel left behind, plus the
        // aggregate showing through where the slab has been walked bare
        float sweep = vnoise2(x * 0.004f + y * 0.0015f, y * 0.012f, 78u);
        v *= 1.0f + (sweep - 0.5f) * 0.10f;
        float agg = lat(x >> 1, y >> 1, 79u);
        if (agg > 0.972f) v *= 1.13f; else if (agg < 0.028f) v *= 0.88f;
        float r = 93 * v, g = 91 * v, b = 87 * v;
        float oil = fbm2(x * 0.009f, y * 0.009f, 86u, 4);
        if (oil > 0.60f) {                                             // old oil stains
            float t = std::min(0.75f, (oil - 0.60f) * 2.6f);
            r = r * (1 - t) + 52 * t; g = g * (1 - t) + 48 * t; b = b * (1 - t) + 40 * t;
        }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    return finishTexture(img, true);
}

Texture2D makeConcreteCeilTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float v = 1.0f + (fbm2(x * 0.025f, y * 0.025f, 87u, 3) - 0.5f) * 0.22f;
        float form = fbm2(x * 0.004f, y * 0.09f, 88u, 2);              // formwork seams
        if (form > 0.62f) v *= 0.82f;
        p[y * W + x] = { cl8(76 * v), cl8(76 * v), cl8(73 * v), 255 };
    }
    return finishTexture(img, true);
}

// Red Halls: oppressive dark red brick
Texture2D makeRedBrickTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        // 12 courses to the texture, as a float: at a flat `y / 42` the twelfth
        // course is only 8 pixels tall before the texture wraps, so a wall shows a
        // squashed line of half-bricks at every vertical repeat. It was always
        // there — giving each brick its own tone off its course index is what made
        // it visible, because the mismatch stopped being a mismatch of nothing.
        const float CH = 512.0f / 12.0f;
        int row = (int)(y / CH);
        int by = (int)(y - row * CH);
        int bx = (x + (row % 2) * 64) % 128;
        // Every brick fired differently, so no two are the same colour. One noise
        // field for the whole wall gave a single sheet of red with lines ruled on
        // it; keying the tone off the brick's own index is what breaks the wall
        // back up into bricks.
        int bi = (x + (row % 2) * 64) / 128;
        float bh = lat(bi, row, 99u);
        float v = 1.0f + (fbm2(x * 0.04f, y * 0.04f, 96u, 3) - 0.5f) * 0.35f + (bh - 0.5f) * 0.30f;
        float r = 118 * v, g = 26 * v, b = 20 * v;
        if (bh > 0.93f) { r *= 0.72f; g *= 0.80f; b *= 0.88f; }        // the odd blue-burnt header
        // Mortar is raked back behind the brick face, so the joint is not a flat
        // dark stripe: it is a shadow at the top of the course and a lit ledge at
        // the bottom. That one gradient is what gives a brick wall its depth.
        int em = std::min(std::min(by, (int)CH - 1 - by), std::min(bx, 127 - bx));
        if (em < 4) {
            float mv = 1.0f + (lat(x, y, 100u) - 0.5f) * 0.22f;
            mv *= (by < 4) ? 0.68f : (by > (int)CH - 5 ? 1.22f : 0.95f);   // shadow above, ledge below
            r = 40 * mv; g = 13 * mv; b = 11 * mv;
        } else if (em < 8) {                                           // the brick's own arris
            float t = (em - 4) / 4.0f;
            float m = (by < (int)CH / 2 ? 1.10f : 0.90f);
            float mm = 1.0f + (m - 1.0f) * (1.0f - t);
            r *= mm; g *= mm; b *= mm;
        }
        float rot = fbm2(x * 0.008f, y * 0.008f, 97u, 4);
        if (rot > 0.60f) { float t = (rot - 0.60f) * 1.8f; r *= 1 - t * 0.7f; g *= 1 - t * 0.5f; b *= 1 - t * 0.5f; }
        // efflorescence: salt bloomed out of the wet brick in pale patches
        float eff = fbm2(x * 0.012f + 7.0f, y * 0.012f, 101u, 3);
        if (eff > 0.68f) { float t = std::min(0.40f, (eff - 0.68f) * 1.5f);
                           r = r * (1 - t) + 150 * t; g = g * (1 - t) + 138 * t; b = b * (1 - t) + 128 * t; }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    return finishTexture(img, true);
}

// Poolrooms: white ceramic tile
Texture2D makeTileTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int gx = x % 64, gy = y % 64;
        int tx = x / 64, ty = y / 64;
        float r, g, b;
        int ex = std::min(gx, 63 - gx), ey = std::min(gy, 63 - gy), ed = std::min(ex, ey);
        if (ed < 2) { // fine, clean grout; no mildew or chipped ceramic
            float gv = 1.0f + (lat(x,y,90u)-0.5f)*0.025f;
            r=183*gv; g=189*gv; b=185*gv;
        } else {
            float tv=1.0f+(lat(tx,ty,91u)-0.5f)*0.018f;
            float bevel=clampf((ed-2)/3.0f,0,1);
            tv *= 0.97f+0.03f*bevel;
            r=226*tv; g=229*tv; b=222*tv;
        }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    return finishTexture(img, true);
}

// LEVEL FUN =): children's-party wallpaper — bunting up top, confetti,
// crayon smiley faces, and the same grime as everywhere else down here
Texture2D makePartyWallTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float vy = (float)y / H;
        float grime = fbm2(x * 0.013f, y * 0.013f, 207u, 4);
        float stain = fbm2(x * 0.006f + 17.0f, y * 0.006f, 212u, 4);
        float base = (0.985f + 0.015f * sinf(x * 0.9f)) * (1.0f - 0.14f * grime) * (1.0f - 0.08f * vy);
        if (stain > 0.64f) base *= 1.0f - (stain - 0.64f) * 0.8f;
        float r = 226 * base, g = 206 * base, b = 168 * base;
        // confetti print, gone dingy
        uint32_t ch = ih(x >> 3, y >> 3, 209u);
        if (ch % 11 == 0) {
            int lx = x & 7, ly2 = y & 7;
            if ((lx - 4) * (lx - 4) + (ly2 - 4) * (ly2 - 4) < 7) {
                Color c = PARTY[(ch >> 6) % 5];
                r = c.r * base; g = c.g * base; b = c.b * base;
            }
        }
        // crayon smileys, one per 128px cell or so, mid-wall
        if (y > 96 && y < H - 96) {
            uint32_t sh2 = ih(x >> 7, y >> 7, 214u);
            if (sh2 % 3 == 0) {
                float cx2 = (float)((x >> 7) << 7) + 40 + (sh2 % 48), cy2 = (float)(((y >> 7) << 7) + 44 + ((sh2 >> 8) % 40));
                float dx2 = x - cx2, dy2 = y - cy2, d = sqrtf(dx2 * dx2 + dy2 * dy2);
                bool ring = fabsf(d - 14.0f) < 1.7f;
                bool eye = (fabsf(dx2 + 5) < 1.6f || fabsf(dx2 - 5) < 1.6f) && fabsf(dy2 + 4) < 1.8f;
                bool smile = fabsf(d - 8.0f) < 1.6f && dy2 > 3.0f;
                if (ring || eye || smile) { r = 168 * base; g = 62 * base; b = 54 * base; }
            }
        }
        if (y >= 10 && y < 62) {   // bunting strung along the top of the wall
            if (y < 14) { r = 70; g = 58; b = 48; }   // the string
            else {
                int seg = x / 64;
                float lx = (float)(x % 64), halfw = 24.0f * (1.0f - (y - 14) / 48.0f);
                if (fabsf(lx - 32) < halfw) {
                    Color c = PARTY[seg % 5];
                    float pv = base * (0.92f + 0.08f * sinf(x * 0.7f));
                    r = c.r * pv; g = c.g * pv; b = c.b * pv;
                }
            }
        }
        if (y > H - 46) {   // baseboard
            float t = fbm2(x * 0.02f, y * 0.1f, 216u, 3);
            r = 92 - 22 * t; g = 74 - 18 * t; b = 42 - 11 * t;
            if (y < H - 40) { r *= 0.45f; g *= 0.45f; b *= 0.45f; }
        }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    return finishTexture(img, true);
}

// LEVEL FUN =): the same sad carpet, but someone spilled confetti into it forever
Texture2D makePartyCarpetTex() {
    // deep banquet-hall red, worn dark in the walked lanes, confetti ground in
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float n = lat(x, y, 205u) * 0.16f - 0.08f;
        float fiber = (fbm2(x * 0.18f, y * 0.18f, 233u, 2) - 0.5f) * 0.14f;
        float blotch = fbm2(x * 0.008f, y * 0.008f, 221u, 4);
        float v = 1.0f + n + fiber;
        if (blotch > 0.54f) v *= 1.0f - (blotch - 0.54f) * 1.0f;   // trodden-dark patches
        float r = 156 * v, g = 34 * v, b = 42 * v;
        uint32_t fh = ih(x >> 2, y >> 2, 231u);
        if (fh % 24 == 0) {   // trodden-in confetti, brighter than the carpet
            Color c = PARTY[(fh >> 7) % 5];
            float cv = v * 0.9f;
            r = c.r * cv; g = c.g * cv; b = c.b * cv;
        }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    return finishTexture(img, true);
}

// LEVEL FUN =): the ceiling has gone dark, so the light panels read like a
// party hall's — hot rectangles floating in near-black
Texture2D makePartyCeilTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float v = 1.0f + (fbm2(x * 0.03f, y * 0.03f, 244u, 3) - 0.5f) * 0.5f;
        float r = 20 * v, g = 18 * v, b = 22 * v;   // near-black with a faint cool tint
        int bx = x % 128, by = y % 128;             // suggestion of big tiles
        if (bx < 3 || bx > 124 || by < 3 || by > 124) { r *= 1.8f; g *= 1.8f; b *= 1.9f; }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    return finishTexture(img, true);
}

// Baked-AO gradient: a strip that fades from solid shadow (v=0) to nothing
// (v=1). Every contact-shadow decal in the world samples this — the smooth
// alpha falloff is what makes the creases read soft instead of painted on.
Texture2D makeAOStripTex() {
    const int W = 8, H = 64;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float v = y / (float)(H - 1);
        float s = 1.0f - v;
        float a = 200.0f * s * s * (0.4f + 0.6f * s);   // steep near the crease, long soft tail
        p[y * W + x] = { 255, 255, 255, cl8(a) };
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);   // v past 1 stays fully clear
    return t;
}

// The pack, seen side-on: a low, long-backed quadruped with too much leg and a
// head that hangs. Drawn wide rather than tall — it reads as an animal from the
// silhouette alone, which is all you get before it reaches you.
// The pack, four frames of a run. A quadruped moves its legs in diagonal pairs
// — front-left with back-right — so that is how they are offset; moving all four
// together would read as a rocking horse.
Texture2D makeDogTex() {
    const int FW = 192, H = 128, W = FW * DOG_FRAMES;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    int fx = 0;
    auto put = [&](int x, int y, Color c) { x += fx; if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; };
    auto vspan = [&](int x, int y0, int y1, Color c) { for (int y = y0; y <= y1; y++) put(x, y, c); };
    Color hide  = { 44, 28, 26, 255 };
    Color hide2 = { 62, 38, 32, 255 };
    for (int f = 0; f < DOG_FRAMES; f++) {
    fx = f * FW;
    float phd = (float)f / DOG_FRAMES;
    float ax, al2, bx, bl2;
    legPose(phd, ax, al2);            // front-left and back-right move together...
    legPose(phd + 0.5f, bx, bl2);     // ...against front-right and back-left
    const float legSw[4]   = { ax * 0.030f, bx * 0.030f, bx * 0.030f, ax * 0.030f };
    const float legLift[4] = { al2 * 7.0f,  bl2 * 7.0f,  bl2 * 7.0f,  al2 * 7.0f  };
    float gather = cosf(phd * TAU) * 1.6f;        // the back bunches as it gathers
    for (int x = 22; x < 170; x++) {
        float u = (x - 22) / 148.0f;
        float mange = vnoise2(x * 0.22f, 3.1f, 921u);
        Color c = (mange > 0.56f) ? hide2 : hide;
        // body: deepest over the shoulders, tucked at the waist, rump lifted
        float top = 46.0f + gather + 5.0f * sinf(u * 3.14159f) - 4.0f * expf(-powf((u - 0.22f) * 5.0f, 2.0f));
        float bot = 78.0f - 4.0f * expf(-powf((u - 0.55f) * 6.0f, 2.0f));
        if (u > 0.06f && u < 0.94f) vspan(x, (int)top, (int)bot, c);
        // neck and hanging head, forward of the shoulders
        if (u < 0.20f) {
            float t = u / 0.20f;
            vspan(x, (int)(52 + 16 * t), (int)(70 + 14 * t), c);
        }
        // four legs, thin and a little too long
        for (int L = 0; L < 4; L++) {
            float lu = 0.16f + L * 0.22f + legSw[L];
            if (fabsf(u - lu) < 0.022f) vspan(x, (int)bot - 2, 116 - (int)legLift[L], (L & 1) ? hide : hide2);
        }
        // tail, low and straight
        if (u > 0.90f) vspan(x, (int)(58 + (u - 0.90f) * 120.0f), (int)(64 + (u - 0.90f) * 130.0f), hide);
    }
    // the muzzle, and the two pale eyes that find you before you find them
    for (int x = 12; x < 30; x++) vspan(x, 66, 78, hide);
    for (int e = 0; e < 2; e++)
        for (int dx = 0; dx < 4; dx++) for (int dy = 0; dy < 3; dy++)
            put(26 + dx + e * 7, 60 + dy, Color{ 226, 216, 176, 255 });
    }
    return finishTexture(img, false);
}

// The almond water can, unwrapped for a real cylinder: one atlas holding the
// label as a flat strip (top half, u wraps once around the can) plus the lid and
// the base as squares below it. Nothing here is shaded — unlike the billboard
// sheet this replaces, the geometry is real, so the world shader lights it.
Texture2D makeAlmondWrapTex() {
    const int W = 192, H = 192;
    const int SIDE_H = 128;                       // rows 0..127 wrap round the barrel
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    auto put = [&](int x, int y, Color c) { if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; };

    const Color alu   = { 214, 217, 224, 255 };
    const Color aluDk = { 165, 168, 176, 255 };
    const Color cream = { 248, 244, 232, 255 };
    const Color ink   = {  74,  50,  33, 255 };
    const Color inkSoft = { 104, 74, 50, 255 };
    const Color nut   = { 198, 156, 108, 255 };
    const Color nutHi = { 224, 192, 152, 255 };

    // ---- the label, as horizontal bands down the can
    // Brown on cream, not cream on brown. A dark band over half the barrel makes
    // the whole can read as a black blob at any distance — which is how the
    // first pass of this looked in the world.
    for (int y = 0; y < SIDE_H; y++) {
        Color c;
        if      (y < 13)  c = alu;                // shoulder
        else if (y < 26)  c = cream;
        else if (y < 30)  c = ink;                // pinstripe closing the field
        else if (y < 99)  c = cream;              // the label field
        else if (y < 103) c = ink;
        else if (y < 114) c = cream;
        else              c = alu;                // base roll
        for (int x = 0; x < W; x++) put(x, y, c);
    }
    // the artwork twice round, so something readable is facing you from most angles
    for (int rep = 0; rep < 2; rep++) {
        int cx = 48 + rep * 96;
        for (int y = -15; y <= 15; y++) {         // the almond
            float t = (y + 15) / 30.0f;
            float hw = 10.0f * sinf(powf(t, 0.72f) * 3.14159f * 0.94f);
            for (int x = -(int)hw; x <= (int)hw; x++) {
                float e = fabsf(x / (hw + 0.001f));
                put(cx + x, 52 + y, (e > 0.80f || t < 0.06f) ? ink : (x < -1 ? nutHi : nut));
            }
        }
        for (int y = -10; y <= 12; y++) put(cx, 52 + y, inkSoft);   // seam
    }
    const char *l1 = "ALMOND", *l2 = "WATER";
    for (int rep = 0; rep < 2; rep++) {
        int cx = 48 + rep * 96;
        ImageDrawText(&img, l1, cx - MeasureText(l1, 11) / 2, 72, 11, ink);
        ImageDrawText(&img, l2, cx - MeasureText(l2, 11) / 2, 85, 11, ink);
    }

    // ---- lid (x 0..63) and base (x 64..127), both on rows 128..191
    for (int q = 0; q < 2; q++) {
        int ox = q * 64;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                float dx = (x - 31.5f) / 30.0f, dy = (y - 31.5f) / 30.0f;
                float r = sqrtf(dx * dx + dy * dy);
                if (r > 1.0f) continue;                       // outside the disc
                Color c = alu;
                if (r > 0.93f) c = aluDk;                     // the chime round the rim
                else if (r > 0.86f) c = Color{ 205, 208, 214, 255 };
                else if (q == 0) {                            // lid: countersink + tab + mouth
                    if (r > 0.70f && r < 0.76f) c = aluDk;
                    if (dy < -0.30f && fabsf(dx) < 0.20f && r < 0.66f)
                        c = Color{ 96, 99, 105, 255 };        // the mouth, a teardrop up top
                    if (fabsf(dy - 0.06f) < 0.09f && fabsf(dx) < 0.42f)
                        c = Color{ 158, 161, 168, 255 };      // tab lying across the lid
                    if (fabsf(dy - 0.06f) < 0.04f && fabsf(dx) < 0.30f) c = aluDk;
                } else {                                      // base: a recessed dome
                    if (r < 0.72f) c = Color{ 160, 163, 170, 255 };
                    if (r < 0.62f) c = Color{ 178, 181, 188, 255 };
                }
                put(ox + x, 128 + y, c);
            }
    }

    // ---- wear, so it doesn't read as showroom stock
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            Color &c = p[y * W + x];
            if (c.a == 0) continue;
            float g = vnoise2(x * 0.05f, y * 0.05f, 4471u);
            float stain = clampf((g - 0.58f) * 2.1f, 0.0f, 1.0f) * 0.17f;
            float grime = (vnoise2(x * 0.5f, y * 0.5f, 9137u) - 0.5f) * 0.05f;
            float k = 1.0f - stain + grime;
            c.r = cl8(c.r * k); c.g = cl8(c.g * k * 0.998f); c.b = cl8(c.b * k * 0.984f);
        }
    return finishTexture(img, false);
}

// The tape player, unwrapped. Four 64px tiles in a 128px atlas, because the
// thing is a box and a box only needs four different faces:
//   (0,0) top     — the cassette bay, seen from above, and the label above it
//   (1,0) body    — moulded plastic for the sides, back and underside
//   (0,1) front   — speaker grille and the transport buttons
//   (1,1) reel    — one hub with tape wound on it, for the two spinning discs
Texture2D makeDeckTex() {
    const int W = 128, H = 128, T = 64;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    auto put = [&](int x, int y, Color c) { if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; };

    const Color shell   = { 108, 110, 114, 255 };   // grey moulded plastic
    const Color shellDk = {  78,  80,  85, 255 };
    const Color shellHi = { 138, 140, 145, 255 };
    const Color bay     = {  26,  25,  29, 255 };   // inside the cassette door
    const Color trim    = {  52,  52,  58, 255 };
    const Color tape    = {  58,  42,  34, 255 };   // wound oxide
    const Color tapeHi  = {  82,  60,  48, 255 };
    const Color hub     = { 176, 172, 168, 255 };   // light plastic: the slots have to read against it
    const Color label   = { 206, 198, 178, 255 };

    Rng r(0xDEC4ULL);
    // ---- (0,0) and (1,0) both start as plastic, with a moulding grain and wear
    for (int ty = 0; ty < 2; ty++) for (int tx = 0; tx < 2; tx++) {
        if (ty == 1 && tx == 1) continue;             // the reel tile is drawn from scratch
        int ox = tx * T, oy = ty * T;
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
            float g = vnoise2(x * 0.55f + ox, y * 0.55f + oy, 7u);
            float e = fminf(fminf((float)x, (float)y), fminf(T - 1.0f - x, T - 1.0f - y));
            float wear = clampf(1.0f - e / 5.0f, 0, 1) * 0.35f;   // the edges have been rubbed shiny
            Color c = shell;
            c.r = cl8(c.r * (0.90f + g * 0.16f) + wear * 40);
            c.g = cl8(c.g * (0.90f + g * 0.16f) + wear * 40);
            c.b = cl8(c.b * (0.90f + g * 0.16f) + wear * 42);
            put(ox + x, oy + y, c);
        }
    }
    // ---- (0,0) top: the bay window, and a strip of label above it
    for (int y = 19; y < 45; y++) for (int x = 12; x < 52; x++) {
        bool edge = (y < 21 || y > 42 || x < 14 || x > 49);
        put(x, y, edge ? trim : bay);
    }
    for (int y = 4; y < 16; y++) for (int x = 6; x < 58; x++) {
        float g = vnoise2(x * 0.9f, y * 0.9f, 11u);
        put(x, y, { cl8(label.r * (0.86f + g * 0.2f)), cl8(label.g * (0.86f + g * 0.2f)),
                    cl8(label.b * (0.86f + g * 0.2f)), 255 });
    }
    ImageDrawText(&img, "FIELD REC", 9, 5, 10, { 62, 58, 54, 255 });

    // ---- (0,1) front: speaker grille on the left, transport buttons on the right
    {
        const int oy = T;
        for (int gy = 0; gy < 9; gy++) for (int gx = 0; gx < 9; gx++) {
            int cx = 8 + gx * 3, cy = oy + 18 + gy * 3;
            put(cx, cy, shellDk); put(cx + 1, cy, { 60, 62, 66, 255 });
        }
        for (int b = 0; b < 3; b++) {                 // play, stop, and the one that never worked
            int bx = 38, by = oy + 14 + b * 13;
            for (int y = 0; y < 9; y++) for (int x = 0; x < 18; x++) {
                bool lip = (y == 0 || x == 0);
                put(bx + x, by + y, lip ? shellHi : (y > 6 ? shellDk : shell));
            }
        }
        for (int x = 0; x < T; x++) { put(x, oy + 2, shellDk); put(x, oy + 3, shellHi); }  // seam
    }

    // ---- (1,1) reel: tape wound on a hub, with the spoke slots cut in it
    {
        const int ox = T, oy = T;
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
            float dx = (x - 31.5f) / 30.0f, dy = (y - 31.5f) / 30.0f;
            float rad = sqrtf(dx * dx + dy * dy), ang = atan2f(dy, dx);
            Color c;
            if (rad > 1.0f) c = bay;                             // outside the flange: the dark bay
            else if (rad > 0.42f) {                              // wound tape, in fine rings
                float ring = sinf(rad * 130.0f) * 0.5f + 0.5f;
                c = { cl8(tape.r + ring * (tapeHi.r - tape.r)), cl8(tape.g + ring * (tapeHi.g - tape.g)),
                      cl8(tape.b + ring * (tapeHi.b - tape.b)), 255 };
            } else {
                // the hub: three slots cut through it, which is the only thing
                // that says whether the reel is turning
                float sl = fmodf(ang + TAU, 2.0943951f);
                c = (rad > 0.10f && rad < 0.34f && sl < 0.72f) ? Color{ 20, 19, 22, 255 } : hub;
            }
            put(ox + x, oy + y, c);
        }
    }

    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

Texture2D makeSurfaceDetail(Texture2D albedo, bool ceramic, float strength) {
    Image source = LoadImageFromTexture(albedo);
    Color *pixels = LoadImageColors(source);
    const int w = source.width, h = source.height;
    Image detail = GenImageColor(w, h, BLANK);
    Color *out = (Color *)detail.data;
    auto height = [&](int x, int y) {
        x = (x + w) % w; y = (y + h) % h;
        if (ceramic) {
            // A pillowed glaze above recessed grout. Derive geometry from the
            // joint, never from printed colour or a baked highlight.
            int ex = std::min(x % 64, 63 - x % 64);
            int ey = std::min(y % 64, 63 - y % 64);
            float t = clampf((std::min(ex, ey) - 2.0f) / 7.0f, 0, 1);
            return t * t * (3 - 2 * t);
        }
        Color c = pixels[y * w + x];
        return (c.r * 0.30f + c.g * 0.59f + c.b * 0.11f) / 255.0f;
    };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float sx = (height(x + 1, y) - height(x - 1, y)) * strength;
        float sy = (height(x, y + 1) - height(x, y - 1)) * strength;
        float mask = ceramic ? 0.08f + 0.92f * height(x, y)
                             : 0.65f + 0.35f * height(x, y);
        out[y * w + x] = {cl8(128 + 127 * clampf(sx, -1, 1)),
                          cl8(128 + 127 * clampf(sy, -1, 1)), cl8(255 * mask), 255};
    }
    UnloadImageColors(pixels);
    UnloadImage(source);
    return finishTexture(detail, true);
}

Texture2D makeParticleTex() {
    Image img = GenImageColor(32,32,BLANK);
    Color *p = (Color *)img.data;
    for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
        float dx=(x-15.5f)/15.5f, dy=(y-15.5f)/15.5f;
        float a=clampf(1-dx*dx-dy*dy,0,1);
        p[y*32+x]={255,255,255,cl8(255*a*a)};
    }
    return finishTexture(img,false);
}

Texture2D makePropDetail(Texture2D albedo) {
    Texture2D detail=makeSurfaceDetail(albedo,false,0.55f);
    Image img=LoadImageFromTexture(detail);
    Color *p=(Color *)img.data;
    for (int y=0;y<img.height;++y) for(int x=0;x<img.width;++x) {
        // Alpha marks an absolute object gloss, independent of the level's floor.
        p[y*img.width+x].b=x<256 ? 12 : x<512 ? 108 : y<256 ? 48 : 8;
        p[y*img.width+x].a=128;
    }
    UnloadTexture(detail);
    return finishTexture(img,true);
}
