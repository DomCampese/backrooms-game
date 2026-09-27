// texdump: run every texture generator in src/textures.cpp without a window or
// a GL context, write each result to PNG and print its mean colour.
//
//   tools/sandbox-build.sh texdump
//   ./texdump out/                # every texture
//   ./texdump out/ carpet ceiling # only the ones whose name contains a filter
//
// Why it exists: a capture shows you a texture through the lighting, the tone
// curve, the fog and the post pass, which is the wrong place to look for a seam
// at the wrap or to check that a rework kept a surface's mean albedo. Every
// ambient in the level table is tuned against the textures' brightness, so a
// surface that comes out 15% darker is a level that comes out 15% darker; the
// mean printed here is the number to hold steady.
//
// How: the generators end in LoadTextureFromImage and friends, which need GL.
// This file defines those few functions itself. A definition in an object file
// wins over the shared library at link time, so the generators call these, the
// image is kept instead of uploaded, and nothing else in raylib is touched.
// Detail maps are written too (<name>_detail.png): RG are the packed slopes,
// B the gloss mask. A "_wrap" copy tiles each surface 2x2 so a seam at the
// repeat is in the middle of the picture instead of at its edges.
#include "raylib.h"
#include "../src/textures.h"
#include "../src/hand.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static std::map<unsigned, Image> gImages;
static unsigned gNextId = 1;

extern "C" {
Texture2D LoadTextureFromImage(Image image) {
    Texture2D t{};
    t.id = gNextId++;
    t.width = image.width; t.height = image.height; t.mipmaps = 1; t.format = image.format;
    gImages[t.id] = ImageCopy(image);
    return t;
}
void GenTextureMipmaps(Texture2D *) {}
void SetTextureFilter(Texture2D, int) {}
void SetTextureWrap(Texture2D, int) {}
void UnloadTexture(Texture2D t) {
    auto it = gImages.find(t.id);
    if (it != gImages.end()) { UnloadImage(it->second); gImages.erase(it); }
}
// Meshes stay on the CPU here: dumpMesh writes them out as OBJ.
void UploadMesh(Mesh *, bool) {}
void UnloadMesh(Mesh) {}
Image LoadImageFromTexture(Texture2D t) {
    auto it = gImages.find(t.id);
    return it != gImages.end() ? ImageCopy(it->second) : GenImageColor(1, 1, BLANK);
}
}

static std::string gOut = ".";
static std::vector<std::string> gFilters;

static bool wanted(const char *name) {
    if (gFilters.empty()) return true;
    for (auto &f : gFilters) if (strstr(name, f.c_str())) return true;
    return false;
}

static void dump(const char *name, Texture2D t, bool tiled) {
    auto it = gImages.find(t.id);
    if (it == gImages.end()) { printf("%-16s (no image)\n", name); return; }
    Image img = ImageCopy(it->second);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const Color *p = (const Color *)img.data;
    double r = 0, g = 0, b = 0, a = 0, n = (double)img.width * img.height;
    for (int i = 0; i < img.width * img.height; i++) { r += p[i].r; g += p[i].g; b += p[i].b; a += p[i].a; }
    printf("%-16s %4dx%-4d mean %6.1f %6.1f %6.1f  luma %6.1f  alpha %5.1f\n", name, img.width, img.height,
           r / n, g / n, b / n, (0.299 * r + 0.587 * g + 0.114 * b) / n, a / n);
    std::string base = gOut + "/" + name;
    ExportImage(img, (base + ".png").c_str());
    if (tiled) {
        // 2x2 of the tile, shifted half a tile, so both seams cross the middle
        Image w = GenImageColor(img.width * 2, img.height * 2, BLANK);
        for (int ty = 0; ty < 2; ty++) for (int tx = 0; tx < 2; tx++)
            ImageDraw(&w, img, { 0, 0, (float)img.width, (float)img.height },
                      { (float)(tx * img.width), (float)(ty * img.height), (float)img.width, (float)img.height }, WHITE);
        ImageCrop(&w, { (float)img.width / 2, (float)img.height / 2, (float)img.width, (float)img.height });
        ExportImage(w, (base + "_wrap.png").c_str());
        UnloadImage(w);
    }
    UnloadImage(img);
}

