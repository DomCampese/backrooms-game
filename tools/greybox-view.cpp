// Draws the greybox (shared/port/greybox.h) round a spot with raylib and saves one
// frame, with the game's camera at the same spot, so a greybox frame can be laid
// over a capture of the game. What a port's first milestone should show,
// checked before any engine is involved.
//
//   tools/sandbox-build.sh greybox-view
//   BACKROOMS_SHOT=out.png BACKROOMS_LEVEL=0 BACKROOMS_POS=15,15,0.8 ./greybox-view
//
// Reads BACKROOMS_SEED, BACKROOMS_LEVEL, BACKROOMS_VISIT (default: the visit a
// capture of that level shows), BACKROOMS_STOREY and BACKROOMS_POS as the game
// does, and writes BACKROOMS_SHOT into the working directory, as the game's
// TakeScreenshot does. Prints each chunk's triangle count.
//
// BACKROOMS_TEXTURED=1 puts the level's surfaces on floors, ceilings and walls
// through the greybox's texture coordinates, at the tile sizes the Unreal
// material uses (unreal/Plugins/Backrooms/Content/Python/backrooms_looks.py),
// so the mapping can be held against a capture of the game.
#include "raylib.h"
#include "raymath.h"
#include "../shared/port/greybox.h"
#include "../web/src/textures.h"
#include "../shared/core/level_rules.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// Flat colours per surface, so the pieces tell apart in a screenshot.
const Color SURFACE_COLOUR[(int)GreySurface::Count] = {
    { 150, 140, 110, 255 },   // Floor
    { 200, 200, 190, 255 },   // Ceiling
    { 170, 165, 140, 255 },   // Wall
    { 120, 100, 80, 255 },    // Step
    { 140, 110, 90, 255 },    // Stair
    { 130, 130, 130, 255 },   // Pillar
    { 90, 110, 160, 255 },    // Prop
    { 110, 70, 40, 255 },     // Door
    { 60, 200, 90, 255 },     // Exit
    { 200, 40, 40, 255 },     // CursedExit
    { 150, 200, 230, 255 },   // Glass
    { 100, 100, 110, 255 },   // Rail
    { 60, 120, 200, 255 },    // Water
    { 255, 255, 240, 255 },   // Light
    { 60, 60, 60, 255 },      // DeadLight
};

// Which of a level's surfaces a section takes when textured: 0 floor,
// 1 ceiling, 2 walls, -1 none. The Python's PARTS, as indices.
int partOf(GreySurface s) {
    switch (s) {
        case GreySurface::Floor: case GreySurface::Stair: return 0;
        case GreySurface::Ceiling: return 1;
        case GreySurface::Wall: case GreySurface::Step: case GreySurface::Pillar: return 2;
        default: return -1;
    }
}

// One raylib mesh per section, lit by a fixed direction baked into the colours.
// Texture coordinates are the section's metres over `tile`.
Mesh toMesh(const GreyboxMesh::Section &s, Color c, float storeyY, Vector2 tile) {
    Mesh m = {};
    m.vertexCount = (int)s.pos.size();
    m.triangleCount = (int)s.index.size() / 3;
    m.vertices = (float *)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.normals = (float *)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.colors = (unsigned char *)MemAlloc(m.vertexCount * 4);
    m.texcoords = (float *)MemAlloc(m.vertexCount * 2 * sizeof(float));
    m.indices = (unsigned short *)MemAlloc(s.index.size() * sizeof(unsigned short));
    const float L[3] = { 0.35f, 0.8f, 0.48f };
    for (int v = 0; v < m.vertexCount; v++) {
        Vec3 p = s.pos[v], n = s.normal[v];
        m.vertices[v * 3] = p.x; m.vertices[v * 3 + 1] = p.y + storeyY; m.vertices[v * 3 + 2] = p.z;
        m.normals[v * 3] = n.x; m.normals[v * 3 + 1] = n.y; m.normals[v * 3 + 2] = n.z;
        m.texcoords[v * 2] = s.uv[v].x / tile.x; m.texcoords[v * 2 + 1] = s.uv[v].y / tile.y;
        float k = 0.55f + 0.45f * fabsf(n.x * L[0] + n.y * L[1] + n.z * L[2]);
        m.colors[v * 4] = (unsigned char)(c.r * k); m.colors[v * 4 + 1] = (unsigned char)(c.g * k);
        m.colors[v * 4 + 2] = (unsigned char)(c.b * k); m.colors[v * 4 + 3] = 255;
    }
    for (size_t q = 0; q < s.index.size(); q++) m.indices[q] = (unsigned short)s.index[q];
    UploadMesh(&m, false);
    return m;
}

}  // namespace

