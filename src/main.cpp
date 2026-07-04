// THE BACKROOMS — Level 0
// Single-file native game. raylib + OpenGL 3.3. All textures/sounds procedural.
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unordered_map>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------- constants
static const float CELL = 2.0f;        // metres per grid cell
static const int   CCELLS = 16;        // cells per chunk side
static const float CHUNK = CELL * CCELLS;
static const float WT = 0.11f;         // wall half-thickness


// ---------------------------------------------------------------- rng/noise
static uint64_t hash64(uint64_t x) {
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
static uint32_t ih(int x, int y, uint32_t s) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16);
}
static float lat(int x, int y, uint32_t s) { return (float)(ih(x, y, s) & 0xFFFFFF) / 16777216.0f; }
static float vnoise2(float x, float y, uint32_t s) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = lat(xi, yi, s), b = lat(xi + 1, yi, s), c = lat(xi, yi + 1, s), d = lat(xi + 1, yi + 1, s);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm2(float x, float y, uint32_t s, int oct) {
    float v = 0, amp = 0.5f, f = 1;
    for (int i = 0; i < oct; i++) { v += vnoise2(x * f, y * f, s + (uint32_t)i * 101u) * amp; amp *= 0.5f; f *= 2; }
    return v;
}
static unsigned char cl8(float v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// ---------------------------------------------------------------- shaders
static const char *WORLD_VS = R"GLSL(
#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;
uniform mat4 mvp;
out vec3 fragPos; out vec2 fragUV; out vec3 fragN; out vec4 fragC;
void main(){
    fragPos = vertexPosition; fragUV = vertexTexCoord; fragN = vertexNormal; fragC = vertexColor;
    gl_Position = mvp*vec4(vertexPosition,1.0);
}
)GLSL";

static const char *WORLD_FS = R"GLSL(
#version 330
in vec3 fragPos; in vec2 fragUV; in vec3 fragN; in vec4 fragC;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform float uTime; uniform float uBlackout; uniform vec3 uViewPos;
uniform float uFlash; uniform vec3 uFlashDir;
uniform vec3 uFlarePos; uniform float uFlareInt;
uniform vec3 uAmb; uniform vec3 uFogCol; uniform float uFogDen;
uniform vec3 uLightCol; uniform float uLS; uniform float uLY; uniform float uDead; uniform float uLightMul;
out vec4 finalColor;
float lhash(vec2 g){ return fract(sin(dot(g, vec2(127.1,311.7)))*43758.5453123); }
float lightState(vec2 g){
    float h = lhash(g);
    if (h < uDead) return 0.0;                      // dead tube
    float s = 1.0;
    if (h > 0.86){                                  // faulty tube: intermittent strobe fits
        float fh = fract(h*97.31);
        float gate = fract(sin(floor(uTime*0.6+fh*37.0)*12.9898)*43758.5453);
        if (gate > 0.62){
            float n = fract(sin(uTime*(9.0+fh*15.0) + fh*211.0)*43758.5453);
            s = 0.25 + 0.75*step(0.5, n);
        }
    }
    return s * uBlackout;
}
vec3 roomLight(vec3 P, vec3 N){
    vec2 base = floor((P.xz - uLS*0.5)/uLS + 0.5);
    vec3 light = vec3(0.0);
    for (int dx=-1; dx<=1; dx++)
    for (int dz=-1; dz<=1; dz++){
        vec2 g = base + vec2(float(dx), float(dz));
        float st = lightState(g);
        if (st <= 0.001) continue;
        vec3 lp = vec3(g.x*uLS + uLS*0.5, uLY, g.y*uLS + uLS*0.5);
        vec3 ld = lp - P;
        float d2 = dot(ld,ld);
        float atten = 1.0/(1.0 + 0.075*d2);
        float ndl = clamp(dot(N, normalize(ld))*0.55 + 0.45, 0.0, 1.0);
        light += uLightCol*(st*atten*ndl*2.0*uLightMul);
    }
    // handheld flashlight: cone from the camera along the view direction
    if (uFlash > 0.01){
        vec3 fv = P - uViewPos;
        float fd2 = dot(fv,fv);
        vec3 fn = normalize(fv);
        float cone = pow(max(dot(fn, uFlashDir), 0.0), 26.0);
        float sput = 0.93 + 0.07*fract(sin(floor(uTime*24.0)*12.9898)*43758.5453);
        float fl = uFlash * cone * sput * 7.5/(1.0 + 0.10*fd2);
        light += vec3(1.0,0.97,0.86) * fl * clamp(dot(N, -fn)*0.6 + 0.4, 0.0, 1.0);
    }
    if (uFlareInt > 0.01){                            // burning flare: orange point light
        vec3 lv2 = uFlarePos - P;
        float d2 = dot(lv2,lv2);
        float ndl = clamp(dot(N, normalize(lv2))*0.6 + 0.4, 0.0, 1.0);
        light += vec3(1.0,0.42,0.15) * (uFlareInt * ndl * 5.0/(1.0 + 0.30*d2));
    }
    light += uAmb*(0.35+0.65*uBlackout);
    return light;
}
void main(){
    vec3 col;
    float aOut = 1.0;
    if (fragC.a < 0.62){
        if (fragC.a < 0.1){                          // light panel (emissive, flickers)
            vec2 g = floor((fragPos.xz - uLS*0.5)/uLS + 0.5);
            float st = lightState(g);
            col = vec3(0.50,0.48,0.44) * roomLight(fragPos, vec3(0.0,-1.0,0.0)) + uLightCol*1.9*st;
        } else if (fragC.a < 0.4){                   // raw emissive (exit glow)
            col = fragC.rgb * 2.4;
        } else {                                     // water surface
            vec3 light = roomLight(fragPos, vec3(0.0,1.0,0.0));
            float sh = 0.75 + 0.25*sin(fragPos.x*2.3 + uTime*1.4)*sin(fragPos.z*1.9 - uTime*1.1)
                            + 0.10*sin(fragPos.x*5.1 - uTime*2.2);
            col = fragC.rgb * (light*0.75 + uAmb*1.4) * sh;
            aOut = 0.62;
        }
    } else {
        vec3 albedo = texture(texture0, fragUV).rgb * fragC.rgb;
        col = albedo * roomLight(fragPos, normalize(fragN));
    }
    float dist = distance(fragPos, uViewPos);
    float f = clamp(exp(-dist*uFogDen), 0.0, 1.0);
    vec3 fogc = uFogCol * mix(0.10, 1.0, uBlackout);
    col = mix(fogc, col, f);
    col = 1.0 - exp(-col*1.25);                      // filmic-ish rolloff, no clipping
    finalColor = vec4(col, aOut) * colDiffuse;
}
)GLSL";

static const char *POST_FS = R"GLSL(
#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform float uTime; uniform float uFear;
out vec4 finalColor;
float hh(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)))*43758.5453); }
void main(){
    vec2 uv = fragTexCoord;
    vec2 dir = uv - 0.5;
    float ca = 0.0012 + uFear*0.0035;               // chromatic aberration
    vec3 c;
    c.r = texture(texture0, uv + dir*ca).r;
    c.g = texture(texture0, uv).g;
    c.b = texture(texture0, uv - dir*ca).b;
    float g = hh(uv*vec2(1287.0,721.0) + vec2(fract(uTime*13.71)*61.0, fract(uTime*7.31)*83.0)) - 0.5;
    c += g * (0.032 + 0.08*uFear);                   // film grain
    float d = length(dir);
    c *= 1.0 - smoothstep(0.34, 0.95, d)*(0.42 + 0.34*uFear); // vignette
    c *= 0.985 + 0.015*sin(uTime*377.0);             // mains-hum luma shimmer
    finalColor = vec4(c, 1.0);
}
)GLSL";

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

