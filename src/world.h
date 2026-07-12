#pragma once
// The infinite maze: deterministic chunk generation, mesh baking, collision,
// line of sight. Chunks stream in around the player and unload behind them.
#include "raylib.h"
#include <cstdint>
#include <cmath>
#include <unordered_map>

constexpr float CELL = 2.0f;           // metres per grid cell
constexpr int   CCELLS = 16;           // cells per chunk side
constexpr float CHUNK = CELL * CCELLS;
constexpr float WT = 0.11f;            // wall half-thickness

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
    Mesh meshes[7] = {};   // 0 floor, 1 ceiling, 2 walls, 3 props, 4 water, 5 wall scrawl, 6 window glass
};
struct AABB { float minx, minz, maxx, maxz, top; };   // top: height you can stand on

inline int fdiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int cellOf(float x) { return (int)floorf(x / CELL); }

struct MB;   // mesh builder, internal to world.cpp

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
    // can the hunter walk from cell (ci,ck) into the adjacent cell (ni,nk)?
    bool canStep(int ci, int ck, int ni, int nk);
    // BFS the cell grid from (si,sk) toward (ti,tk); fills the next cell to move
    // to in (outI,outK). false if no route within budget (fall back to a beeline).
    bool pathStep(int si, int sk, int ti, int tk, int &outI, int &outK);
    // Level 0 only: a rare patch of carpet that has stopped being a floor
    bool softAt(int ci, int ck);
    // some exit doors glow red and were never going anywhere good
    bool cursedExit(int ci, int ck);
    Vector2 findOpenSpot(float x, float z);
    void unloadFar(int pcx, int pcz, int radius);
    void unloadAll();
};