// Generation time, not counting the PNG export: this is startup time in the game.
static double gGenMs = 0;
template <class F> static auto timed(F make) {
    auto t0 = std::chrono::steady_clock::now();
    auto r = make();
    gGenMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}
static void surface(const char *name, Surface s) {
    if (!wanted(name)) return;
    printf("%-16s generated in %.0f ms\n", name, gGenMs);
    dump(name, s.albedo, true);
    dump((std::string(name) + "_detail").c_str(), s.detail, true);
}
static void sprite(const char *name, Texture2D (*make)()) {
    if (!wanted(name)) return;
    dump(name, make(), false);
}

static void dumpMesh(const char *name, const Mesh &m) {
    std::string path = gOut + "/" + name + ".obj";
    FILE *f = fopen(path.c_str(), "w");
    if (!f) return;
    for (int i = 0; i < m.vertexCount; i++) fprintf(f, "v %f %f %f\n", m.vertices[i * 3], m.vertices[i * 3 + 1], m.vertices[i * 3 + 2]);
    for (int i = 0; i < m.vertexCount; i++) fprintf(f, "vn %f %f %f\n", m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2]);
    for (int t = 0; t < m.triangleCount; t++) {
        int a = m.indices[t * 3] + 1, b = m.indices[t * 3 + 1] + 1, c = m.indices[t * 3 + 2] + 1;
        fprintf(f, "f %d//%d %d//%d %d//%d\n", a, a, b, b, c, c);
    }
    fclose(f);
    printf("%-16s %d vertices, %d triangles -> %s\n", name, m.vertexCount, m.triangleCount, path.c_str());
}

int main(int argc, char **argv) {
    SetTraceLogLevel(LOG_WARNING);
    if (argc > 1) gOut = argv[1];
    for (int i = 2; i < argc; i++) gFilters.push_back(argv[i]);
    // Surfaces are generated only when asked for: several take a second each.
    auto want = [&](const char *n) { return wanted(n) || wanted((std::string(n) + "_detail").c_str()); };
    if (want("wallpaper"))    surface("wallpaper", timed([] { return makeWallpaperSurface(); }));
    if (want("carpet"))       surface("carpet", timed([] { return makeCarpetSurface(); }));
    if (want("ceiling"))      surface("ceiling", timed([] { return makeCeilingSurface(); }));
    if (want("concwall"))     surface("concwall", timed([] { return makeConcreteWallSurface(); }));
    if (want("concfloor"))    surface("concfloor", timed([] { return makeConcreteFloorSurface(); }));
    if (want("concceil"))     surface("concceil", timed([] { return makeConcreteCeilSurface(); }));
    if (want("brick"))        surface("brick", timed([] { return makeRedBrickSurface(); }));
    if (want("poolfloor"))    surface("poolfloor", timed([] { return makeTileSurface(false); }));
    if (want("poolwall"))     surface("poolwall", timed([] { return makeTileSurface(true); }));
    if (want("partywall"))    surface("partywall", timed([] { return makePartyWallSurface(); }));
    if (want("partycarpet"))  surface("partycarpet", timed([] { return makePartyCarpetSurface(); }));
    if (want("partyceil"))    surface("partyceil", timed([] { return makePartyCeilSurface(); }));
    if (wanted("props")) {
        Texture2D props = makePropsTex();
        dump("props", props, false);
        dump("props_detail", makePropDetail(props), false);
    }
    if (wanted("hand")) {
        HeldHand h = timed([] { return buildHeldHand(); });
        printf("%-16s generated in %.0f ms\n", "hand", gGenMs);
        dumpMesh("glove", h.glove); dumpMesh("sleeve", h.sleeve);
        dump("leather", h.leather, true); dump("knit", h.knit, true);
    }
    sprite("fixtures", makeFixturesTex);
    sprite("scrawl", makeScrawlTex);
    sprite("can", makeAlmondWrapTex);
    sprite("deck", makeDeckTex);
    sprite("clark", makeClarkTex);
    sprite("partygoer", makePartygoerTex);
    sprite("dog", makeDogTex);
    if (wanted("smiler")) { dump("smiler", makeSmilerTex(false), false); dump("smiler_glow", makeSmilerTex(true), false); }
    return 0;
}
