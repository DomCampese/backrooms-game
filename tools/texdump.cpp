// texdump: run every texture generator in web/src/textures.cpp without a window or
// a GL context, write each result to PNG and print its mean colour.
//
//   tools/sandbox-build.sh texdump
//   ./texdump out/                # every texture
//   ./texdump out/ carpet ceiling # only the ones whose name contains a filter
//   ./texdump --unreal dir/ [revolver.glb]   # what the Unreal import needs, and world.json
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
#include "../shared/port/levels.h"
#include "../shared/port/model.h"
#include "../web/src/textures.h"
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

// How a texture is sampled in the Web build, which the Unreal import copies:
// mipmapped trilinear (finishTexture's tiled, the can, the deck, the
// revolver's maps) or plain bilinear, and repeating or clamped.
struct Sampling { bool mips, clamp; };
static const Sampling TILED = { true, false }, FLAT = { false, false }, CLAMPED = { false, true };

static FILE *gJson;
static bool gFirst = true;
static void exported(const char *name, Texture2D t, Sampling s) {
    dump(name, t, false);
    fprintf(gJson, "%s\n    {\"name\": \"%s\", \"mips\": %s, \"clamp\": %s}", gFirst ? "" : ",", name,
            s.mips ? "true" : "false", s.clamp ? "true" : "false");
    gFirst = false;
}
static Texture2D imageTexture(Image img) {
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    return t;
}

