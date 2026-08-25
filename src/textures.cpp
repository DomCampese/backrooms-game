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

// THE PARTYGOER =): pale yellow, painted-on smile, striped party hat. It was
// here before the bunting went up. It will be here after.
Texture2D makePartygoerTex() {
    const int W = 128, H = 256;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    auto put = [&](int x, int y, Color c) { if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; };
    auto hspan = [&](int y, float cx, float halfw, Color c) {
        for (int x = (int)(cx - halfw); x <= (int)(cx + halfw); x++) put(x, y, c);
    };
    Color skin = { 208, 182, 84, 255 };
    Color skin2 = { 176, 150, 62, 255 };
    for (int y = 24; y < 252; y++) {
        float wob = (vnoise2(0.05f * y, 8.2f, 177u) - 0.5f) * 6.0f;
        float rag = (vnoise2(0.35f * y, 4.4f, 188u) - 0.5f) * 2.2f;
        float cx = 64 + wob * 0.3f;
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
            hspan(y, cx - off, 3.4f + rag * 0.4f, skin2);
            hspan(y, cx + off, 3.4f + rag * 0.4f, skin2);
        }
        if (y > 200 && y < 252) {   // legs
            hspan(y, cx - 9 + wob * 0.2f, 5.2f + rag * 0.4f, skin2);
            hspan(y, cx + 9 + wob * 0.2f, 5.2f + rag * 0.4f, skin2);
            if (y > 246) { hspan(y, cx - 9, 7, skin2); hspan(y, cx + 9, 7, skin2); }
        }
    }
    // something sweet dripped down it once and never dried
    for (int x = 0; x < W; x++) {
        if (lat(x, 7, 191u) < 0.82f) continue;
        int len = 30 + (int)(lat(x, 9, 192u) * 90);
        for (int y = 70; y < 70 + len && y < 250; y++)
            if (p[y * W + x].a) {
                Color &c = p[y * W + x];
                c.r = cl8(c.r * 0.82f); c.g = cl8(c.g * 0.80f); c.b = cl8(c.b * 0.72f);
            }
    }
    // the face: two dot eyes and a smile that was painted on, not grown
    auto putIf = [&](int x, int y, Color c) {
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
            hspan(y, 60 + t * 4, 1.0f + 11.0f * t, ((y / 5) & 1) ? ha : hb);
        }
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy < 5) put(60 + dx, 2 + dy, { 226, 218, 200, 255 });   // pompom
    }
    return finishTexture(img, false);
}

