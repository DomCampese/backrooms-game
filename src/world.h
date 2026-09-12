#pragma once
// The infinite maze: deterministic chunk generation, mesh baking, collision,
// line of sight. Chunks stream in around the player and unload behind them.
#include "raylib.h"
#include <cstdint>
#include <cmath>
#include <unordered_map>

constexpr float CELL = 2.0f;           // metres per grid cell
constexpr int   CCELLS = 16;           // cells per chunk side
// How rare a phrase on a wall is, and how many there are to find. The rate is
// per solid wall edge and applies to both orientations, so a corridor of ten
// cells offers about twenty chances. SCRAWL_PHRASES must match the atlas built
// by makeScrawlTex (4 columns x 8 rows) — change one and change the other.
constexpr uint32_t SCRAWL_RATE = 40;
constexpr uint32_t SCRAWL_PHRASES = 32;
constexpr float CHUNK = CELL * CCELLS;
constexpr float WT = 0.11f;            // wall half-thickness

// What sits on one edge of a cell. Only WALL_SOLID and WALL_WINDOW are
// floor-to-ceiling blockers; a doorway is a hole you (and light) walk through.
enum WallKind : uint8_t {
    WALL_NONE   = 0,   // open floor, nothing on the edge
    WALL_SOLID  = 1,
    WALL_EXIT   = 2,   // a doorway out of this level
    WALL_WINDOW = 3,   // glass, with nothing behind it
};

// Which piece of furniture, if any, stands in a cell. The generator picks these
// per level (see World::generate); the mesher builds each one in
// addProp (world.cpp) and gatherCellAABBs gives it a collision box.
enum PropKind : uint8_t {
    PROP_NONE = 0,
    PROP_BOXES,          // 1  a stack of cartons
    PROP_CABINET,        // 2  filing cabinet
    PROP_TABLE,          // 3  folding table
    PROP_FALLEN_TILE,    // 4  collapsed ceiling, tile leaning below the hole
    PROP_COUCH,          // 5
    PROP_ARMOIRE,        // 6
    PROP_LAMP,           // 7  floor lamp, never lit
    PROP_NIGHTSTAND,     // 8
    PROP_BED,            // 9
    PROP_VENDING,        // 10 vending machine — takes doubloons
    PROP_PARTY_TABLE,    // 11 the cake nobody cut
    PROP_DESK,           // 12 office desk + chair
    PROP_SHELVING,       // 13 steel shelving
    PROP_COOLER,         // 14 water cooler
    PROP_PLANT,          // 15 potted plant
};

// Slots in ChunkData::meshes. Each is baked separately because each needs a
// different material or a different draw order (see Game::renderScene).
enum ChunkMesh {
    MESH_FLOOR = 0,
    MESH_CEILING,
    MESH_WALLS,
    MESH_PROPS,
    MESH_WATER,
    MESH_SCRAWL,      // graffiti decals, pressed just off the wall faces
    MESH_GLASS,       // window panes
    MESH_AO,          // baked contact-shadow gradients in every crease
    MESH_COUNT,
};

// ChunkData::elev is stored in decimetres so it fits in an int8_t.
constexpr float ELEV_UNIT = 0.1f;

// walls: wallN[i][k] = north edge of cell (i,k) at z=k*CELL; wallW = west edge at x=i*CELL
struct ChunkData {
    uint8_t wallN[CCELLS][CCELLS];   // WallKind
    uint8_t wallW[CCELLS][CCELLS];   // WallKind
    uint8_t pillar[CCELLS][CCELLS];
    uint8_t prop[CCELLS][CCELLS];    // PropKind
    uint8_t propRot[CCELLS][CCELLS];
    uint8_t pool[CCELLS][CCELLS];
    int8_t elev[CCELLS][CCELLS];   // floor height in ELEV_UNIT steps: -5 sunken lounge, down to -25
                                   // in an L0 atrium's terraced heart; +6 loading dock, +12 upper tier (L1)
    bool built = false;
    Mesh meshes[MESH_COUNT] = {};
};
struct AABB { float minx, minz, maxx, maxz, top; };   // top: height you can stand on

