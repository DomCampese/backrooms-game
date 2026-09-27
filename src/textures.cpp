#include "textures.h"
#include "util.h"
#include "object_materials.generated.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------- textures
static inline float sstepT(float a, float b, float v) {
    float t = clampf((v - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
Texture2D finishTexture(Image img, bool tiled) {
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

// PIRATE CLARK, six frames of a walk.
//
// The pose is the one this generator always drew; what moves between frames is
// where the limbs are. Each limb swings about its own pivot and its offset is
// scaled by how far down the limb the scanline is, so a leg pivots at the hip
// and travels furthest at the boot rather than sliding sideways as a block.
//
// The two legs are not mirror images. He has a peg on the right, and a peg does
// not stride — it is planted and swung stiffly from the hip. Giving the real leg
// a longer throw than the peg is what turns a walk into his walk.
Texture2D makeClarkTex() {
    const int FW = 128, FH = 256, W = FW * ENT_FRAMES, H = FH * ENT_ROWS;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    int fx = 0, fy = 0;   // origin of the cell being drawn; every write goes through it
    auto put = [&](int x, int y, Color c) { x += fx; y += fy; if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; };
    auto hspan = [&](int y, float cx, float halfw, Color c) {
        for (int x = (int)(cx - halfw); x <= (int)(cx + halfw); x++) put(x, y, c);
    };
    Color body = { 13, 11, 10, 255 };
    Color hat  = { 18, 15, 13, 255 };
    Color wood = { 62, 48, 33, 255 };
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
    // How far sideways the body is at a given height: the lean, pivoting at the
    // boots, plus the head turn over the top 80 px. Everything stamped on him
    // after the scanline loop — the eyepatch, the eye, the skull, the bandolier
    // — has to go through this too. Those stamps only paint pixels that are
    // already opaque, so a stamp at the unsheared position silently lands on
    // empty background and vanishes: on the first lean row his eye went out.
    auto bodyOff = [&](float y) {
        float o = shear * clampf((252.0f - y) / 250.0f, 0, 1);
        if (y < 80) o += headShift * clampf((80.0f - y) / 60.0f, 0, 1);
        return o;
    };
    float ph = (float)f / ENT_FRAMES;
    float lx0, ll0, rx0, rl0;
    legPose(ph, lx0, ll0);           // the real leg
    legPose(ph + 0.5f, rx0, rl0);    // and the peg, half a cycle behind it
    // A peg is not a leg: it is planted and swung stiffly from the hip, with
    // barely any throw and almost no lift. That asymmetry is the limp.
    float legReal = lx0 * 7.0f,  liftReal = ll0 * 7.0f;
    float legPeg  = rx0 * 4.0f,  liftPeg  = rl0 * 2.5f;
    float armL = -rx0 * 5.5f, armR = -lx0 * 4.5f;   // arms answer the opposite leg
    float hemSway = lx0 * 2.4f;
    for (int y = 2; y < 252; y++) {
        float wob = (vnoise2(0.05f * y, 3.7f, 77u) - 0.5f) * 7.0f;
        float rag = (vnoise2(0.35f * y, 9.1f, 88u) - 0.5f) * 2.5f;
        float cx = 64 + wob * 0.35f;
        cx += bodyOff((float)y);
        if (y >= 2 && y < 16) hspan(y, cx, 10 + (y - 2) * 0.35f + rag * 0.5f, hat);   // hat crown
        if (y >= 10 && y < 16) {                                                     // upturned brim corners
            for (int s = -1; s <= 1; s += 2)
                for (int x = (int)(cx + s * 20); x != (int)(cx + s * 28); x += s) put(x, y, hat);
        }
        if (y >= 16 && y < 22) hspan(y, cx, 27 + rag * 0.5f, hat);                   // brim
        if (y >= 22 && y <= 52) {                                                    // head
            float dy = (y - 36) / 17.0f;
            if (dy * dy < 1.0f) hspan(y, cx, 15.0f * sqrtf(1 - dy * dy) + rag, body);
        }
        if (y >= 44 && y <= 74) {                                                    // long ragged beard
            float br = vnoise2(0.4f * y, 17.3f, 91u);
            if (br > 0.30f) hspan(y, cx, 13.0f * (1.0f - (y - 44) / 34.0f) + rag, body);
        }
        if (y > 56 && y <= 66) hspan(y, cx, 7 + rag, body);                          // neck
        if (y > 62 && y <= 165) {                                                    // long coat, flared hem
            float t = (y - 62) / 103.0f;
            float halfw = (t < 0.10f) ? 12 + t * 110 : (y < 150 ? 23 - 5 * t : 22 + (y - 150) * 0.35f);
            float hem = (y > 158) ? (vnoise2(0.6f * y, 5.5f, 71u) - 0.5f) * 4 : 0;
            // heavy wool does not keep up with the legs inside it
            float sway = (y > 138) ? hemSway * (y - 138) / 27.0f : 0.0f;
            hspan(y, cx + sway, halfw + rag + hem, body);
        }
        if (y > 68 && y <= 190) {                                                    // arms
            float t = (y - 68) / 122.0f;
            float off = 24 + 7 * t;
            hspan(y, cx - off + armL * t, 3.6f + rag * 0.5f, body);
            if (y <= 184) hspan(y, cx + off + armR * t, 3.6f + rag * 0.5f, body);
        }
        if (y > 165 && y < 252) {                                                    // legs: boot + peg
            float t = (y - 165) / 87.0f;
            float lx = cx - 10 + wob * 0.2f + legReal * t;
            float rx = cx + 10 + wob * 0.2f + legPeg * t;
            if (y < 252 - liftReal) {
                hspan(y, lx, 5.8f - 1.2f * t + rag * 0.5f, body);                    // left: real leg
                if (y > 244 - liftReal) hspan(y, lx, 8, body);                        // boot
            }
            if (y < 252 - liftPeg) {
                if (y <= 185) hspan(y, rx, 5.8f + rag * 0.5f, body);                 // right: stump...
                else hspan(y, rx, 2.4f, wood);                                       // ...then peg leg
            }
        }
    }
    // eyepatch strap across the face, turning with it
    for (int x = 46; x <= 82; x++) {
        int y = 30 + (x - 46) / 9 + fy;
        int xx = fx + x + (int)bodyOff(30.0f + (x - 46) / 9.0f);
        if (xx >= 0 && xx < W && p[y * W + xx].a) { p[y * W + xx] = { 58, 52, 46, 255 }; p[(y + 1) * W + xx] = { 48, 43, 38, 255 }; }
    }
    // Single glowing eye; the left is under the patch. It is only lit on the row
    // where he is facing you — eyeshine you can see means something is looking
    // back, so it must not be there while his head is turned away.
    if (look > 0.55f) {
        float ex = 64 + 7.5f + bodyOff(36.0f), ey = 36;
        for (int dy = -7; dy <= 7; dy++) for (int dx = -7; dx <= 7; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            int x = (int)(ex + dx) + fx, y = (int)(ey + dy) + fy;
            if (x < 0 || x >= W || y < 0 || y >= H || p[y * W + x].a == 0) continue;
            if (d < 3.0f) p[y * W + x] = { 244, 238, 214, 255 };
            else if (d < 7.0f) {
                float t = expf(-(d - 3.0f) * 1.0f) * 0.6f;
                Color &c = p[y * W + x];
                c.r = cl8(c.r + 205 * t); c.g = cl8(c.g + 195 * t); c.b = cl8(c.b + 160 * t);
            }
        }
    }
    // hook where the right hand should be
    {
        float hx = 64 + 31 + armR, hy = 194;   // it is on the end of the arm that just swung
        for (int dy = -6; dy <= 8; dy++) for (int dx = -7; dx <= 7; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            if (fabsf(d - 5.0f) < 1.4f && dy > -3) put((int)(hx + dx), (int)(hy + dy), { 150, 150, 158, 255 });
        }
        for (int y = 186; y < 191; y++) hspan(y, hx, 2, { 120, 120, 126, 255 });     // hook base
        for (int q = -1; q <= 1; q++) put((int)(hx + 4), (int)(hy + q), { 224, 226, 234, 255 }); // glint
    }
    // the movie-poster details: skull on the hat, bandolier, brass buttons
    auto putIf = [&](int x, int y, Color c) {
        x += fx + (int)bodyOff((float)y); y += fy;
        if (x >= 0 && x < W && y >= 0 && y < H && p[y * W + x].a) p[y * W + x] = c;
    };
    {   // bone-white skull emblem, crossbones behind
        for (int s = -1; s <= 1; s += 2)
            for (int t = 2; t <= 7; t++) { putIf(64 + s * t, 7 + t, { 188, 180, 156, 255 }); putIf(64 + s * t, 8 + t, { 172, 164, 140, 255 }); }
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++)
            if (dx * dx + dy * dy * 1.6f < 10.5f) putIf(64 + dx, 8 + dy, { 208, 199, 172, 255 });
        putIf(62, 7, { 25, 20, 16, 255 }); putIf(63, 7, { 25, 20, 16, 255 });   // sockets
        putIf(65, 7, { 25, 20, 16, 255 }); putIf(66, 7, { 25, 20, 16, 255 });
        for (int x = 62; x <= 66; x++) putIf(x, 11, (x & 1) ? Color{ 30, 24, 18, 255 } : Color{ 196, 188, 162, 255 }); // teeth
    }
    {   // bandolier slung shoulder to hip, brass studs
        for (int y = 68; y <= 128; y++) {
            int xc = 54 + (y - 68) * 22 / 60;
            for (int dx = -2; dx <= 2; dx++)
                putIf(xc + dx, y, dx == 0 && (y % 9) < 2 ? Color{ 172, 136, 66, 255 } : Color{ 54, 43, 34, 255 });
        }
        for (int y = 82; y <= 152; y += 14) { putIf(59, y, { 158, 124, 58, 255 }); putIf(60, y, { 182, 148, 74, 255 }); } // buttons
    }
    }
    return finishTexture(img, false);
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

// Printed lettering: the same glyph splines as the scrawl, drawn with an even
// pen and no wobble, so they come out as a clean round-stroke sans. raylib's
// own font is an 8 px bitmap; scaled onto a can label or a carton it is a
// staircase, and nothing printed in a factory looks like that. `h` is the cap
// height in pixels, `weight` the stroke half-width as a fraction of it.
static float printWidth(const char *text, float h) {
    float w = 0;
    for (const char *c = text; *c; c++) w += (*c == ' ' ? 0.55f : *c == 'I' || *c == '1' ? 0.40f : 0.78f) * h;
    return w;
}
static void printText(Color *p, int W, int H, const char *text, float x, float y, float h, Color ink,
                      float weight = 0.075f, float track = 0.0f) {
    // Rendered into a transparent scratch over the text's own box, then laid
    // over the destination by coverage: inkDab composites onto transparency
    // and leaves an opaque atlas untouched.
    float rad = h * weight;
    int bx0 = std::max(0, (int)(x - rad) - 2), by0 = std::max(0, (int)(y - rad) - 2);
    int bx1 = std::min(W - 1, (int)(x + printWidth(text, h) + track * strlen(text) + rad) + 2);
    int by1 = std::min(H - 1, (int)(y + h + rad) + 2);
    if (bx1 <= bx0 || by1 <= by0) return;
    int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
    std::vector<Color> tmp((size_t)bw * bh, BLANK);
    Color solid = { ink.r, ink.g, ink.b, 255 };
    for (const char *c = text; *c; c++) {
        char ch = *c >= 'a' && *c <= 'z' ? (char)(*c - 32) : *c;
        float adv = (ch == ' ' ? 0.55f : ch == 'I' || ch == '1' ? 0.40f : 0.78f) * h + track;
        const Glyph *g = nullptr;
        for (const Glyph &cand : GLYPHS) if (cand.ch == ch) { g = &cand; break; }
        if (g) {
            float ox = x + (ch == 'I' || ch == '1' ? -0.12f * h : 0.0f);
            for (int si = 0; si < 3 && g->stroke[si]; si++) {
                const char *sp = g->stroke[si];
                int n = std::min(16, (int)(strlen(sp) / 2));
                float cxs[16], cys[16];
                for (int k = 0; k < n; k++) {
                    cxs[k] = ox + (sp[k * 2] - '0') / 9.0f * h * 0.62f - bx0;
                    cys[k] = y + (sp[k * 2 + 1] - '0') / 9.0f * h - by0;
                }
                int steps = 8 + n * 14;
                for (int st = 0; st <= steps; st++) {
                    float px, py;
                    splineAt(cxs, cys, n, (float)st / steps, px, py);
                    inkDab(tmp.data(), bw, bh, px, py, rad, solid, 1.0f, 0, 0, bw - 1, bh - 1);
                }
            }
        }
        x += adv;
    }
    float op = ink.a / 255.0f;
    for (int yy = 0; yy < bh; yy++) for (int xx = 0; xx < bw; xx++) {
        float a = tmp[(size_t)yy * bw + xx].a / 255.0f * op;
        if (a <= 0) continue;
        Color &d = p[(by0 + yy) * W + bx0 + xx];
        d.r = cl8(d.r + (ink.r - d.r) * a); d.g = cl8(d.g + (ink.g - d.g) * a); d.b = cl8(d.b + (ink.b - d.b) * a);
    }
}

// wall scrawl atlas: 32 phrases in 32 different hands, 4 x 8 cells of 256x128
Texture2D makeScrawlTex() {
    const int W = 1024, H = 1024, CW = W / 4, CH = H / 8;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    // Thirty-two of them, so that seeing the same line twice in one run means
    // something rather than meaning the pool is small. They are all somebody
    // trying to leave a fact behind: a count, a warning, a rule they worked out.
    // Several are Level 0's own lore passed hand to hand — the carpet fluid
    // that "is not water", the tearing paper of a noclip wall, the red that
    // means the Red Rooms, the one room where the hum goes quiet.
    static const char *LINES[32] = {
        "NO CLIP",              "dont stare",           "its not water",        "the exit lies",
        "he hears the flares",  "keep walking",         "it hums at night",     "wrong door =)",
        "i counted 12 doors",   "none of them out",     "turn left. always",    "dont sleep here",
        "the lights know",      "day 1 again",          "it wears a coat",      "smells like almond",
        "i was here. was i",    "same room twice",      "find the manila room", "hold still :(",
        "water is a floor",     "dont say your name",   "fire moves it",        "watch the paper tear",
        "421 and counting",     "my watch stopped",     "listen for dogs",      "the party never ends",
        "i can hear the hum",   "red = turn back",      "follow the pipes",     "help",
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
// ---- the vending machine's door, at its real size: 0.88 x 1.77 m in 250 x
// 504 px, about 3.5 mm a pixel. It is a glass-front drink merchandiser of the
// kind every warehouse break room has: a backlit header, the glass over six
// shelves of spirals, the coin and selection column down the right, and the
// push flap you reach into at the bottom. What it sells is the one thing the
// game has to sell, so the spirals hold almond water, in the same brown on
// cream as the can in your hand. A few slots are empty, and one can has hung
// up on its spiral, which is what a vending machine is for.
//
// Everything here is painted flat and lit by the room; the mesher cuts the
// door into pieces along the VEND_* rects and gives the header, the display,
// the price strips and the back of the cabinet (where the cans are painted)
// vertex alpha 240, which the world shader lights from behind as well.
static void drawVendingFront(Color *p, int W, int H) {
    const unsigned char OP = 254;
    const int X0 = VEND_PX[0], Y0 = VEND_PX[1], PW = VEND_PX[2], PH = VEND_PX[3];
    auto pxOf = [&](float x) { return X0 + (x + VEND_HW) / (2 * VEND_HW) * PW; };
    auto pyOf = [&](float y) { return Y0 + (VEND_Y1 - y) / (VEND_Y1 - VEND_Y0) * PH; };
    const float PPM = PW / (2 * VEND_HW);                       // pixels per metre
    auto inDoor = [&](int x, int y) { return x >= X0 && x < X0 + PW && y >= Y0 && y < Y0 + PH; };
    auto put = [&](int x, int y, Color c) { if (inDoor(x, y)) p[y * W + x] = { c.r, c.g, c.b, OP }; };
    auto mix = [&](int x, int y, Color c, float a) {
        if (!inDoor(x, y) || a <= 0) return;
        Color &d = p[y * W + x];
        a = std::min(a, 1.0f);
        d = { cl8(d.r + (c.r - d.r) * a), cl8(d.g + (c.g - d.g) * a), cl8(d.b + (c.b - d.b) * a), OP };
    };
    auto scale = [](Color c, float k) { return Color{ cl8(c.r * k), cl8(c.g * k), cl8(c.b * k), c.a }; };
    // a rect in door metres, shaded per pixel: f(x, y, u, v) with u, v 0..1 across it (v down)
    auto fill = [&](float x0, float y0, float x1, float y1, auto &&f) {
        float fx0 = pxOf(x0), fx1 = pxOf(x1), fy0 = pyOf(y1), fy1 = pyOf(y0);
        for (int y = (int)floorf(fy0); y < (int)ceilf(fy1); y++)
            for (int x = (int)floorf(fx0); x < (int)ceilf(fx1); x++)
                put(x, y, f(x, y, (x + 0.5f - fx0) / (fx1 - fx0), (y + 0.5f - fy0) / (fy1 - fy0)));
    };
    auto text = [&](const char *s, float cx, float y, float h, Color ink, float weight = 0.12f) {
        // centred on cx (metres), cap top at y (metres)
        printText(p, W, H, s, pxOf(cx) - printWidth(s, h) * 0.5f, pyOf(y), h, ink, weight);
    };
    auto frameRect = [&](float x0, float y0, float x1, float y1, float t, Color c) {   // t in metres, outside the rect
        fill(x0 - t, y1, x1 + t, y1 + t, [&](int, int, float, float) { return c; });
        fill(x0 - t, y0 - t, x1 + t, y0, [&](int, int, float, float) { return c; });
        fill(x0 - t, y0, x0, y1, [&](int, int, float, float) { return c; });
        fill(x1, y0, x1 + t, y1, [&](int, int, float, float) { return c; });
    };

    // -- the door: black powder coat, orange-peel texture, the folded edge
    // catching the light, and dirt that thickens toward the floor
    const Color coat = { 46, 48, 53, OP };
    fill(-VEND_HW, VEND_Y0, VEND_HW, VEND_Y1, [&](int x, int y, float u, float v) {
        float k = 0.94f + 0.08f * fbm2(x * 0.35f, y * 0.35f, 0x7E1u, 2);
        float y_m = VEND_Y1 - v * (VEND_Y1 - VEND_Y0);
        k *= 0.84f + 0.16f * sstepT(0.06f, 0.45f, y_m);
        int ex = std::min(x - X0, X0 + PW - 1 - x), ey = std::min(y - Y0, Y0 + PH - 1 - y);
        if (std::min(ex, ey) < 2) k *= 1.40f;
        (void)u;
        return scale(coat, k);
    });
    // rubber gaskets round the two openings
    const Color gasket = { 16, 16, 18, OP };
    frameRect(VEND_WIN.x0, VEND_WIN.y0, VEND_WIN.x1, VEND_WIN.y1, 0.010f, gasket);
    frameRect(VEND_BIN.x0, VEND_BIN.y0, VEND_BIN.x1, VEND_BIN.y1, 0.008f, gasket);

    // -- the header: a sheet of backlit acrylic in a bright bezel
    {
        const float x0 = VEND_HEADER.x0 + 0.018f, x1 = VEND_HEADER.x1 - 0.018f;
        const float y0 = VEND_HEADER.y0 + 0.016f, y1 = VEND_HEADER.y1 - 0.014f;
        frameRect(x0, y0, x1, y1, 0.005f, Color{ 150, 152, 156, OP });
        const Color brown = { 94, 60, 38, OP };
        fill(x0, y0, x1, y1, [&](int, int, float u, float v) {
            float k = 0.90f + 0.10f * sinf(u * 3.14159f) + 0.04f * (1.0f - v);   // brightest over the tube
            if (v < 0.10f || v > 0.90f) return brown;
            if (v < 0.14f || v > 0.86f) return Color{ 196, 156, 86, OP };       // gold hairline, as on the can
            return scale(Color{ 244, 236, 212, OP }, k);
        });
        text("ALMOND WATER", 0.02f, y1 - 0.024f, 15, brown, 0.11f);
        for (int s = -1; s <= 1; s += 2) {                    // an almond each side of the name
            float cx = pxOf(s * 0.345f), cy = pyOf((y0 + y1) * 0.5f);
            for (int y = -8; y <= 8; y++) for (int x = -6; x <= 6; x++) {
                float t = (y + 8) / 16.0f, hw = 5.2f * sinf(powf(t, 0.72f) * 3.0f);
                float e = fabsf(x) / (hw + 0.01f);
                if (e > 1.0f) continue;
                mix((int)cx + x, (int)cy + y, e > 0.75f ? brown : scale(Color{ 200, 150, 98, OP }, 1.05f - 0.25f * t), 1);
            }
        }
    }

    // -- the back of the cabinet, seen through the glass: white, lit by a tube
    // down the left and one across the top, six trays of spirals
    {
        const VendRect &w = VEND_WIN;
        fill(w.x0, w.y0, w.x1, w.y1, [&](int x, int y, float u, float v) {
            // dark grey, as they are, so the cans stand out against it
            float k = 0.62f + 0.40f * expf(-u * 5.0f) + 0.22f * expf(-v * 6.0f);
            k *= 0.97f + 0.05f * lat(x, y >> 1, 0x7E2u);
            return scale(Color{ 92, 96, 100, OP }, k);
        });
        const float pitch = (w.x1 - w.x0) / VEND_COLS;
        for (int r = 0; r < VEND_ROWS; r++) {
            float yr = VEND_ROW0 + r * VEND_ROWP;
            // the tray and the shadow the shelf above throws down the back
            fill(w.x0, yr - 0.035f, w.x1, yr + 0.004f, [&](int x, int y, float, float v) {
                return scale(Color{ 78, 80, 84, OP }, 0.8f + 0.3f * v + 0.04f * lat(x, y, 0x7E3u));
            });
            float top = std::min(yr + VEND_ROWP - 0.035f, w.y1);
            fill(w.x0, top - 0.045f, w.x1, top, [&](int x, int y, float, float v) {
                Color &d = p[y * W + x];
                return scale(d, 0.62f + 0.38f * v);
            });
            for (int c = 1; c < VEND_COLS; c++) {                // the dividers between spirals
                float dx = w.x0 + c * pitch;
                fill(dx - 0.0025f, yr, dx + 0.0025f, yr + 0.13f, [&](int x, int y, float, float) {
                    return scale(p[y * W + x], 0.72f);
                });
            }
            for (int c = 0; c < VEND_COLS; c++) {
                uint32_t e = ih(r, c, 0x7E4u);
                float cx = w.x0 + (c + 0.5f) * pitch;
                bool stuck = (r == 3 && c == 1);                  // hung up on the spiral, forever
                bool empty = !stuck && e % 6 == 0;
                if (!empty) {
                    // A can front-on, drawn in its own frame so the stuck one can lean
                    float ang = stuck ? 0.30f : 0.0f;
                    float cw = 0.066f * PPM, ch = 0.122f * PPM;
                    float bx = pxOf(cx), by = pyOf(yr + 0.006f + (stuck ? 0.03f : 0.0f));   // bottom centre
                    float ca = cosf(ang), sa = sinf(ang);
                    for (int y = (int)(by - ch - 6); y <= (int)by + 6; y++)
                        for (int x = (int)(bx - ch); x <= (int)(bx + ch); x++) {
                            float dx = x + 0.5f - bx, dy = y + 0.5f - by;
                            float lx = dx * ca + dy * sa, ly = -dx * sa + dy * ca;   // ly: up is negative
                            float s = lx / (cw * 0.5f), t = -ly / ch;                 // s -1..1 across, t 0..1 up
                            if (fabsf(s) > 1.0f || t < 0.0f || t > 1.0f) continue;
                            // round the rims a little
                            if ((t < 0.03f || t > 0.97f) && fabsf(s) > 0.86f) continue;
                            Color c;
                            if (t < 0.07f || t > 0.90f) c = { 196, 199, 206, OP };           // drawn aluminium
                            else if ((t > 0.17f && t < 0.20f) || (t > 0.80f && t < 0.83f)) c = { 74, 50, 33, OP };
                            else {
                                c = { 244, 238, 222, OP };
                                // the almond, and the name under it as two brown dashes
                                float ax = s * 0.5f, ay = (t - 0.56f) * 1.5f;
                                if (ax * ax * 5.0f + ay * ay * 3.2f < 0.06f) c = { 190, 142, 94, OP };
                                if (fabsf(s) < 0.62f && ((t > 0.27f && t < 0.31f) || (t > 0.34f && t < 0.37f)))
                                    c = { 104, 74, 50, OP };
                            }
                            // a cylinder: dark at the sides, a highlight a third across
                            float sh = 0.55f + 0.45f * sqrtf(std::max(0.0f, 1.0f - s * s));
                            sh += 0.35f * expf(-(s + 0.35f) * (s + 0.35f) * 30.0f);
                            put(x, y, scale(c, sh));
                        }
                }
                // The front turn of the spiral, round whatever it holds (or
                // held). An empty one shows the turns behind it too, smaller
                // and dimmer as they go back into the cabinet.
                for (int turn = empty ? 3 : 0; turn >= 0; turn--) {
                    float sc = 1.0f - 0.13f * turn, dim = 1.0f - 0.2f * turn;
                    float rr = 0.047f * PPM * sc, ccx = pxOf(cx), ccy = pyOf(yr + 0.049f * sc + 0.004f * turn);
                    for (int y = (int)(ccy - rr - 2); y <= (int)(ccy + rr + 2); y++)
                        for (int x = (int)(ccx - rr - 2); x <= (int)(ccx + rr + 2); x++) {
                            float dx = x + 0.5f - ccx, dy = y + 0.5f - ccy;
                            float d = fabsf(sqrtf(dx * dx + dy * dy) - rr);
                            if (d > 1.3f) continue;
                            // wire: bright where it faces up into the light, dark underneath
                            float lit = (0.62f + 0.5f * (-dy / rr)) * dim;
                            mix(x, y, scale(Color{ 188, 190, 196, OP }, std::max(0.30f, lit)), 1.3f - d);
                        }
                }
            }
        }
    }

    // -- the push flap: smoked plastic on a hinge, scuffed where hands go
    {
        const VendRect &b = VEND_BIN;
        fill(b.x0, b.y0, b.x1, b.y1, [&](int x, int y, float, float v) {
            float k = 0.9f + 0.25f * (1.0f - v) + 0.05f * fbm2(x * 0.2f, y * 0.2f, 0x7E5u, 2);
            if (v < 0.06f) k = 0.55f;                           // the hinge
            return scale(Color{ 42, 44, 50, OP }, k);
        });
        text("PUSH", (b.x0 + b.x1) * 0.5f, b.y1 - 0.105f, 13, Color{ 84, 86, 94, OP }, 0.10f);
    }

    // -- the control column: brushed steel, and everything you touch on it
    {
        const float cx0 = 0.195f, cx1 = 0.425f, cmid = (cx0 + cx1) * 0.5f;
        fill(cx0, 0.29f, cx1, 1.665f, [&](int x, int y, float u, float v) {
            float brush = fbm2(x * 0.08f, y * 1.3f, 0x7E6u, 2);
            float k = 0.86f + 0.16f * brush;
            // finger smudges round the keypad and the coin slot
            float sm = fbm2(x * 0.09f, y * 0.09f, 0x7E7u, 3);
            if (v > 0.10f && v < 0.36f) k *= 1.0f - 0.10f * sstepT(0.5f, 0.75f, sm);
            if (u < 0.03f || v < 0.004f) k *= 1.22f;           // top and left edges catch the light
            if (u > 0.97f || v > 0.996f) k *= 0.62f;
            return scale(Color{ 166, 168, 170, OP }, k);
        });
        text("SELECTION", cmid, 1.648f, 6, Color{ 40, 40, 44, OP });
        // the display (VEND_DISP): LEDs behind smoked red
        {
            const VendRect &d = VEND_DISP;
            fill(d.x0, d.y0, d.x1, d.y1, [&](int, int, float u, float v) {
                if (u < 0.05f || u > 0.95f || v < 0.12f || v > 0.88f) return Color{ 18, 18, 20, OP };
                return Color{ 30, 9, 8, OP };
            });
            text("INSERT 3", (d.x0 + d.x1) * 0.5f, d.y1 - 0.024f, 8, Color{ 255, 72, 40, OP }, 0.11f);
        }
        // keypad: letters for the shelf, digits for the spiral
        const char *keys = "ABCDEF123456";
        for (int k = 0; k < 12; k++) {
            float kx = 0.245f + (k % 3) * 0.065f, ky = 1.465f - (k / 3) * 0.07f;
            const float hs = 0.025f;
            fill(kx - hs - 0.003f, ky - hs - 0.003f, kx + hs + 0.003f, ky + hs + 0.003f,
                 [&](int, int, float, float) { return Color{ 60, 62, 66, OP }; });
            fill(kx - hs, ky - hs, kx + hs, ky + hs, [&](int, int, float u, float v) {
                float s = 0.92f + 0.10f * (1.0f - v);
                if (u < 0.08f || v < 0.08f) s = 1.18f;
                if (u > 0.92f || v > 0.92f) s = 0.62f;
                return scale(Color{ 200, 202, 206, OP }, s);
            });
            char lab[2] = { keys[k], 0 };
            text(lab, kx, ky + 0.013f, 7, Color{ 36, 36, 40, OP });
        }
        // the coin mech: a chromed escutcheon with its slot, and the return lever's recess
        fill(0.325f, 1.065f, 0.405f, 1.175f, [&](int, int, float u, float v) {
            float s = 1.0f + 0.25f * (1.0f - v) - 0.2f * u;
            if (u < 0.06f || u > 0.94f || v < 0.05f || v > 0.95f) s *= 0.7f;
            return scale(Color{ 196, 198, 202, OP }, s);
        });
        fill(0.362f, 1.095f, 0.368f, 1.150f, [&](int, int, float, float) { return Color{ 8, 8, 10, OP }; });
        fill(0.225f, 1.080f, 0.295f, 1.135f, [&](int, int, float u, float v) {
            float d = (u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f);
            return d < 0.2f ? Color{ 22, 22, 24, OP } : Color{ 140, 142, 146, OP };
        });
        text("COINS", 0.365f, 1.058f, 4.5f, Color{ 40, 40, 44, OP });
        // the note acceptor, taped over by someone who knew what it takes
        fill(0.215f, 0.86f, 0.405f, 1.01f, [&](int, int, float u, float v) {
            float s = 1.0f + 0.35f * (v < 0.06f ? 1.0f : 0.0f);
            if (fabsf(v - 0.42f) < 0.035f && u > 0.10f && u < 0.90f) return Color{ 6, 6, 8, OP };
            if (fabsf(v - 0.55f) < 0.03f && u > 0.18f && u < 0.82f && ((int)(u * 30) & 1))
                return Color{ 70, 220, 96, OP };                // the little green arrows
            return scale(Color{ 26, 26, 28, OP }, s);
        });
        fill(0.200f, 0.900f, 0.418f, 0.972f, [&](int x, int y, float u, float v) {
            // masking tape, torn at both ends
            float tear = 0.02f + 0.03f * lat(0, y, 0x7E8u);
            if (u < tear || u > 1.0f - tear) return p[y * W + x];
            float k = 0.94f + 0.06f * lat(x, y, 0x7E9u) - 0.05f * v;
            return scale(Color{ 222, 205, 158, OP }, k);
        });
        text("DOUBLOONS", cmid, 0.962f, 6.5f, Color{ 34, 30, 44, OP }, 0.10f);
        text("ONLY", cmid, 0.932f, 6.5f, Color{ 34, 30, 44, OP }, 0.10f);
        // the instructions, and the lock
        fill(0.212f, 0.64f, 0.408f, 0.82f, [&](int x, int y, float u, float v) {
            float k = 0.95f + 0.05f * lat(x, y, 0x7EAu) - 0.08f * v * u;
            return scale(Color{ 230, 228, 218, OP }, k);
        });
        text("1 INSERT COINS", cmid, 0.805f, 4.2f, Color{ 40, 40, 44, OP });
        text("2 MAKE SELECTION", cmid, 0.775f, 4.2f, Color{ 40, 40, 44, OP });
        text("3 TAKE PRODUCT", cmid, 0.745f, 4.2f, Color{ 40, 40, 44, OP });
        text("EXACT CHANGE", cmid, 0.705f, 4.2f, Color{ 190, 30, 26, OP });
        text("ONLY", cmid, 0.680f, 4.2f, Color{ 190, 30, 26, OP });
        {
            float lx = pxOf(0.395f), ly = pyOf(0.555f), rr = 0.019f * PPM;
            for (int y = (int)(ly - rr - 1); y <= (int)(ly + rr + 1); y++)
                for (int x = (int)(lx - rr - 1); x <= (int)(lx + rr + 1); x++) {
                    float dx = x + 0.5f - lx, dy = y + 0.5f - ly, d = sqrtf(dx * dx + dy * dy);
                    if (d > rr) continue;
                    Color c = scale(Color{ 200, 202, 206, OP }, 1.1f - 0.4f * (dy + dx) / (2 * rr) - 0.2f * (d / rr));
                    if (fabsf(dx) < 0.8f && fabsf(dy) < rr * 0.55f) c = { 20, 20, 22, OP };   // the keyway
                    put(x, y, c);
                }
        }
        // coin return cup: a chromed lip round a dark hole
        text("COIN RETURN", 0.31f, 0.485f, 4.2f, Color{ 40, 40, 44, OP });
        fill(0.235f, 0.33f, 0.385f, 0.46f, [&](int, int, float u, float v) {
            if (u < 0.05f || u > 0.95f || v < 0.06f || v > 0.94f) return Color{ 190, 192, 196, OP };
            return scale(Color{ 20, 20, 22, OP }, 0.6f + 0.8f * v);
        });
    }

    // -- the sticker every one of them has, on the kick rail under the flap
    {
        const float x0 = -0.34f, x1 = -0.06f, y0 = 0.085f, y1 = 0.195f;
        fill(x0, y0, x1, y1, [&](int x, int y, float u, float v) {
            if (u < 0.025f || u > 0.975f || v < 0.06f || v > 0.94f) return Color{ 24, 22, 20, OP };
            float k = 0.92f + 0.06f * lat(x, y, 0x7EBu) - 0.10f * v;
            return scale(Color{ 232, 192, 44, OP }, k);
        });
        const Color ink = { 22, 20, 18, OP };
        text("WARNING", (x0 + x1) * 0.5f, 0.182f, 8, ink, 0.12f);
        text("DO NOT ROCK OR TILT", (x0 + x1) * 0.5f, 0.139f, 4.4f, ink);
        text("MACHINE MAY FALL", (x0 + x1) * 0.5f, 0.115f, 4.4f, ink);
    }

    // -- kick marks along the bottom, and grime over the lot
    Rng kr(0x7ECULL);
    for (int i = 0; i < 26; i++) {
        float sx = pxOf(-0.42f + kr.f01() * 0.60f), sy = pyOf(0.07f + kr.f01() * 0.13f);
        float len = 3 + kr.f01() * 10, ang = (kr.f01() - 0.5f) * 0.6f;
        for (int t = 0; t < (int)len; t++)
            mix((int)(sx + cosf(ang) * t), (int)(sy + sinf(ang) * t), Color{ 120, 118, 112, OP }, 0.5f);
    }
    for (int y = Y0; y < Y0 + PH; y++) for (int x = X0; x < X0 + PW; x++) {
        Color &c = p[y * W + x];
        float g = 1.0f - 0.10f * fbm2(x * 0.05f, y * 0.05f, 0x7EDu, 3);
        c = { cl8(c.r * g), cl8(c.g * g), cl8(c.b * g), OP };
    }

    // -- the price strips clipped into each shelf's front lip, top shelf A
    for (int r = 0; r < VEND_ROWS; r++) {
        const int sx = VEND_STRIP_PX[0], sy = VEND_STRIP_PX[1] + r * 12, sw = VEND_STRIP_PX[2], sh = VEND_STRIP_PX[3];
        for (int y = sy; y < sy + sh; y++) for (int x = sx; x < sx + sw; x++)
            p[y * W + x] = { 44, 46, 50, OP };
        for (int y = sy; y < sy + sh; y++) { p[y * W + sx] = { 30, 30, 32, OP }; p[y * W + sx + sw - 1] = { 30, 30, 32, OP }; }
        for (int x = sx; x < sx + sw; x++) p[sy * W + x] = { 150, 152, 156, OP };   // the lip's bright edge
        for (int c = 0; c < VEND_COLS; c++) {
            int tcx = sx + (int)((c + 0.5f) * sw / VEND_COLS);
            for (int y = sy + 2; y < sy + sh - 1; y++) for (int x = tcx - 14; x < tcx + 14; x++)
                p[y * W + x] = { 234, 232, 224, OP };
            char lab[3] = { (char)('A' + (VEND_ROWS - 1 - r)), (char)('1' + c), 0 };
            printText(p, W, H, lab, tcx - 12, sy + 3, 5, Color{ 30, 30, 34, OP }, 0.13f);
            printText(p, W, H, "3", tcx + 7, sy + 3, 5, Color{ 196, 28, 24, OP }, 0.13f);
        }
    }
}

static const int FIXPX[FIX_COUNT][4] = {   // x, y, w, h in the atlas (the fittings keep its left half)
    {   8,   8,  96, 152 },   // FIX_OUTLET        75 x 118 mm
    { 120,   8,  96, 152 },   // FIX_OUTLET_BROKEN
    { 232,   8,  92, 148 },   // FIX_SWITCH        72 x 115 mm
    {   8, 168, 224, 160 },   // FIX_GRILLE        560 x 400 mm
    { 296, 176, 192, 192 },   // FIX_DIFFUSER      600 x 600 mm
    { 280, 400, 224,  80 },   // FIX_SIGN          560 x 200 mm
    {   8, 336, 128, 128 },   // FIX_MANILA        one 500 mm tile of the Manila Room's paper
    { 336,   8, 104, 148 },   // FIX_NOTE          A5, 148 x 210 mm
};
const FixtureRect FIXTURES[FIX_COUNT] = {
    { 8/1024.f,     8/512.f, 104/1024.f, 160/512.f, 0.0375f, 0.059f  },
    { 120/1024.f,   8/512.f, 216/1024.f, 160/512.f, 0.0375f, 0.059f  },
    { 232/1024.f,   8/512.f, 324/1024.f, 156/512.f, 0.036f,  0.0575f },
    { 8/1024.f,   168/512.f, 232/1024.f, 328/512.f, 0.280f,  0.200f  },
    { 296/1024.f, 176/512.f, 488/1024.f, 368/512.f, 0.300f,  0.300f  },
    { 280/1024.f, 400/512.f, 504/1024.f, 480/512.f, 0.280f,  0.100f  },
    // inset half a texel: these are tiled edge to edge, and bilinear filtering
    // would otherwise pull the neighbouring cell into every seam
    { 8.5f/1024.f, 336.5f/512.f, 135.5f/1024.f, 463.5f/512.f, 0.250f, 0.250f },
    { 336/1024.f,   8/512.f, 440/1024.f, 156/512.f, 0.074f,  0.105f  },
};

Texture2D makeFixturesTex() {
    const int W = FIX_ATLAS_W, H = FIX_ATLAS_H;
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
            // A duplex receptacle at its real proportions (the plate is 1.28 px
            // a millimetre): each face a disc with flats top and bottom, two
            // blade slots — the neutral the longer — over a D-shaped ground.
            // The old one drew 17 mm slots and a ground that was a third slot
            // lying on its side, which is no socket anywhere.
            auto cover = [&](float d) { return clampf(0.5f - d, 0.0f, 1.0f); };
            auto blend = [&](int x, int y, Color c, float a) {
                if (a <= 0) return;
                Color &d = p[y * W + x];
                d.r = cl8(d.r + (c.r - d.r) * a); d.g = cl8(d.g + (c.g - d.g) * a); d.b = cl8(d.b + (c.b - d.b) * a);
            };
            const Color face = { 232, 226, 206, OP }, rim = { 176, 168, 148, OP };
            for (int k = 0; k < 2; k++) {
                float cx = x0 + r[2] * 0.5f, cy = y0 + 40.0f + k * 72.0f;
                for (int y = (int)cy - 26; y <= (int)cy + 26; y++) for (int x = (int)cx - 26; x <= (int)cx + 26; x++) {
                    float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                    // the face: a 44 mm disc cut flat top and bottom at 34 mm
                    float disc = std::max(sqrtf(dx * dx + dy * dy) - 23.0f, fabsf(dy) - 18.0f);
                    blend(x, y, rim, cover(disc - 1.5f));
                    blend(x, y, face, cover(disc + 1.0f));
                    float hot = std::max(fabsf(dx - 7.5f) - 1.3f, fabsf(dy + 5.0f) - 4.3f);    // the live blade
                    float neu = std::max(fabsf(dx + 7.5f) - 1.3f, fabsf(dy + 5.0f) - 5.4f);    // the neutral, longer
                    float gy = dy - 9.0f;                                                      // the ground: a D
                    float gnd = gy < 0 ? sqrtf(dx * dx + gy * gy) - 3.4f : std::max(fabsf(dx) - 3.4f, gy - 3.0f);
                    blend(x, y, slot, cover(std::min(std::min(hot, neu), gnd)));
                }
            }
            // the centre screw holding the plate on, slot turned at random
            float scx = x0 + r[2] * 0.5f, scy = y0 + 76.0f;
            for (int y = (int)scy - 6; y <= (int)scy + 6; y++) for (int x = (int)scx - 6; x <= (int)scx + 6; x++) {
                float dx = x + 0.5f - scx, dy = y + 0.5f - scy;
                blend(x, y, { 150, 146, 132, OP }, cover(sqrtf(dx * dx + dy * dy) - 4.6f));
                blend(x, y, { 92, 88, 78, OP }, cover(fabsf(dx * 0.34f + dy * 0.94f) - 0.7f) * cover(sqrtf(dx * dx + dy * dy) - 4.0f));
            }
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
        for (int k = 0; k < 2; k++) {                                        // screws, slots at random
            float scx = (x0 + x1) * 0.5f + 0.5f, scy = k ? y1 - 15.5f : y0 + 16.5f;
            float sa = k ? 0.3f : 1.2f;
            for (int y = (int)scy - 6; y <= (int)scy + 6; y++) for (int x = (int)scx - 6; x <= (int)scx + 6; x++) {
                float dx = x + 0.5f - scx, dy = y + 0.5f - scy, d = sqrtf(dx * dx + dy * dy);
                float a = clampf(4.9f - d, 0.0f, 1.0f);
                float sl = clampf(1.2f - fabsf(dx * sinf(sa) - dy * cosf(sa)), 0.0f, 1.0f) * clampf(4.2f - d, 0.0f, 1.0f);
                Color c = { cl8(150 - 58 * sl), cl8(146 - 58 * sl), cl8(132 - 54 * sl), OP };
                Color &q = p[y * W + x];
                q.r = cl8(q.r + (c.r - q.r) * a); q.g = cl8(q.g + (c.g - q.g) * a); q.b = cl8(q.b + (c.b - q.b) * a);
            }
        }
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

    // --- the Manila Room's paper: the beige of a manila folder, "reminiscent
    // of manila paper" as the lore puts it, with a small printed diamond
    // lattice and a fibre grain. Every term repeats inside 128 px (the lattice
    // is 32 px, the grain is a per-pixel hash) so tiles butt without a seam.
    {
        const int *r = FIXPX[FIX_MANILA];
        for (int y = 0; y < r[3]; y++) for (int x = 0; x < r[2]; x++) {
            int u = x % 32, v = y % 32;
            float dmd = fabsf(u - 15.5f) + fabsf(v - 15.5f);
            float lattice = (dmd > 13.5f && dmd < 15.5f) ? 0.93f : 1.0f;
            float dot = (dmd < 2.5f) ? 0.94f : 1.0f;
            float fibre = 0.97f + 0.06f * lat(x, y >> 2, 0x3A7u) + 0.02f * lat(x >> 1, y, 0x3A8u);
            float k = lattice * dot * fibre;
            p[(r[1] + y) * W + r[0] + x] = { cl8(226 * k), cl8(206 * k), cl8(158 * k), OP };
        }
    }
    // --- a note from the table: lined paper gone soft and yellow, ruled in
    // faint blue, with a few lines of cramped pencil. Unreadable at the size it
    // is seen; the words are in the game's overlay when you pick them up.
    {
        const int *r = FIXPX[FIX_NOTE];
        int x0 = r[0], y0 = r[1], x1 = r[0] + r[2] - 1, y1 = r[1] + r[3] - 1;
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            float k = 0.94f + 0.06f * fbm2(x * 0.09f, y * 0.09f, 0x4D2u, 2);
            p[y * W + x] = { cl8(236 * k), cl8(228 * k), cl8(200 * k), OP };
        }
        for (int ly = y0 + 18; ly < y1 - 6; ly += 9) box(x0 + 4, ly, x1 - 4, ly, { 168, 184, 206, OP });
        box(x0 + 14, y0 + 4, x0 + 14, y1 - 4, { 214, 150, 150, OP });   // the margin
        Rng nr(0x4D07EULL);
        for (int ly = y0 + 16; ly < y1 - 12; ly += 9) {
            int x = x0 + 18, end = x1 - 6 - nr.ri(0, 30);
            while (x < end) {                                           // pencil "words"
                int wl = 4 + nr.ri(0, 12);
                for (int q = 0; q < wl && x + q < end; q++)
                    box(x + q, ly - 1 - nr.ri(0, 2), x + q, ly, { 70, 66, 64, OP });
                x += wl + 3;
            }
        }
    }
    // --- plain galvanised metal, covering the pixel at UV (0.375, 0.75).
    //
    // That is not an arbitrary corner: `addSolidBox` hardcodes exactly that UV
    // for every face it emits, and the props atlas keeps its plain metal there
    // for the same reason. Two atlases agreeing on where "plain metal" lives is
    // what lets the conduit and sprinkler bodies — geometry, not decals — go
    // through the same helper as everything else. Move it and they sample a
    // transparent cell and vanish without a word. The atlas doubled in width
    // for the vending machine, which moved that UV from pixel (192, 384) to
    // (384, 384): the strip between the diffuser and the exit sign.
    {
        const int ox = 300, oy = 371, w = 180, h = 26;   // contains (384, 384), 12 px clear each way
        for (int y = oy; y < oy + h; y++) for (int x = ox; x < ox + w; x++) {
            float v = 0.88f + 0.24f * fbm2(x * 0.11f, y * 0.11f, 0x5Eu, 3);
            p[y * W + x] = { cl8(150 * v), cl8(150 * v), cl8(146 * v), OP };
        }
    }
    drawVendingFront(p, W, H);
    return finishTexture(img, false);
}

// A carton, in two regions of the props atlas' left strip (the UVs are in
// addProp, world_mesh.cpp): its side, y 2-297, and its top, y 312-507.
//
// The old cardboard was one noise field with a band of tape round the middle,
// mapped whole onto every face, the top included — every box was taped round
// its waist, which is not how anyone has ever packed one. A regular slotted
// carton is taped along the seam where its top flaps meet, and the tape laps
// over the edge and a hand's width down each end. Its sides carry the flutes
// of the corrugation showing through the liner, the printed handling marks, the
// box-maker's certificate stamp, and floor dirt along the bottom. The top has
// the flap seam, the tape, and the shipping label with its barcode. These
// cartons are the only cover on Clark's level, so they are looked at hard.
static void drawCarton(Color *p, int W) {
    const Color ink = { 42, 36, 30, 215 };
    auto kraft = [&](int x, int y, float flutePitch, bool vertical) {
        float n = (fbm2(x * 0.05f, y * 0.05f, 61u, 3) - 0.47f) * 0.16f;
        float fib = (lat(x, y, 611u) - 0.5f) * 0.05f + (fbm2(x * 0.6f, y * 0.12f, 614u, 1) - 0.5f) * 0.05f;
        float u = vertical ? (float)x : (float)y;
        float flute = 0.022f * sinf(u * TAU / flutePitch);   // the corrugation showing through
        float v = 1.0f + n + fib + flute;
        return Color{ cl8(166 * v), cl8(128 * v), cl8(84 * v), 255 };
    };
    auto tape = [&](int x, int y, float edge) {
        float wr = fbm2(x * 0.08f, y * 0.35f, 615u, 2);           // wrinkles, lengthwise
        float v = 0.93f + 0.14f * wr;
        v *= 1.0f - 0.18f * sstepT(2.0f, 0.0f, edge);             // its edges lift and darken
        return Color{ cl8(200 * v), cl8(162 * v), cl8(104 * v), 255 };
    };
    // ---- the side, 236 x 296 from (10, 2); the top of the face is at the top
    const int SX = 10, SY = 2, SW = 236, SH = 296;
    for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) {
        Color c = kraft(SX + x, SY + y, 2.7f, true);
        float v = 1.0f;
        float ex = (float)std::min(x, SW - 1 - x);
        if (ex < 7) v *= 0.80f + 0.20f * ex / 7.0f;                // scored and scuffed corners
        float low = (float)(SH - 1 - y);
        if (low < 40) v *= 0.84f + 0.16f * (low / 40.0f);         // floor dirt along the bottom
        if (low < 40 && fbm2(x * 0.15f, y * 0.4f, 616u, 2) > 0.62f) v *= 0.82f;
        c.r = cl8(c.r * v); c.g = cl8(c.g * v); c.b = cl8(c.b * v);
        // the seam tape lapping over the top edge and under the bottom one,
        // finished with the dispenser's zigzag
        float cx = fabsf(x - SW * 0.5f);
        float zig = 3.0f * fabsf(fmodf(x * 0.5f, 2.0f) - 1.0f);
        if (cx < 16 && (y < 30 - zig || low < 26 - zig)) c = tape(SX + x, SY + y, 16 - cx);
        p[(SY + y) * W + SX + x] = c;
    }
    // "this way up": two arrows over a bar, top left, the way ISO 780 draws them
    for (int k = 0; k < 2; k++) {
        float ax = SX + 26 + k * 20, top = SY + 46;
        for (int y = 0; y < 34; y++) for (int x = -9; x <= 9; x++) {
            float fy = (float)y, fx = (float)x;
            bool head = fy < 12 && fabsf(fx) < fy * 0.75f + 0.5f;
            bool shaft = fy >= 11 && fabsf(fx) < 2.6f;
            if (!head && !shaft) continue;
            Color &d = p[(int)(top + y) * W + (int)(ax + x)];
            float a = ink.a / 255.0f;
            d.r = cl8(d.r + (ink.r - d.r) * a); d.g = cl8(d.g + (ink.g - d.g) * a); d.b = cl8(d.b + (ink.b - d.b) * a);
        }
    }
    for (int y = 0; y < 4; y++) for (int x = -10; x < 50; x++) {
        Color &d = p[(SY + 84 + y) * W + SX + 26 + x];
        float a = ink.a / 255.0f;
        d.r = cl8(d.r + (ink.r - d.r) * a); d.g = cl8(d.g + (ink.g - d.g) * a); d.b = cl8(d.b + (ink.b - d.b) * a);
    }
    // what was in it, flexo-printed a little unevenly: the building's supplies
    printText(p, W, 512, "ALMOND WATER", SX + 26, SY + 128, 17, ink, 0.09f, 0.5f);
    printText(p, W, 512, "24 X 330ML", SX + 56, SY + 156, 11, ink, 0.09f, 0.5f);
    printText(p, W, 512, "THIS SIDE UP", SX + 82, SY + 60, 9, ink, 0.09f, 0.2f);
    // the box maker's certificate: a ring of small print, bottom right
    {
        float cx = SX + 196, cy = SY + 222;
        for (int y = -22; y <= 22; y++) for (int x = -22; x <= 22; x++) {
            float d = sqrtf((float)(x * x + y * y));
            float cov = sstepT(1.2f, 0.0f, fabsf(d - 19.0f)) + sstepT(0.9f, 0.0f, fabsf(d - 15.0f))
                      + (fabsf((float)y) < 1.0f && d < 14.0f ? 0.8f : 0.0f);
            // the ring's small print, suggested: broken dashes between the rings
            if (d > 15.8f && d < 18.2f && ((int)((atan2f((float)y, (float)x) + 3.2f) * 9.0f) & 1)) cov += 0.6f;
            cov = std::min(cov, 1.0f) * 0.75f;
            if (cov <= 0) continue;
            Color &q = p[(int)(cy + y) * W + (int)(cx + x)];
            q.r = cl8(q.r + (ink.r - q.r) * cov); q.g = cl8(q.g + (ink.g - q.g) * cov); q.b = cl8(q.b + (ink.b - q.b) * cov);
        }
    }
    // addPropBox maps u across each side face mirrored as seen from outside the
    // box, which nothing printed on cardboard ever showed until now: the
    // lettering came out backwards on every face. Flip the whole side region
    // (the tape and edges are symmetric) rather than the mesher's UVs, which
    // every other prop shares.
    for (int y = SY; y < SY + SH; y++) std::reverse(p + y * W + SX, p + y * W + SX + SW);
    // ---- the top, 236 x 196 from (10, 312): two flaps meeting across the middle
    const int TX = 10, TY = 312, TW = 236, TH = 196;
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) {
        Color c = kraft(TX + x, TY + y, 2.7f, false);
        float v = 1.0f;
        float e = (float)std::min(std::min(x, TW - 1 - x), std::min(y, TH - 1 - y));
        if (e < 6) v *= 0.84f + 0.16f * e / 6.0f;
        float seam = fabsf(y - TH * 0.5f);
        if (seam < 1.2f) v *= 0.45f;                              // the gap between the flaps
        c.r = cl8(c.r * v); c.g = cl8(c.g * v); c.b = cl8(c.b * v);
        if (seam < 17) c = tape(TX + x, TY + y, 17 - seam);
        p[(TY + y) * W + TX + x] = c;
    }
    // the shipping label: white, a barcode, an address nobody will ever read
    {
        const int LX = 150, LY = 332, LW = 86, LH = 60;             // crateBody() samples exactly this rect
        for (int y = 0; y < LH; y++) for (int x = 0; x < LW; x++) {
            float v = 0.95f + 0.05f * lat(LX + x, LY + y, 617u);
            if (x < 2 || y < 2 || x > LW - 3 || y > LH - 3) v *= 0.90f;
            p[(LY + y) * W + LX + x] = { cl8(236 * v), cl8(232 * v), cl8(220 * v), 255 };
        }
        const Color bar = { 30, 30, 32, 240 };
        int x = LX + 8;
        for (int k = 0; x < LX + LW - 10; k++) {
            int bw = 1 + (int)(ih(k, 0, 618u) % 3), gap = 1 + (int)(ih(k, 1, 618u) % 2);
            for (int y = LY + 36; y < LY + 52; y++) for (int q = 0; q < bw; q++) p[y * W + x + q] = { bar.r, bar.g, bar.b, 255 };
            x += bw + gap;
        }
        printText(p, W, 512, "SHIP TO:", LX + 6, LY + 6, 6, bar, 0.10f);
        printText(p, W, 512, "LEVEL 0", LX + 6, LY + 15, 6, bar, 0.10f);
        printText(p, W, 512, "RM 421", LX + 6, LY + 24, 6, bar, 0.10f);
        printText(p, W, 512, "1 OF 1", LX + 52, LY + 6, 5, bar, 0.10f);
    }
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
        } else if (x < 256) {                               // cardboard: drawn below
            r = 166; g = 128; b = 84;
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
    drawCarton(p, W);
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
// silhouette alone, which is all you get before it reaches you — four frames
// of a run.
//
// Built from an actual dog's skeleton rather than a box on four sticks, because
// the silhouette is the whole read and a box on sticks reads as a table: a deep
// chest and a tucked waist, the neck dropping from the withers to a head
// carried low, a muzzle, ears laid back, front legs with a wrist, and hind legs
// with the backward-angled hock that says "dog" before anything else does.
// Legs move in diagonal pairs (front-left with back-right), off the same
// legPose as every other gait in the game, the far pair a shade darker so the
// animal has two sides. The coat is ragged at the edge and thin enough over the
// flank that the ribs show.
Texture2D makeDogTex() {
    const int FW = 192, H = 128, W = FW * DOG_FRAMES;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    const float GROUND = 116.0f;
    struct Seg { float ax, ay, bx, by, ra, rb; int part; };   // part: 0 body, 1 near leg, 2 far leg
    auto segDist = [](const Seg &s, float x, float y) {
        float vx = s.bx - s.ax, vy = s.by - s.ay, wx = x - s.ax, wy = y - s.ay;
        float t = clampf((wx * vx + wy * vy) / (vx * vx + vy * vy + 1e-6f), 0.0f, 1.0f);
        float dx = wx - vx * t, dy = wy - vy * t;
        return sqrtf(dx * dx + dy * dy) - (s.ra + (s.rb - s.ra) * t);
    };
    for (int f = 0; f < DOG_FRAMES; f++) {
        float phd = (float)f / DOG_FRAMES;
        float ax, al, bx, bl;
        legPose(phd, ax, al);             // front-left and back-right move together...
        legPose(phd + 0.5f, bx, bl);      // ...against front-right and back-left
        float gather = cosf(phd * TAU) * 1.6f;          // the back bunches as it gathers
        std::vector<Seg> segs;
        // the body, head to tail: withers, chest, waist, loin, croup
        segs.push_back({ 50, 60 + gather, 76, 62 + gather, 15, 13, 0 });   // shoulders and chest
        segs.push_back({ 76, 62 + gather, 112, 60 + gather, 12, 8.5f, 0 });   // ribs to the tucked waist
        segs.push_back({ 112, 60 + gather, 146, 58 + gather * 0.6f, 8.5f, 12, 0 });   // loin to haunch
        segs.push_back({ 60, 68 + gather, 90, 70 + gather, 10, 6, 0 });   // the brisket, deep
        segs.push_back({ 48, 54 + gather, 32, 70, 8.5f, 7, 0 });          // the neck, dropping
        segs.push_back({ 32, 71, 25, 75, 8, 7, 0 });                      // the skull
        segs.push_back({ 25, 76, 10, 85, 5.2f, 3.6f, 0 });                // the muzzle, pointing down
        segs.push_back({ 34, 64, 43, 58, 3.0f, 1.0f, 0 });                // an ear, laid back
        segs.push_back({ 152, 56 + gather * 0.6f, 166, 70, 3.2f, 2.4f, 0 });   // tail, low
        segs.push_back({ 166, 70, 174, 88, 2.4f, 1.4f, 0 });
        // Legs. `sw` +1 is the paw reaching forward (toward the head, -x),
        // `lift` 0 planted to 1 at the top of the swing.
        auto frontLeg = [&](float sx, float sw, float lift, int part) {
            float px = sx - sw * 13.0f + lift * 4.0f, py = GROUND - lift * 11.0f;
            float ex = sx + 2.0f - sw * 5.0f, ey = 88.0f + gather * 0.5f - lift * 4.0f;   // elbow
            float wx = px + 1.5f + lift * 5.0f, wy = py - 9.0f + lift * 2.0f;             // wrist
            segs.push_back({ sx, 70 + gather, ex, ey, 6.0f, 3.6f, part });
            segs.push_back({ ex, ey, wx, wy, 3.2f, 2.6f, part });
            segs.push_back({ wx, wy, px - 3.0f, py - 1.5f, 2.6f, 2.8f, part });           // the paw
        };
        auto hindLeg = [&](float hx, float sw, float lift, int part) {
            float px = hx + 2.0f - sw * 14.0f, py = GROUND - lift * 9.0f;
            float kx = hx - 9.0f - sw * 6.0f, ky = 84.0f - lift * 4.0f;                  // stifle
            float jx = px + 7.0f + lift * 3.0f, jy = 99.0f - lift * 7.0f;                // hock, behind the paw
            segs.push_back({ hx, 62 + gather * 0.6f, kx, ky, 9.0f, 4.5f, part });        // the thigh
            segs.push_back({ kx, ky, jx, jy, 3.6f, 2.8f, part });
            segs.push_back({ jx, jy, px, py - 1.5f, 2.6f, 2.4f, part });
            segs.push_back({ px, py - 1.5f, px - 4.0f, py - 1.0f, 2.6f, 2.6f, part });
        };
        frontLeg(60, bx, bl, 2);   // far front-right, with back-left
        hindLeg(142, ax, al, 2);   // far back-left
        frontLeg(54, ax, al, 1);   // near front-left
        hindLeg(148, bx, bl, 1);   // near back-right
        for (int y = 0; y < H; y++) for (int x = 0; x < FW; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float best = 1e9f; int part = 0;
            for (const Seg &sg : segs) {
                float d = segDist(sg, px, py);
                // near legs over everything, the body over the far legs
                float bias = sg.part == 1 ? -0.8f : sg.part == 2 ? 0.8f : 0.0f;
                if (d + bias < best + (part == 1 ? -0.8f : part == 2 ? 0.8f : 0.0f)) { best = d; part = sg.part; }
                else if (d < best && sg.part == part) best = d;
            }
            // ragged coat: the edge wanders, most along the back and belly
            float rag = (vnoise2(x * 0.45f, y * 0.45f + f * 7.0f, 931u) - 0.5f) * (part == 0 ? 2.6f : 1.0f);
            float a = clampf(0.5f - (best + rag), 0.0f, 1.0f);
            if (a <= 0) continue;
            float v = 1.0f;
            float mange = vnoise2(x * 0.22f, y * 0.22f, 921u);
            if (mange > 0.58f) v *= 1.30f;                              // bald, paler patches
            if (part == 0 && x > 64 && x < 108 && y > 50 && y < 74) {   // ribs under a thin coat
                float rib = sinf((x + (y - 62) * 0.35f) * 0.62f);
                v *= 1.0f + 0.16f * sstepT(0.55f, 0.95f, rib);
            }
            v *= 1.0f + 0.22f * sstepT(62.0f, 46.0f, (float)y) * (part == 0 ? 1.0f : 0.0f);   // light along the spine
            if (part == 2) v *= 0.72f;                                  // the far legs, in the body's shadow
            p[y * W + f * FW + x] = { cl8(46 * v), cl8(29 * v), cl8(26 * v), cl8(255 * a) };
        }
        // the two pale eyes that find you before you find them
        for (int e = 0; e < 2; e++)
            for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 3; dx++)
                p[(72 + dy - e) * W + f * FW + 21 + dx + e * 6] = { 226, 216, 176, 255 };
    }
    return finishTexture(img, false);
}

// The almond water can, unwrapped for a real cylinder: the label as a strip
// round the barrel (top two thirds; u wraps once round), then the lid and the
// base as squares below it. Nothing is shaded in — the geometry is real and
// the world shader lights it — except what printing and pressing put there.
//
// 576 px square, three times what it was: the can is held up to the camera to
// drink, where the old 192 px label came out soft and its raylib bitmap
// wordmark came out a staircase. The layout is the old one scaled by three, so
// buildCanMesh's UVs (fractions of the atlas) did not have to move.
Texture2D makeAlmondWrapTex() {
    const int K = 3, W = 192 * K, H = 192 * K;
    const int SIDE_H = 128 * K;                   // rows 0..SIDE_H-1 wrap round the barrel
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    auto put = [&](int x, int y, Color c) { x = ((x % W) + W) % W; if (y >= 0 && y < H) p[y * W + x] = c; };
    auto blendAt = [&](int x, int y, Color c, float a) {
        x = ((x % W) + W) % W;
        if (y < 0 || y >= H || a <= 0) return;
        Color &d = p[y * W + x];
        d.r = cl8(d.r + (c.r - d.r) * a); d.g = cl8(d.g + (c.g - d.g) * a); d.b = cl8(d.b + (c.b - d.b) * a);
    };
    const Color cream = { 246, 241, 226, 255 };
    const Color ink   = {  74,  50,  33, 255 };
    const Color inkSoft = { 104, 74, 50, 255 };
    // Drawn aluminium: fine lines round the can from the ironing die, and the
    // soft bands a curved mirror picks up from any room, so the metal reads as
    // metal even before the light moves across it.
    auto alu = [&](int x, int y, float k) {
        float band = 0.5f + 0.5f * sinf(y * 0.11f + 0.8f) * sinf(y * 0.037f);
        float line = (fbm2(x * 0.004f, y * 0.9f, 4471u, 2) - 0.47f) * 0.10f;
        float v = k * (0.84f + 0.22f * band + line);
        return Color{ cl8(214 * v), cl8(217 * v), cl8(224 * v), 255 };
    };
    // ---- the label, as bands down the can. Brown on cream, not cream on brown:
    // a dark band over half the barrel reads as a black blob at any distance.
    for (int y = 0; y < SIDE_H; y++) for (int x = 0; x < W; x++) {
        int yy = y / K;
        Color c;
        if      (yy < 13)  c = alu(x, y, 1.0f);             // the shoulder, necked in
        else if (yy < 26)  c = cream;
        else if (yy < 30)  c = ink;                         // a rule closing the field
        else if (yy < 99)  c = cream;
        else if (yy < 103) c = ink;
        else if (yy < 114) c = cream;
        else               c = alu(x, y, 0.94f);             // the base roll
        put(x, y, c);
    }
    // gold hairlines inside the brown rules: printers love them
    for (int x = 0; x < W; x++) { put(x, 31 * K, { 190, 150, 82, 255 }); put(x, 98 * K, { 190, 150, 82, 255 }); }
    // The artwork twice round, so something readable faces you from most angles.
    for (int rep = 0; rep < 2; rep++) {
        int cx = (48 + rep * 96) * K, cy = 52 * K;
        // the almond: a pointed oval with its ridged skin, lit from the upper left
        for (int y = -46; y <= 46; y++) {
            float t = (y + 46) / 92.0f;
            float hw = 30.0f * sinf(powf(t, 0.72f) * 3.14159f * 0.94f);
            for (int x = -(int)hw - 2; x <= (int)hw + 2; x++) {
                float e = fabsf(x) / (hw + 0.001f);
                float edge = sstepT(1.0f, 0.86f, e) * sstepT(0.0f, 0.04f, t) * sstepT(1.0f, 0.97f, t);
                float shade = 0.80f + 0.30f * (1.0f - e * e) - 0.18f * (x / (hw + 1.0f)) - 0.10f * t;
                float ridge = 0.94f + 0.06f * sinf(x * 0.9f + sinf(y * 0.21f) * 2.0f);
                float v = shade * ridge;
                Color nut = { cl8(196 * v), cl8(150 * v), cl8(100 * v), 255 };
                if (e > 1.06f) continue;
                blendAt(cx + x, cy + y, ink, 1.0f);                    // outline first
                blendAt(cx + x, cy + y, nut, edge);
            }
        }
        for (int y = -30; y <= 36; y++) blendAt(cx + (int)(y * 0.05f), cy + y, inkSoft, 0.55f);   // the seam
        // a leaf behind it, a flat green, the way a label draws one
        for (int y = -18; y <= 18; y++) for (int x = 0; x <= 44; x++) {
            float u = x / 44.0f, lw = 14.0f * sinf(u * 3.14159f);
            float dy = y - (u - 0.5f) * -10.0f;
            if (fabsf(dy) > lw || u < 0.03f) continue;
            int px = cx + 22 + x, py = cy - 30 + y;
            Color &d = p[py * W + ((px % W) + W) % W];
            if (d.r > 200) blendAt(px, py, fabsf(dy) < 1.2f ? Color{ 70, 96, 52, 255 } : Color{ 98, 130, 70, 255 }, 0.95f);
        }
        const char *l1 = "ALMOND", *l2 = "WATER";
        printText(p, W, H, l1, cx - printWidth(l1, 27) * 0.5f, 208, 27, ink, 0.085f, 2.0f);
        printText(p, W, H, l2, cx - printWidth(l2, 27) * 0.5f, 244, 27, ink, 0.085f, 2.0f);
        const char *l3 = "330 ML";
        printText(p, W, H, l3, cx - printWidth(l3, 11) * 0.5f, 281, 11, inkSoft, 0.09f, 1.0f);
    }
    // The back panel between the two, where every can keeps its small print...
    {
        int cx = 96 * K;
        const char *lines[] = { "PURIFIED WATER", "WITH ALMOND", "DRINK CHILLED", "DO NOT BOIL" };
        for (int k = 0; k < 4; k++)
            printText(p, W, H, lines[k], cx - printWidth(lines[k], 9) * 0.5f, 118 + k * 16, 9, inkSoft, 0.10f, 0.5f);
    }
    // ...and at the seam, a barcode laddered round the barrel.
    for (int k = 0, y = 120; y < 200; k++) {
        int bh = 1 + (int)(ih(k, 7, 0x0CA7u) % 3), gap = 1 + (int)(ih(k, 8, 0x0CA7u) % 2);
        for (int q = 0; q < bh; q++) for (int x = -20; x <= 20; x++) put(x, y + q, { 30, 26, 24, 255 });
        y += bh + gap;
    }
    // ---- lid (x 0..191) and base (x 192..383), on rows 384..575
    for (int q = 0; q < 2; q++) {
        int ox = q * 64 * K, oy = SIDE_H;
        for (int y = 0; y < 64 * K; y++) for (int x = 0; x < 64 * K; x++) {
            float dx = (x - 95.5f) / 90.0f, dy = (y - 95.5f) / 90.0f;
            float r = sqrtf(dx * dx + dy * dy);
            if (r > 1.0f) continue;
            // turned rings from the press, round the centre
            float ring = 0.96f + 0.05f * sinf(r * 190.0f) * sinf(r * 23.0f);
            float v = ring;
            Color c;
            if (r > 0.93f) v *= 0.80f;                       // the chime round the rim
            else if (r > 0.86f) v *= 0.96f;
            if (q == 0) {                                    // lid: countersink, score, tab, rivet
                if (r > 0.70f && r < 0.76f) v *= 0.80f;
                float mx = dx, my = dy + 0.44f;              // the mouth: a teardrop, scored, up top
                float mouth = sqrtf(mx * mx * 2.2f + my * my * 3.5f);
                if (mouth < 0.50f && dy < -0.12f) v *= 0.86f;
                if (fabsf(mouth - 0.50f) < 0.018f && dy < -0.10f) v *= 0.62f;   // the score line
                float tab = sqrtf(dx * dx * 3.6f + (dy - 0.10f) * (dy - 0.10f) * 5.0f);
                if (tab < 0.58f) {                           // the ring-pull lying across it
                    v = 0.78f + 0.12f * (1 - tab / 0.58f);
                    float hole = sqrtf(dx * dx * 3.6f + (dy - 0.26f) * (dy - 0.26f) * 9.0f);
                    if (hole < 0.26f) v = 0.40f;              // its finger hole
                    if (fabsf(tab - 0.56f) < 0.03f) v *= 0.85f;
                }
                if (dx * dx + (dy + 0.02f) * (dy + 0.02f) < 0.006f) v = 0.66f;   // the rivet
            } else {                                         // base: a recessed dome
                if (r < 0.72f) v *= 0.76f;
                if (r < 0.62f) v *= 1.10f;
                if (fabsf(r - 0.30f) < 0.01f) v *= 0.85f;
            }
            c = { cl8(210 * v), cl8(213 * v), cl8(220 * v), 255 };
            put(ox + x, oy + y, c);
        }
    }
    // a date code inkjetted on the base, the one thing on it that is not metal
    printText(p, W, H, "2002 14:07", 64 * K + 58, SIDE_H + 118, 8, { 40, 40, 44, 220 }, 0.10f, 0.5f);
    // ---- wear, so it doesn't read as showroom stock
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            Color &c = p[y * W + x];
            if (c.a == 0) continue;
            float g = vnoise2(x * 0.05f / K, y * 0.05f / K, 4471u);
            float stain = clampf((g - 0.58f) * 2.1f, 0.0f, 1.0f) * 0.15f;
            float grime = (vnoise2(x * 0.5f / K, y * 0.5f / K, 9137u) - 0.5f) * 0.04f;
            float k = 1.0f - stain + grime;
            c.r = cl8(c.r * k); c.g = cl8(c.g * k * 0.998f); c.b = cl8(c.b * k * 0.984f);
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);     // held up close and seen far off on a floor
    return t;
}

// The tape player, unwrapped. Four tiles in one atlas, because the thing is a
// box and a box only needs four different faces:
//   (0,0) top     — the cassette bay's frame, and the label above it
//   (1,0) body    — moulded plastic for the sides, back and underside
//   (0,1) front   — speaker grille and the transport keys
//   (1,1) reel    — one hub with tape wound on it, for the two spinning discs
// 512 px, four times what it was: it is carried in the hand, a foot from the
// eye, where a 64 px face is a smear. The layout is the old one at 4x, so
// buildDeckMesh's UVs did not move.
Texture2D makeDeckTex() {
    const int K = 4, W = 128 * K, H = 128 * K, T = 64 * K;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    auto put = [&](int x, int y, Color c) { if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; };
    auto blendAt = [&](int x, int y, Color c, float a) {
        if (x < 0 || x >= W || y < 0 || y >= H || a <= 0) return;
        Color &d = p[y * W + x];
        a = std::min(a, 1.0f);
        d.r = cl8(d.r + (c.r - d.r) * a); d.g = cl8(d.g + (c.g - d.g) * a); d.b = cl8(d.b + (c.b - d.b) * a);
    };
    // a rounded rectangle's coverage at a pixel, antialiased over one pixel
    auto rrect = [](float x, float y, float x0, float y0, float x1, float y1, float rad) {
        float cx = std::max(x0 + rad - x, std::max(0.0f, x - (x1 - rad)));
        float cy = std::max(y0 + rad - y, std::max(0.0f, y - (y1 - rad)));
        float d = sqrtf(cx * cx + cy * cy) - rad;
        return clampf(0.5f - d, 0.0f, 1.0f);
    };
    const Color shell   = { 108, 110, 114, 255 };   // grey moulded plastic
    const Color shellDk = {  70,  72,  77, 255 };
    const Color shellHi = { 146, 148, 153, 255 };
    const Color bay     = {  24,  23,  27, 255 };   // inside the cassette door
    const Color trim    = {  48,  48,  54, 255 };
    const Color tape    = {  56,  40,  32, 255 };   // wound oxide
    const Color tapeHi  = {  86,  62,  50, 255 };
    const Color hub     = { 178, 174, 170, 255 };   // light plastic: the slots have to read against it
    const Color printC  = { 206, 208, 212, 255 };   // the silk-screened legends
    // ---- (0,0), (1,0), (0,1): plastic with the fine spark-erosion texture every
    // piece of consumer electronics was moulded with, rubbed shiny at the edges
    for (int ty = 0; ty < 2; ty++) for (int tx = 0; tx < 2; tx++) {
        if (ty == 1 && tx == 1) continue;             // the reel tile is drawn from scratch
        int ox = tx * T, oy = ty * T;
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
            float g = vnoise2((x + ox) * 0.45f, (y + oy) * 0.45f, 7u) * 0.6f + lat(x + ox, y + oy, 8u) * 0.4f;
            float e = fminf(fminf((float)x, (float)y), fminf(T - 1.0f - x, T - 1.0f - y));
            float wear = clampf(1.0f - e / 18.0f, 0, 1) * 0.30f;
            float scratch = fbm2((x + ox) * 0.02f, (y + oy) * 0.9f, 9u, 2) > 0.72f ? 10.0f : 0.0f;
            float k = 0.92f + g * 0.12f;
            put(ox + x, oy + y, { cl8(shell.r * k + wear * 40 + scratch), cl8(shell.g * k + wear * 40 + scratch),
                                  cl8(shell.b * k + wear * 42 + scratch), 255 });
        }
    }
    // ---- (0,0) top: the bay's frame, a paper label above it, four screws
    for (int y = 76; y < 180; y++) for (int x = 48; x < 208; x++) {
        float cov = rrect(x + 0.5f, y + 0.5f, 48, 76, 208, 180, 8);
        blendAt(x, y, trim, cov);
        float in = rrect(x + 0.5f, y + 0.5f, 56, 84, 200, 172, 5);
        blendAt(x, y, bay, in);
    }
    for (int y = 16; y < 64; y++) for (int x = 24; x < 232; x++) {
        float g = vnoise2(x * 0.22f, y * 0.22f, 11u);
        float k = 0.88f + g * 0.14f - (y > 58 ? 0.06f : 0.0f);
        put(x, y, { cl8(212 * k), cl8(204 * k), cl8(182 * k), 255 });
    }
    for (int x = 24; x < 232; x++) put(x, 30, { 170, 60, 50, 255 });     // the label's red rule
    printText(p, W, H, "FIELD REC", 34, 36, 17, { 58, 54, 50, 255 }, 0.09f, 1.0f);
    printText(p, W, H, "CR-40", 186, 42, 10, { 90, 84, 78, 255 }, 0.09f, 0.5f);
    for (int sx = 0; sx < 2; sx++) for (int sy = 0; sy < 2; sy++) {   // a Phillips head in each corner
        float cx = sx ? 240.5f : 15.5f, cy = sy ? 240.5f : 15.5f;
        for (int y = -6; y <= 6; y++) for (int x = -6; x <= 6; x++) {
            float d = sqrtf((float)(x * x + y * y));
            float cov = clampf(5.5f - d, 0.0f, 1.0f);
            blendAt((int)cx + x, (int)cy + y, (x + y < 0) ? Color{ 150, 152, 156, 255 } : Color{ 104, 106, 110, 255 }, cov);
            if (d < 4.2f && (abs(x) < 1 || abs(y) < 1)) blendAt((int)cx + x, (int)cy + y, shellDk, 0.9f);
        }
    }
    // ---- (0,1) front: speaker grille on the left, transport keys on the right
    {
        const int oy = T;
        for (int x = 0; x < T; x++) for (int y = 8; y < 14; y++)       // the case seam
            blendAt(x, oy + y, y < 11 ? shellDk : shellHi, 0.8f);
        // the grille: a recessed panel drilled in a hex pattern
        for (int y = 64; y < 184; y++) for (int x = 24; x < 144; x++)
            blendAt(x, oy + y, shellDk, rrect(x + 0.5f, y + 0.5f, 24, 64, 144, 184, 10) * 0.55f);
        for (int row = 0; row < 11; row++) for (int col = 0; col < 12; col++) {
            float cx = 34 + col * 9.4f + (row & 1) * 4.7f, cy = 74 + row * 9.8f;
            if (cx > 136) continue;
            for (int y = -4; y <= 4; y++) for (int x = -4; x <= 4; x++) {
                float d = sqrtf((x + 0.5f + cx - (int)cx - 0.5f) * (x + 0.5f + cx - (int)cx - 0.5f) + y * y);
                blendAt((int)cx + x, oy + (int)cy + y, { 14, 14, 16, 255 }, clampf(3.2f - d, 0.0f, 1.0f));
                if (y == -3 && fabsf((float)x) < 2) blendAt((int)cx + x, oy + (int)cy + y, shellHi, 0.35f);
            }
        }
        // play, stop, record: piano keys with their symbols silk-screened on
        for (int k = 0; k < 3; k++) {
            int bx = 152, by = oy + 56 + k * 52;
            for (int y = 0; y < 36; y++) for (int x = 0; x < 72; x++) {
                float cov = rrect(x + 0.5f, y + 0.5f, 0, 0, 72, 36, 5);
                Color c = y < 4 ? shellHi : y > 29 ? shellDk : Color{ 96, 98, 103, 255 };
                blendAt(bx + x, by + y, c, cov);
            }
            float cx = bx + 36.0f, cy = by + 17.0f;
            for (int y = -9; y <= 9; y++) for (int x = -9; x <= 9; x++) {
                float fx = x + 0.5f, fy = y + 0.5f, cov = 0;
                if (k == 0) cov = clampf(fminf(fminf(fx + 6.0f, 7.0f - fx * 0.95f - fabsf(fy) * 1.7f), 7.0f - fabsf(fy)), 0, 1);
                else if (k == 1) cov = clampf(fminf(6.5f - fabsf(fx), 6.5f - fabsf(fy)), 0, 1);
                else cov = clampf(6.5f - sqrtf(fx * fx + fy * fy), 0, 1);
                blendAt((int)cx + x, (int)cy + y, k == 2 ? Color{ 190, 44, 38, 255 } : printC, cov);
            }
        }
        printText(p, W, H, "MIC", 64, oy + 200, 10, printC, 0.09f, 1.0f);
        for (int y = -4; y <= 4; y++) for (int x = -4; x <= 4; x++)      // the mic's pinhole
            blendAt(50 + x, oy + 205 + y, { 12, 12, 14, 255 }, clampf(3.0f - sqrtf((float)(x * x + y * y)), 0, 1));
    }
    // ---- (1,1) reel: tape wound on a hub, with the drive teeth and slots cut in it
    {
        const int ox = T, oy = T;
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
            float dx = (x - 127.5f) / 120.0f, dy = (y - 127.5f) / 120.0f;
            float rad = sqrtf(dx * dx + dy * dy), ang = atan2f(dy, dx);
            Color c;
            if (rad > 1.0f) c = bay;                             // outside the flange: the dark bay
            else if (rad > 0.42f) {                              // wound tape, in fine rings
                float ring = sinf(rad * 520.0f) * 0.25f + sinf(rad * 61.0f) * 0.25f + 0.5f;
                float sheen = 0.5f + 0.5f * cosf(ang * 2.0f);    // the pack catches light across one axis
                ring = ring * 0.7f + sheen * 0.3f;
                c = { cl8(tape.r + ring * (tapeHi.r - tape.r)), cl8(tape.g + ring * (tapeHi.g - tape.g)),
                      cl8(tape.b + ring * (tapeHi.b - tape.b)), 255 };
                if (rad > 0.985f) c = { 40, 30, 26, 255 };      // the pack's edge
            } else {
                // The hub: three slots cut through it, which is the only thing
                // that says whether the reel is turning, and six drive teeth
                // round its bore.
                float sl = fmodf(ang + TAU, 2.0943951f);
                bool slot = rad > 0.14f && rad < 0.34f && sl < 0.62f;
                float tooth = fmodf(ang + TAU, 1.0471976f);
                bool bore = rad < 0.11f && !(rad > 0.075f && tooth < 0.30f);
                c = (slot || bore) ? Color{ 20, 19, 22, 255 } : hub;
                if (!slot && !bore && rad > 0.38f) c = { 150, 146, 142, 255 };   // the hub's rim
            }
            put(ox + x, oy + y, c);
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);     // in the hand, and on the floor across a room
    return t;
}

// Relief guessed from luminance, for the props atlas only: its regions are
// scanned material photographs whose shading already carries their relief.
// The world surfaces author a real height field instead (surfaces.cpp).
static Texture2D makeSurfaceDetail(Texture2D albedo, bool ceramic, float strength) {
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
