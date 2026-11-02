// The camcorder's tape look three ways on one GPU: TAPE_FS as the Web build
// compiles it; the Unreal port's BackroomsTape.ush; and M_BackroomsTape's
// Custom node round it, which reads its reel Unreal's way up. The HLSL is
// compiled by DXC to SPIR-V and back to GLSL by spirv-cross
// (tools/tape-check.sh does that and runs this). Each draws the same pictures
// under the same uniforms into a target; the frames must match TAPE_FS's.
//
//   tape-check USH_AS_GLSL MATERIAL_AS_GLSL PICTURE.png
#include "raylib.h"
#include "shaders.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
constexpr int W = 720, H = 425;   // the 1440x850 window at half size
// The reel's layout at 1440x850 (Camcorder::fitReel): 288 x 170 cells, 7 across.
constexpr int CELL_W = 288, CELL_H = 170, COLS = 7, ROWS = 10;

struct Case {
    const char *name;
    int slotA, slotB;
    float time, mix, track, still, snow;
};

// A slot's rectangle on the reel as uv, v up the picture: Camcorder::cellUV
// for a reel whose rows run up from the bottom of the texture.
Vector4 cell(int slot) {
    const float rw = CELL_W * COLS, rh = CELL_H * ROWS;
    const float x = (float)(slot % COLS * CELL_W), y = (float)(slot / COLS * CELL_H);
    return { x / rw, y / rh, (x + CELL_W) / rw, (y + CELL_H) / rh };
}