// Scratch capacity for "everything solid in the 3x3 cells around a point" —
// three walls and a prop per cell would be 36, so 48 leaves headroom.
constexpr int MAX_NEARBY_AABBS = 48;

// A wall or a window stops you getting through a cell edge; a doorway does not.
// Collision, pathfinding, line of sight and the mesher all share this test.
// Light is the exception and does NOT use it — buildOccupancy checks for
// WALL_SOLID on its own, because glass blocks a body but not a fluorescent.
inline bool blocksEdge(uint8_t wall) { return wall == WALL_SOLID || wall == WALL_WINDOW; }

inline int fdiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int cellOf(float x) { return (int)floorf(x / CELL); }

struct MB;   // mesh builder, internal to world.cpp

// One almond water can at life size, base on y=0. UVs index makeAlmondWrapTex.
Mesh buildCanMesh();
// The tape player, underside on y=0, and the two reels + record lamp that go on
// it. Separate meshes because the reels turn and the lamp only burns while the
// tape is running. UVs index makeDeckTex.
Mesh buildDeckMesh();
Mesh buildReelMesh();
Mesh buildDeckLampMesh();

struct World {
    unsigned seed = 1337;
    int level = 0;           // 0 = Level 0, 1 = Level 1 (garage), 2 = Poolrooms, 3 = Red Halls, 4 = LEVEL FUN
    float wallH = 3.0f;
    bool exitTest = false;   // BACKROOMS_EXITS env: exits everywhere, for visual testing
    std::unordered_map<uint64_t, ChunkData> chunks;

    static uint64_t key(int cx, int cz) { return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz; }

    ChunkData &data(int cx, int cz);
    void generate(ChunkData &d, int cx, int cz);
    uint8_t wallNVal(int ci, int ck);
    uint8_t wallWVal(int ci, int ck);
    bool pillarAt(int ci, int ck);
    uint8_t propAt(int ci, int ck);
    bool poolAt(int ci, int ck);
    float floorY(int ci, int ck);
    void ensureMesh(int cx, int cz);
    int gatherCellAABBs(int ci, int ck, AABB *out, int cap, int cnt, bool includeProps = true);
    // feetY: obstacles whose top is at or below your feet are walkable, not solid
    void collideCircle(float &px, float &pz, float r, float feetY = 0.0f);
    // floor height here, counting prop tops at or below your feet (so you can stand on furniture)
    float groundAt(float x, float z, float feetY);
    bool lineOfSight(float ax, float az, float bx, float bz);
    // Snapshot the local floorplan into a byte grid the shader marches for light
    // occlusion: bit0 = solid wall on this cell's north edge, bit1 = on its west
    // edge, bit2 = a pillar fills the cell. Only opaque blockers go in —
    // doorways, window glass and furniture all let light through.
    void buildOccupancy(int originI, int originK, int n, unsigned char *out);
    // can the hunter walk from cell (ci,ck) into the adjacent cell (ni,nk)?
    // Furniture and pillars are solid; only a doorway opens a walled edge.
    bool canStep(int ci, int ck, int ni, int nk);
    // BFS the cell grid from (si,sk) toward (ti,tk); fills the next cell to move
    // to in (outI,outK). false if no route within budget (fall back to a beeline).
    bool pathStep(int si, int sk, int ti, int tk, int &outI, int &outK);
    // Level 0 only: a rare patch of carpet that has stopped being a floor
    bool softAt(int ci, int ck);
    // Red Halls only: a standpipe with a shut-off wheel on it. The mesher builds
    // the pipe, the game logic runs the puzzle, so both ask this.
    bool valveAt(int ci, int ck);
    // some exit doors glow red and were never going anywhere good
    bool cursedExit(int ci, int ck);
    Vector2 findOpenSpot(float x, float z);
    void unloadFar(int pcx, int pcz, int radius);
    void unloadAll();
};