// wall scrawl atlas: eight phrases in a shaky triple-struck hand, 2 x 4 cells
Texture2D makeScrawlTex() {
    const int W = 512, H = 512;
    Image img = GenImageColor(W, H, BLANK);
    const char *lines[8] = { "NO CLIP", "dont stare", "day 407", "the exit lies",
                             "he hears the flares", "keep walking", "it hums at night", "wrong door =)" };
    Rng r(0x5C12ULL);
    for (int i = 0; i < 8; i++) {
        int cx = (i % 2) * 256, cy = (i / 2) * 128;
        Color ink = (i % 3 == 0) ? Color{ 104, 32, 26, 215 } : Color{ 54, 46, 40, 205 };
        int fs = 36, tw = MeasureText(lines[i], fs);
        while (tw > 228 && fs > 18) { fs -= 2; tw = MeasureText(lines[i], fs); }
        int x0 = cx + 128 - tw / 2, y0 = cy + 64 - fs / 2;
        ImageDrawText(&img, lines[i], x0, y0, fs, ink);
        ImageDrawText(&img, lines[i], x0 + r.ri(-2, 2), y0 + r.ri(-2, 2), fs,
                      { ink.r, ink.g, ink.b, 80 });
        ImageDrawText(&img, lines[i], x0 + r.ri(-2, 2), y0 + r.ri(-2, 2), fs,
                      { ink.r, ink.g, ink.b, 60 });
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
Texture2D makeDogTex() {
    const int W = 192, H = 128;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    auto put = [&](int x, int y, Color c) { if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; };
    auto vspan = [&](int x, int y0, int y1, Color c) { for (int y = y0; y <= y1; y++) put(x, y, c); };
    Color hide  = { 44, 28, 26, 255 };
    Color hide2 = { 62, 38, 32, 255 };
    for (int x = 22; x < 170; x++) {
        float u = (x - 22) / 148.0f;
        float mange = vnoise2(x * 0.22f, 3.1f, 921u);
        Color c = (mange > 0.56f) ? hide2 : hide;
        // body: deepest over the shoulders, tucked at the waist, rump lifted
        float top = 46.0f + 5.0f * sinf(u * 3.14159f) - 4.0f * expf(-powf((u - 0.22f) * 5.0f, 2.0f));
        float bot = 78.0f - 4.0f * expf(-powf((u - 0.55f) * 6.0f, 2.0f));
        if (u > 0.06f && u < 0.94f) vspan(x, (int)top, (int)bot, c);
        // neck and hanging head, forward of the shoulders
        if (u < 0.20f) {
            float t = u / 0.20f;
            vspan(x, (int)(52 + 16 * t), (int)(70 + 14 * t), c);
        }
        // four legs, thin and a little too long
        for (int L = 0; L < 4; L++) {
            float lu = 0.16f + L * 0.22f;
            if (fabsf(u - lu) < 0.022f) vspan(x, (int)bot - 2, 116, (L & 1) ? hide : hide2);
        }
        // tail, low and straight
        if (u > 0.90f) vspan(x, (int)(58 + (u - 0.90f) * 120.0f), (int)(64 + (u - 0.90f) * 130.0f), hide);
    }
    // the muzzle, and the two pale eyes that find you before you find them
    for (int x = 12; x < 30; x++) vspan(x, 66, 78, hide);
    for (int e = 0; e < 2; e++)
        for (int dx = 0; dx < 4; dx++) for (int dy = 0; dy < 3; dy++)
            put(26 + dx + e * 7, 60 + dy, Color{ 226, 216, 176, 255 });
    return finishTexture(img, false);
}

// The almond water can: a slim aluminium tallboy, cream label, brown band with
// the almond on it. Drawn big in the hand while you drink and small as a
// billboard where one is sitting out, so the whole silhouette lives in one
// portrait sheet with the shape carried by alpha — no quad edges to give it
// away. The label is drawn flat and then wrapped: every column is shaded by the
// cylinder's curvature and the artwork is remapped through asin, so the
// wordmark compresses toward the edges the way print on a real can does.
Texture2D makeAlmondTex() {
    const int W = 112, H = 192;
    Image img = GenImageColor(W, H, BLANK);
    Color *p = (Color *)img.data;
    auto put = [&](int x, int y, Color c) { if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; };

    const float cx = 56.0f;
    const float bodyHW = 38.0f;        // the barrel of the can
    const float lidHW  = 30.0f;        // narrower, above the shoulder
    const int   lidY   = 25;           // where the lid's front edge sits
    const int   neckY  = 36;           // shoulder: taper finishes here
    const int   botY   = 172;          // where the base starts rolling under
    const int   endY   = 182;

    const Color alu    = { 176, 178, 184, 255 };   // bare aluminium
    const Color aluDk  = { 118, 120, 127, 255 };
    const Color cream  = { 236, 230, 214, 255 };   // the printed label
    const Color ink    = {  74,  50,  33, 255 };   // label brown
    const Color inkSoft= { 104,  74,  50, 255 };
    const Color nut    = { 198, 156, 108, 255 };
    const Color nutHi  = { 224, 192, 152, 255 };

    // ---- the label, drawn flat. s runs -1..1 around the visible half of the
    // can, so this strip is what you'd see if you unrolled it.
    const int LW = 176, LH = 108, labelTop = 54;
    Image lbl = GenImageColor(LW, LH, BLANK);
    Color *lp = (Color *)lbl.data;
    for (int y = 0; y < LH; y++)
        for (int x = 0; x < LW; x++) lp[y * LW + x] = cream;
    ImageDrawRectangle(&lbl, 0, 18, LW, 74, ink);                    // the brown band
    ImageDrawRectangle(&lbl, 0, 16, LW, 2, Color{ 176, 150, 112, 255 });
    ImageDrawRectangle(&lbl, 0, 92, LW, 2, Color{ 176, 150, 112, 255 });
    {   // the almond, a teardrop with a seam, sat in the middle of the band
        const int ax = LW / 2, ay = 40, ah = 13, aw = 9;
        for (int y = -ah; y <= ah; y++) {
            float t = (y + ah) / (2.0f * ah);
            float hw = aw * sinf(powf(t, 0.72f) * 3.14159f * 0.94f);
            for (int x = -(int)hw; x <= (int)hw; x++) {
                float e = fabsf(x / (hw + 0.001f));
                int px2 = ax + x, py2 = ay + y;
                if (px2 < 0 || px2 >= LW || py2 < 0 || py2 >= LH) continue;
                lp[py2 * LW + px2] = (e > 0.78f || t < 0.06f) ? inkSoft : (x < -1 ? nutHi : nut);
            }
        }
        for (int y = -ah + 5; y <= ah - 3; y++)
            if (ay + y >= 0 && ay + y < LH) lp[(ay + y) * LW + ax] = inkSoft;
    }
    const char *l1 = "ALMOND", *l2 = "WATER";
    ImageDrawText(&lbl, l1, LW / 2 - MeasureText(l1, 13) / 2, 57, 13, Color{ 238, 228, 206, 255 });
    ImageDrawText(&lbl, l2, LW / 2 - MeasureText(l2, 13) / 2, 74, 13, Color{ 238, 228, 206, 255 });

    // ---- roll it onto the can, shading each column by the curvature
    auto shade = [&](float u) {                       // u: -1 (left edge) .. 1 (right)
        float curve = sqrtf(fmaxf(0.0f, 1.0f - u * u));          // the cylinder's facing
        float spec  = expf(-powf((u + 0.42f) * 3.1f, 2.0f));     // key light, upper left
        float rim   = expf(-powf((u - 0.86f) * 7.0f, 2.0f));     // a thin bounce off the far edge
        return clampf(0.40f + 0.52f * curve + 0.40f * spec + 0.18f * rim, 0.0f, 1.55f);
    };
    auto tint = [&](Color c, float k) {
        return Color{ cl8(c.r * k), cl8(c.g * k), cl8(c.b * k), 255 };
    };

    for (int y = lidY; y <= endY; y++) {
        float hw;
        if (y < neckY) hw = lidHW + (bodyHW - lidHW) * (y - lidY) / (float)(neckY - lidY);
        else if (y <= botY) hw = bodyHW;
        else hw = bodyHW - (bodyHW - 31.0f) * (y - botY) / (float)(endY - botY);
        for (int x = (int)(cx - hw); x <= (int)(cx + hw); x++) {
            float u = (x - cx) / hw;
            float k = shade(u);
            Color base = alu;
            int ly = y - labelTop;
            if (ly >= 0 && ly < LH) {                 // inside the printed wrap
                // The flat strip is wider than the can it wraps onto, and near
                // the edges many flat columns fall into one column here. Point
                // sampling that drops most of them and eats the thin strokes of
                // the wordmark, so average the whole span each column covers.
                auto flatX = [&](float ux) {
                    float sx = asinf(clampf(ux, -1.0f, 1.0f)) / 1.5707963f;
                    return (sx + 1.0f) * 0.5f * (LW - 1);
                };
                float f0 = flatX((x - 0.5f - cx) / hw), f1 = flatX((x + 0.5f - cx) / hw);
                int i0 = (int)floorf(fminf(f0, f1)), i1 = (int)ceilf(fmaxf(f0, f1));
                i0 = i0 < 0 ? 0 : i0;
                i1 = i1 > LW - 1 ? LW - 1 : i1;
                float ar = 0, ag = 0, ab = 0;
                int n = 0;
                for (int fx = i0; fx <= i1; fx++) {
                    Color sc = lp[ly * LW + fx];
                    ar += sc.r; ag += sc.g; ab += sc.b; n++;
                }
                if (n > 0) base = { cl8(ar / n), cl8(ag / n), cl8(ab / n), 255 };
            }
            put(x, y, tint(base, k));
        }
        // the rolled seams top and bottom read as a darker line
        if (y == neckY || y == botY)
            for (int x = (int)(cx - hw); x <= (int)(cx + hw); x++)
                put(x, y, tint(aluDk, shade((x - cx) / hw)));
    }
    UnloadImage(lbl);

    // ---- the lid, seen from just above: a shallow ellipse with a pull tab
    for (int y = lidY - 8; y <= lidY; y++) {
        float t = (y - (lidY - 8)) / 8.0f;
        float hw = lidHW * sqrtf(fmaxf(0.0f, 1.0f - (1.0f - t) * (1.0f - t)));
        for (int x = (int)(cx - hw); x <= (int)(cx + hw); x++) {
            float u = (x - cx) / (hw + 0.001f);
            // the lid catches more light than the wall does, and dishes in the middle
            float k = 0.86f + 0.30f * expf(-powf(u * 1.7f, 2.0f)) - 0.16f * t;
            put(x, y, tint(alu, k));
        }
    }
    for (int x = (int)(cx - lidHW); x <= (int)(cx + lidHW); x++)   // the chime, round the rim
        put(x, lidY, tint(aluDk, 0.92f));
    {   // pull tab: a small dark ring with the rivet holding it down
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -8; dx <= 8; dx++) {
                float e = sqrtf((dx / 8.0f) * (dx / 8.0f) + (dy / 4.0f) * (dy / 4.0f));
                if (e > 1.0f) continue;
                put((int)cx + dx + 2, lidY - 4 + dy, (e > 0.62f) ? aluDk : tint(alu, 0.80f));
            }
        put((int)cx + 2, lidY - 4, tint(aluDk, 0.7f));
    }

    // ---- wear. Nothing down here has been on a shelf recently, and a can that
    // reads factory-fresh looks pasted on top of the game rather than found in
    // it. Faint blotches and a scuff or two; the wordmark stays legible.
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            Color &c = p[y * W + x];
            if (c.a == 0) continue;
            float g = vnoise2(x * 0.055f, y * 0.055f, 4471u);
            float stain = clampf((g - 0.58f) * 2.1f, 0.0f, 1.0f) * 0.18f;
            float grime = (vnoise2(x * 0.5f, y * 0.5f, 9137u) - 0.5f) * 0.05f;
            float k = 1.0f - stain + grime;
            c.r = cl8(c.r * k);
            c.g = cl8(c.g * k * 0.998f);
            c.b = cl8(c.b * k * 0.986f);   // stains go warm
        }

    return finishTexture(img, false);
}