// TAPE_FS's uniforms (the Web build's and the .ush's), or the material's:
// the collection's vectors and the view's size.
struct Tape {
    Shader sh;
    bool material;
    std::vector<int> locs;
    Tape(Shader s, const std::string &prefix, bool isMaterial) : sh(s), material(isMaterial) {
        const char *tape[] = { "uTime", "uRes", "uCellA", "uCellB", "uMix", "uTrack", "uStill", "uSnow" };
        const char *mat[] = { "View.ViewSizeAndInvSize", "Post", "TapeA", "TapeB", "Tape", "TapeFrom" };
        if (material) for (const char *n : mat) locs.push_back(GetShaderLocation(sh, (prefix + n).c_str()));
        else for (const char *n : tape) locs.push_back(GetShaderLocation(sh, (prefix + n).c_str()));
    }
    bool complete() const {
        for (int l : locs) if (l < 0) return false;
        return true;
    }
    void draw(const Case &c, Texture2D tex, RenderTexture2D target) const {
        const Vector4 a = cell(c.slotA), b = cell(c.slotB);
        if (material) {
            const Vector4 view = { (float)W, (float)H, 1.0f / W, 1.0f / H }, post = { c.time, 0, 0, 0 },
                          look = { c.mix, 0, c.track, c.still }, from = { c.snow, 1.0f, 0, 0 };
            const Vector4 *v[] = { &view, &post, &a, &b, &look, &from };
            for (int i = 0; i < 6; i++) SetShaderValue(sh, locs[i], v[i], SHADER_UNIFORM_VEC4);
        } else {
            const Vector2 r = { (float)W, (float)H };
            const void *v[] = { &c.time, &r, &a, &b, &c.mix, &c.track, &c.still, &c.snow };
            const int type[] = { SHADER_UNIFORM_FLOAT, SHADER_UNIFORM_VEC2, SHADER_UNIFORM_VEC4, SHADER_UNIFORM_VEC4,
                                 SHADER_UNIFORM_FLOAT, SHADER_UNIFORM_FLOAT, SHADER_UNIFORM_FLOAT,
                                 SHADER_UNIFORM_FLOAT };
            for (int i = 0; i < 8; i++) SetShaderValue(sh, locs[i], v[i], type[i]);
        }
        BeginTextureMode(target);
        ClearBackground(BLANK);
        BeginShaderMode(sh);
        DrawTexturePro(tex, { 0, 0, (float)tex.width, (float)tex.height }, { 0, 0, (float)W, (float)H }, { 0, 0 }, 0,
                       WHITE);
        EndShaderMode();
        EndTextureMode();
    }
};
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: tape-check USH_AS_GLSL MATERIAL_AS_GLSL PICTURE.png\n");
        return 2;
    }
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(W, H, "tape-check");
    char *ush = LoadFileText(argv[1]), *glue = LoadFileText(argv[2]);
    if (!ush || !glue) {
        fprintf(stderr, "cannot read %s or %s\n", argv[1], argv[2]);
        return 2;
    }
    const Tape web(LoadShaderFromMemory(nullptr, TAPE_FS), "", false);
    const Tape ported(LoadShaderFromMemory(nullptr, ush), "Uniforms.", false);
    const Tape material(LoadShaderFromMemory(nullptr, glue), "Material.", true);
    UnloadFileText(ush);
    UnloadFileText(glue);
    if (!web.complete() || !ported.complete() || !material.complete()) {
        fprintf(stderr, "FAIL a shader did not compile, or lacks a uniform (see the SHADER lines above)\n");
        return 1;
    }

    // A reel cut from the picture (bilinear, as CamcorderReel::fit sets it),
    // and the same reel the material's way up: Unreal's rows run down from v = 0.
    Image picture = LoadImage(argv[3]);
    if (!picture.data) {
        fprintf(stderr, "cannot read %s\n", argv[3]);
        return 2;
    }
    ImageFormat(&picture, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageResize(&picture, CELL_W * COLS, CELL_H * ROWS);
    Texture2D reel = LoadTextureFromImage(picture);
    SetTextureFilter(reel, TEXTURE_FILTER_BILINEAR);
    ImageFlipVertical(&picture);
    Texture2D reelUp = LoadTextureFromImage(picture);
    SetTextureFilter(reelUp, TEXTURE_FILTER_BILINEAR);
    RenderTexture2D outWeb = LoadRenderTexture(W, H), outPort = LoadRenderTexture(W, H);

    // Every term: interference light and heavy (the tearing band needs track
    // over 0.15), a paused frame, snow, the playback's cross-fade, and clocks
    // from a fresh start to an hour in, where sin's argument is large.
    std::vector<Case> cases = {
        { "clean", 0, 0, 1.25f, 0, 0, 0, 0 },
        { "track 0.4", 3, 3, 12.0f, 0, 0.4f, 0, 0 },
        { "torn", 9, 9, 33.3f, 0, 1.0f, 0, 0 },
        { "mix", 23, 24, 140.2f, 0.37f, 0.1f, 0, 0 },
        { "late", 63, 0, 300.0f, 0.9f, 0.2f, 0, 0 },
        { "still", 41, 41, 512.75f, 0, 0.3f, 1, 0 },
        { "snow", 5, 6, 1000.0f, 0.5f, 0.6f, 0, 0.6f },
        { "all snow", 5, 6, 3600.5f, 0.5f, 1.0f, 1, 1.0f },
    };
    SetRandomSeed(1337);
    for (int i = 0; i < 40; i++) {   // and random states
        Case c = { "random", GetRandomValue(0, 69), GetRandomValue(0, 69), GetRandomValue(0, 400000) / 100.0f,
                   GetRandomValue(0, 100) / 100.0f, GetRandomValue(0, 100) / 100.0f,
                   GetRandomValue(0, 3) == 0 ? 1.0f : 0.0f,
                   GetRandomValue(0, 4) == 0 ? GetRandomValue(0, 100) / 100.0f : 0.0f };
        cases.push_back(c);
    }

    int bad = 0;
    for (const Tape *port : { &ported, &material }) {
        const char *what = port == &ported ? "BackroomsTape.ush" : "M_BackroomsTape";
        long long total = 0, differ = 0;
        int worst = 0, failed = 0;
        for (const Case &c : cases) {
            web.draw(c, reel, outWeb);
            port->draw(c, port->material ? reelUp : reel, outPort);
            Image a = LoadImageFromTexture(outWeb.texture), b = LoadImageFromTexture(outPort.texture);
            const unsigned char *pa = (const unsigned char *)a.data, *pb = (const unsigned char *)b.data;
            long long n = (long long)W * H, px = 0;
            int most = 0;
            for (long long p = 0; p < n; p++) {
                int d = 0;
                for (int k = 0; k < 4; k++) d = std::max(d, abs(pa[p * 4 + k] - pb[p * 4 + k]));
                if (d) px++;
                most = std::max(most, d);
            }
            total += n;
            differ += px;
            worst = std::max(worst, most);
            if (px) {
                failed++;
                printf("FAIL %s %-16s t=%9.2f %lld px differ, by up to %d\n", what, c.name, c.time, px, most);
            }
            UnloadImage(a);
            UnloadImage(b);
        }
        printf("%s %s: %d cases, %lld pixels, %lld differ (most by %d)\n", failed ? "FAIL" : "ok  ", what,
               (int)cases.size(), total, differ, worst);
        bad += failed;
    }
    CloseWindow();
    return bad ? 1 : 0;
}