// The revolver's maps as the Web build draws them (web/src/model_asset.cpp):
// each material's base colour, and its detail map packed from the normal and
// metallic-roughness images by port/model.h's glbDetailMap.
static bool exportRevolver(const char *path) {
    int size = 0;
    unsigned char *data = LoadFileData(path, &size);
    GlbModel glb;
    std::string error;
    if (!data || !loadGlb(data, (size_t)size, glb, error)) {
        fprintf(stderr, "texdump: %s: %s\n", path, data ? error.c_str() : "cannot read");
        if (data) UnloadFileData(data);
        return false;
    }
    auto image = [&](int i) {
        const GlbModel::Image &im = glb.images[(size_t)i];
        return LoadImageFromMemory(im.mime == "image/png" ? ".png" : ".jpg", data + im.offset, (int)im.size);
    };
    for (size_t m = 0; m < glb.materials.size(); m++) {
        const GlbMaterial &mat = glb.materials[m];
        char name[64];
        snprintf(name, sizeof name, "revolver_%zu", m);
        exported(name, imageTexture(mat.baseColour >= 0 ? image(mat.baseColour) : GenImageColor(1, 1, WHITE)), TILED);
        Image n = mat.normal >= 0 ? image(mat.normal) : GenImageColor(1, 1, { 128, 128, 255, 255 });
        Image r = mat.metalRough >= 0 ? image(mat.metalRough) : GenImageColor(n.width, n.height, { 255, 255, 0, 255 });
        if (r.width != n.width || r.height != n.height) ImageResize(&r, n.width, n.height);
        Color *nc = LoadImageColors(n), *rc = LoadImageColors(r);
        glbDetailMap((const uint8_t *)nc, (const uint8_t *)rc, n.width * n.height, (uint8_t *)nc);
        Image packed = { nc, n.width, n.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        snprintf(name, sizeof name, "revolver_%zu_detail", m);
        exported(name, LoadTextureFromImage(packed), TILED);
        UnloadImageColors(nc);
        UnloadImageColors(rc);
        UnloadImage(n);
        UnloadImage(r);
    }
    UnloadFileData(data);
    return true;
}

// What the Unreal editor imports (unreal/Plugins/Backrooms/Content/Python/
// backrooms_looks.py): every texture the Web build binds when it draws the
// world (each surface and its detail map, the atlases, the objects' maps, the
// neutral detail map, the particle, the actors' sheets and the revolver's
// maps), and world.json, which says how each is sampled and gives the
// numbers worldFragmentShader() writes into the GLSL, for the HLSL's defines.
static int dumpForUnreal(const char *revolver) {
    std::string path = gOut + "/world.json";
    gJson = fopen(path.c_str(), "w");
    if (!gJson) { fprintf(stderr, "texdump: cannot write %s\n", path.c_str()); return 1; }
    fprintf(gJson, "{\"defines\": {");
    const struct { const char *name; float value; } numbers[] = {
        { "PANEL_HALF", PANEL_HALF }, { "STOREY_HASH_STEP", STOREY_HASH_STEP }, { "STOREY_HASH_SPAN", STOREY_HASH_SPAN },
        { "TUBE_HASH_X", TUBE_HASH_X }, { "TUBE_HASH_Z", TUBE_HASH_Z }, { "TUBE_HASH_SCALE", TUBE_HASH_SCALE },
        { "TUBE_STOREY_SKEW", TUBE_STOREY_SKEW }, { "TUBE_OUTPUT_SPREAD", TUBE_OUTPUT_SPREAD },
        { "TUBE_STUTTER_SPREAD", TUBE_STUTTER_SPREAD }, { "TUBE_STUTTER_RATE", TUBE_STUTTER_RATE },
        { "TUBE_STUTTER_PHASE", TUBE_STUTTER_PHASE }, { "TUBE_STUTTER_HASH", TUBE_STUTTER_HASH },
        { "TUBE_STUTTER_OPEN", TUBE_STUTTER_OPEN }, { "TUBE_STUTTER_DIP", TUBE_STUTTER_DIP },
        { "TUBE_BUZZ_BASE", TUBE_BUZZ_BASE }, { "TUBE_BUZZ_SPREAD", TUBE_BUZZ_SPREAD },
        { "TUBE_BUZZ_PHASE", TUBE_BUZZ_PHASE }, { "TUBE_BUZZ_DUTY", TUBE_BUZZ_DUTY },
        { "PILLAR_LO", PILLAR_LO }, { "PILLAR_HI", PILLAR_HI },
    };
    for (size_t i = 0; i < sizeof numbers / sizeof numbers[0]; i++)
        fprintf(gJson, "%s\n    \"BR_%s\": %.9g", i ? "," : "", numbers[i].name, numbers[i].value);
    // The occupancy grid's bits, which the HLSL masks with, so ints.
    fprintf(gJson, "\n  },\n  \"bits\": {");
    const struct { const char *name; int value; } bits[] = {
        { "OCC_WALL_N", OCC_WALL_N }, { "OCC_WALL_W", OCC_WALL_W }, { "OCC_PILLAR", OCC_PILLAR },
        { "OCC_OPEN_ABOVE", OCC_OPEN_ABOVE }, { "OCC_NEAR_OPENING", OCC_NEAR_OPENING },
    };
    for (size_t i = 0; i < sizeof bits / sizeof bits[0]; i++)
        fprintf(gJson, "%s\n    \"BR_%s\": %d", i ? "," : "", bits[i].name, bits[i].value);
    fprintf(gJson, "\n  },\n  \"textures\": [");
    for (int k = 0; k < SURF_COUNT; k++) {
        Surface sf = timed([k] { return makeSurface((SurfSlot)k); });
        printf("%-16s generated in %.0f ms\n", SURF_NAMES[k], gGenMs);
        exported(SURF_NAMES[k], sf.albedo, TILED);
        exported((std::string(SURF_NAMES[k]) + "_detail").c_str(), sf.detail, TILED);
    }
    Texture2D props = makePropsTex();
    exported("props", props, TILED);
    exported("props_detail", makePropDetail(props), TILED);
    exported("fixtures", makeFixturesTex(), FLAT);
    exported("scrawl", makeScrawlTex(), FLAT);
    exported("ao", makeAOStripTex(), CLAMPED);
    exported("can", makeAlmondWrapTex(), TILED);
    exported("deck", makeDeckTex(), TILED);
    // game.cpp's neutral detail map: alpha 250, level-relative gloss, no relief
    exported("neutral", imageTexture(GenImageColor(1, 1, { 128, 128, 255, 250 })), FLAT);
    exported("particle", makeParticleTex(), FLAT);
    // The actors' sheets (port/sheets.h has their layout), clamped so the
    // last column of frames does not sample the first.
    exported("clark", makeClarkTex(), CLAMPED);
    exported("smiler", makeSmilerTex(false), CLAMPED);
    exported("smiler_glow", makeSmilerTex(true), CLAMPED);
    exported("partygoer", makePartygoerTex(), CLAMPED);
    exported("dog", makeDogTex(), CLAMPED);
    const bool gun = exportRevolver(revolver);
    fprintf(gJson, "\n  ]\n}\n");
    fclose(gJson);
    printf("wrote %s\n", path.c_str());
    return gun ? 0 : 1;
}

int main(int argc, char **argv) {
    SetTraceLogLevel(LOG_WARNING);
    if (argc > 2 && strcmp(argv[1], "--unreal") == 0) {
        gOut = argv[2];
        return dumpForUnreal(argc > 3 ? argv[3] : "assets/models/revolver.glb");
    }
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
