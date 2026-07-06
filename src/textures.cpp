#include "textures.h"
#include "util.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------- textures
static Texture2D finishTexture(Image img, bool tiled) {
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    if (tiled) {
        GenTextureMipmaps(&t);
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
        float stripe = 0.97f + 0.03f * sinf(x * 6.2831853f / 42.0f);
        float lines = 0.985f + 0.015f * sinf(x * 0.9f);
        float grime = fbm2(x * 0.013f, y * 0.013f, 7u, 4);
        float stain = fbm2(x * 0.006f + 31.0f, y * 0.006f, 12u, 4);
        float base = stripe * lines * (1.0f - 0.16f * grime) * (1.0f - 0.10f * vy);
        if (stain > 0.62f) base *= 1.0f - (stain - 0.62f) * 0.8f;
        float r = 199 * base, g = 178 * base, b = 104 * base;
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
        float fiber = (fbm2(x * 0.18f, y * 0.18f, 33u, 2) - 0.5f) * 0.14f;
        float blotch = fbm2(x * 0.008f, y * 0.008f, 21u, 4);
        float v = 1.0f + n + fiber;
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
        if (sp > 0.90f) v *= 0.78f;
        if (sp > 0.985f) v *= 0.55f;
        float r = 208 * v, g = 202 * v, b = 179 * v;
        float stain = fbm2(x * 0.01f, y * 0.01f, 55u, 3);
        if (stain > 0.64f) {
            float t = std::min(0.6f, (stain - 0.64f) * 2.2f);
            r = r * (1 - t) + 172 * t; g = g * (1 - t) + 150 * t; b = b * (1 - t) + 96 * t;
        }
        int bx = x % 256, by = y % 256;
        if (bx < 4 || bx > 251 || by < 4 || by > 251) { r *= 0.62f; g *= 0.62f; b *= 0.62f; }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    return finishTexture(img, true);
}

// PIRATE CLARK: tricorn hat, eyepatch strap, one glowing eye, hook hand, peg leg
Texture2D makeEntityTex() {
    const int W = 128, H = 256;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    auto put = [&](int x, int y, Color c) { if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; };
    auto hspan = [&](int y, float cx, float halfw, Color c) {
        for (int x = (int)(cx - halfw); x <= (int)(cx + halfw); x++) put(x, y, c);
    };
    Color body = { 13, 11, 10, 255 };
    Color hat  = { 18, 15, 13, 255 };
    Color wood = { 62, 48, 33, 255 };
    for (int y = 2; y < 252; y++) {
        float wob = (vnoise2(0.05f * y, 3.7f, 77u) - 0.5f) * 7.0f;
        float rag = (vnoise2(0.35f * y, 9.1f, 88u) - 0.5f) * 2.5f;
        float cx = 64 + wob * 0.35f;
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
            hspan(y, cx, halfw + rag + hem, body);
        }
        if (y > 68 && y <= 190) {                                                    // arms
            float t = (y - 68) / 122.0f;
            float off = 24 + 7 * t;
            hspan(y, cx - off, 3.6f + rag * 0.5f, body);
            if (y <= 184) hspan(y, cx + off, 3.6f + rag * 0.5f, body);
        }
        if (y > 165 && y < 252) {                                                    // legs: boot + peg
            float t = (y - 165) / 87.0f;
            hspan(y, cx - 10 + wob * 0.2f, 5.8f - 1.2f * t + rag * 0.5f, body);      // left: real leg
            if (y > 244) hspan(y, cx - 10 + wob * 0.2f, 8, body);                    // boot
            if (y <= 185) hspan(y, cx + 10 + wob * 0.2f, 5.8f + rag * 0.5f, body);   // right: stump...
            else hspan(y, cx + 10 + wob * 0.2f, 2.4f, wood);                         // ...then peg leg
        }
    }
    // eyepatch strap across the face
    for (int x = 46; x <= 82; x++) {
        int y = 30 + (x - 46) / 9;
        if (p[y * W + x].a) { p[y * W + x] = { 58, 52, 46, 255 }; p[(y + 1) * W + x] = { 48, 43, 38, 255 }; }
    }
    // single glowing eye (right side; left is under the patch) — brighter for the close-ups
    {
        float ex = 64 + 7.5f, ey = 36;
        for (int dy = -7; dy <= 7; dy++) for (int dx = -7; dx <= 7; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            int x = (int)(ex + dx), y = (int)(ey + dy);
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
        float hx = 64 + 31, hy = 194;
        for (int dy = -6; dy <= 8; dy++) for (int dx = -7; dx <= 7; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            if (fabsf(d - 5.0f) < 1.4f && dy > -3) put((int)(hx + dx), (int)(hy + dy), { 150, 150, 158, 255 });
        }
        for (int y = 186; y < 191; y++) hspan(y, hx, 2, { 120, 120, 126, 255 });     // hook base
        for (int q = -1; q <= 1; q++) put((int)(hx + 4), (int)(hy + q), { 224, 226, 234, 255 }); // glint
    }
    // the movie-poster details: skull on the hat, bandolier, brass buttons
    auto putIf = [&](int x, int y, Color c) {
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
    return finishTexture(img, false);
}

// prop atlas: left half cardboard, right-top cabinet front (drawers), right-bottom plain metal
Texture2D makePropsTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float r, g, b;
        if (x < 256) {                               // cardboard
            float n = (fbm2(x * 0.03f, y * 0.03f, 61u, 3) - 0.5f) * 0.18f;
            float v = 1.0f + n;
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
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
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
        float crack = fbm2(x * 0.015f, y * 0.015f, 83u, 4);
        if (fabsf(crack - 0.5f) < 0.005f) v *= 0.55f;                  // hairline cracks
        v *= 1.0f - 0.14f * vy;
        p[y * W + x] = { cl8(119 * v), cl8(117 * v), cl8(111 * v), 255 };
    }
    return finishTexture(img, true);
}

Texture2D makeConcreteFloorTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float v = 1.0f + (lat(x, y, 84u) - 0.5f) * 0.10f + (fbm2(x * 0.03f, y * 0.03f, 85u, 3) - 0.5f) * 0.2f;
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
        int row = y / 42;
        int bx = (x + (row % 2) * 64) % 128;
        float v = 1.0f + (fbm2(x * 0.04f, y * 0.04f, 96u, 3) - 0.5f) * 0.35f;
        float r = 118 * v, g = 26 * v, b = 20 * v;
        if (y % 42 < 4 || bx < 5) { r = 38; g = 12; b = 10; }          // mortar
        float rot = fbm2(x * 0.008f, y * 0.008f, 97u, 4);
        if (rot > 0.60f) { float t = (rot - 0.60f) * 1.8f; r *= 1 - t * 0.7f; g *= 1 - t * 0.5f; b *= 1 - t * 0.5f; }
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
        if (gx < 3 || gx > 60 || gy < 3 || gy > 60) {                  // grout
            float gv = 1.0f + (lat(x, y, 90u) - 0.5f) * 0.12f;
            r = 166 * gv; g = 172 * gv; b = 176 * gv;
        } else {
            float tv = 1.0f + (lat(tx, ty, 91u) - 0.5f) * 0.09f       // per-tile variation
                     + (lat(x, y, 92u) - 0.5f) * 0.035f;
            r = 221 * tv; g = 226 * tv; b = 229 * tv;
            if (lat(tx, ty, 93u) > 0.94f) { r *= 0.90f; g *= 0.92f; b *= 0.86f; }  // aged tile
            float gl = fbm2(x * 0.01f, y * 0.01f, 94u, 3);             // faint sheen variation
            if (gl > 0.62f) { r *= 1.04f; g *= 1.04f; b *= 1.05f; }
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
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float n = lat(x, y, 205u) * 0.16f - 0.08f;
        float fiber = (fbm2(x * 0.18f, y * 0.18f, 233u, 2) - 0.5f) * 0.14f;
        float blotch = fbm2(x * 0.008f, y * 0.008f, 221u, 4);
        float v = 1.0f + n + fiber;
        if (blotch > 0.56f) v *= 1.0f - (blotch - 0.56f) * 0.9f;
        float r = 152 * v, g = 128 * v, b = 78 * v;
        uint32_t fh = ih(x >> 2, y >> 2, 231u);
        if (fh % 29 == 0) {   // trodden-in confetti
            Color c = PARTY[(fh >> 7) % 5];
            float cv = v * 0.85f;
            r = c.r * cv; g = c.g * cv; b = c.b * cv;
        }
        p[y * W + x] = { cl8(r), cl8(g), cl8(b), 255 };
    }
    return finishTexture(img, true);
}