static Texture2D makeWallpaperTex() {
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

static Texture2D makeCarpetTex() {
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

static Texture2D makeCeilingTex() {
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
static Texture2D makeEntityTex() {
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
        if (y >= 44 && y <= 64) {                                                    // ragged beard
            float br = vnoise2(0.4f * y, 17.3f, 91u);
            if (br > 0.28f) hspan(y, cx, 13.0f * (1.0f - (y - 44) / 24.0f) + rag, body);
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
    // single glowing eye (right side; left is under the patch)
    {
        float ex = 64 + 7.5f, ey = 36;
        for (int dy = -6; dy <= 6; dy++) for (int dx = -6; dx <= 6; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            int x = (int)(ex + dx), y = (int)(ey + dy);
            if (x < 0 || x >= W || y < 0 || y >= H || p[y * W + x].a == 0) continue;
            if (d < 2.6f) p[y * W + x] = { 231, 224, 202, 255 };
            else if (d < 6.0f) {
                float t = expf(-(d - 2.6f) * 1.1f) * 0.5f;
                Color &c = p[y * W + x];
                c.r = cl8(c.r + 190 * t); c.g = cl8(c.g + 180 * t); c.b = cl8(c.b + 150 * t);
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
    }
    return finishTexture(img, false);
}

// prop atlas: left half cardboard, right-top cabinet front (drawers), right-bottom plain metal
static Texture2D makePropsTex() {
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
static Texture2D makeConcreteWallTex() {
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

static Texture2D makeConcreteFloorTex() {
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

static Texture2D makeConcreteCeilTex() {
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
static Texture2D makeRedBrickTex() {
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
static Texture2D makeTileTex() {
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

// ---------------------------------------------------------------- sounds
static Wave makeWaveBuf(int frames) {
    Wave w = {};
    w.frameCount = (unsigned)frames; w.sampleRate = 44100; w.sampleSize = 16; w.channels = 1;
    w.data = MemAlloc(frames * 2);
    return w;
}
static float clampf1(float v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }

static Sound makeFootstep(uint32_t seed)
{
    const int sampleRate = 44100;
    const int n = (int)(0.22f * sampleRate);

    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;

    Rng r((uint64_t)seed * 2000u + 3u);

    float thump = 52.0f + r.f01() * 8.0f;
    float knock = 92.0f + r.f01() * 12.0f;
    float toeDelay = 0.055f + r.f01() * 0.02f;

    for (int i = 0; i < n; i++)
    {
        float t = (float)i / sampleRate;

        // Heel impact
        float env1 = (1.0f - expf(-t * 1200.0f)) * expf(-t * 18.0f);

        float heel =
            (sinf(6.2831853f * thump * t) * 1.45f +
             sinf(6.2831853f * knock * t) * 0.50f)
            * env1;

        // Toe impact
        float toe = 0.0f;
        float tt = t - toeDelay;
        if (tt > 0.0f)
        {
            float env2 = (1.0f - expf(-tt * 900.0f)) * expf(-tt * 42.0f);

            toe =
                (sinf(6.2831853f * 120.0f * tt) * 0.50f +
                 sinf(6.2831853f * 180.0f * tt) * 0.18f)
                * env2;
        }

        // Tiny bit of texture
        float noise =
            (r.f01() * 2.0f - 1.0f) *
            expf(-t * 120.0f) *
            0.03f;

        // Mix
        float sample = heel + toe + noise;

        // Soft saturation for punch
        sample = tanhf(sample * 1.45f);

        d[i] = (short)(clampf1(sample) * 32000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

static Sound makeJumpscare() {
    int n = (int)(1.1f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xDEADULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.35f * (wn - lp);
        float shriek = lp * expf(-t * 3.2f) * 1.6f;
        float sweep = sinf(6.2831853f * (210 - 150 * t) * t) * expf(-t * 2.4f);
        float sub = sinf(6.2831853f * 38 * t) * expf(-t * 1.6f);
        d[i] = (short)(clampf1(tanhf((shriek + sweep * 0.8f + sub * 1.1f) * 2.4f) * expf(-t * 1.1f)) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

static Sound makeSplash(uint32_t seed, bool big) {
    int n = (int)((big ? 0.55f : 0.32f) * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r((uint64_t)seed * 6151u + 11u);
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.05f * (wn - lp);
        lp2 += 0.16f * (wn - lp2);
        float env = (1 - expf(-t * 160)) * expf(-t * (big ? 6.5f : 11.0f));
        float s = (lp * 2.6f + lp2 * 0.9f * expf(-t * 20)) * env * (big ? 0.8f : 0.5f);
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

static Sound makeClick() {
    int n = (int)(0.035f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xC11CULL);
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float s = (r.f01() * 2 - 1) * expf(-t * 320) * 0.5f + sinf(6.2831853f * 2100 * t) * expf(-t * 260) * 0.35f;
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

<<<<<<< HEAD
static Sound makeFlareStrike() {
    int n = (int)(0.8f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xF1A2EULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.30f * (wn - lp);
        float scratch = (wn - lp) * expf(-t * 34.0f) * 1.3f;                  // striker scrape
        float pop = sinf(6.2831853f * 150.0f * t) * expf(-t * 42.0f) * 0.8f;  // ignition pop
        float swell = (wn - lp) * (1 - expf(-t * 9.0f)) * expf(-t * 2.6f) * 0.5f; // hiss hands off to synth
        d[i] = (short)(clampf1(tanhf(scratch + pop + swell)) * 30000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

=======
>>>>>>> bb7a9663312e01303ae6307b867d48e9e7b1333d
Sound makeWinChime() {
    int n = (int)(1.8f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    const float freqs[3] = { 392.0f, 523.25f, 659.25f };
    const float starts[3] = { 0.0f, 0.28f, 0.56f };
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f, s = 0;
        for (int k = 0; k < 3; k++) {
            float lt = t - starts[k];
            if (lt < 0) continue;
            s += sinf(6.2831853f * freqs[k] * lt) * (1 - expf(-lt * 30)) * expf(-lt * 2.4f) * 0.20f;
        }
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// ------------------------------------------------------------- audio synth
// Continuously synthesized ambience: fluorescent hum, room tone, entity growl.
struct AudioSynth {
    AudioStream stream;
    float humTarget = 1, hum = 1, growlTarget = 0, growl = 0;   // humTarget ducks all ambience
<<<<<<< HEAD
    float hissTarget = 0, hiss = 0;                             // burning flare hiss
=======
>>>>>>> bb7a9663312e01303ae6307b867d48e9e7b1333d
    float tHum = 1, tDrone = 0, tWater = 0;                     // per-level ambience mix targets
    float wHum = 1, wDrone = 0, wWater = 0;
    double ph[16] = {};
    float lp1 = 0, lp2 = 0, lp3 = 0;
    uint32_t xr = 0x12345u;
    float frand() { xr ^= xr << 13; xr ^= xr >> 17; xr ^= xr << 5; return (float)(xr & 0xFFFFFF) / 8388608.0f - 1.0f; }
    float osc(int i, float freq) {
        ph[i] += 6.283185307 * freq / 44100.0;
        if (ph[i] > 6.283185307) ph[i] -= 6.283185307;
        return sinf((float)ph[i]);
    }
    void init() {
        SetAudioStreamBufferSizeDefault(2048);
        stream = LoadAudioStream(44100, 16, 2);
        PlayAudioStream(stream);
    }
    void update() {
        static short buf[4096];
        while (IsAudioStreamProcessed(stream)) {
            for (int i = 0; i < 2048; i++) {
                hum += (humTarget - hum) * 2e-5f;
                growl += (growlTarget - growl) * 4e-5f;
<<<<<<< HEAD
                hiss += (hissTarget - hiss) * 6e-5f;
=======
>>>>>>> bb7a9663312e01303ae6307b867d48e9e7b1333d
                wHum += (tHum - wHum) * 1.5e-5f;
                wDrone += (tDrone - wDrone) * 1.5e-5f;
                wWater += (tWater - wWater) * 1.5e-5f;
                float wn = frand();
                lp1 += 0.035f * (wn - lp1);
                // L0: fluorescent hum
                float h = osc(0, 119.7f) * 0.45f + osc(1, 239.4f) * 0.20f + osc(2, 359.1f) * 0.09f + osc(3, 59.85f) * 0.12f;
                float humS = h * (0.85f + 0.15f * osc(4, 0.23f)) * 0.16f * wHum;
                // L1: cavernous drone
                float droneS = (osc(8, 41.2f) * 0.6f + osc(9, 55.3f) * 0.45f)
                             * (0.55f + 0.45f * osc(10, 0.11f)) * 0.20f * wDrone
                             + lp1 * 0.14f * wDrone;
                // Poolrooms: moving water
                lp3 += 0.010f * (wn - lp3);
                float waterS = (lp3 * 3.2f * (0.55f + 0.45f * osc(11, 0.16f))
                              + lp1 * 0.55f * (0.5f + 0.5f * osc(12, 0.071f))) * 0.30f * wWater;
                float room = lp1 * 0.08f;
                float amb = (humS + droneS + waterS + room) * hum;   // hum var = blackout duck
                float g = osc(5, 46.0f) * 0.7f + osc(6, 33.5f) * 0.35f;
                float trem = 0.55f + 0.45f * osc(7, 2.1f);
                lp2 += 0.02f * (wn - lp2);
                float growlOut = (g * trem + lp2 * 2.2f) * growl * 0.5f;
<<<<<<< HEAD
                float hissOut = (wn - lp1) * 0.16f * hiss;      // flare burn: bright noise
                if (hiss > 0.01f) { float c = frand(); if (c > 0.998f) hissOut += c * 0.7f * hiss; }  // crackle
                short v = (short)(tanhf((amb + growlOut + hissOut) * 1.3f) * 30000);
=======
                short v = (short)(tanhf((amb + growlOut) * 1.3f) * 30000);
>>>>>>> bb7a9663312e01303ae6307b867d48e9e7b1333d
                buf[i * 2] = v; buf[i * 2 + 1] = v;
            }
            UpdateAudioStream(stream, buf, 2048);
        }
    }
};

// ---------------------------------------------------------------- world
struct MB {
    std::vector<float> v, uv, n;
    std::vector<unsigned char> c;
    std::vector<unsigned short> idx;
    void quad(Vector3 a, Vector3 b, Vector3 cc, Vector3 d, Vector3 nn,
              Vector2 ta, Vector2 tb, Vector2 tc, Vector2 td, Color col) {
        unsigned short base = (unsigned short)(v.size() / 3);
        const Vector3 P[4] = { a, b, cc, d };
        const Vector2 T[4] = { ta, tb, tc, td };
        for (int i = 0; i < 4; i++) {
            v.push_back(P[i].x); v.push_back(P[i].y); v.push_back(P[i].z);
            uv.push_back(T[i].x); uv.push_back(T[i].y);
            n.push_back(nn.x); n.push_back(nn.y); n.push_back(nn.z);
            c.push_back(col.r); c.push_back(col.g); c.push_back(col.b); c.push_back(col.a);
        }
        const unsigned short q[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; i++) idx.push_back(base + q[i]);
    }
    Mesh bake() {
        Mesh m = {};
        if (idx.empty()) return m;
        m.vertexCount = (int)(v.size() / 3);
        m.triangleCount = (int)(idx.size() / 3);
        m.vertices = (float *)MemAlloc((unsigned)(v.size() * sizeof(float)));
        m.texcoords = (float *)MemAlloc((unsigned)(uv.size() * sizeof(float)));
        m.normals = (float *)MemAlloc((unsigned)(n.size() * sizeof(float)));
        m.colors = (unsigned char *)MemAlloc((unsigned)c.size());
        m.indices = (unsigned short *)MemAlloc((unsigned)(idx.size() * sizeof(unsigned short)));
        memcpy(m.vertices, v.data(), v.size() * sizeof(float));
        memcpy(m.texcoords, uv.data(), uv.size() * sizeof(float));
        memcpy(m.normals, n.data(), n.size() * sizeof(float));
        memcpy(m.colors, c.data(), c.size());
        memcpy(m.indices, idx.data(), idx.size() * sizeof(unsigned short));
        UploadMesh(&m, false);
        return m;
    }
};

// walls: wallN[i][k] = north edge of cell (i,k) at z=k*CELL; wallW = west edge at x=i*CELL
// values: 0 open, 1 wall, 2 exit doorway
// prop types: 0 none, 1 box stack, 2 filing cabinet, 3 folding table, 4 fallen ceiling tile
struct ChunkData {
    uint8_t wallN[CCELLS][CCELLS];
    uint8_t wallW[CCELLS][CCELLS];
    uint8_t pillar[CCELLS][CCELLS];
    uint8_t prop[CCELLS][CCELLS];
    uint8_t propRot[CCELLS][CCELLS];
    uint8_t pool[CCELLS][CCELLS];
    bool built = false;
    Mesh meshes[5] = {};   // 0 floor, 1 ceiling, 2 walls, 3 props, 4 water
};

struct AABB { float minx, minz, maxx, maxz; };

static int fdiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
static int cellOf(float x) { return (int)floorf(x / CELL); }

struct World {
    unsigned seed = 1337;
    int level = 0;           // 0 = Level 0, 1 = Level 1 (garage), 2 = Poolrooms
    float wallH = 3.0f;
    bool exitTest = false;   // BACKROOMS_EXITS env: exits everywhere, for visual testing
    std::unordered_map<uint64_t, ChunkData> chunks;

    static uint64_t key(int cx, int cz) { return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz; }

    ChunkData &data(int cx, int cz) {
        uint64_t k = key(cx, cz);
        auto it = chunks.find(k);
        if (it != chunks.end()) return it->second;
        ChunkData &d = chunks[k];
        generate(d, cx, cz);
        return d;
    }

    void generate(ChunkData &d, int cx, int cz) {
        memset(d.wallN, 0, sizeof(d.wallN));
        memset(d.wallW, 0, sizeof(d.wallW));
        memset(d.pillar, 0, sizeof(d.pillar));
        memset(d.pool, 0, sizeof(d.pool));
        uint64_t k = key(cx, cz);
        Rng rng(hash64(k ^ ((uint64_t)seed + (uint64_t)level * 0x51ED270Bu) * 0x9E3779B97F4A7C15ULL));
        int nseg = level == 0 ? 9 + rng.ri(0, 5) : level == 1 ? 5 + rng.ri(0, 4) : 4 + rng.ri(0, 3);
        int lenBase = level == 0 ? 4 : 6;
        for (int s = 0; s < nseg; s++) {
            bool horiz = rng.next() & 1;
            int len = lenBase + rng.ri(0, 8);
            int a = rng.ri(0, CCELLS - 1), b = rng.ri(0, CCELLS - 1);
            int end = std::min(CCELLS - 1, a + len);
            int doorAt = (rng.f01() < 0.8f) ? a + 1 + rng.ri(0, std::max(0, end - a - 2)) : -1;
            for (int i = a; i <= end; i++) {
                if (i == doorAt) continue;
                if (horiz) d.wallN[i][b] = 1; else d.wallW[b][i] = 1;
            }
        }
        int np = level == 0 ? 4 + rng.ri(0, 5) : level == 1 ? 10 + rng.ri(0, 8) : 2 + rng.ri(0, 3);
        for (int i = 0; i < np; i++) d.pillar[rng.ri(0, CCELLS - 1)][rng.ri(0, CCELLS - 1)] = 1;
        memset(d.prop, 0, sizeof(d.prop));
        memset(d.propRot, 0, sizeof(d.propRot));
        int npr = level == 0 ? 5 + rng.ri(0, 7) : level == 1 ? 8 + rng.ri(0, 8) : 0;
        for (int i = 0; i < npr; i++) {
            int a = rng.ri(0, CCELLS - 1), b = rng.ri(0, CCELLS - 1);
            if (d.pillar[a][b] || d.prop[a][b]) continue;
            float f = rng.f01();
            if (level == 1) d.prop[a][b] = f < 0.55f ? 1 : f < 0.80f ? 2 : 3;  // warehouse: no fallen tiles
            else d.prop[a][b] = f < 0.40f ? 1 : f < 0.65f ? 2 : f < 0.85f ? 3 : 4;
            d.propRot[a][b] = (uint8_t)rng.ri(0, 3);
            // boxes like company: sometimes a neighbouring stack
            if (d.prop[a][b] == 1 && a + 1 < CCELLS && rng.f01() < 0.4f && !d.pillar[a + 1][b] && !d.prop[a + 1][b]) {
                d.prop[a + 1][b] = 1; d.propRot[a + 1][b] = (uint8_t)rng.ri(0, 3);
            }
        }
        if (level == 2) {   // sunken pools where noise blobs say so, never under walls
            for (int i = 1; i < CCELLS - 1; i++) for (int kk = 1; kk < CCELLS - 1; kk++) {
                if (d.pillar[i][kk]) continue;
                if (d.wallN[i][kk] || d.wallW[i][kk] || d.wallN[i][kk + 1] || d.wallW[i + 1][kk]) continue;
                float gxc = (float)(cx * CCELLS + i), gzc = (float)(cz * CCELLS + kk);
                if (fbm2(gxc * 0.11f, gzc * 0.11f, seed ^ 0x77AAu, 3) > 0.565f) d.pool[i][kk] = 1;
            }
        }
        // rare exit door carved into an existing wall run
        if (hash64(k ^ 0xE717ULL ^ (uint64_t)seed) % (exitTest ? 1 : 16) == 0) {
            bool placed = false;
            for (int i = 1; i < CCELLS - 1 && !placed; i++)
                for (int kk = 0; kk < CCELLS && !placed; kk++)
                    if (d.wallN[i][kk] == 1 && d.wallN[i - 1][kk] == 1 && d.wallN[i + 1][kk] == 1) {
                        d.wallN[i][kk] = 2; placed = true;
                    }
            for (int i = 0; i < CCELLS && !placed; i++)
                for (int kk = 1; kk < CCELLS - 1 && !placed; kk++)
                    if (d.wallW[i][kk] == 1 && d.wallW[i][kk - 1] == 1 && d.wallW[i][kk + 1] == 1) {
                        d.wallW[i][kk] = 2; placed = true;
                    }
        }
        if (cx == 0 && cz == 0) {   // clear spawn room
            for (int i = 5; i <= 10; i++) for (int kk = 5; kk <= 10; kk++)
                d.wallN[i][kk] = d.wallW[i][kk] = d.pillar[i][kk] = d.prop[i][kk] = d.pool[i][kk] = 0;
            if (exitTest) { d.wallN[6][11] = 1; d.wallN[7][11] = 2; d.wallN[8][11] = 1; }
        }
    }

    uint8_t wallNVal(int ci, int ck) {
        int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
        return data(cx, cz).wallN[ci - cx * CCELLS][ck - cz * CCELLS];
    }
    uint8_t wallWVal(int ci, int ck) {
        int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
        return data(cx, cz).wallW[ci - cx * CCELLS][ck - cz * CCELLS];
    }
    bool pillarAt(int ci, int ck) {
        int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
        return data(cx, cz).pillar[ci - cx * CCELLS][ck - cz * CCELLS] != 0;
    }
    uint8_t propAt(int ci, int ck) {
        int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
        return data(cx, cz).prop[ci - cx * CCELLS][ck - cz * CCELLS];
    }
    bool poolAt(int ci, int ck) {
        if (level != 2) return false;
        int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
        return data(cx, cz).pool[ci - cx * CCELLS][ck - cz * CCELLS] != 0;
    }

    // rotated prop box: 4 sides + top, one UV region for sides, another for the top
    static void addPropBox(MB &mb, float cx, float cz, float yaw, float hx, float hz, float y0, float y1,
                           float u0, float v0, float u1, float v1,
                           float tu0, float tv0, float tu1, float tv1) {
        float ca = cosf(yaw), sa = sinf(yaw);
        auto pt = [&](float lx, float lz) { return Vector3{ cx + lx * ca - lz * sa, 0, cz + lx * sa + lz * ca }; };
        Vector3 corners[5] = { pt(-hx, -hz), pt(hx, -hz), pt(hx, hz), pt(-hx, hz), pt(-hx, -hz) };
        for (int f = 0; f < 4; f++) {
            Vector3 a = corners[f], b = corners[f + 1];
            Vector3 n = { b.z - a.z, 0, -(b.x - a.x) };
            float nl = sqrtf(n.x * n.x + n.z * n.z);
            n.x /= nl; n.z /= nl;
            float mx = (a.x + b.x) * 0.5f - cx, mz = (a.z + b.z) * 0.5f - cz;
            if (n.x * mx + n.z * mz < 0) { n.x = -n.x; n.z = -n.z; }
            mb.quad({a.x,y0,a.z},{b.x,y0,b.z},{b.x,y1,b.z},{a.x,y1,a.z}, n,
                    {u0,v1},{u1,v1},{u1,v0},{u0,v0}, WHITE);
        }
        mb.quad({corners[0].x,y1,corners[0].z},{corners[1].x,y1,corners[1].z},
                {corners[2].x,y1,corners[2].z},{corners[3].x,y1,corners[3].z},{0,1,0},
                {tu0,tv0},{tu1,tv0},{tu1,tv1},{tu0,tv1}, WHITE);
    }

    static void addBoxSides(MB &mb, float x0, float y0, float z0, float x1, float y1, float z1, bool bottomFace = false) {
        Color w = WHITE;
        float va = 1 - y0 / 3, vb = 1 - y1 / 3;
        mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{0,0,-1},{x0/3,va},{x1/3,va},{x1/3,vb},{x0/3,vb},w);
        mb.quad({x1,y0,z1},{x0,y0,z1},{x0,y1,z1},{x1,y1,z1},{0,0,1},{x1/3,va},{x0/3,va},{x0/3,vb},{x1/3,vb},w);
        mb.quad({x0,y0,z1},{x0,y0,z0},{x0,y1,z0},{x0,y1,z1},{-1,0,0},{z1/3,va},{z0/3,va},{z0/3,vb},{z1/3,vb},w);
        mb.quad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0},{1,0,0},{z0/3,va},{z1/3,va},{z1/3,vb},{z0/3,vb},w);
        if (bottomFace)
            mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},{0,-1,0},{x0/3,z0/3},{x1/3,z0/3},{x1/3,z1/3},{x0/3,z1/3},w);
    }

    void ensureMesh(int cx, int cz) {
        ChunkData &d = data(cx, cz);
        if (d.built) return;
        MB fl, ce, wa, pr, wt;
        float wx = cx * CHUNK, wz = cz * CHUNK;
        Color wcol = WHITE;
        if (level == 2) {   // per-cell floor: pool basins sit 0.6m down, with tiled skirts
            Color water = { 115, 190, 217, 128 };
            for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
                float gx = wx + i * CELL, gz = wz + kk * CELL;
                float fy = d.pool[i][kk] ? -0.6f : 0.0f;
                fl.quad({gx,fy,gz},{gx+CELL,fy,gz},{gx+CELL,fy,gz+CELL},{gx,fy,gz+CELL},{0,1,0},
                        {gx/2,gz/2},{(gx+CELL)/2,gz/2},{(gx+CELL)/2,(gz+CELL)/2},{gx/2,(gz+CELL)/2},wcol);
                if (!d.pool[i][kk]) continue;
                auto dry = [&](int a, int b) { return a < 0 || a >= CCELLS || b < 0 || b >= CCELLS || !d.pool[a][b]; };
                if (dry(i, kk-1)) fl.quad({gx,0,gz},{gx+CELL,0,gz},{gx+CELL,-0.6f,gz},{gx,-0.6f,gz},{0,0,1},
                        {gx/2,0},{(gx+CELL)/2,0},{(gx+CELL)/2,0.3f},{gx/2,0.3f},wcol);
                if (dry(i, kk+1)) fl.quad({gx,0,gz+CELL},{gx+CELL,0,gz+CELL},{gx+CELL,-0.6f,gz+CELL},{gx,-0.6f,gz+CELL},{0,0,-1},
                        {gx/2,0},{(gx+CELL)/2,0},{(gx+CELL)/2,0.3f},{gx/2,0.3f},wcol);
                if (dry(i-1, kk)) fl.quad({gx,0,gz},{gx,0,gz+CELL},{gx,-0.6f,gz+CELL},{gx,-0.6f,gz},{1,0,0},
                        {gz/2,0},{(gz+CELL)/2,0},{(gz+CELL)/2,0.3f},{gz/2,0.3f},wcol);
                if (dry(i+1, kk)) fl.quad({gx+CELL,0,gz},{gx+CELL,0,gz+CELL},{gx+CELL,-0.6f,gz+CELL},{gx+CELL,-0.6f,gz},{-1,0,0},
                        {gz/2,0},{(gz+CELL)/2,0},{(gz+CELL)/2,0.3f},{gz/2,0.3f},wcol);
                wt.quad({gx,-0.12f,gz},{gx+CELL,-0.12f,gz},{gx+CELL,-0.12f,gz+CELL},{gx,-0.12f,gz+CELL},{0,1,0},
                        {0,0},{1,0},{1,1},{0,1}, water);
            }
        } else {
            fl.quad({wx,0,wz},{wx+CHUNK,0,wz},{wx+CHUNK,0,wz+CHUNK},{wx,0,wz+CHUNK},{0,1,0},
                    {wx/2,wz/2},{(wx+CHUNK)/2,wz/2},{(wx+CHUNK)/2,(wz+CHUNK)/2},{wx/2,(wz+CHUNK)/2},wcol);
        }
        ce.quad({wx,wallH,wz},{wx,wallH,wz+CHUNK},{wx+CHUNK,wallH,wz+CHUNK},{wx+CHUNK,wallH,wz},{0,-1,0},
                {wx/2,wz/2},{wx/2,(wz+CHUNK)/2},{(wx+CHUNK)/2,(wz+CHUNK)/2},{(wx+CHUNK)/2,wz/2},wcol);
        // light panels on the global grid (emissive: alpha=0); spacing varies per level
        Color panel = {255,255,255,0};
        float ls = level == 1 ? 12.0f : 8.0f;
        int g0x = (int)floorf(wx / ls), g1x = (int)floorf((wx + CHUNK) / ls);
        int g0z = (int)floorf(wz / ls), g1z = (int)floorf((wz + CHUNK) / ls);
        for (int gx = g0x; gx <= g1x; gx++)
            for (int gz = g0z; gz <= g1z; gz++) {
                float lx = gx * ls + ls * 0.5f, lz = gz * ls + ls * 0.5f, hp = 0.62f, yq = wallH - 0.02f;
                if (lx < wx || lx >= wx + CHUNK || lz < wz || lz >= wz + CHUNK) continue;
                ce.quad({lx-hp,yq,lz-hp},{lx-hp,yq,lz+hp},{lx+hp,yq,lz+hp},{lx+hp,yq,lz-hp},{0,-1,0},
                        {0,0},{0,1},{1,1},{1,0},panel);
            }
        ChunkData &dd = d;
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            float gx = wx + i * CELL, gz = wz + kk * CELL;
            uint8_t nv = dd.wallN[i][kk];
            if (nv == 1) addBoxSides(wa, gx - WT, 0, gz - WT, gx + CELL + WT, wallH, gz + WT);
            else if (nv == 2) {   // exit doorway on x-running wall
                addBoxSides(wa, gx - WT, 0, gz - WT, gx + 0.35f, wallH, gz + WT);
                addBoxSides(wa, gx + 1.65f, 0, gz - WT, gx + CELL + WT, wallH, gz + WT);
                addBoxSides(wa, gx + 0.35f, 2.3f, gz - WT, gx + 1.65f, wallH, gz + WT, true);
                Color glow = {255, 248, 225, 70};
                wa.quad({gx+0.35f,0,gz},{gx+1.65f,0,gz},{gx+1.65f,2.3f,gz},{gx+0.35f,2.3f,gz},{0,0,-1},
                        {0,1},{1,1},{1,0},{0,0},glow);
            }
            uint8_t wv = dd.wallW[i][kk];
            if (wv == 1) addBoxSides(wa, gx - WT, 0, gz - WT, gx + WT, wallH, gz + CELL + WT);
            else if (wv == 2) {   // exit doorway on z-running wall
                addBoxSides(wa, gx - WT, 0, gz - WT, gx + WT, wallH, gz + 0.35f);
                addBoxSides(wa, gx - WT, 0, gz + 1.65f, gx + WT, wallH, gz + CELL + WT);
                addBoxSides(wa, gx - WT, 2.3f, gz + 0.35f, gx + WT, wallH, gz + 1.65f, true);
                Color glow = {255, 248, 225, 70};
                wa.quad({gx,0,gz+0.35f},{gx,0,gz+1.65f},{gx,2.3f,gz+1.65f},{gx,2.3f,gz+0.35f},{1,0,0},
                        {0,1},{1,1},{1,0},{0,0},glow);
            }
            if (dd.pillar[i][kk])
                addBoxSides(wa, gx + 0.42f, 0, gz + 0.42f, gx + 1.58f, wallH, gz + 1.58f);
            if (dd.prop[i][kk]) {
                float pcx = gx + 1.0f, pcz = gz + 1.0f;
                float rot = dd.propRot[i][kk] * 1.5708f;
                uint32_t h = ih(cx * CCELLS + i, cz * CCELLS + kk, seed ^ 0xB0B5u);
                float r1 = (h & 0xFF) / 255.0f, r2 = ((h >> 8) & 0xFF) / 255.0f, r3 = ((h >> 16) & 0xFF) / 255.0f;
                // UV regions of the prop atlas
                const float CU0=0.02f, CV0=0.02f, CU1=0.48f, CV1=0.98f;       // cardboard
                const float FU0=0.52f, FV0=0.02f, FU1=0.98f, FV1=0.48f;       // cabinet front
                const float MU0=0.52f, MV0=0.52f, MU1=0.98f, MV1=0.98f;       // plain metal
                switch (dd.prop[i][kk]) {
                case 1: {   // box stack
                    float bh = 0.55f + r1 * 0.2f, bhx = 0.34f + r2 * 0.08f;
                    addPropBox(pr, pcx + (r3 - 0.5f) * 0.5f, pcz + (r1 - 0.5f) * 0.5f, rot + r2,
                               bhx, bhx, 0, bh, CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1);
                    if (r2 > 0.35f)   // second box on top, skewed
                        addPropBox(pr, pcx + (r3 - 0.5f) * 0.5f + 0.06f, pcz + (r1 - 0.5f) * 0.5f - 0.05f,
                                   rot + r2 + 0.5f, bhx * 0.8f, bhx * 0.8f, bh, bh + 0.5f,
                                   CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1);
                    if (r1 > 0.6f)    // third box beside
                        addPropBox(pr, pcx + 0.62f, pcz + 0.3f, rot + r3 * 2, 0.27f, 0.27f, 0, 0.5f,
                                   CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1);
                    break;
                }
                case 2:     // filing cabinet
                    addPropBox(pr, pcx, pcz, rot, 0.26f, 0.34f, 0, 1.32f,
                               FU0, FV0, FU1, FV1, MU0, MV0, MU1, MV1);
                    break;
                case 3: {   // folding table
                    float ty = 0.72f;
                    addPropBox(pr, pcx, pcz, rot, 0.62f, 0.40f, ty - 0.04f, ty,
                               MU0, MV0, MU1, MV1, MU0, MV0, MU1, MV1);
                    float ca = cosf(rot), sa = sinf(rot);
                    for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2) {
                        float ox = lx * 0.54f, oz = lz * 0.32f;
                        addPropBox(pr, pcx + ox * ca - oz * sa, pcz + ox * sa + oz * ca, rot,
                                   0.03f, 0.03f, 0, ty - 0.04f, MU0, MV0, MU1, MV1, MU0, MV0, MU1, MV1);
                    }
                    break;
                }
                case 4: {   // collapsed ceiling: dark hole above, tile leaning below, debris
                    Color hole = { 12, 11, 9, 51 };
                    ce.quad({pcx-0.85f,2.994f,pcz-0.85f},{pcx-0.85f,2.994f,pcz+0.85f},
                            {pcx+0.85f,2.994f,pcz+0.85f},{pcx+0.85f,2.994f,pcz-0.85f},{0,-1,0},
                            {0,0},{0,1},{1,1},{1,0}, hole);
                    float ca = cosf(rot), sa = sinf(rot);
                    float bx0 = pcx - 0.58f * ca, bz0 = pcz - 0.58f * sa;   // base edge on floor
                    float tx = pcx + 0.35f * ca, tz = pcz + 0.35f * sa;     // top edge, lifted
                    Vector3 a = { bx0 - 0.58f * sa, 0.02f, bz0 + 0.58f * ca };
                    Vector3 b = { bx0 + 0.58f * sa, 0.02f, bz0 - 0.58f * ca };
                    Vector3 c2 = { tx + 0.58f * sa, 0.42f, tz - 0.58f * ca };
                    Vector3 dq = { tx - 0.58f * sa, 0.42f, tz + 0.58f * ca };
                    ce.quad(a, b, c2, dq, { -ca * 0.5f, 0.87f, -sa * 0.5f },
                            {0.05f,0.45f},{0.45f,0.45f},{0.45f,0.05f},{0.05f,0.05f}, WHITE);
                    Color deb = { 110, 105, 95, 255 };
                    ce.quad({pcx+0.4f,0.012f,pcz+0.5f},{pcx+0.75f,0.012f,pcz+0.55f},
                            {pcx+0.7f,0.012f,pcz+0.85f},{pcx+0.38f,0.012f,pcz+0.8f},{0,1,0},
                            {0.1f,0.1f},{0.3f,0.1f},{0.3f,0.3f},{0.1f,0.3f}, deb);
                    ce.quad({pcx-0.7f,0.012f,pcz-0.35f},{pcx-0.45f,0.012f,pcz-0.42f},
                            {pcx-0.4f,0.012f,pcz-0.2f},{pcx-0.68f,0.012f,pcz-0.15f},{0,1,0},
                            {0.3f,0.3f},{0.45f,0.3f},{0.45f,0.45f},{0.3f,0.45f}, deb);
                    break;
                }
                }
            }
        }
        d.meshes[0] = fl.bake();
        d.meshes[1] = ce.bake();
        d.meshes[2] = wa.bake();
        d.meshes[3] = pr.bake();
        d.meshes[4] = wt.bake();
        d.built = true;
    }

    int gatherCellAABBs(int ci, int ck, AABB *out, int cap, int cnt, bool includeProps = true) {
        float x0 = ci * CELL, z0 = ck * CELL;
        if (cnt < cap && wallNVal(ci, ck) == 1) out[cnt++] = { x0 - WT, z0 - WT, x0 + CELL + WT, z0 + WT };
        if (cnt < cap && wallWVal(ci, ck) == 1) out[cnt++] = { x0 - WT, z0 - WT, x0 + WT, z0 + CELL + WT };
        if (cnt < cap && pillarAt(ci, ck)) out[cnt++] = { x0 + 0.42f, z0 + 0.42f, x0 + 1.58f, z0 + 1.58f };
        if (includeProps && cnt < cap) {
            switch (propAt(ci, ck)) {
            case 1: out[cnt++] = { x0 + 0.35f, z0 + 0.35f, x0 + 1.65f, z0 + 1.65f }; break;  // boxes
            case 2: out[cnt++] = { x0 + 0.60f, z0 + 0.60f, x0 + 1.40f, z0 + 1.40f }; break;  // cabinet
            case 3: out[cnt++] = { x0 + 0.32f, z0 + 0.32f, x0 + 1.68f, z0 + 1.68f }; break;  // table
            }
        }
        return cnt;
    }

    void collideCircle(float &px, float &pz, float r) {
        AABB boxes[48];
        int cnt = 0;
        int ci = cellOf(px), ck = cellOf(pz);
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++)
                cnt = gatherCellAABBs(ci + dx, ck + dz, boxes, 48, cnt);
        for (int pass = 0; pass < 3; pass++)
            for (int i = 0; i < cnt; i++) {
                const AABB &b = boxes[i];
                float cx = clampf(px, b.minx, b.maxx), cz = clampf(pz, b.minz, b.maxz);
                float dx = px - cx, dz = pz - cz, d2 = dx * dx + dz * dz;
                if (d2 >= r * r) continue;
                if (d2 < 1e-8f) {   // centre inside box: push out along smallest penetration
                    float dl = px - b.minx, dr = b.maxx - px, dn = pz - b.minz, ds = b.maxz - pz;
                    float m = std::min(std::min(dl, dr), std::min(dn, ds));
                    if (m == dl) px = b.minx - r; else if (m == dr) px = b.maxx + r;
                    else if (m == dn) pz = b.minz - r; else pz = b.maxz + r;
                } else {
                    float d = sqrtf(d2), push = (r - d) / d;
                    px += dx * push; pz += dz * push;
                }
            }
    }

    bool lineOfSight(float ax, float az, float bx, float bz) {
        float dx = bx - ax, dz = bz - az;
        float dist = sqrtf(dx * dx + dz * dz);
        int steps = (int)(dist / 0.22f) + 1;
        for (int s = 1; s < steps; s++) {
            float t = (float)s / steps, x = ax + dx * t, z = az + dz * t;
            AABB boxes[48];
            int cnt = 0, ci = cellOf(x), ck = cellOf(z);
            for (int ddx = -1; ddx <= 1; ddx++)
                for (int ddz = -1; ddz <= 1; ddz++)
                    cnt = gatherCellAABBs(ci + ddx, ck + ddz, boxes, 48, cnt, false);  // props don't block sight
            for (int i = 0; i < cnt; i++)
                if (x > boxes[i].minx && x < boxes[i].maxx && z > boxes[i].minz && z < boxes[i].maxz) return false;
        }
        return true;
    }

    Vector2 findOpenSpot(float x, float z) {
        int ci0 = cellOf(x), ck0 = cellOf(z);
        for (int r = 0; r < 14; r++)
            for (int dx = -r; dx <= r; dx++)
                for (int dz = -r; dz <= r; dz++) {
                    if (std::max(abs(dx), abs(dz)) != r) continue;
                    if (!pillarAt(ci0 + dx, ck0 + dz) && propAt(ci0 + dx, ck0 + dz) == 0 && !poolAt(ci0 + dx, ck0 + dz))
                        return { (ci0 + dx) * CELL + 1.0f, (ck0 + dz) * CELL + 1.0f };
                }
        return { x, z };
    }

    void unloadFar(int pcx, int pcz, int radius) {
        for (auto it = chunks.begin(); it != chunks.end();) {
            int cx = (int)(int32_t)(it->first >> 32), cz = (int)(int32_t)(it->first & 0xFFFFFFFF);
            if (abs(cx - pcx) > radius || abs(cz - pcz) > radius) {
                if (it->second.built)
                    for (int i = 0; i < 5; i++)
                        if (it->second.meshes[i].vertexCount > 0) UnloadMesh(it->second.meshes[i]);
                it = chunks.erase(it);
            } else ++it;
        }
    }

    void unloadAll() {
        for (auto &kv : chunks)
            if (kv.second.built)
                for (int i = 0; i < 5; i++)
                    if (kv.second.meshes[i].vertexCount > 0) UnloadMesh(kv.second.meshes[i]);
        chunks.clear();
    }
};

// -------------------------------------------------- CPU-side light estimate
// mirrors the shader's hash so billboard tinting matches the room lighting
static float lhashCPU(float gx, float gz) {
    float v = sinf(gx * 127.1f + gz * 311.7f) * 43758.5453f;
    return v - floorf(v);
}
static float lightAtCPU(float x, float y, float z, float blackout,
                        float ls, float ly, float dead, float mul, float ambLum) {
    float bx = floorf((x - ls * 0.5f) / ls + 0.5f), bz = floorf((z - ls * 0.5f) / ls + 0.5f);
    float sum = 0;
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        float gx = bx + dx, gz = bz + dz;
        float h = lhashCPU(gx, gz);
        if (h < dead) continue;
        float lx = gx * ls + ls * 0.5f, lz = gz * ls + ls * 0.5f;
        float d2 = (lx - x) * (lx - x) + (ly - y) * (ly - y) + (lz - z) * (lz - z);
        sum += 1.0f / (1.0f + 0.055f * d2) * 1.8f * mul;
    }
    return clampf(sum * blackout + ambLum * 2.2f, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- levels
struct LevelCfg {
    float wallH, ls, dead, lightMul, fogDen;
    Vector3 lightCol, amb, fogCol;
    const char *name;
};
static const int NLEVELS = 4;
static const LevelCfg LEVELS[NLEVELS] = {
    { 3.0f,  8.0f, 0.06f, 1.00f, 0.055f, {1.00f,0.94f,0.74f}, {0.045f,0.042f,0.030f}, {0.140f,0.125f,0.070f}, "LEVEL 0" },
    { 4.2f, 12.0f, 0.30f, 0.85f, 0.075f, {0.72f,0.80f,0.95f}, {0.016f,0.017f,0.022f}, {0.018f,0.020f,0.026f}, "LEVEL 1" },
    { 3.6f,  8.0f, 0.02f, 0.90f, 0.038f, {1.00f,1.00f,0.97f}, {0.30f,0.33f,0.36f},    {0.36f,0.42f,0.47f},    "THE POOLROOMS" },
    { 3.0f,  8.0f, 0.45f, 0.80f, 0.095f, {1.00f,0.22f,0.15f}, {0.030f,0.008f,0.006f}, {0.055f,0.010f,0.008f}, "THE RED HALLS" },
};

// ---------------------------------------------------------------- entity
enum class EState { Hidden, Stalk, Chase };
struct Entity {
    EState st = EState::Hidden;
    float x = 0, z = 0;
    double nextSpawn = 12.0;
    float gaze = 0, life = 0, unseen = 0;
};

// ---------------------------------------------------------------- main
int main() {
    const char *shotPath = getenv("BACKROOMS_SHOT");
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1440, 850, "THE BACKROOMS — Level 0");
    SetExitKey(KEY_NULL);
    SetWindowMinSize(640, 400);
    InitAudioDevice();
    rlDisableBackfaceCulling();

    Texture2D texEntity = makeEntityTex();
    Texture2D texProps = makePropsTex();
    // per-level surface sets: [floor, ceiling, walls]
    Texture2D floorTexs[NLEVELS] = { makeCarpetTex(), makeConcreteFloorTex(), makeTileTex(), Texture2D{} };
    Texture2D ceilTexs[NLEVELS]  = { makeCeilingTex(), makeConcreteCeilTex(), floorTexs[2], Texture2D{} };
    Texture2D wallTexs[NLEVELS]  = { makeWallpaperTex(), makeConcreteWallTex(), floorTexs[2], makeRedBrickTex() };
    floorTexs[3] = floorTexs[1];   // red halls reuse the concrete floor/ceiling in red light
    ceilTexs[3] = ceilTexs[1];

    Shader worldShader = LoadShaderFromMemory(WORLD_VS, WORLD_FS);
    int locTime = GetShaderLocation(worldShader, "uTime");
    int locBlackout = GetShaderLocation(worldShader, "uBlackout");
    int locViewPos = GetShaderLocation(worldShader, "uViewPos");
    int locFlash = GetShaderLocation(worldShader, "uFlash");
    int locFlashDir = GetShaderLocation(worldShader, "uFlashDir");
    int locAmb = GetShaderLocation(worldShader, "uAmb");
    int locFogCol = GetShaderLocation(worldShader, "uFogCol");
    int locFogDen = GetShaderLocation(worldShader, "uFogDen");
    int locLightCol = GetShaderLocation(worldShader, "uLightCol");
    int locLS = GetShaderLocation(worldShader, "uLS");
    int locLY = GetShaderLocation(worldShader, "uLY");
    int locDead = GetShaderLocation(worldShader, "uDead");
    int locLightMul = GetShaderLocation(worldShader, "uLightMul");
    Shader postShader = LoadShaderFromMemory(NULL, POST_FS);
    int locPTime = GetShaderLocation(postShader, "uTime");
    int locPFear = GetShaderLocation(postShader, "uFear");

    Material mats[4];
    for (int i = 0; i < 4; i++) {
        mats[i] = LoadMaterialDefault();
        mats[i].shader = worldShader;
    }
    mats[3].maps[MATERIAL_MAP_DIFFUSE].texture = texProps;

    Sound steps[4];
    for (int i = 0; i < 4; i++) steps[i] = makeFootstep(100 + i * 17);
    Sound splashes[2] = { makeSplash(1, false), makeSplash(2, false) };
    Sound sndBigSplash = makeSplash(3, true);
    Sound sndClick = makeClick();
    Sound sndScare = makeJumpscare();
    Sound sndWin = makeWinChime();

    AudioSynth synth;
    synth.init();

    World world;
    world.seed = shotPath ? 1337u : (unsigned)time(nullptr);
    world.exitTest = getenv("BACKROOMS_EXITS") != nullptr;

    Rng grng(hash64(world.seed ^ 0xABCDEF));

    // player
    Vector2 sp = world.findOpenSpot(15, 15);
    float px = sp.x, pz = sp.y;
    float yaw = 0.8f, pitch = 0.0f;
    if (const char *posEnv = getenv("BACKROOMS_POS")) {   // testing: "x,z,yaw"
        float ex, ez, ey;
        if (sscanf(posEnv, "%f,%f,%f", &ex, &ez, &ey) == 3) {
            Vector2 s2 = world.findOpenSpot(ex, ez);
            px = s2.x; pz = s2.y; yaw = ey;
        }
    }
    float velx = 0, velz = 0;
    float py = 0, vy = 0;     // feet height (0 = dry floor, -0.6 = pool bottom)
    bool grounded = true;
    float stamina = 1.0f, fov = 70.0f, stepAcc = 0, bobPhase = 0;
    bool flashOn = false;
    float flashCur = 0;
    const float PR = 0.34f;   // player radius

    // state
    int level = 0;
    Entity ent;
    double nextBlackout = 40 + grng.f01() * 60;
    double blackoutEnd = -1;
    float blackoutCur = 1.0f;
    float fear = 0.0f;
    float caughtT = 0, escapeT = 0;
    int caughtCount = 0, escapeCount = 0;
    float distWalked = 0;
    double runStart = GetTime();
    bool debugHud = false;
    int frame = 0;
    RenderTexture2D rt = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    auto applyLevel = [&](int lv) {
        level = lv;
        const LevelCfg &c = LEVELS[lv];
        world.unloadAll();
        world.level = lv;
        world.wallH = c.wallH;
        for (int i = 0; i < 3; i++) {
            Texture2D t = i == 0 ? floorTexs[lv] : i == 1 ? ceilTexs[lv] : wallTexs[lv];
            mats[i].maps[MATERIAL_MAP_DIFFUSE].texture = t;
        }
        float ly = c.wallH - 0.12f;
        SetShaderValue(worldShader, locAmb, &c.amb, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, locFogCol, &c.fogCol, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, locFogDen, &c.fogDen, SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldShader, locLightCol, &c.lightCol, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, locLS, &c.ls, SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldShader, locLY, &ly, SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldShader, locDead, &c.dead, SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldShader, locLightMul, &c.lightMul, SHADER_UNIFORM_FLOAT);
        synth.tHum = lv == 0 ? 1.0f : 0.15f;
        synth.tDrone = lv == 1 ? 1.0f : 0.0f;
        synth.tWater = lv == 2 ? 1.0f : 0.0f;
        nextBlackout = lv == 2 ? 1e18 : GetTime() + 30 + grng.f01() * 60;   // no blackouts in the poolrooms
        blackoutEnd = -1;
        SetWindowTitle(TextFormat("THE BACKROOMS — %s", c.name));
    };
    applyLevel(0);
    if (const char *lvEnv = getenv("BACKROOMS_LEVEL")) applyLevel(atoi(lvEnv) % 3);   // testing

    if (!shotPath) DisableCursor();

    while (!WindowShouldClose()) {
        float dt = fminf(GetFrameTime(), 0.05f);
        double now = GetTime();
        frame++;

        if (IsWindowResized()) {
            UnloadRenderTexture(rt);
            rt = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
        }
        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        if (IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_L)) {
            flashOn = !flashOn;
            SetSoundPitch(sndClick, flashOn ? 1.0f : 0.85f);
            PlaySound(sndClick);
        }
        flashCur += ((flashOn ? 1.0f : 0.0f) - flashCur) * fminf(1, 25 * dt);
        if (IsKeyPressed(KEY_F3)) debugHud = !debugHud;
        if (IsKeyPressed(KEY_ESCAPE) && IsCursorHidden()) EnableCursor();
        if (!IsCursorHidden() && !shotPath && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) DisableCursor();

        // ---- look
        if (IsCursorHidden()) {
            Vector2 md = GetMouseDelta();
            yaw += md.x * 0.0030f;
            pitch = clampf(pitch - md.y * 0.0030f, -1.45f, 1.45f);
        }
        Vector3 fwd = { cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw) };
        float f2x = cosf(yaw), f2z = sinf(yaw);
        float r2x = -sinf(yaw), r2z = cosf(yaw);

        // ---- move
        float ix = 0, iz = 0;
        if (IsKeyDown(KEY_W)) { ix += f2x; iz += f2z; }
        if (IsKeyDown(KEY_S)) { ix -= f2x; iz -= f2z; }
        if (IsKeyDown(KEY_D)) { ix += r2x; iz += r2z; }
        if (IsKeyDown(KEY_A)) { ix -= r2x; iz -= r2z; }
        float il = sqrtf(ix * ix + iz * iz);
        bool moving = il > 0.01f;
        if (moving) { ix /= il; iz /= il; }
        bool sprinting = moving && IsKeyDown(KEY_LEFT_SHIFT) && stamina > 0.02f;
        stamina = clampf(stamina + (sprinting ? -dt / 10.0f : dt / 6.0f), 0, 1);
        float groundY = world.poolAt(cellOf(px), cellOf(pz)) ? -0.6f : 0.0f;
        bool inWater = grounded && py < -0.1f;
        float speed = (sprinting ? 6.8f : 3.6f) * (inWater ? 0.55f : 1.0f);
        float tvx = ix * speed, tvz = iz * speed;
        float accel = moving ? 12.0f : 9.0f;
        velx += (tvx - velx) * fminf(1, accel * dt);
        velz += (tvz - velz) * fminf(1, accel * dt);
        px += velx * dt; pz += velz * dt;
        world.collideCircle(px, pz, PR);
        float spd = sqrtf(velx * velx + velz * velz);
        distWalked += spd * dt;

        // jump + pools (groundY recomputed after collision)
        groundY = world.poolAt(cellOf(px), cellOf(pz)) ? -0.6f : 0.0f;
        if (IsKeyPressed(KEY_SPACE) && grounded) { vy = inWater ? 4.3f : 5.6f; grounded = false; }
        if (grounded) {
            if (py > groundY + 0.001f) { grounded = false; vy = 0; }      // walked off a pool edge
            else if (py < groundY - 0.001f) py = groundY;                 // wade up out of the pool
        }
        if (!grounded) {
            vy -= 20.0f * dt;
            py += vy * dt;
            if (py <= groundY) {
                py = groundY; grounded = true;
                if (groundY < -0.1f) { SetSoundVolume(sndBigSplash, 0.7f); PlaySound(sndBigSplash); }
                else {
                    Sound &s = steps[grng.ri(0, 3)];    // landing thud
                    SetSoundPitch(s, 0.62f + grng.f01() * 0.1f);
                    SetSoundVolume(s, clampf(-vy * 0.14f, 0.3f, 0.85f));
                    PlaySound(s);
                }
                vy = 0;
            }
        }

        // head bob + footsteps (grounded only)
        float bobAmt = clampf(spd / 5.3f, 0, 1) * (grounded ? 1.0f : 0.0f);
        stepAcc += spd * dt * (grounded ? 1.0f : 0.0f);
        bobPhase += spd * dt * 1.65f;
        float eyeY = 1.62f + py + sinf(bobPhase * 3.14159f) * 0.045f * bobAmt;
        float stepLen = 1.85f + speed * 0.13f;
        if (stepAcc > stepLen) {
            stepAcc -= stepLen;
            Sound &s = inWater ? splashes[grng.ri(0, 1)] : steps[grng.ri(0, 3)];
            SetSoundPitch(s, 0.9f + grng.f01() * 0.22f);
            SetSoundVolume(s, (0.35f + 0.3f * bobAmt) * (inWater ? 1.4f : 1.0f));
            PlaySound(s);
        }
        float fovT = sprinting ? 79.0f : 70.0f;
        fov += (fovT - fov) * fminf(1, 6 * dt);

        // ---- blackout events
        bool blackout = now < blackoutEnd;
        if (!blackout && now > nextBlackout) {
            blackoutEnd = now + 2.5 + grng.f01() * 4.0;
            nextBlackout = blackoutEnd + 45 + grng.f01() * 75;
            if (grng.f01() < 0.45f && ent.st == EState::Hidden)
                ent.nextSpawn = blackoutEnd + 1.5;   // something arrives in the dark
            blackout = true;
        }
        blackoutCur += ((blackout ? 0.02f : 1.0f) - blackoutCur) * fminf(1, 18 * dt);
        synth.humTarget = blackout ? 0.12f : 1.0f;

        // ---- entity
        if (shotPath && frame == 300 && ent.st == EState::Hidden) {   // autotest: force a visible spawn
            Vector2 spot = world.findOpenSpot(px + fwd.x * 8, pz + fwd.z * 8);
            ent.x = spot.x; ent.z = spot.y;
            ent.st = EState::Stalk; ent.gaze = -100; ent.life = 0; ent.unseen = 0;
        }
        float fearT = 0.06f;
        float entDist = 1e9f;
        bool entVisible = false;
        if (ent.st == EState::Hidden) {
            if (now > ent.nextSpawn) {
                float a = grng.f01() * 6.2831853f;
                float d = 20 + grng.f01() * 10;
                Vector2 spot = world.findOpenSpot(px + cosf(a) * d, pz + sinf(a) * d);
                ent.x = spot.x; ent.z = spot.y;
                ent.st = EState::Stalk; ent.gaze = 0; ent.life = 0; ent.unseen = 0;
            }
        } else {
            float ex = ent.x - px, ez = ent.z - pz;
            entDist = sqrtf(ex * ex + ez * ez);
            float dirDot = (entDist > 0.01f) ? (fwd.x * ex + fwd.z * ez) / entDist : 1;
            entVisible = entDist < 36 && dirDot > 0.86f && world.lineOfSight(px, pz, ent.x, ent.z);
            if (ent.st == EState::Stalk) {
                ent.life += dt;
                fearT = entVisible ? 0.45f : 0.15f;
                if (entVisible) ent.gaze += dt;
                if (ent.gaze > 1.6f || (entVisible && entDist < 8) || entDist < 3.0f) ent.st = EState::Chase;
                else if (ent.life > 24 && !entVisible) ent.st = EState::Hidden, ent.nextSpawn = now + 18 + grng.f01() * 35;
            }
            if (ent.st == EState::Chase) {
                fearT = entDist < 8 ? 1.0f : 0.8f;
                float chaseSpd = 3.3f + 1.0f * clampf(1 - entDist / 25.0f, 0, 1);
                if (entDist > 0.01f) {
                    ent.x -= ex / entDist * chaseSpd * dt;
                    ent.z -= ez / entDist * chaseSpd * dt;
                }
                world.collideCircle(ent.x, ent.z, 0.38f);
                ent.unseen = entVisible ? 0 : ent.unseen + dt;
                if (ent.unseen > 6 && entDist > 14) ent.st = EState::Hidden, ent.nextSpawn = now + 25 + grng.f01() * 40;
                if (entDist < 1.25f) {   // caught
                    PlaySound(sndScare);
                    caughtT = 2.4f; caughtCount++;
                    float a = grng.f01() * 6.2831853f;
                    Vector2 spot = world.findOpenSpot(px + cosf(a) * 800, pz + sinf(a) * 800);
                    px = spot.x; pz = spot.y; velx = velz = 0;
                    ent.st = EState::Hidden; ent.nextSpawn = now + 30 + grng.f01() * 30;
                    entDist = 1e9f;
                }
            }
        }
        fear += (fearT - fear) * fminf(1, 2.2f * dt);
        synth.growlTarget = (ent.st == EState::Chase) ? 0.75f * clampf(1 - entDist / 35.0f, 0.1f, 1.0f)
                          : (ent.st == EState::Stalk && entVisible) ? 0.22f * clampf(1 - entDist / 35.0f, 0, 1) : 0.0f;
        synth.update();

        // ---- exit doors
        if (escapeT <= 0) {
            int ci = cellOf(px), ck = cellOf(pz);
            for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
                int i = ci + dx, k = ck + dz;
                float doorX = -1, doorZ = -1;
                if (world.wallNVal(i, k) == 2) { doorX = i * CELL + 1.0f; doorZ = k * CELL; }
                else if (world.wallWVal(i, k) == 2) { doorX = i * CELL; doorZ = k * CELL + 1.0f; }
                else continue;
                float ddx = px - doorX, ddz = pz - doorZ;
                if (ddx * ddx + ddz * ddz < 0.72f * 0.72f) {
                    PlaySound(sndWin);
                    escapeT = 6.0f; escapeCount++;
                    applyLevel((level + 1) % 3);      // the exit leads deeper
                    Vector2 spot = world.findOpenSpot(px, pz);
                    px = spot.x; pz = spot.y; velx = velz = 0; py = 0; vy = 0; grounded = true;
                    ent.st = EState::Hidden; ent.nextSpawn = now + 30;
                }
            }
        }
        caughtT = fmaxf(0, caughtT - dt);
        escapeT = fmaxf(0, escapeT - dt);

        // ---- chunk streaming
        int pcx = fdiv(cellOf(px), CCELLS), pcz = fdiv(cellOf(pz), CCELLS);
        int budget = (frame < 3) ? 64 : 4;
        for (int r = 0; r <= 2 && budget > 0; r++)
            for (int dx = -r; dx <= r && budget > 0; dx++)
                for (int dz = -r; dz <= r && budget > 0; dz++) {
                    if (std::max(abs(dx), abs(dz)) != r) continue;
                    ChunkData &d = world.data(pcx + dx, pcz + dz);
                    if (!d.built) { world.ensureMesh(pcx + dx, pcz + dz); budget--; }
                }
        if (frame % 90 == 0) world.unloadFar(pcx, pcz, 5);

        // ---- render 3D scene into rt
        Camera3D cam = {};
        cam.position = { px, eyeY, pz };
        cam.target = Vector3Add(cam.position, fwd);
        cam.up = { 0, 1, 0 };
        cam.fovy = fov;
        cam.projection = CAMERA_PERSPECTIVE;

        float timeF = (float)now;
        Vector3 viewPos = cam.position;
        SetShaderValue(worldShader, locTime, &timeF, SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldShader, locBlackout, &blackoutCur, SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldShader, locViewPos, &viewPos, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, locFlash, &flashCur, SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldShader, locFlashDir, &fwd, SHADER_UNIFORM_VEC3);

        BeginTextureMode(rt);
        ClearBackground(BLACK);
        BeginMode3D(cam);
        Matrix ident = MatrixIdentity();
        for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {
            int cx = pcx + dx, cz = pcz + dz;
            auto it = world.chunks.find(World::key(cx, cz));
            if (it == world.chunks.end() || !it->second.built) continue;
            float ccx = cx * CHUNK + CHUNK / 2 - px, ccz = cz * CHUNK + CHUNK / 2 - pz;
            if (ccx * f2x + ccz * f2z < -24.0f && (ccx * ccx + ccz * ccz) > 24 * 24) continue;
            for (int m = 0; m < 4; m++)
                if (it->second.meshes[m].vertexCount > 0)
                    DrawMesh(it->second.meshes[m], mats[m], ident);
        }
        for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {   // water last (blended)
            auto it = world.chunks.find(World::key(pcx + dx, pcz + dz));
            if (it != world.chunks.end() && it->second.built && it->second.meshes[4].vertexCount > 0)
                DrawMesh(it->second.meshes[4], mats[0], ident);
        }
        if (ent.st != EState::Hidden && entDist < 45) {
            const LevelCfg &c = LEVELS[level];
            float ambLum = (c.amb.x + c.amb.y + c.amb.z) / 3.0f;
            float eg = world.poolAt(cellOf(ent.x), cellOf(ent.z)) ? -0.6f : 0.0f;
            float lum = lightAtCPU(ent.x, eg + 0.95f, ent.z, blackoutCur,
                                   c.ls, c.wallH - 0.12f, c.dead, c.lightMul, ambLum);
            if (flashCur > 0.05f) {   // flashlight picks him out of the dark
                float vx2 = ent.x - px, vz2 = ent.z - pz;
                float d2 = vx2 * vx2 + vz2 * vz2 + 1e-4f, dl = sqrtf(d2);
                float cone = powf(fmaxf((vx2 * fwd.x + vz2 * fwd.z) / dl, 0.0f), 26.0f);
                lum = clampf(lum + flashCur * cone * 7.5f / (1.0f + 0.10f * d2), 0.0f, 1.0f);
            }
            float fogf = expf(-entDist * c.fogDen);
            unsigned char lum8 = cl8(40 + 215 * lum);
            unsigned char al = cl8(255 * clampf(fogf * 1.6f, 0, 1));
            DrawBillboardRec(cam, texEntity, { 0, 0, 128, 256 },
                             { ent.x, eg + 0.98f, ent.z }, { 0.98f, 1.96f }, { lum8, lum8, lum8, al });
        }
        EndMode3D();
        EndTextureMode();

        // ---- post + UI
        BeginDrawing();
        ClearBackground(BLACK);
        SetShaderValue(postShader, locPTime, &timeF, SHADER_UNIFORM_FLOAT);
        SetShaderValue(postShader, locPFear, &fear, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(postShader);
        DrawTextureRec(rt.texture, { 0, 0, (float)rt.texture.width, -(float)rt.texture.height }, { 0, 0 }, WHITE);
        EndShaderMode();

        int sw = GetScreenWidth(), sh = GetScreenHeight();
        double elapsed = now - runStart;

        if (elapsed < 9.0) {   // intro
            float a = 1.0f - clampf((float)elapsed / 3.0f, 0, 1);
            DrawRectangle(0, 0, sw, sh, Fade(BLACK, a));
            float ta = clampf((float)elapsed / 1.5f, 0, 1) * (1.0f - clampf(((float)elapsed - 6.0f) / 3.0f, 0, 1));
            const char *t1 = "L E V E L   0";
            DrawText(t1, sw / 2 - MeasureText(t1, 52) / 2, sh / 3, 52, Fade({ 220, 205, 150, 255 }, ta));
            const char *t2 = "if you're reading this, you've already noclipped";
            DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 3 + 66, 18, Fade({ 160, 150, 110, 255 }, ta * 0.9f));
            const char *t3 = "WASD walk   SHIFT run   SPACE jump   F flashlight   F11 fullscreen   ESC mouse";
            DrawText(t3, sw / 2 - MeasureText(t3, 16) / 2, sh - 60, 16, Fade({ 140, 132, 100, 255 }, ta * 0.8f));
        }
        if (caughtT > 0) {
            DrawRectangle(0, 0, sw, sh, Fade(BLACK, clampf(caughtT / 2.4f * 1.8f, 0, 1)));
            if (caughtT > 0.5f) {
                const char *t = "PIRATE CLARK FOUND YOU";
                DrawText(t, sw / 2 - MeasureText(t, 60) / 2, sh / 2 - 30, 60, { 170, 20, 12, 255 });
                const char *t2 = TextFormat("you wake up somewhere else   ·   %d m wandered   ·   taken %d time%s",
                                            (int)distWalked, caughtCount, caughtCount == 1 ? "" : "s");
                DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 2 + 46, 18, { 120, 90, 80, 255 });
            }
        }
        if (escapeT > 0) {
            float a = clampf(escapeT > 5.4f ? (6.0f - escapeT) / 0.6f : escapeT / 5.4f, 0, 1);
            DrawRectangle(0, 0, sw, sh, Fade(WHITE, a * (escapeT > 5.4f ? 0.9f : 0.12f)));
            const char *t = "YOU FOUND AN EXIT";
            DrawText(t, sw / 2 - MeasureText(t, 48) / 2, sh / 2 - 60, 48, Fade({ 235, 228, 200, 255 }, a));
            const char *t2 = TextFormat("...it leads to %s.  %d m wandered  ·  %d escape%s  ·  %s",
                                        LEVELS[level].name, (int)distWalked, escapeCount, escapeCount == 1 ? "" : "s",
                                        TextFormat("%02d:%02d", (int)elapsed / 60, (int)elapsed % 60));
            DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 2 + 4, 18, Fade({ 180, 170, 140, 255 }, a));
        }
        if (!IsCursorHidden() && !shotPath) {
            const char *t = "click to capture mouse";
            DrawText(t, sw / 2 - MeasureText(t, 20) / 2, sh / 2 + 80, 20, { 200, 190, 150, 200 });
        }
        DrawText(TextFormat("%d", GetFPS()), sw - MeasureText(TextFormat("%d", GetFPS()), 16) - 14, 12, 16, { 190, 180, 140, 150 });
        // persistent flashlight reminder until first use; small state dot after
        static bool everFlashed = false;
        if (flashOn) everFlashed = true;
        if (!everFlashed && elapsed > 9.0)
            DrawText("F — flashlight", sw - MeasureText("F — flashlight", 16) - 16, sh - 28, 16, { 190, 180, 140, 160 });
        else if (flashOn)
            DrawText("[ flashlight ]", sw - MeasureText("[ flashlight ]", 14) - 16, sh - 26, 14, { 235, 225, 180, 120 });
        if (debugHud) {
            DrawText(TextFormat("%d fps  pos(%.0f, %.0f)  chunks %d  entity %s  d=%.0fm",
                                GetFPS(), px, pz, (int)world.chunks.size(),
                                ent.st == EState::Hidden ? "hidden" : ent.st == EState::Stalk ? "STALKING" : "CHASING",
                                entDist > 1e8 ? 0.0f : entDist),
                     12, 12, 18, { 230, 220, 160, 220 });
        }
        EndDrawing();

        if (shotPath && frame == 600) {
            TakeScreenshot(shotPath);
            printf("fps=%d chunks=%d\n", GetFPS(), (int)world.chunks.size());
            break;
        }
    }

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
