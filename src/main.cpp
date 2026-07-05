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
#include <unordered_set>
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

// LEVEL FUN =) — the party decorations share one faded palette
static const Color PARTY[5] = {
    { 206, 64, 58, 255 }, { 222, 172, 62, 255 }, { 84, 142, 198, 255 },
    { 106, 178, 92, 255 }, { 182, 96, 178, 255 },
};

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
uniform float uGloss;
out vec4 finalColor;
float lhash(vec2 g){ return fract(sin(dot(g, vec2(127.1,311.7)))*43758.5453123); }
float lightState(vec2 g){
    float h = lhash(g);
    if (h < uDead) return 0.0;                      // dead tube
    float s = 1.0;
    if (h > 0.93){                                  // faulty tube: occasional gentle stutter
        float fh = fract(h*97.31);
        float gate = fract(sin(floor(uTime*0.45+fh*37.0)*12.9898)*43758.5453);
        if (gate > 0.74){
            float n = fract(sin(uTime*(7.0+fh*10.0) + fh*211.0)*43758.5453);
            s = 0.62 + 0.38*step(0.5, n);
        }
    }
    return s * uBlackout;
}
vec3 roomLight(vec3 P, vec3 N){
    vec2 base = floor((P.xz - uLS*0.5)/uLS + 0.5);
    vec3 light = vec3(0.0);
    vec3 V = normalize(uViewPos - P);
    for (int dx=-1; dx<=1; dx++)
    for (int dz=-1; dz<=1; dz++){
        vec2 g = base + vec2(float(dx), float(dz));
        float st = lightState(g);
        if (st <= 0.001) continue;
        vec3 lp = vec3(g.x*uLS + uLS*0.5, uLY, g.y*uLS + uLS*0.5);
        vec3 ld = lp - P;
        float d2 = dot(ld,ld);
        float atten = 1.0/(1.0 + 0.075*d2);
        vec3 Ln = normalize(ld);
        float ndl = clamp(dot(N, Ln)*0.55 + 0.45, 0.0, 1.0);
        light += uLightCol*(st*atten*ndl*2.0*uLightMul);
        if (uGloss > 0.005){                        // glossy sheen: tile shines, concrete barely
            float sp = pow(max(dot(normalize(Ln + V), N), 0.0), 64.0);
            light += uLightCol*(sp*uGloss*st*atten*3.0);
        }
    }
    // handheld flashlight: cone from the camera along the view direction
    if (uFlash > 0.01){
        vec3 fv = P - uViewPos;
        float fd2 = dot(fv,fv);
        vec3 fn = normalize(fv);
        float cone = pow(max(dot(fn, uFlashDir), 0.0), 26.0);
        float sput = 0.975 + 0.025*fract(sin(floor(uTime*24.0)*12.9898)*43758.5453);
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
            col *= 0.93 + 0.07*sin(fragUV.x*33.0)*sin(fragUV.y*33.0);   // prismatic lens ribs
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
        aOut = fragC.a;                              // lets contact shadows stay translucent
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
    vec2 pxs = 1.0/vec2(textureSize(texture0, 0));   // soft bloom off the fluorescents
    vec3 bl = texture(texture0, uv + pxs*vec2( 3.0, 0.0)).rgb
            + texture(texture0, uv + pxs*vec2(-3.0, 0.0)).rgb
            + texture(texture0, uv + pxs*vec2( 0.0, 3.0)).rgb
            + texture(texture0, uv + pxs*vec2( 0.0,-3.0)).rgb
            + texture(texture0, uv + pxs*vec2( 2.2, 2.2)).rgb
            + texture(texture0, uv + pxs*vec2(-2.2, 2.2)).rgb
            + texture(texture0, uv + pxs*vec2( 2.2,-2.2)).rgb
            + texture(texture0, uv + pxs*vec2(-2.2,-2.2)).rgb;
    c += max(bl*0.125 - 0.60, 0.0)*0.6;
    float g = hh(uv*vec2(1287.0,721.0) + vec2(fract(uTime*13.71)*61.0, fract(uTime*7.31)*83.0)) - 0.5;
    c += g * (0.032 + 0.08*uFear);                   // film grain
    float d = length(dir);
    c *= 1.0 - smoothstep(0.34, 0.95, d)*(0.42 + 0.34*uFear); // vignette
    c *= 0.994 + 0.006*sin(uTime*377.0);             // mains-hum luma shimmer
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

// LEVEL FUN =): children's-party wallpaper — bunting up top, confetti,
// crayon smiley faces, and the same grime as everywhere else down here
static Texture2D makePartyWallTex() {
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
static Texture2D makePartyCarpetTex() {
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

static Sound makeGunshot() {
    int n = (int)(0.9f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0x6A17ULL);
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.60f * (wn - lp);
        lp2 += 0.07f * (wn - lp2);
        float crack = lp * expf(-t * 170.0f) * 2.8f;                          // supersonic crack
        float body = lp2 * expf(-t * 16.0f) * 2.4f;                           // blast body
        float thump = sinf(6.2831853f * (72.0f - 30.0f * t) * t) * expf(-t * 8.0f) * 1.2f;
        float tail = lp2 * expf(-t * 3.2f) * 0.4f;                            // hallway slap-back
        float s = tanhf((crack + body + thump + tail) * 1.9f) * expf(-t * 0.9f);
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

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
    float hissTarget = 0, hiss = 0;                             // burning flare hiss
    float whisperTarget = 0, whisper = 0;                       // something in the walls
    float tHum = 1, tDrone = 0, tWater = 0, tParty = 0;         // per-level ambience mix targets
    float wHum = 1, wDrone = 0, wWater = 0, wParty = 0;
    double musT = 0; int musI = 0;                              // LEVEL FUN music box position
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
                hiss += (hissTarget - hiss) * 6e-5f;
                whisper += (whisperTarget - whisper) * 8e-5f;
                wHum += (tHum - wHum) * 1.5e-5f;
                wDrone += (tDrone - wDrone) * 1.5e-5f;
                wWater += (tWater - wWater) * 1.5e-5f;
                wParty += (tParty - wParty) * 1.5e-5f;
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
                // LEVEL FUN: a music box grinding through the birthday song, slightly flat, forever
                float partyS = 0;
                if (wParty > 0.001f) {
                    static const float MEL[25] = { 392, 392, 440, 392, 523, 494,
                                                   392, 392, 440, 392, 587, 523,
                                                   392, 392, 784, 659, 523, 494, 440,
                                                   698, 698, 659, 523, 587, 523 };
                    musT += 1.0 / 44100.0;
                    if (musT > 0.42) { musT -= 0.42; musI = (musI + 1) % 25; }
                    float env = expf(-(float)musT * 4.0f);
                    float note = osc(15, MEL[musI] * 0.972f);   // half a semitone flat
                    partyS = (note * 0.75f + sinf(3.0f * (float)ph[15]) * 0.22f) * env * 0.085f * wParty;
                }
                float amb = (humS + droneS + waterS + partyS + room) * hum;   // hum var = blackout duck
                float g = osc(5, 46.0f) * 0.7f + osc(6, 33.5f) * 0.35f;
                float trem = 0.55f + 0.45f * osc(7, 2.1f);
                lp2 += 0.02f * (wn - lp2);
                float growlOut = (g * trem + lp2 * 2.2f) * growl * 0.5f;
                float hissOut = (wn - lp1) * 0.16f * hiss;      // flare burn: bright noise
                if (hiss > 0.01f) { float c = frand(); if (c > 0.998f) hissOut += c * 0.7f * hiss; }  // crackle
                // breathy half-syllables that never resolve into words
                float syl = fmaxf(0.0f, osc(13, 2.7f)) * (0.5f + 0.5f * osc(14, 0.31f));
                float whisperOut = (wn - lp1) * 0.20f * whisper * (0.25f + 0.75f * syl);
                short v = (short)(tanhf((amb + growlOut + hissOut + whisperOut) * 1.3f) * 30000);
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
// values: 0 open, 1 wall, 2 exit doorway, 3 window into the dark
// prop types: 0 none, 1 box stack, 2 filing cabinet, 3 folding table, 4 fallen ceiling tile,
//             5 couch, 6 armoire, 7 floor lamp, 8 nightstand, 9 bed, 10 vending machine,
//             11 party table (cake nobody cut)
struct ChunkData {
    uint8_t wallN[CCELLS][CCELLS];
    uint8_t wallW[CCELLS][CCELLS];
    uint8_t pillar[CCELLS][CCELLS];
    uint8_t prop[CCELLS][CCELLS];
    uint8_t propRot[CCELLS][CCELLS];
    uint8_t pool[CCELLS][CCELLS];
    int8_t elev[CCELLS][CCELLS];   // floor height in decimetres: -5 sunken lounge (L0), +6 loading dock (L1)
    bool built = false;
    Mesh meshes[5] = {};   // 0 floor, 1 ceiling, 2 walls, 3 props, 4 water
};

struct AABB { float minx, minz, maxx, maxz, top; };   // top: height you can stand on

static int fdiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
static int cellOf(float x) { return (int)floorf(x / CELL); }

struct World {
    unsigned seed = 1337;
    int level = 0;           // 0 = Level 0, 1 = Level 1 (garage), 2 = Poolrooms, 3 = Red Halls, 4 = LEVEL FUN
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
        memset(d.elev, 0, sizeof(d.elev));
        uint64_t k = key(cx, cz);
        Rng rng(hash64(k ^ ((uint64_t)seed + (uint64_t)level * 0x51ED270Bu) * 0x9E3779B97F4A7C15ULL));
        bool openChunk = (hash64(k ^ 0xA11CEULL ^ (uint64_t)seed) & 7) == 0;   // occasional open plaza
        int nseg = level == 0 ? 12 + rng.ri(0, 5) : level == 1 ? 7 + rng.ri(0, 4)
                 : level == 4 ? 9 + rng.ri(0, 4) : 5 + rng.ri(0, 3);
        if (openChunk) nseg = 2 + rng.ri(0, 2);
        int lenBase = level == 0 ? 5 : 6;
        for (int s = 0; s < nseg; s++) {
            bool horiz = rng.next() & 1;
            int len = lenBase + rng.ri(0, 8);
            int a = rng.ri(0, CCELLS - 1), b = rng.ri(0, CCELLS - 1);
            int end = std::min(CCELLS - 1, a + len);
            int doorAt = (rng.f01() < (level == 0 ? 0.72f : 0.8f)) ? a + 1 + rng.ri(0, std::max(0, end - a - 2)) : -1;
            for (int i = a; i <= end; i++) {
                if (i == doorAt) continue;
                // rarely a window instead of blank wall; behind it, nothing
                uint8_t v = ((level == 0 || level == 3) && rng.f01() < 0.035f) ? 3 : 1;
                if (horiz) d.wallN[i][b] = v; else d.wallW[b][i] = v;
            }
        }
        int np = level == 0 ? 4 + rng.ri(0, 5) : level == 1 ? 10 + rng.ri(0, 8) : 2 + rng.ri(0, 3);
        for (int i = 0; i < np; i++) d.pillar[rng.ri(0, CCELLS - 1)][rng.ri(0, CCELLS - 1)] = 1;
        memset(d.prop, 0, sizeof(d.prop));
        memset(d.propRot, 0, sizeof(d.propRot));
        int npr = level == 0 ? 7 + rng.ri(0, 7) : level == 1 ? 8 + rng.ri(0, 8)
                : level == 3 ? 5 + rng.ri(0, 6) : level == 4 ? 6 + rng.ri(0, 6) : 0;
        for (int i = 0; i < npr; i++) {
            int a = rng.ri(0, CCELLS - 1), b = rng.ri(0, CCELLS - 1);
            if (d.pillar[a][b] || d.prop[a][b]) continue;
            float f = rng.f01();
            if (level == 1) d.prop[a][b] = f < 0.55f ? 1 : f < 0.78f ? 2 : f < 0.96f ? 3 : 10;  // warehouse
            else if (level == 3)                                               // red halls: someone's bedroom
                d.prop[a][b] = f < 0.30f ? 9 : f < 0.55f ? 6 : f < 0.80f ? 8 : 7;
            else if (level == 4)                                               // level fun: the party never ended
                d.prop[a][b] = f < 0.40f ? 11 : f < 0.62f ? 1 : f < 0.76f ? 5 : f < 0.90f ? 3 : 10;
            else   // L0: office clutter, plus furniture that has no business here
                d.prop[a][b] = f < 0.26f ? 1 : f < 0.40f ? 2 : f < 0.50f ? 3 : f < 0.62f ? 4 :
                               f < 0.74f ? 5 : f < 0.84f ? 6 : f < 0.90f ? 7 : f < 0.95f ? 8 :
                               f < 0.985f ? 9 : 10;
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
        if (level == 0 || level == 1) {   // sunken lounges (L0) / loading docks (L1), never under walls
            for (int i = 1; i < CCELLS - 1; i++) for (int kk = 1; kk < CCELLS - 1; kk++) {
                if (d.pillar[i][kk]) continue;
                if (d.wallN[i][kk] || d.wallW[i][kk] || d.wallN[i][kk + 1] || d.wallW[i + 1][kk]) continue;
                float gxc = (float)(cx * CCELLS + i), gzc = (float)(cz * CCELLS + kk);
                if (fbm2(gxc * 0.09f, gzc * 0.09f, seed ^ (level == 0 ? 0x51ABu : 0xD0CCu), 3) > 0.60f)
                    d.elev[i][kk] = level == 0 ? -5 : 6;
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
            for (int i = 5; i <= 10; i++) for (int kk = 5; kk <= 10; kk++) {
                d.wallN[i][kk] = d.wallW[i][kk] = d.pillar[i][kk] = d.prop[i][kk] = d.pool[i][kk] = 0;
                d.elev[i][kk] = 0;
            }
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
    float floorY(int ci, int ck) {
        if (poolAt(ci, ck)) return -0.6f;
        int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
        return data(cx, cz).elev[ci - cx * CCELLS][ck - cz * CCELLS] * 0.1f;
    }

    // rotated prop box: 4 sides + top, one UV region for sides, another for the top
    static void addPropBox(MB &mb, float cx, float cz, float yaw, float hx, float hz, float y0, float y1,
                           float u0, float v0, float u1, float v1,
                           float tu0, float tv0, float tu1, float tv1, Color tint = WHITE) {
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
                    {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tint);
        }
        mb.quad({corners[0].x,y1,corners[0].z},{corners[1].x,y1,corners[1].z},
                {corners[2].x,y1,corners[2].z},{corners[3].x,y1,corners[3].z},{0,1,0},
                {tu0,tv0},{tu1,tv0},{tu1,tv1},{tu0,tv1}, tint);
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
                // submerged step along dry edges — tiled bench to walk down into the water
                auto pstep = [&](float bx0, float bz0, float bx1, float bz1, float nx, float nz) {
                    float mid = -0.3f, ox = nx * 0.32f, oz = nz * 0.32f;
                    fl.quad({bx0,mid,bz0},{bx1,mid,bz1},{bx1+ox,mid,bz1+oz},{bx0+ox,mid,bz0+oz},{0,1,0},
                            {0,0},{1,0},{1,0.16f},{0,0.16f},wcol);
                    fl.quad({bx0+ox,mid,bz0+oz},{bx1+ox,mid,bz1+oz},{bx1+ox,-0.6f,bz1+oz},{bx0+ox,-0.6f,bz0+oz},{nx,0,nz},
                            {0,0},{1,0},{1,0.15f},{0,0.15f},wcol);
                };
                if (dry(i, kk-1)) pstep(gx, gz, gx+CELL, gz, 0, 1);
                if (dry(i, kk+1)) pstep(gx, gz+CELL, gx+CELL, gz+CELL, 0, -1);
                if (dry(i-1, kk)) pstep(gx, gz, gx, gz+CELL, 1, 0);
                if (dry(i+1, kk)) pstep(gx+CELL, gz, gx+CELL, gz+CELL, -1, 0);
                wt.quad({gx,-0.12f,gz},{gx+CELL,-0.12f,gz},{gx+CELL,-0.12f,gz+CELL},{gx,-0.12f,gz+CELL},{0,1,0},
                        {0,0},{1,0},{1,1},{0,1}, water);
            }
        } else {
            // per-cell floor: sunken lounges (L0) and loading docks (L1) change height, with real steps
            auto stepEdge = [&](float bx0, float bz0, float bx1, float bz1, float hi, float lo, float nx, float nz) {
                fl.quad({bx0,hi,bz0},{bx1,hi,bz1},{bx1,lo,bz1},{bx0,lo,bz0},{nx,0,nz},
                        {(bx0+bz0)/2,0},{(bx1+bz1)/2,0},{(bx1+bz1)/2,(hi-lo)/2},{(bx0+bz0)/2,(hi-lo)/2},wcol);
                float mid = (hi + lo) * 0.5f, ox = nx * 0.35f, oz = nz * 0.35f;
                fl.quad({bx0,mid,bz0},{bx1,mid,bz1},{bx1+ox,mid,bz1+oz},{bx0+ox,mid,bz0+oz},{0,1,0},
                        {0,0},{1,0},{1,0.18f},{0,0.18f},wcol);
                fl.quad({bx0+ox,mid,bz0+oz},{bx1+ox,mid,bz1+oz},{bx1+ox,lo,bz1+oz},{bx0+ox,lo,bz0+oz},{nx,0,nz},
                        {0,0},{1,0},{1,(mid-lo)/2},{0,(mid-lo)/2},wcol);
                fl.quad({bx0,mid,bz0},{bx0+ox,mid,bz0+oz},{bx0+ox,lo,bz0+oz},{bx0,lo,bz0},{bz1-bz0,0,bx0-bx1},
                        {0,0},{0.18f,0},{0.18f,(mid-lo)/2},{0,(mid-lo)/2},wcol);   // end caps
                fl.quad({bx1,mid,bz1},{bx1+ox,mid,bz1+oz},{bx1+ox,lo,bz1+oz},{bx1,lo,bz1},{bz0-bz1,0,bx1-bx0},
                        {0,0},{0.18f,0},{0.18f,(mid-lo)/2},{0,(mid-lo)/2},wcol);
            };
            for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
                float gx = wx + i * CELL, gz = wz + kk * CELL;
                float fy = d.elev[i][kk] * 0.1f;
                fl.quad({gx,fy,gz},{gx+CELL,fy,gz},{gx+CELL,fy,gz+CELL},{gx,fy,gz+CELL},{0,1,0},
                        {gx/2,gz/2},{(gx+CELL)/2,gz/2},{(gx+CELL)/2,(gz+CELL)/2},{gx/2,(gz+CELL)/2},wcol);
                if (d.elev[i][kk] == 0) continue;
                auto hgt = [&](int a, int b) {
                    return (a < 0 || a >= CCELLS || b < 0 || b >= CCELLS) ? 0.0f : d.elev[a][b] * 0.1f;
                };
                float hN = hgt(i, kk - 1), hS = hgt(i, kk + 1), hW = hgt(i - 1, kk), hE = hgt(i + 1, kk);
                // dock: steps drop into the lower neighbour; lounge: steps climb out into this cell
                if (hN < fy) stepEdge(gx, gz, gx + CELL, gz, fy, hN, 0, -1);
                else if (hN > fy) stepEdge(gx, gz, gx + CELL, gz, hN, fy, 0, 1);
                if (hS < fy) stepEdge(gx, gz + CELL, gx + CELL, gz + CELL, fy, hS, 0, 1);
                else if (hS > fy) stepEdge(gx, gz + CELL, gx + CELL, gz + CELL, hS, fy, 0, -1);
                if (hW < fy) stepEdge(gx, gz, gx, gz + CELL, fy, hW, -1, 0);
                else if (hW > fy) stepEdge(gx, gz, gx, gz + CELL, hW, fy, 1, 0);
                if (hE < fy) stepEdge(gx + CELL, gz, gx + CELL, gz + CELL, fy, hE, 1, 0);
                else if (hE > fy) stepEdge(gx + CELL, gz, gx + CELL, gz + CELL, hE, fy, -1, 0);
            }
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
            else if (nv == 3) {   // window on x-running wall; behind the glass, nothing
                addBoxSides(wa, gx - WT, 0, gz - WT, gx + CELL + WT, 1.0f, gz + WT);
                addBoxSides(wa, gx - WT, 2.1f, gz - WT, gx + CELL + WT, wallH, gz + WT, true);
                addBoxSides(wa, gx - WT, 1.0f, gz - WT, gx + 0.45f, 2.1f, gz + WT);
                addBoxSides(wa, gx + 1.55f, 1.0f, gz - WT, gx + CELL + WT, 2.1f, gz + WT);
                wa.quad({gx-WT,1.0f,gz-WT},{gx+CELL+WT,1.0f,gz-WT},{gx+CELL+WT,1.0f,gz+WT},{gx-WT,1.0f,gz+WT},
                        {0,1,0},{0,0},{1,0},{1,0.1f},{0,0.1f}, WHITE);   // sill top
                Color glass = { 5, 6, 9, 60 };   // emissive void — no light touches it
                wa.quad({gx+0.45f,1.0f,gz},{gx+1.55f,1.0f,gz},{gx+1.55f,2.1f,gz},{gx+0.45f,2.1f,gz},
                        {0,0,-1},{0,1},{1,1},{1,0},{0,0}, glass);
            }
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
            else if (wv == 3) {   // window on z-running wall
                addBoxSides(wa, gx - WT, 0, gz - WT, gx + WT, 1.0f, gz + CELL + WT);
                addBoxSides(wa, gx - WT, 2.1f, gz - WT, gx + WT, wallH, gz + CELL + WT, true);
                addBoxSides(wa, gx - WT, 1.0f, gz - WT, gx + WT, 2.1f, gz + 0.45f);
                addBoxSides(wa, gx - WT, 1.0f, gz + 1.55f, gx + WT, 2.1f, gz + CELL + WT);
                wa.quad({gx-WT,1.0f,gz-WT},{gx+WT,1.0f,gz-WT},{gx+WT,1.0f,gz+CELL+WT},{gx-WT,1.0f,gz+CELL+WT},
                        {0,1,0},{0,0},{1,0},{1,0.1f},{0,0.1f}, WHITE);   // sill top
                Color glass = { 5, 6, 9, 60 };
                wa.quad({gx,1.0f,gz+0.45f},{gx,1.0f,gz+1.55f},{gx,2.1f,gz+1.55f},{gx,2.1f,gz+0.45f},
                        {1,0,0},{0,1},{1,1},{1,0},{0,0}, glass);
            }
            else if (wv == 2) {   // exit doorway on z-running wall
                addBoxSides(wa, gx - WT, 0, gz - WT, gx + WT, wallH, gz + 0.35f);
                addBoxSides(wa, gx - WT, 0, gz + 1.65f, gx + WT, wallH, gz + CELL + WT);
                addBoxSides(wa, gx - WT, 2.3f, gz + 0.35f, gx + WT, wallH, gz + 1.65f, true);
                Color glow = {255, 248, 225, 70};
                wa.quad({gx,0,gz+0.35f},{gx,0,gz+1.65f},{gx,2.3f,gz+1.65f},{gx,2.3f,gz+0.35f},{1,0,0},
                        {0,1},{1,1},{1,0},{0,0},glow);
            }
            if (dd.pillar[i][kk]) {
                addBoxSides(wa, gx + 0.42f, 0, gz + 0.42f, gx + 1.58f, wallH, gz + 1.58f);
                wa.quad({gx+0.30f,0.004f,gz+0.30f},{gx+1.70f,0.004f,gz+0.30f},   // contact shadow
                        {gx+1.70f,0.004f,gz+1.70f},{gx+0.30f,0.004f,gz+1.70f},
                        {0,1,0},{0,0},{0.04f,0},{0.04f,0.04f},{0,0.04f}, Color{ 12, 12, 12, 160 });
            }
            if (dd.prop[i][kk]) {
                float pcx = gx + 1.0f, pcz = gz + 1.0f;
                float rot = dd.propRot[i][kk] * 1.5708f;
                float ey = dd.elev[i][kk] * 0.1f;   // furniture sits on the local floor
                uint32_t h = ih(cx * CCELLS + i, cz * CCELLS + kk, seed ^ 0xB0B5u);
                float r1 = (h & 0xFF) / 255.0f, r2 = ((h >> 8) & 0xFF) / 255.0f, r3 = ((h >> 16) & 0xFF) / 255.0f;
                // UV regions of the prop atlas
                const float CU0=0.02f, CV0=0.02f, CU1=0.48f, CV1=0.98f;       // cardboard
                const float FU0=0.52f, FV0=0.02f, FU1=0.98f, FV1=0.48f;       // cabinet front
                const float MU0=0.52f, MV0=0.52f, MU1=0.98f, MV1=0.98f;       // plain metal
                float ca = cosf(rot), sa = sinf(rot);
                // rotated sub-box placed relative to the prop centre
                auto part = [&](float ox, float oz, float hx2, float hz2, float y0, float y1,
                                bool wood, Color tint) {
                    addPropBox(pr, pcx + ox * ca - oz * sa, pcz + ox * sa + oz * ca, rot, hx2, hz2, y0, y1,
                               wood ? CU0 : MU0, wood ? CV0 : MV0, wood ? CU1 : MU1, wood ? CV1 : MV1,
                               wood ? CU0 : MU0, wood ? CV0 : MV0, wood ? CU1 : MU1, wood ? CV1 : MV1, tint);
                };
                auto blob = [&](float hx2, float hz2) {   // soft contact shadow under the piece
                    auto ptv = [&](float lx, float lz) {
                        return Vector3{ pcx + lx * ca - lz * sa, ey + 0.006f, pcz + lx * sa + lz * ca };
                    };
                    pr.quad(ptv(-hx2, -hz2), ptv(hx2, -hz2), ptv(hx2, hz2), ptv(-hx2, hz2), {0,1,0},
                            {0.75f,0.75f},{0.75f,0.75f},{0.75f,0.75f},{0.75f,0.75f}, Color{ 12, 12, 12, 160 });
                };
                switch (dd.prop[i][kk]) {
                case 1: {   // box stack
                    blob(0.58f, 0.58f);
                    float bh = 0.55f + r1 * 0.2f, bhx = 0.34f + r2 * 0.08f;
                    addPropBox(pr, pcx + (r3 - 0.5f) * 0.5f, pcz + (r1 - 0.5f) * 0.5f, rot + r2,
                               bhx, bhx, ey, ey + bh, CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1);
                    if (r2 > 0.35f)   // second box on top, skewed
                        addPropBox(pr, pcx + (r3 - 0.5f) * 0.5f + 0.06f, pcz + (r1 - 0.5f) * 0.5f - 0.05f,
                                   rot + r2 + 0.5f, bhx * 0.8f, bhx * 0.8f, ey + bh, ey + bh + 0.5f,
                                   CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1);
                    if (r1 > 0.6f)    // third box beside
                        addPropBox(pr, pcx + 0.62f, pcz + 0.3f, rot + r3 * 2, 0.27f, 0.27f, ey, ey + 0.5f,
                                   CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1);
                    break;
                }
                case 2:     // filing cabinet
                    blob(0.34f, 0.42f);
                    addPropBox(pr, pcx, pcz, rot, 0.26f, 0.34f, ey, ey + 1.32f,
                               FU0, FV0, FU1, FV1, MU0, MV0, MU1, MV1);
                    break;
                case 3: {   // folding table
                    blob(0.68f, 0.46f);
                    float ty = 0.72f;
                    addPropBox(pr, pcx, pcz, rot, 0.62f, 0.40f, ey + ty - 0.04f, ey + ty,
                               MU0, MV0, MU1, MV1, MU0, MV0, MU1, MV1);
                    for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2) {
                        float ox = lx * 0.54f, oz = lz * 0.32f;
                        addPropBox(pr, pcx + ox * ca - oz * sa, pcz + ox * sa + oz * ca, rot,
                                   0.03f, 0.03f, ey, ey + ty - 0.04f, MU0, MV0, MU1, MV1, MU0, MV0, MU1, MV1);
                    }
                    break;
                }
                case 4: {   // collapsed ceiling: dark hole above, tile leaning below, debris
                    Color hole = { 12, 11, 9, 51 };
                    ce.quad({pcx-0.85f,2.994f,pcz-0.85f},{pcx-0.85f,2.994f,pcz+0.85f},
                            {pcx+0.85f,2.994f,pcz+0.85f},{pcx+0.85f,2.994f,pcz-0.85f},{0,-1,0},
                            {0,0},{0,1},{1,1},{1,0}, hole);
                    float bx0 = pcx - 0.58f * ca, bz0 = pcz - 0.58f * sa;   // base edge on floor
                    float tx = pcx + 0.35f * ca, tz = pcz + 0.35f * sa;     // top edge, lifted
                    Vector3 a = { bx0 - 0.58f * sa, ey + 0.02f, bz0 + 0.58f * ca };
                    Vector3 b = { bx0 + 0.58f * sa, ey + 0.02f, bz0 - 0.58f * ca };
                    Vector3 c2 = { tx + 0.58f * sa, ey + 0.42f, tz - 0.58f * ca };
                    Vector3 dq = { tx - 0.58f * sa, ey + 0.42f, tz + 0.58f * ca };
                    ce.quad(a, b, c2, dq, { -ca * 0.5f, 0.87f, -sa * 0.5f },
                            {0.05f,0.45f},{0.45f,0.45f},{0.45f,0.05f},{0.05f,0.05f}, WHITE);
                    Color deb = { 110, 105, 95, 255 };
                    ce.quad({pcx+0.4f,ey+0.012f,pcz+0.5f},{pcx+0.75f,ey+0.012f,pcz+0.55f},
                            {pcx+0.7f,ey+0.012f,pcz+0.85f},{pcx+0.38f,ey+0.012f,pcz+0.8f},{0,1,0},
                            {0.1f,0.1f},{0.3f,0.1f},{0.3f,0.3f},{0.1f,0.3f}, deb);
                    ce.quad({pcx-0.7f,ey+0.012f,pcz-0.35f},{pcx-0.45f,ey+0.012f,pcz-0.42f},
                            {pcx-0.4f,ey+0.012f,pcz-0.2f},{pcx-0.68f,ey+0.012f,pcz-0.15f},{0,1,0},
                            {0.3f,0.3f},{0.45f,0.3f},{0.45f,0.45f},{0.3f,0.45f}, deb);
                    break;
                }
                case 5: {   // couch: mustard upholstery gone grey, facing nothing in particular
                    blob(0.86f, 0.56f);
                    Color uph = { 172, 152, 96, 255 };
                    part(0, 0.10f, 0.78f, 0.42f, ey + 0.16f, ey + 0.44f, false, uph);   // seat
                    part(0, -0.36f, 0.78f, 0.14f, ey + 0.16f, ey + 0.92f, false, uph);  // backrest
                    part(-0.64f, 0.06f, 0.14f, 0.46f, ey, ey + 0.62f, false, uph);      // arms
                    part( 0.64f, 0.06f, 0.14f, 0.46f, ey, ey + 0.62f, false, uph);
                    part(0, 0.10f, 0.74f, 0.38f, ey, ey + 0.16f, false, Color{ 120, 106, 70, 255 });
                    break;
                }
                case 6:     // armoire: a wardrobe looming where no bedroom is
                    blob(0.54f, 0.46f);
                    part(0, 0, 0.44f, 0.36f, ey, ey + 1.78f, true, Color{ 118, 82, 58, 255 });
                    part(0, 0, 0.48f, 0.40f, ey + 1.78f, ey + 1.90f, true, Color{ 92, 63, 44, 255 });  // cornice
                    part(0, 0.37f, 0.015f, 0.015f, ey + 0.85f, ey + 1.0f, false, Color{ 190, 170, 110, 255 }); // handles
                    break;
                case 7:     // floor lamp, shade askew, never lit
                    blob(0.22f, 0.22f);
                    part(0, 0, 0.14f, 0.14f, ey, ey + 0.05f, false, Color{ 66, 62, 60, 255 });
                    part(0, 0, 0.025f, 0.025f, ey, ey + 1.34f, false, Color{ 66, 62, 60, 255 });
                    addPropBox(pr, pcx + 0.05f, pcz, rot + 0.3f, 0.17f, 0.17f, ey + 1.30f, ey + 1.60f,
                               CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1, Color{ 214, 190, 142, 255 });
                    break;
                case 8:     // nightstand, nowhere near a bed. usually.
                    blob(0.36f, 0.36f);
                    part(0, 0, 0.26f, 0.26f, ey, ey + 0.55f, true, Color{ 126, 90, 62, 255 });
                    part(0, 0, 0.30f, 0.30f, ey + 0.55f, ey + 0.60f, true, Color{ 104, 74, 50, 255 });
                    break;
                case 9: {   // bed: bare stained mattress, headboard against nothing
                    blob(0.60f, 1.02f);
                    Color wd = { 110, 78, 54, 255 };
                    part(0, 0, 0.52f, 0.92f, ey + 0.12f, ey + 0.26f, true, wd);          // frame
                    part(0, 0.04f, 0.48f, 0.86f, ey + 0.26f, ey + 0.46f, true, Color{ 216, 208, 188, 255 }); // mattress
                    part(0, -0.97f, 0.52f, 0.05f, ey, ey + 0.95f, true, wd);             // headboard
                    break;
                }
                case 11: {  // party table: paper cloth, a cake nobody ever cut
                    blob(0.62f, 0.62f);
                    float ty = 0.74f;
                    Color cloth = PARTY[ih(cx * CCELLS + i, cz * CCELLS + kk, 0xCAFEu) % 5];
                    part(0, 0, 0.55f, 0.55f, ey + ty - 0.05f, ey + ty, false, cloth);
                    for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2)
                        part(lx * 0.44f, lz * 0.44f, 0.035f, 0.035f, ey, ey + ty - 0.05f,
                             false, Color{ 120, 118, 112, 255 });
                    part(0, 0, 0.17f, 0.17f, ey + ty, ey + ty + 0.16f, false, Color{ 238, 232, 220, 255 });   // cake
                    part(0, 0, 0.11f, 0.11f, ey + ty + 0.16f, ey + ty + 0.26f, false, Color{ 232, 152, 172, 255 });
                    part(0, 0, 0.013f, 0.013f, ey + ty + 0.26f, ey + ty + 0.37f, false, Color{ 240, 226, 172, 255 }); // candle
                    break;
                }
                case 10: {  // vending machine: still stocked, still humming, takes doubloons
                    blob(0.52f, 0.44f);
                    part(0, 0, 0.44f, 0.36f, ey, ey + 1.85f, false, Color{ 148, 152, 158, 255 });
                    auto ptv2 = [&](float lx, float ly2, float lz) {
                        return Vector3{ pcx + lx * ca - lz * sa, ly2, pcz + lx * sa + lz * ca };
                    };
                    Color panel = { 66, 90, 122, 60 };   // emissive front: soft cold glow
                    pr.quad(ptv2(-0.28f, ey + 0.55f, -0.375f), ptv2(0.28f, ey + 0.55f, -0.375f),
                            ptv2(0.28f, ey + 1.68f, -0.375f), ptv2(-0.28f, ey + 1.68f, -0.375f),
                            { sa, 0, -ca }, {0,1},{1,1},{1,0},{0,0}, panel);
                    break;
                }
                }
            }
        }
        if (level == 4) {   // crepe streamers sag from the ceiling, in pairs of quads
            Rng srng(hash64(key(cx, cz) ^ 0xFE57AULL ^ (uint64_t)seed));
            int ns = 3 + srng.ri(0, 3);
            for (int s = 0; s < ns; s++) {
                float ax = wx + srng.f01() * CHUNK, az = wz + srng.f01() * CHUNK;
                float bx2 = ax + (srng.f01() - 0.5f) * 9, bz2 = az + (srng.f01() - 0.5f) * 9;
                float mx2 = (ax + bx2) * 0.5f, mz2 = (az + bz2) * 0.5f;
                float ytop = wallH - 0.03f, ymid = wallH - 0.55f - srng.f01() * 0.35f;
                Color sc = PARTY[srng.ri(0, 4)];
                float dx2 = bx2 - ax, dz2 = bz2 - az, dl = sqrtf(dx2 * dx2 + dz2 * dz2) + 1e-4f;
                Vector3 nrm = { dz2 / dl, 0, -dx2 / dl };
                Vector2 uvp = { 0.75f, 0.75f };   // plain-metal corner of the prop atlas: flat colour
                pr.quad({ ax, ytop, az }, { mx2, ymid + 0.06f, mz2 }, { mx2, ymid, mz2 }, { ax, ytop - 0.06f, az },
                        nrm, uvp, uvp, uvp, uvp, sc);
                pr.quad({ mx2, ymid + 0.06f, mz2 }, { bx2, ytop, bz2 }, { bx2, ytop - 0.06f, bz2 }, { mx2, ymid, mz2 },
                        nrm, uvp, uvp, uvp, uvp, sc);
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
        uint8_t nv = wallNVal(ci, ck), wv = wallWVal(ci, ck);
        if (cnt < cap && (nv == 1 || nv == 3)) out[cnt++] = { x0 - WT, z0 - WT, x0 + CELL + WT, z0 + WT, wallH };
        if (cnt < cap && (wv == 1 || wv == 3)) out[cnt++] = { x0 - WT, z0 - WT, x0 + WT, z0 + CELL + WT, wallH };
        if (cnt < cap && pillarAt(ci, ck)) out[cnt++] = { x0 + 0.42f, z0 + 0.42f, x0 + 1.58f, z0 + 1.58f, wallH };
        if (includeProps && cnt < cap) {
            uint8_t pv = propAt(ci, ck);
            if (pv) {
                float ey = floorY(ci, ck);
                uint32_t h = ih(ci, ck, seed ^ 0xB0B5u);   // same hash the mesher uses
                float r1 = (h & 0xFF) / 255.0f, r2 = ((h >> 8) & 0xFF) / 255.0f;
                switch (pv) {
                case 1: {   // boxes: top of the tallest stacked one
                    float t = 0.55f + r1 * 0.2f;
                    if (r2 > 0.35f) t += 0.5f;
                    out[cnt++] = { x0 + 0.35f, z0 + 0.35f, x0 + 1.65f, z0 + 1.65f, ey + t }; break;
                }
                case 2: out[cnt++] = { x0 + 0.60f, z0 + 0.60f, x0 + 1.40f, z0 + 1.40f, ey + 1.32f }; break;  // cabinet
                case 3: out[cnt++] = { x0 + 0.32f, z0 + 0.32f, x0 + 1.68f, z0 + 1.68f, ey + 0.72f }; break;  // table
                case 5: out[cnt++] = { x0 + 0.20f, z0 + 0.38f, x0 + 1.80f, z0 + 1.62f, ey + 0.44f }; break;  // couch
                case 6: out[cnt++] = { x0 + 0.55f, z0 + 0.62f, x0 + 1.45f, z0 + 1.38f, ey + 1.90f }; break;  // armoire
                case 7: out[cnt++] = { x0 + 0.82f, z0 + 0.82f, x0 + 1.18f, z0 + 1.18f, ey + 1.62f }; break;  // lamp
                case 8: out[cnt++] = { x0 + 0.66f, z0 + 0.66f, x0 + 1.34f, z0 + 1.34f, ey + 0.60f }; break;  // nightstand
                case 9: out[cnt++] = { x0 + 0.30f, z0 + 0.15f, x0 + 1.70f, z0 + 1.85f, ey + 0.46f }; break;  // bed
                case 10: out[cnt++] = { x0 + 0.50f, z0 + 0.58f, x0 + 1.50f, z0 + 1.42f, ey + 1.85f }; break; // vending
                case 11: out[cnt++] = { x0 + 0.40f, z0 + 0.40f, x0 + 1.60f, z0 + 1.60f, ey + 0.74f }; break; // party table
                }
            }
        }
        return cnt;
    }

    // feetY: obstacles whose top is at or below your feet are walkable, not solid
    void collideCircle(float &px, float &pz, float r, float feetY = 0.0f) {
        AABB boxes[48];
        int cnt = 0;
        int ci = cellOf(px), ck = cellOf(pz);
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++)
                cnt = gatherCellAABBs(ci + dx, ck + dz, boxes, 48, cnt);
        for (int pass = 0; pass < 3; pass++)
            for (int i = 0; i < cnt; i++) {
                const AABB &b = boxes[i];
                if (feetY >= b.top - 0.02f) continue;
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

    // floor height here, counting prop tops at or below your feet (so you can stand on furniture)
    float groundAt(float x, float z, float feetY) {
        float g = floorY(cellOf(x), cellOf(z));
        AABB boxes[48];
        int cnt = 0;
        int ci = cellOf(x), ck = cellOf(z);
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++)
                cnt = gatherCellAABBs(ci + dx, ck + dz, boxes, 48, cnt);
        for (int i = 0; i < cnt; i++)
            if (x > boxes[i].minx && x < boxes[i].maxx && z > boxes[i].minz && z < boxes[i].maxz &&
                boxes[i].top <= feetY + 0.05f && boxes[i].top > g) g = boxes[i].top;
        return g;
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
    float wallH, ls, dead, lightMul, fogDen, gloss;
    Vector3 lightCol, amb, fogCol;
    const char *name;
};
static const int NLEVELS = 5;
static const LevelCfg LEVELS[NLEVELS] = {
    { 3.0f,  8.0f, 0.06f, 1.00f, 0.055f, 0.06f, {1.00f,0.94f,0.74f}, {0.045f,0.042f,0.030f}, {0.140f,0.125f,0.070f}, "LEVEL 0" },
    { 4.2f, 12.0f, 0.30f, 0.85f, 0.075f, 0.22f, {0.72f,0.80f,0.95f}, {0.016f,0.017f,0.022f}, {0.018f,0.020f,0.026f}, "LEVEL 1" },
    { 3.6f,  8.0f, 0.06f, 0.72f, 0.045f, 0.55f, {1.00f,1.00f,0.97f}, {0.16f,0.18f,0.20f},    {0.19f,0.23f,0.27f},    "THE POOLROOMS" },
    { 3.0f,  8.0f, 0.45f, 0.80f, 0.095f, 0.10f, {1.00f,0.22f,0.15f}, {0.030f,0.008f,0.006f}, {0.055f,0.010f,0.008f}, "THE RED HALLS" },
    { 3.0f,  8.0f, 0.10f, 1.05f, 0.055f, 0.06f, {1.00f,0.82f,0.76f}, {0.050f,0.040f,0.036f}, {0.150f,0.100f,0.085f}, "LEVEL FUN =)" },
};
// where each level's exit door leads; the Red Halls and the party both dump you back at the start
static const int EXIT_NEXT[NLEVELS] = { 1, 2, 4, 0, 0 };

// ---------------------------------------------------------------- entity
enum class EState { Hidden, Stalk, Chase, Flee, Die };
struct Entity {
    EState st = EState::Hidden;
    float x = 0, z = 0;
    double nextSpawn = 12.0;
    float gaze = 0, life = 0, unseen = 0;
    float dispY = 0;   // smoothed floor height under him, so he doesn't pop on stairs
    float lunge = 0;   // mid-chase burst of speed
    int hp = 3;
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
    Texture2D floorTexs[NLEVELS] = { makeCarpetTex(), makeConcreteFloorTex(), makeTileTex(), Texture2D{}, makePartyCarpetTex() };
    Texture2D ceilTexs[NLEVELS]  = { makeCeilingTex(), makeConcreteCeilTex(), floorTexs[2], Texture2D{}, Texture2D{} };
    Texture2D wallTexs[NLEVELS]  = { makeWallpaperTex(), makeConcreteWallTex(), floorTexs[2], makeRedBrickTex(), makePartyWallTex() };
    floorTexs[3] = floorTexs[1];   // red halls reuse the concrete floor/ceiling in red light
    ceilTexs[3] = ceilTexs[1];
    ceilTexs[4] = ceilTexs[0];     // the party is under the same office tiles as Level 0

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
    int locFlarePos = GetShaderLocation(worldShader, "uFlarePos");
    int locFlareInt = GetShaderLocation(worldShader, "uFlareInt");
    int locGloss = GetShaderLocation(worldShader, "uGloss");
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
    Sound sndFlare = makeFlareStrike();
    Sound sndShot = makeGunshot();
    Sound sndHit = makeJumpscare();  SetSoundPitch(sndHit, 1.7f);  SetSoundVolume(sndHit, 0.40f);
    Sound sndKill = makeJumpscare(); SetSoundPitch(sndKill, 0.55f); SetSoundVolume(sndKill, 0.80f);

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

    // flare weapon: thrown, burns orange, Pirate Clark won't go near one
    const int   MAXFLARES = 3;
    const float FLAREBURN = 9.0f;    // seconds
    int flares = MAXFLARES;
    double nextFlareRegen = GetTime() + 75;
    struct { bool active = false, flying = false; float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, burn = 0; } flare;

    // revolver: hitscan, six rounds, three hits put Clark down
    const int MAXAMMO = 6;
    int weapon = 0;   // 0 flare, 1 revolver — keys 1/2 or mouse wheel
    int ammo = MAXAMMO;
    float reloadT = 0, gunCd = 0, muzzleT = 0, recoil = 0, wheelCd = 0;

    // state
    int level = 0;
    Entity ent;
    double nextBlackout = 40 + grng.f01() * 60;
    double blackoutEnd = -1;
    float blackoutCur = 1.0f;
    float fear = 0.0f;
    float caughtT = 0, escapeT = 0, killT = 0;
    int caughtCount = 0, escapeCount = 0, killCount = 0;
    float distWalked = 0;
    double runStart = GetTime();
    bool debugHud = false;
    int frame = 0;
    RenderTexture2D rt = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    // pickups, currency, chalk, ambient events, records
    std::unordered_set<uint64_t> taken;      // world pickups already grabbed (reset per level)
    std::vector<Vector3> coinsWorld;         // doubloons Clark spills when he goes down
    std::vector<Vector3> chalk;              // navigation marks
    int almond = 0, coins = 0;
    float boostT = 0, crouchCur = 0, whisperT = 0;
    double nextWhisper = runStart + 45 + grng.f01() * 60;
    char bestPath[512];
    snprintf(bestPath, sizeof(bestPath), "%s/.backrooms_best", getenv("HOME") ? getenv("HOME") : ".");
    int bestEsc = 0, bestKill = 0, bestM = 0;
    if (FILE *bf = fopen(bestPath, "r")) {
        if (fscanf(bf, "%d %d %d", &bestEsc, &bestKill, &bestM) != 3) bestEsc = bestKill = bestM = 0;
        fclose(bf);
    }
    auto saveBest = [&]() {
        bool up = false;
        if (escapeCount > bestEsc) { bestEsc = escapeCount; up = true; }
        if (killCount > bestKill) { bestKill = killCount; up = true; }
        if ((int)distWalked > bestM) { bestM = (int)distWalked; up = true; }
        if (up) if (FILE *bf = fopen(bestPath, "w")) { fprintf(bf, "%d %d %d\n", bestEsc, bestKill, bestM); fclose(bf); }
    };

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
        SetShaderValue(worldShader, locGloss, &c.gloss, SHADER_UNIFORM_FLOAT);
        synth.tHum = lv == 0 ? 1.0f : lv == 4 ? 0.5f : 0.15f;
        synth.tDrone = lv == 1 ? 1.0f : 0.0f;
        synth.tWater = lv == 2 ? 1.0f : 0.0f;
        synth.tParty = lv == 4 ? 1.0f : 0.0f;
        nextBlackout = lv == 2 ? 1e18 : GetTime() + 30 + grng.f01() * 60;   // no blackouts in the poolrooms
        blackoutEnd = -1;
        taken.clear(); coinsWorld.clear(); chalk.clear();   // it's a different maze down here
        SetWindowTitle(TextFormat("THE BACKROOMS — %s", c.name));
    };
    applyLevel(0);
    if (const char *lvEnv = getenv("BACKROOMS_LEVEL")) applyLevel(atoi(lvEnv) % NLEVELS);   // testing

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
        bool crouched = IsKeyDown(KEY_LEFT_CONTROL);
        crouchCur += ((crouched ? 1.0f : 0.0f) - crouchCur) * fminf(1, 10 * dt);
        bool sprinting = moving && IsKeyDown(KEY_LEFT_SHIFT) && stamina > 0.02f && !crouched;
        stamina = clampf(stamina + (sprinting ? -dt / 10.0f : dt / 6.0f), 0, 1);
        boostT = fmaxf(0, boostT - dt);
        float groundY = world.groundAt(px, pz, py);
        bool inWater = grounded && py < -0.1f && world.poolAt(cellOf(px), cellOf(pz));
        float speed = (sprinting ? 6.8f : crouched ? 1.9f : 3.6f) * (inWater ? 0.55f : 1.0f)
                    * (boostT > 0 ? 1.12f : 1.0f);
        float tvx = ix * speed, tvz = iz * speed;
        float accel = moving ? 12.0f : 9.0f;
        velx += (tvx - velx) * fminf(1, accel * dt);
        velz += (tvz - velz) * fminf(1, accel * dt);
        px += velx * dt; pz += velz * dt;
        world.collideCircle(px, pz, PR, py);
        float spd = sqrtf(velx * velx + velz * velz);
        distWalked += spd * dt;

        // jump + floor height (groundY recomputed after collision; furniture tops count)
        groundY = world.groundAt(px, pz, py);
        if (IsKeyPressed(KEY_SPACE) && grounded) { vy = inWater ? 4.3f : 5.6f; grounded = false; }
        if (grounded) {
            if (py > groundY + 0.05f && world.poolAt(cellOf(px), cellOf(pz))) { grounded = false; vy = 0; }  // pool edge: drop in
            else {   // stairs, steps, furniture edges: glide to the new floor height
                py += (groundY - py) * fminf(1, 14 * dt);
                if (fabsf(py - groundY) < 0.004f) py = groundY;
            }
        }
        if (!grounded) {
            vy -= 20.0f * dt;
            py += vy * dt;
            if (py <= groundY) {
                py = groundY; grounded = true;
                if (groundY < -0.1f && world.poolAt(cellOf(px), cellOf(pz))) { SetSoundVolume(sndBigSplash, 0.7f); PlaySound(sndBigSplash); }
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
        float eyeY = 1.62f - 0.55f * crouchCur + py + sinf(bobPhase * 3.14159f) * 0.045f * bobAmt;
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

        // ---- dev tools (only while the F3 debug HUD is up)
        if (debugHud) {
            if (IsKeyPressed(KEY_B)) {   // force a blackout right now
                blackoutEnd = now + 3.0 + grng.f01() * 3.0;
                nextBlackout = level == 2 ? 1e18 : blackoutEnd + 45 + grng.f01() * 75;
            }
            if (IsKeyPressed(KEY_E)) {   // (re)spawn Clark stalking ~12m ahead
                Vector2 spot = world.findOpenSpot(px + f2x * 12, pz + f2z * 12);
                ent.x = spot.x; ent.z = spot.y;
                ent.st = EState::Stalk; ent.gaze = 0; ent.life = 0; ent.unseen = 0; ent.hp = 3;
            }
            if (IsKeyPressed(KEY_C)) {   // force chase (spawns him first if hidden)
                if (ent.st == EState::Hidden) {
                    Vector2 spot = world.findOpenSpot(px + f2x * 14, pz + f2z * 14);
                    ent.x = spot.x; ent.z = spot.y;
                    ent.hp = 3;
                }
                ent.st = EState::Chase; ent.gaze = 0; ent.life = 0; ent.unseen = 0;
            }
            if (IsKeyPressed(KEY_H)) {   // banish him
                ent.st = EState::Hidden; ent.nextSpawn = now + 20 + grng.f01() * 20;
            }
            if (IsKeyPressed(KEY_G)) { flares = MAXFLARES; ammo = MAXAMMO; reloadT = 0; }   // refill weapons
            if (IsKeyPressed(KEY_N)) {   // jump to next level (incl. Red Halls)
                applyLevel((level + 1) % NLEVELS);
                Vector2 spot = world.findOpenSpot(px, pz);
                px = spot.x; pz = spot.y; velx = velz = 0; py = 0; vy = 0; grounded = true;
                ent.st = EState::Hidden; ent.nextSpawn = now + 30;
            }
        }

        // ---- weapons: 1 flare, 2 revolver; wheel cycles; left click uses the selected one
        if (IsKeyPressed(KEY_ONE)) weapon = 0;
        if (IsKeyPressed(KEY_TWO)) weapon = 1;
        wheelCd = fmaxf(0, wheelCd - dt);
        if (wheelCd <= 0 && fabsf(GetMouseWheelMove()) > 0.5f) { weapon ^= 1; wheelCd = 0.25f; }
        gunCd = fmaxf(0, gunCd - dt);
        muzzleT = fmaxf(0, muzzleT - dt);
        recoil += (0 - recoil) * fminf(1, 10 * dt);
        if (reloadT > 0) {
            reloadT -= dt;
            if (reloadT <= 0) { ammo = MAXAMMO; SetSoundPitch(sndClick, 1.15f); PlaySound(sndClick); }
        }
        if (weapon == 1 && IsCursorHidden() && caughtT <= 0 && reloadT <= 0 && gunCd <= 0 &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (ammo <= 0) { SetSoundPitch(sndClick, 0.7f); PlaySound(sndClick); gunCd = 0.25f; }  // dry fire
            else {
                ammo--; gunCd = 0.42f; muzzleT = 0.09f; recoil = 1.0f;
                PlaySound(sndShot);
                // the report carries down every hallway
                if (ent.st == EState::Hidden) ent.nextSpawn = fmin(ent.nextSpawn, now + 5 + grng.f01() * 6);
                else if (ent.st == EState::Stalk) ent.gaze += 0.8f;
                if (ent.st == EState::Stalk || ent.st == EState::Chase || ent.st == EState::Flee) {
                    float ex = ent.x - px, ez = ent.z - pz;
                    float along = ex * f2x + ez * f2z;          // in front of the muzzle
                    float perp = fabsf(ex * r2x + ez * r2z);    // off the aim line
                    if (along > 0 && perp < 0.55f && world.lineOfSight(px, pz, ent.x, ent.z)) {
                        ent.hp--;
                        if (ent.hp <= 0) {   // put down
                            PlaySound(sndKill);
                            killT = 3.0f; killCount++;
                            ent.st = EState::Die; ent.life = 0;
                            for (int c2 = 0; c2 < 5; c2++) {   // he spills his doubloons
                                float aa = c2 * 1.2566f + grng.f01();
                                coinsWorld.push_back({ ent.x + cosf(aa) * 0.5f, 0, ent.z + sinf(aa) * 0.5f });
                            }
                            saveBest();
                        } else {             // staggered: knocked back, bolts
                            PlaySound(sndHit);
                            float dd = sqrtf(ex * ex + ez * ez);
                            if (dd > 0.01f) { ent.x += ex / dd * 2.0f; ent.z += ez / dd * 2.0f; }
                            world.collideCircle(ent.x, ent.z, 0.38f);
                            ent.st = EState::Flee; ent.life = 1.4f;
                        }
                    }
                }
            }
        }
        if (IsKeyPressed(KEY_R) && weapon == 1 && ammo < MAXAMMO && reloadT <= 0) {
            reloadT = 1.8f;
            SetSoundPitch(sndClick, 0.95f); PlaySound(sndClick);
        }

        // ---- flare weapon
        if (IsCursorHidden() && caughtT <= 0 && flares > 0 &&
            (IsKeyPressed(KEY_Q) || (weapon == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))) {
            flares--;
            flare.active = true; flare.flying = true; flare.burn = FLAREBURN;
            flare.x = px + fwd.x * 0.4f; flare.y = eyeY - 0.15f; flare.z = pz + fwd.z * 0.4f;
            flare.vx = fwd.x * 10.5f; flare.vz = fwd.z * 10.5f; flare.vy = fwd.y * 10.5f + 2.4f;
            PlaySound(sndFlare);
            if (ent.st == EState::Hidden) ent.nextSpawn = fmin(ent.nextSpawn, now + 12 + grng.f01() * 10);  // he hears the strike
        }
        if (flare.active) {
            if (flare.flying) {
                flare.vy -= 18.0f * dt;
                flare.x += flare.vx * dt; flare.y += flare.vy * dt; flare.z += flare.vz * dt;
                float ox = flare.x, oz = flare.z;
                world.collideCircle(flare.x, flare.z, 0.07f, flare.y);
                if (fabsf(ox - flare.x) > 1e-5f) flare.vx *= -0.35f;   // clatter off walls
                if (fabsf(oz - flare.z) > 1e-5f) flare.vz *= -0.35f;
                float fg = world.groundAt(flare.x, flare.z, flare.y);
                if (flare.y < fg + 0.04f && flare.vy < 0) {
                    flare.y = fg + 0.04f;
                    if (flare.vy < -2.0f) { flare.vy *= -0.30f; flare.vx *= 0.55f; flare.vz *= 0.55f; }
                    else { flare.flying = false; flare.vx = flare.vy = flare.vz = 0; }
                }
                if (world.poolAt(cellOf(flare.x), cellOf(flare.z)) && flare.y < -0.10f) {   // hit pool water: fizzles out
                    Sound &s = splashes[grng.ri(0, 1)];
                    SetSoundPitch(s, 1.1f); SetSoundVolume(s, 0.8f);
                    PlaySound(s);
                    flare.active = false;
                }
            }
            flare.burn -= dt;
            if (flare.burn <= 0) flare.active = false;
        }
        if (flares >= MAXFLARES) nextFlareRegen = now + 75;   // scavenge a fresh flare over time
        else if (now > nextFlareRegen) { flares++; nextFlareRegen = now + 75; }
        if (flare.active) {
            float fdx = flare.x - px, fdz = flare.z - pz;
            synth.hissTarget = clampf(flare.burn / 1.5f, 0, 1) / (1.0f + 0.05f * (fdx * fdx + fdz * fdz));
        } else synth.hissTarget = 0;

        // ---- pickups, drinking, vending machines, chalk
        int pci = cellOf(px), pck = cellOf(pz);
        auto cellKey2 = [](int a, int b) { return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b; };
        auto bottleAt = [&](int a, int b) {   // almond water, left out for whoever needs it
            if (world.pillarAt(a, b) || world.propAt(a, b) || world.poolAt(a, b)) return false;
            return ih(a, b, (uint32_t)world.seed ^ 0xA1A1u) % 331 == 0;
        };
        auto coinAt = [&](int a, int b) {     // a doubloon he dropped on his rounds
            if (world.pillarAt(a, b) || world.propAt(a, b) || world.poolAt(a, b)) return false;
            return ih(a, b, (uint32_t)world.seed ^ 0xC01Du) % 449 == 0;
        };
        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
            int a = pci + dx, b = pck + dz;
            uint64_t ky = cellKey2(a, b);
            if (taken.count(ky)) continue;
            bool isB = bottleAt(a, b), isC = !isB && coinAt(a, b);
            if (!isB && !isC) continue;
            float bxx = a * CELL + 1.0f, bzz = b * CELL + 1.0f;
            float ddx = px - bxx, ddz = pz - bzz;
            if (ddx * ddx + ddz * ddz < 0.8f * 0.8f) {
                taken.insert(ky);
                if (isB) almond++; else coins++;
                SetSoundPitch(sndClick, isB ? 1.3f : 1.6f); PlaySound(sndClick);
            }
        }
        for (size_t c2 = 0; c2 < coinsWorld.size();) {   // spilled doubloons
            float ddx = px - coinsWorld[c2].x, ddz = pz - coinsWorld[c2].z;
            if (ddx * ddx + ddz * ddz < 0.7f * 0.7f) {
                coins++;
                SetSoundPitch(sndClick, 1.6f); PlaySound(sndClick);
                coinsWorld.erase(coinsWorld.begin() + c2);
            } else ++c2;
        }
        if (IsKeyPressed(KEY_THREE) && almond > 0) {   // drink: catch your breath, steady your hands
            almond--; stamina = 1.0f; fear *= 0.35f; boostT = 8.0f;
            SetSoundPitch(splashes[0], 1.5f); SetSoundVolume(splashes[0], 0.5f);
            PlaySound(splashes[0]);
        }
        if (IsKeyPressed(KEY_E)) {   // vending machine: three doubloons a bottle
            for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
                int a = pci + dx, b = pck + dz;
                if (world.propAt(a, b) != 10) continue;
                float mx = a * CELL + 1.0f, mz = b * CELL + 1.0f;
                float ddx = px - mx, ddz = pz - mz;
                if (ddx * ddx + ddz * ddz < 1.6f * 1.6f && coins >= 3) {
                    coins -= 3; almond++;
                    SetSoundPitch(sndClick, 0.8f); PlaySound(sndClick);
                }
            }
        }
        if (IsKeyPressed(KEY_M)) {   // chalk mark: the only map you get
            chalk.push_back({ px + f2x * 0.5f, world.groundAt(px + f2x * 0.5f, pz + f2z * 0.5f, py) + 0.012f,
                              pz + f2z * 0.5f });
            if (chalk.size() > 128) chalk.erase(chalk.begin());
        }

        // ---- whispers in the walls
        if (whisperT <= 0 && now > nextWhisper && ent.st == EState::Hidden) {
            whisperT = 4.5f;
            nextWhisper = now + 70 + grng.f01() * 90;
        }
        whisperT = fmaxf(0, whisperT - dt);
        synth.whisperTarget = whisperT > 0 ? 0.55f : 0.0f;

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
            ent.st = EState::Stalk; ent.gaze = -100; ent.life = 0; ent.unseen = 0; ent.hp = 3;
        }
        float fearT = 0.06f;
        float entDist = 1e9f;
        bool entVisible = false;
        if (ent.st == EState::Hidden) {
            if (sprinting) ent.nextSpawn -= 1.5 * dt;   // running feet echo a long way
            if (now > ent.nextSpawn) {
                float a = grng.f01() * 6.2831853f;
                float d = 20 + grng.f01() * 10;
                Vector2 spot = world.findOpenSpot(px + cosf(a) * d, pz + sinf(a) * d);
                ent.x = spot.x; ent.z = spot.y;
                ent.st = EState::Stalk; ent.gaze = 0; ent.life = 0; ent.unseen = 0; ent.hp = 3;
                ent.dispY = world.floorY(cellOf(ent.x), cellOf(ent.z));
            }
        } else {
            float ex = ent.x - px, ez = ent.z - pz;
            entDist = sqrtf(ex * ex + ez * ez);
            float dirDot = (entDist > 0.01f) ? (fwd.x * ex + fwd.z * ez) / entDist : 1;
            entVisible = entDist < 36 && dirDot > 0.86f && world.lineOfSight(px, pz, ent.x, ent.z);
            if (crouchCur > 0.7f && entDist > 7) entVisible = false;   // low and quiet: hard to pick out
            if (flare.active && (ent.st == EState::Stalk || ent.st == EState::Chase)) {
                float fx = ent.x - flare.x, fz = ent.z - flare.z;
                if (fx * fx + fz * fz < 6.0f * 6.0f) {   // fire is the one thing he remembers
                    ent.st = EState::Flee; ent.life = 0; ent.gaze = 0;
                }
            }
            if (ent.st == EState::Stalk) {
                ent.life += dt;
                fearT = entVisible ? 0.45f : 0.15f;
                if (entVisible) ent.gaze += dt;
                if (ent.gaze > 1.6f || (entVisible && entDist < 8) || entDist < 3.0f) ent.st = EState::Chase;
                else if (ent.life > 24 && !entVisible) ent.st = EState::Hidden, ent.nextSpawn = now + 18 + grng.f01() * 35;
            }
            if (ent.st == EState::Chase) {
                fearT = entDist < 8 ? 1.0f : 0.8f;
                ent.lunge = fmaxf(0, ent.lunge - dt);
                if (entDist < 5.5f && entDist > 1.6f && ent.lunge <= 0 && grng.f01() < dt * 0.5f)
                    ent.lunge = 0.55f;   // sudden burst — don't let him get close
                float chaseSpd = 3.3f + 1.0f * clampf(1 - entDist / 25.0f, 0, 1) + (ent.lunge > 0 ? 3.2f : 0.0f);
                if (entDist > 0.01f) {
                    ent.x -= ex / entDist * chaseSpd * dt;
                    ent.z -= ez / entDist * chaseSpd * dt;
                }
                world.collideCircle(ent.x, ent.z, 0.38f);
                ent.unseen = entVisible ? 0 : ent.unseen + dt * (crouchCur > 0.7f ? 1.7f : 1.0f);
                if (ent.unseen > 6 && entDist > 14) ent.st = EState::Hidden, ent.nextSpawn = now + 25 + grng.f01() * 40;
                if (entDist < 1.25f) {   // caught
                    PlaySound(sndScare);
                    caughtT = 2.4f; caughtCount++;
                    saveBest();
                    float a = grng.f01() * 6.2831853f;
                    Vector2 spot = world.findOpenSpot(px + cosf(a) * 800, pz + sinf(a) * 800);
                    px = spot.x; pz = spot.y; velx = velz = 0;
                    ent.st = EState::Hidden; ent.nextSpawn = now + 30 + grng.f01() * 30;
                    entDist = 1e9f;
                }
            }
            if (ent.st == EState::Flee) {   // bolts away from the burning flare
                fearT = 0.18f;
                ent.life += dt;
                float rx = ent.x - (flare.active ? flare.x : px), rz = ent.z - (flare.active ? flare.z : pz);
                float rl = sqrtf(rx * rx + rz * rz);
                if (rl > 0.01f) { ent.x += rx / rl * 6.5f * dt; ent.z += rz / rl * 6.5f * dt; }
                world.collideCircle(ent.x, ent.z, 0.38f);
                if (ent.life > 3.0f) { ent.st = EState::Hidden; ent.nextSpawn = now + 25 + grng.f01() * 35; }
            }
            if (ent.st == EState::Die) {   // shot down: crumples, gone a long while
                fearT = 0.10f;
                ent.life += dt;
                if (ent.life > 1.2f) { ent.st = EState::Hidden; ent.nextSpawn = now + 90 + grng.f01() * 60; }
            }
        }
        if (ent.st != EState::Hidden) {   // he takes the stairs too, smoothly
            float egt = world.floorY(cellOf(ent.x), cellOf(ent.z));
            ent.dispY += (egt - ent.dispY) * fminf(1, 10 * dt);
        }
        fear += (fearT - fear) * fminf(1, 2.2f * dt);
        if (whisperT > 0) fear = fmaxf(fear, 0.22f);
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
                    saveBest();
                    applyLevel(EXIT_NEXT[level]);     // the exit leads deeper
                    Vector2 spot = world.findOpenSpot(px, pz);
                    px = spot.x; pz = spot.y; velx = velz = 0; py = 0; vy = 0; grounded = true;
                    ent.st = EState::Hidden; ent.nextSpawn = now + 30;
                }
            }
        }
        caughtT = fmaxf(0, caughtT - dt);
        escapeT = fmaxf(0, escapeT - dt);
        killT = fmaxf(0, killT - dt);

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
        float boSend = blackoutCur * (whisperT > 0 ? 0.86f : 1.0f);   // lights sag while it whispers
        SetShaderValue(worldShader, locBlackout, &boSend, SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldShader, locViewPos, &viewPos, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, locFlash, &flashCur, SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldShader, locFlashDir, &fwd, SHADER_UNIFORM_VEC3);
        float flick = 0.91f + 0.09f * sinf(timeF * 31.0f) * sinf(timeF * 47.3f + 1.3f);
        float flareInt = flare.active
            ? clampf((FLAREBURN - flare.burn) * 6.0f, 0, 1) * clampf(flare.burn / 1.5f, 0, 1) * flick
            : 0.0f;
        Vector3 flarePos = { flare.x, flare.y + 0.06f, flare.z };
        if (!flare.active && muzzleT > 0) {   // muzzle flash borrows the flare point light
            flareInt = muzzleT / 0.09f * 1.3f;
            flarePos = { px + f2x * 0.6f, eyeY - 0.05f, pz + f2z * 0.6f };
        }
        SetShaderValue(worldShader, locFlarePos, &flarePos, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, locFlareInt, &flareInt, SHADER_UNIFORM_FLOAT);

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
        for (int dx = -7; dx <= 7; dx++) for (int dz = -7; dz <= 7; dz++) {   // world pickups nearby
            int a = pci + dx, b = pck + dz;
            if (taken.count(cellKey2(a, b))) continue;
            bool isB = bottleAt(a, b), isC = !isB && coinAt(a, b);
            if (!isB && !isC) continue;
            float bxx = a * CELL + 1.0f, bzz = b * CELL + 1.0f;
            float gy = world.floorY(a, b);
            if (isB) {   // almond water: chubby bottle, faint glow
                DrawCylinder({ bxx, gy, bzz }, 0.055f, 0.065f, 0.19f, 8, { 88, 122, 176, 255 });
                DrawCylinder({ bxx, gy + 0.19f, bzz }, 0.02f, 0.05f, 0.07f, 8, { 120, 150, 195, 255 });
            } else {
                float bob = sinf((float)now * 2.0f + a * 1.7f + b) * 0.03f;
                DrawCylinder({ bxx, gy + 0.06f + bob, bzz }, 0.085f, 0.085f, 0.024f, 12, { 214, 172, 66, 255 });
            }
        }
        for (auto &cw : coinsWorld) {
            float gy = world.floorY(cellOf(cw.x), cellOf(cw.z));
            float bob = sinf((float)now * 2.4f + cw.x) * 0.03f;
            DrawCylinder({ cw.x, gy + 0.06f + bob, cw.z }, 0.085f, 0.085f, 0.024f, 12, { 214, 172, 66, 255 });
        }
        if (level == 4) {   // balloons nose against the ceiling, strings hanging down
            for (int dx = -7; dx <= 7; dx++) for (int dz = -7; dz <= 7; dz++) {
                int a = pci + dx, b = pck + dz;
                uint32_t h = ih(a, b, (uint32_t)world.seed ^ 0xBA11u);
                if (h % 17 != 0 || world.pillarAt(a, b)) continue;
                float bxx = a * CELL + 1.0f + (((h >> 4) & 7) / 7.0f - 0.5f) * 0.9f;
                float bzz = b * CELL + 1.0f + (((h >> 7) & 7) / 7.0f - 0.5f) * 0.9f;
                float bob = sinf((float)now * 0.8f + a * 1.3f + b * 2.1f) * 0.05f;
                float by = world.wallH - 0.21f + bob;
                Color bc = PARTY[(h >> 10) % 5];
                bc.r = (unsigned char)(bc.r * 0.72f); bc.g = (unsigned char)(bc.g * 0.72f);
                bc.b = (unsigned char)(bc.b * 0.72f);   // dim: no light of their own down here
                DrawSphere({ bxx, by, bzz }, 0.17f, bc);
                DrawCylinderEx({ bxx, by - 0.15f, bzz }, { bxx + 0.04f, by - 0.95f, bzz + 0.02f },
                               0.005f, 0.005f, 4, { 190, 185, 175, 150 });
            }
        }
        for (auto &cm : chalk)
            if (fabsf(cm.x - px) < 30 && fabsf(cm.z - pz) < 30) {
                DrawCylinderEx({ cm.x - 0.18f, cm.y, cm.z - 0.18f }, { cm.x + 0.18f, cm.y, cm.z + 0.18f },
                               0.014f, 0.014f, 5, { 228, 228, 218, 200 });
                DrawCylinderEx({ cm.x - 0.18f, cm.y, cm.z + 0.18f }, { cm.x + 0.18f, cm.y, cm.z - 0.18f },
                               0.014f, 0.014f, 5, { 228, 228, 218, 200 });
            }
        if (flare.active) {   // the flare itself: hot core, orange halo, stub of a body
            Vector3 fp = { flare.x, flare.y + 0.05f, flare.z };
            DrawCylinder({ flare.x, flare.y - 0.03f, flare.z }, 0.018f, 0.022f, 0.09f, 8, { 130, 30, 22, 255 });
            DrawSphere(fp, 0.035f + 0.012f * flick, { 255, 240, 208, 255 });
            DrawSphere(fp, 0.13f, { 255, 120, 40, (unsigned char)(90 * flareInt) });
            DrawSphere(fp, 0.30f, { 255, 70, 20, (unsigned char)(28 * flareInt) });
        }
        if (ent.st != EState::Hidden && entDist < 45) {
            const LevelCfg &c = LEVELS[level];
            float ambLum = (c.amb.x + c.amb.y + c.amb.z) / 3.0f;
            float eg = ent.dispY;
            float lum = lightAtCPU(ent.x, eg + 0.95f, ent.z, blackoutCur,
                                   c.ls, c.wallH - 0.12f, c.dead, c.lightMul, ambLum);
            if (flashCur > 0.05f) {   // flashlight picks him out of the dark
                float vx2 = ent.x - px, vz2 = ent.z - pz;
                float d2 = vx2 * vx2 + vz2 * vz2 + 1e-4f, dl = sqrtf(d2);
                float cone = powf(fmaxf((vx2 * fwd.x + vz2 * fwd.z) / dl, 0.0f), 26.0f);
                lum = clampf(lum + flashCur * cone * 7.5f / (1.0f + 0.10f * d2), 0.0f, 1.0f);
            }
            if (flareInt > 0.01f) {   // flare glow (or muzzle flash) reaches him too
                float fvx = ent.x - flarePos.x, fvz = ent.z - flarePos.z;
                lum = clampf(lum + flareInt * 3.0f / (1.0f + 0.30f * (fvx * fvx + fvz * fvz)), 0.0f, 1.0f);
            }
            float sink = 0, dieA = 1;
            if (ent.st == EState::Die) {   // crumples into the carpet
                float t = clampf(ent.life / 1.2f, 0, 1);
                sink = 1.1f * t * t; dieA = 1.0f - t;
            }
            float fogf = expf(-entDist * c.fogDen);
            unsigned char lum8 = cl8(40 + 215 * lum);
            unsigned char al = cl8(255 * clampf(fogf * 1.6f, 0, 1) * dieA);
            DrawBillboardRec(cam, texEntity, { 0, 0, 128, 256 },
                             { ent.x, eg + 0.98f - sink, ent.z }, { 0.98f, 1.96f }, { lum8, lum8, lum8, al });
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

        if (weapon == 1) {   // revolver viewmodel, bottom-right, kicks with recoil
            float deg = 210.0f + recoil * 16.0f;
            float rad = deg * DEG2RAD;
            float dirx = cosf(rad), diry = sinf(rad);   // along the barrel
            float nx = -diry, ny = dirx;                // toward the top of the gun
            float bobX = sinf(bobPhase * 3.14159f) * 3.0f * bobAmt;
            Vector2 piv = { sw - 165.0f + bobX - dirx * recoil * 22.0f,
                            sh - 30.0f + fabsf(bobX) * 0.6f - diry * recoil * 22.0f };
            // gun-local frame: t along the barrel, s toward the sights
            const float k = 1.25f;   // overall viewmodel scale
            auto pt = [&](float t, float s) {
                return Vector2{ piv.x + (dirx * t + nx * s) * k, piv.y + (diry * t + ny * s) * k };
            };
            auto quad = [&](Vector2 a, Vector2 b, Vector2 c2, Vector2 d2, Color col) {
                DrawTriangle(a, b, c2, col); DrawTriangle(a, c2, d2, col);
            };
            auto box = [&](float t0, float t1, float s0, float s1, Color col) {
                quad(pt(t0, s1), pt(t0, s0), pt(t1, s0), pt(t1, s1), col);
            };
            Color steel = { 40, 38, 44, 255 }, steel2 = { 57, 54, 62, 255 };
            Color dark = { 21, 20, 24, 255 }, wood = { 88, 58, 38, 255 };
            // grip rakes back and down off the bottom of the screen
            Vector2 gv = { (-dirx * 0.42f - nx * 0.91f) * 70.0f * k, (-diry * 0.42f - ny * 0.91f) * 70.0f * k };
            Vector2 g0 = pt(-14, -8), g1 = pt(12, -8);
            quad(g0, g1, { g1.x + gv.x, g1.y + gv.y },
                 { g0.x + gv.x - dirx * 8 * k, g0.y + gv.y - diry * 8 * k }, wood);
            box(-16, 34, -10, 8, steel);                    // frame rear + recoil shield
            box(-6, 90, 8, 13, steel);                      // top strap over the cylinder
            box(30, 76, -14, 13, steel2);                   // cylinder bulge
            box(43, 46, -12, 11, dark);                     // cylinder flutes
            box(59, 62, -12, 11, dark);
            box(82, 168, -3, 11, steel);                    // barrel
            box(88, 138, -8, -3, steel2);                   // ejector rod shroud under it
            box(163, 168, -3, 11, dark);                    // muzzle band
            box(-26, -14, 9, 19, steel2);                   // hammer spur
            box(-10, -2, 13, 17, steel);                    // rear sight
            box(156, 163, 11, 17, steel);                   // front sight
            DrawRing(pt(24, -15), 7.5f * k, 10.5f * k, 0, 360, 24, steel);   // trigger guard
            box(20, 24, -16, -9, dark);                     // trigger
            if (muzzleT > 0) {
                float mt = muzzleT / 0.09f;
                Vector2 tip = pt(180, 4);
                DrawCircleV(tip, 46 * mt, { 255, 150, 60, (unsigned char)(90 * mt) });
                DrawCircleV(tip, 24 * mt, { 255, 225, 140, (unsigned char)(210 * mt) });
            }
            DrawCircle(sw / 2, sh / 2, 2.0f, { 230, 220, 190, 110 });                     // crosshair
        }

        if (elapsed < 9.0) {   // intro
            float a = 1.0f - clampf((float)elapsed / 3.0f, 0, 1);
            DrawRectangle(0, 0, sw, sh, Fade(BLACK, a));
            float ta = clampf((float)elapsed / 1.5f, 0, 1) * (1.0f - clampf(((float)elapsed - 6.0f) / 3.0f, 0, 1));
            const char *t1 = "L E V E L   0";
            DrawText(t1, sw / 2 - MeasureText(t1, 52) / 2, sh / 3, 52, Fade({ 220, 205, 150, 255 }, ta));
            const char *t2 = "if you're reading this, you've already noclipped";
            DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 3 + 66, 18, Fade({ 160, 150, 110, 255 }, ta * 0.9f));
            const char *t3 = "WASD walk   SHIFT run   CTRL crouch   SPACE jump   F flashlight   1/2 weapon   3 drink   M chalk   E vend";
            DrawText(t3, sw / 2 - MeasureText(t3, 16) / 2, sh - 60, 16, Fade({ 140, 132, 100, 255 }, ta * 0.8f));
            if (bestEsc || bestKill || bestM) {
                const char *tb = TextFormat("best: %d escape%s  ·  %d clark%s put down  ·  %d m wandered",
                                            bestEsc, bestEsc == 1 ? "" : "s", bestKill, bestKill == 1 ? "" : "s", bestM);
                DrawText(tb, sw / 2 - MeasureText(tb, 16) / 2, sh / 3 + 98, 16, Fade({ 150, 140, 105, 255 }, ta * 0.8f));
            }
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
        if (killT > 0) {
            float a = clampf(killT / 3.0f, 0, 1);
            const char *t = "PIRATE CLARK IS DOWN";
            DrawText(t, sw / 2 - MeasureText(t, 44) / 2, sh / 2 - 96, 44, Fade({ 205, 60, 40, 255 }, a));
            const char *t2 = TextFormat("...but nothing stays down, down here   ·   %d put down", killCount);
            DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 2 - 44, 18, Fade({ 150, 122, 100, 255 }, a * 0.9f));
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
        {   // inventory, bottom-left; the selected weapon is lit
            const char *w0 = TextFormat("1  flare  ×%d", flares);
            const char *w1 = reloadT > 0 ? "2  revolver  [reloading]"
                                         : TextFormat("2  revolver  %d/%d%s", ammo, MAXAMMO, ammo == 0 ? "  — R" : "");
            Color selc = { 235, 200, 130, 210 }, dimc = { 150, 138, 112, 110 };
            if (coins > 0)
                DrawText(TextFormat("doubloons  ×%d", coins), 16, sh - 94, 16, { 214, 178, 92, 170 });
            DrawText(TextFormat("3  almond water  ×%d", almond), 16, sh - 72, 16,
                     almond > 0 ? Color{ 150, 190, 235, 170 } : dimc);
            DrawText(w0, 16, sh - 50, 16, weapon == 0 ? selc : dimc);
            DrawText(w1, 16, sh - 28, 16, weapon == 1 ? selc : dimc);
        }
        if (stamina < 0.98f) {   // sprint bar, bottom centre
            int bw = 220, bx2 = sw / 2 - bw / 2, by2 = sh - 42;
            DrawRectangle(bx2 - 1, by2 - 1, bw + 2, 8, { 0, 0, 0, 120 });
            DrawRectangle(bx2, by2, (int)(bw * stamina), 6, { 200, 180, 120, 160 });
        }
        if (crouchCur > 0.5f)
            DrawText("[ crouched ]", sw / 2 - MeasureText("[ crouched ]", 14) / 2, sh - 62, 14, { 180, 170, 140, 120 });
        if (debugHud) {
            DrawText(TextFormat("%d fps  pos(%.0f, %.0f)  chunks %d  entity %s  d=%.0fm",
                                GetFPS(), px, pz, (int)world.chunks.size(),
                                ent.st == EState::Hidden ? "hidden" : ent.st == EState::Stalk ? "STALKING"
                                    : ent.st == EState::Chase ? "CHASING"
                                    : ent.st == EState::Flee ? "FLEEING" : "DYING",
                                entDist > 1e8 ? 0.0f : entDist),
                     12, 12, 18, { 230, 220, 160, 220 });
            DrawText("dev: B blackout   E spawn   C chase   H hide   G flares   N next level",
                     12, 34, 16, { 200, 190, 140, 180 });
        }
        EndDrawing();

        if (shotPath && frame == 600) {
            TakeScreenshot(shotPath);
            printf("fps=%d chunks=%d\n", GetFPS(), (int)world.chunks.size());
            break;
        }
    }

    saveBest();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