int main() {
    const char *out = getenv("BACKROOMS_SHOT");
    int level = getenv("BACKROOMS_LEVEL") ? atoi(getenv("BACKROOMS_LEVEL")) % NLEVELS : 0;
    World w;
    w.seed = getenv("BACKROOMS_SEED") ? (unsigned)strtoul(getenv("BACKROOMS_SEED"), nullptr, 10) : 1337u;
    w.level = level;
    // A capture shows Level 0 at visit 1 and any other level at visit 0 (AGENTS.md).
    w.visit = getenv("BACKROOMS_VISIT") ? (unsigned)atoi(getenv("BACKROOMS_VISIT")) : (level == 0 ? 1u : 0u);
    w.wallH = LEVEL_RULES[level].wallH;
    w.storeyH = LEVEL_RULES[level].storeyH;
    if (getenv("BACKROOMS_STOREY") && w.storeyH > 0) w.setStorey(atoi(getenv("BACKROOMS_STOREY")));
    float x = 15, z = 15, yaw = 0.8f, pitch = 0;
    if (const char *pos = getenv("BACKROOMS_POS")) {
        float ex, ez, ey, ep;
        int n = sscanf(pos, "%f,%f,%f,%f", &ex, &ez, &ey, &ep);
        if (n >= 3) { x = ex; z = ez; yaw = ey; }
        if (n == 4) pitch = ep;
    }
    // The game picks its spot before any level is entered: in Level 0's maze at
    // visit 0, one storey (simBegin). A capture of another level or visit stands
    // wherever that spot lands in the maze it then enters.
    World first;
    first.seed = w.seed;
    Vec2 spot = first.findOpenSpot(x, z);
    x = spot.x; z = spot.y;
    if (getenv("BACKROOMS_STOREY") && w.storeyH > 0) {
        spot = w.findOpenSpot(x, z);
        x = spot.x; z = spot.y;
    }

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1440, 850, "greybox");
    std::vector<Mesh> meshes;
    std::vector<int> meshMat;
    // 0 flat colour, then the level's floor, ceiling and walls.
    Material mats[4] = { LoadMaterialDefault(), LoadMaterialDefault(), LoadMaterialDefault(), LoadMaterialDefault() };
    const bool textured = getenv("BACKROOMS_TEXTURED") && atoi(getenv("BACKROOMS_TEXTURED"));
    const Vector2 tiles[3] = { { FLOOR_TILE_M, FLOOR_TILE_M }, { FLOOR_TILE_M, FLOOR_TILE_M },
                               { WALL_TILE_M, wallTileV(level) } };
    if (textured) {
        const LevelSurfaces &set = LEVEL_SURFACES[level];
        const SurfSlot slots[3] = { set.floor, set.ceiling, set.walls };
        for (int k = 0; k < 3; k++)
            mats[k + 1].maps[MATERIAL_MAP_DIFFUSE].texture = makeSurface(slots[k]).albedo;
    }
    int pcx = fdiv(cellOf(x), CCELLS), pcz = fdiv(cellOf(z), CCELLS);
    for (int rel = (w.storeyH > 0 ? -1 : 0); rel <= (w.storeyH > 0 ? 1 : 0); rel++) {
        StoreyScope sc(w, w.storey + rel);
        int reach = rel == 0 ? 2 : 1;
        for (int dz = -reach; dz <= reach; dz++)
            for (int dx = -reach; dx <= reach; dx++) {
                GreyboxMesh g = greyboxChunk(w, pcx + dx, pcz + dz);
                printf("chunk s%d (%d,%d): %zu triangles\n", w.qs, g.cx, g.cz, g.triangles());
                for (int s = 0; s < (int)GreySurface::Count; s++) {
                    const GreyboxMesh::Section &sec = g.sections[s];
                    if (sec.index.empty()) continue;
                    // 16-bit indices: split a section that outgrows them.
                    if (sec.pos.size() > 65535) { fprintf(stderr, "section too large\n"); return 1; }
                    int part = textured ? partOf((GreySurface)s) : -1;
                    meshes.push_back(toMesh(sec, part >= 0 ? WHITE : SURFACE_COLOUR[s], rel * w.storeyH,
                                            part >= 0 ? tiles[part] : Vector2{ 1, 1 }));
                    meshMat.push_back(part + 1);
                }
            }
    }
    Camera3D cam = {};
    const float EYE_H = 1.62f;
    float eye = w.floorY(cellOf(x), cellOf(z)) + EYE_H;
    cam.position = { x, eye, z };
    cam.target = { x + cosf(pitch) * cosf(yaw), eye + sinf(pitch), z + cosf(pitch) * sinf(yaw) };
    cam.up = { 0, 1, 0 };
    cam.fovy = 70;
    cam.projection = CAMERA_PERSPECTIVE;
    for (int frame = 0; frame < 3; frame++) {
        BeginDrawing();
        ClearBackground({ 20, 20, 24, 255 });
        BeginMode3D(cam);
        for (size_t i = 0; i < meshes.size(); i++) DrawMesh(meshes[i], mats[meshMat[i]], MatrixIdentity());
        EndMode3D();
        EndDrawing();
    }
    if (out) TakeScreenshot(out);
    CloseWindow();
    return 0;
}
