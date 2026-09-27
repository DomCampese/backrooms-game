#pragma once
// The infinite maze: deterministic chunk generation, storeys, collision, line
// of sight, pathfinding and the light-occlusion grid. Chunks are generated on
// demand and unloaded behind the player. Standard library only: the raylib
// mesher is src/world_mesh.cpp.
#include "vec.h"
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr float WATER_Y = -0.12f;       // shared surface for rendering and swimming
constexpr float CELL = 2.0f;           // metres per grid cell
constexpr int   CCELLS = 16;           // cells per chunk side
constexpr float CHUNK = CELL * CCELLS;
constexpr float WT = 0.11f;            // wall half-thickness
// The corridor ring each chunk leaves round its rooms, in cells: one lane on its
// low sides, two on its high ones, so every seam corridor is HALL_LO + HALL_HI
// cells (6 m) wide. Not two lanes each way: 8 m halls take 44% of the floor and
// cut `hidden at 20 m` from 98.4% to 93.4% (enclosure and sightlines are one
// number, AGENTS.md). The room patch is the rest, CCELLS - HALL_LO - HALL_HI
// square.
constexpr int HALL_LO = 1;
constexpr int HALL_HI = 2;

// What sits on one edge of a cell. blocksEdge and blocksLight say what each
// stops.
enum WallKind : uint8_t {
    WALL_NONE   = 0,   // open floor, nothing on the edge
    WALL_SOLID  = 1,
    WALL_EXIT   = 2,   // a doorway out of this level
    WALL_WINDOW = 3,   // glass, with nothing behind it
    // A doorway: a 1.3 m opening under a 2.3 m header, with a frame and a
    // threshold. Passable, but its jambs are solid (gatherCellAABBs).
    WALL_DOOR   = 4,
    // A doorway with a shut, locked leaf. Blocks bodies and light until
    // World::unlockEdge turns it into a WALL_DOOR through the wall overlay.
    WALL_LOCKED = 5,
    // A guard at the edge of a drop: round a stair opening or an atrium, and up
    // the open side of a flight. Stops a body at any height, but not light or
    // sight.
    WALL_RAIL   = 6,
};

// What stands in a cell. The generator picks these per level; addProp
// (world_mesh.cpp) builds each and gatherCellAABBs gives it a collision box.
enum PropKind : uint8_t {
    PROP_NONE = 0,
    PROP_BOXES,          // 1  a stack of cartons
    PROP_CABINET,        // 2  filing cabinet
    PROP_TABLE,          // 3  folding table
    PROP_FALLEN_TILE,    // 4  collapsed ceiling, tile leaning below the hole
    PROP_COUCH,          // 5
    PROP_ARMOIRE,        // 6
    PROP_LAMP,           // 7  floor lamp
    PROP_NIGHTSTAND,     // 8
    PROP_BED,            // 9
    PROP_VENDING,        // 10 vending machine, takes doubloons
    PROP_PARTY_TABLE,    // 11 party table with a cake
    PROP_DESK,           // 12 office desk + chair
    PROP_SHELVING,       // 13 steel shelving
    PROP_COOLER,         // 14 water cooler
    PROP_PLANT,          // 15 potted plant
    // 16: the Manila Room's table, chairs and cupboard. Anchored on room cell
    // (7,7) and built centred on that cell's far corner, the middle of the room.
    PROP_MANILA_TABLE,
};

// propRot bit 2: the prop stands pushed back against the solid wall behind it.
// Only vending machines. World::generate sets it after every pass that changes
// an edge, so the mesher and collision (vendFootprint) agree.
constexpr uint8_t PROP_AGAINST_WALL = 4;

// The vending machine's body: half-width, height, and reach behind and in
// front of its centre (the door hardware stands proud). The mesher builds it
// round vendFootprint's centre and collision takes its box.
constexpr float VEND_HW = 0.44f;
constexpr float VEND_Y1 = 1.83f;
constexpr float VEND_DEPTH_BACK = 0.36f, VEND_DEPTH_FRONT = 0.39f;
// Rotated a quarter turn at a time from propRot; with PROP_AGAINST_WALL,
// pushed back to 8 cm off the wall face behind it. (cx, cz) is the cell centre;
// out: the machine's centre (px, pz) and its axis-aligned box.
void vendFootprint(uint8_t rotByte, float cx, float cz, float &px, float &pz,
                   float &x0, float &z0, float &x1, float &z1);
// Level 1's lift doors: which north edges carry one. The mesher builds them;
// the vending pass keeps machines from backing onto them.
bool liftHash(int gi, int gk, unsigned s);


// The Manila Room (Level 0): 8 x 8 m (room cells MANILA_LO..MANILA_HI on both
// axes), thick walls, a door in each side. One chunk in MANILA_RATE, never in
// the chunks round spawn.
constexpr uint32_t MANILA_RATE = 20;
constexpr int MANILA_LO = 6, MANILA_HI = 9;

// ChunkData::elev is stored in decimetres so it fits in an int8_t.
constexpr float ELEV_UNIT = 0.1f;
// The tallest rise or drop a body walks without leaving the floor. Taller is
// terrain: gatherCellAABBs blocks the riser, canStep refuses it, and the mover
// falls off it instead of gliding down.
constexpr float MAX_STEP = 0.45f;
constexpr int   MAX_STEP_UNITS = (int)(MAX_STEP / ELEV_UNIT);   // 4 decimetres, in elev units
constexpr float SOFT_DEPTH = 0.085f;   // how far a rotten patch has sagged at its middle, metres

// ---- storeys. A level with a storey pitch is a stack of floorplans, each a
// whole maze, joined by stairs and openings. The storey you are on is always at
// y = 0, so everything that reasons in 2D (collision, pathfinding, line of
// sight, the shader's shadow march) works unchanged on it; other storeys are
// drawn a pitch away, and crossing the middle of a flight rebases everything
// with a position by one storey (Game::changeStorey).
//
// Vertical features are pure functions of (chunk, pair of storeys), never of
// generated contents, so both storeys stamp the same stairwell without seeing
// each other. See World::pairFeature.
enum VertKind : uint8_t {
    VK_NONE = 0,
    VK_STAIRWELL,   // an enclosed dogleg: two flights and a half landing in a 4 x 8 m shaft
    VK_STAIR,       // one straight flight in a room, under an opening in the ceiling
    VK_ATRIUM,      // a double-height hall: the floor above is cut away and railed round
};
// One vertical feature. Local frame: u across the rise, v along it, metres
// from the footprint's corner; `dir` turns that frame onto the grid.
struct VertFeat {
    uint8_t kind = VK_NONE;
    uint8_t dir = 0;          // 0 rises toward +z, 1 toward -z, 2 toward +x, 3 toward -x
    int8_t x0 = 0, z0 = 0;    // chunk-local cell of the footprint's min corner
    int8_t wu = 0, lv = 0;    // footprint across and along the rise, in cells
    int8_t stairU = -1;       // which lane carries a flight (VK_STAIR / an atrium's stair); -1 none
    int8_t wallSide = 0;      // VK_STAIR: -1 wall on the u=0 side, +1 on the far side, 0 rails both
    int lo = 0;               // the storey whose floor it rises from; it opens into lo + 1
};
// What a cell is vertically. Set by the generator on the storey it belongs to.
enum VertFlag : uint8_t {
    VF_STAIR    = 1,    // this storey's floor here is a flight or a landing (World::stairY)
    VF_OPENUP   = 2,    // no ceiling: the space runs on up into the storey above
    VF_HOLE     = 4,    // no floor: you look, and fall, into the storey below
    VF_WALKHOLE = 8,    // ...but a flight comes up through it, so it can be walked down
    VF_KEEP     = 16,   // a feature or its margin: nothing else is placed here
    VF_NOWALK   = 32,   // not part of this storey's 2D floor graph (connectivity, flood fills)
};
// How many storeys groundAt looks down through holes, and how far streaming reaches.
constexpr int STOREY_REACH = 6;
// The top an AABB reports for something that stops a body at any height
// (walls, jambs, pillars, flight guards): a body on a flight stands above a
// 3 m wall top.
constexpr float FULL_H = 1.0e4f;
// A rail: half the thickness of its knee wall, and the height of its cap above
// what you stand on. The mesher builds it; gatherCellAABBs collides with it.
constexpr float RAIL_T = 0.075f;
constexpr float RAIL_H = 0.65f;

// walls: wallN[i][k] = north edge of cell (i,k) at z=k*CELL; wallW = west edge at x=i*CELL
struct ChunkData {
    uint8_t wallN[CCELLS][CCELLS];   // WallKind
    uint8_t wallW[CCELLS][CCELLS];   // WallKind
    uint8_t pillar[CCELLS][CCELLS];
    uint8_t prop[CCELLS][CCELLS];    // PropKind
    uint8_t propRot[CCELLS][CCELLS];   // quarter turns in bits 0-1; PROP_AGAINST_WALL in bit 2
    uint8_t pool[CCELLS][CCELLS];
    int8_t elev[CCELLS][CCELLS];   // floor height in ELEV_UNIT steps: -5 sunken lounge, down to -25
                                   // in an L0 atrium; +6 loading dock, +12 upper tier (L1)
    int8_t lockI = -1, lockK = -1;   // cell owning the locked edge, chunk-local
    uint8_t lockWest = 0;            // 0: its north edge, 1: its west edge
    int8_t keyI = -1, keyK = -1;     // where the key for it lies, chunk-local
    bool manila = false;             // this chunk holds the Manila Room (Level 0)
    // Storeys: each cell's VertFlag bits, which of `feats` it belongs to, and
    // which of its two edges (bit 0 north, bit 1 west) a feature owns. Later passes
    // that punch, move, lock or noclip doorways must skip protected edges, or the
    // storeys disagree about a stairwell wall.
    uint8_t vflag[CCELLS][CCELLS] = {};
    int8_t vfeat[CCELLS][CCELLS] = {};
    uint8_t prot[CCELLS][CCELLS] = {};
    VertFeat feats[2];
    int nfeat = 0;
};
struct AABB {
    float minx, minz, maxx, maxz, top;   // top: height you can stand on
    bool seeThrough = false;             // a rail: stops a body, not a look
};

// Scratch capacity for everything solid in the 3x3 cells round a point. Boxes
// past the cap are dropped silently; mapdump reports the worst count.
constexpr int MAX_NEARBY_AABBS = 48;

// Can a body cross this edge? Collision, pathfinding, line of sight and the
// mesher share this test. Light uses blocksLight.
inline bool blocksEdge(uint8_t wall) {
    return wall == WALL_SOLID || wall == WALL_WINDOW || wall == WALL_LOCKED || wall == WALL_RAIL;
}
// Does this edge stop light? Glass and doorways pass light; a locked door
// does not.
inline bool blocksLight(uint8_t wall) { return wall == WALL_SOLID || wall == WALL_LOCKED; }

inline int fdiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int cellOf(float x) { return (int)floorf(x / CELL); }

// A chunk of one storey, for the stale-geometry list.
struct ChunkRef { int storey, cx, cz; };

struct World {
    unsigned seed = 1337;
    int level = 0;           // 0 = Level 0, 1 = Level 1, 2 = Poolrooms, 3 = Red Halls, 4 = LEVEL FUN
    // Which visit to this level, this descent (0 on arrival). Mixed into the
    // chunk seed so a revisit is a different maze; at 0 the mix is a no-op, so a
    // fresh descent reproduces the same maze.
    unsigned visit = 0;
    float wallH = 3.0f;
    bool exitTest = false;   // BACKROOMS_EXITS env: exits everywhere, for visual testing
    bool manilaTest = false; // BACKROOMS_MANILA env: a Manila Room in the chunk east of spawn
    // Storeys. storeyH is the floor-to-floor pitch (0: one floorplan). `chunks`
    // holds the chunks of `storey`, the one you are on; other storeys' chunks live
    // in `layers`.
    //
    // `qs` is the storey the accessors read. It equals `storey` except inside a
    // StoreyScope, and data(), generate() and the mesher all consult it.
    float storeyH = 0.0f;
    int storey = 0;
    int qs = 0;
    std::unordered_map<uint64_t, ChunkData> chunks;
    std::unordered_map<int, std::unordered_map<uint64_t, ChunkData>> layers;

    static uint64_t key(int cx, int cz) { return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz; }

    ChunkData &data(int cx, int cz);
    void generate(ChunkData &d, int cx, int cz);
    // The chunk map for storey s (the live one for the storey you are on).
    std::unordered_map<uint64_t, ChunkData> &layer(int s) { return s == storey ? chunks : layers[s]; }
    // Move to storey s. The chunk maps swap rather than regenerate; baked chunks
    // are keyed by storey, so their meshes stay valid.
    void setStorey(int s);
    // The seed for storey qs. Storey 0 uses the bare seed, so captures away from
    // a feature match a one-storey world.
    unsigned sseed() const {
        return qs == 0 ? seed : seed ^ (uint32_t)((uint32_t)qs * 0x9E3779B1u + 0x7F4A7C15u);
    }
    // Features touching storey s in chunk (cx,cz): the one rising from it and the
    // one arriving at it. Pure functions of seed, level, visit and coordinates.
    int featuresFor(int cx, int cz, int s, VertFeat *out, int cap);
    bool pairFeature(int cx, int cz, int p, VertFeat &out);   // the one feature joining p and p + 1, if any
    bool manilaChunk(int cx, int cz, int s);                  // does storey s hold a Manila Room here
    uint8_t vflagAt(int ci, int ck);                           // VertFlag bits of a cell on storey qs
    // The walking surface of a flight or landing at (x,z), in storey qs's frame,
    // or NAN off a stair. Stepped; `ramp` gives the nosing line.
    float stairY(float x, float z, bool ramp = false);
    // A feature's local frame: world point -> (u, v) metres, and back.
    void featureLocal(const VertFeat &f, int cx, int cz, float x, float z, float &u, float &v) const;
    Vec3 featureWorld(const VertFeat &f, int cx, int cz, float u, float y, float v) const;
    // Do any chunks around this one link storey qs to storey qs + rel?
    bool linksStorey(int cx, int cz, int rel);
    // An enclosed stairwell's light: a batten on the end wall over the half
    // landing, in the frame of the storey it rises from. The renderer gives the
    // nearest one the shader's spare point light (uLamp).
    Vec3 landingLamp(const VertFeat &f, int cx, int cz) const {
        return featureWorld(f, cx, cz, CELL, storeyH * 0.5f + 2.25f, 4 * CELL - WT - 0.07f);
    }
    // Write storey qs's half of a feature into a chunk being generated: its
    // margin, cell flags, walls, rails and doors.
    void stampFeature(ChunkData &d, const VertFeat &f, int cx, int cz);
    uint8_t wallNVal(int ci, int ck);
    uint8_t wallWVal(int ci, int ck);
    bool pillarAt(int ci, int ck);
    uint8_t propAt(int ci, int ck);
    uint8_t propRotAt(int ci, int ck);
    bool poolAt(int ci, int ck);
    float floorY(int ci, int ck);
    // The ceiling height of a cell: its floor (if raised) plus wallH, or the
    // storey pitch where the cell is open to the storey above.
    float ceilY(int ci, int ck);
    int gatherCellAABBs(int ci, int ck, AABB *out, int cap, int cnt, bool includeProps = true);
    // feetY: obstacles whose top is at or below your feet are walkable, not solid
    void collideCircle(float &px, float &pz, float r, float feetY = 0.0f);
    // floor height here, counting prop tops at or below your feet (so you can stand on furniture)
    float groundAt(float x, float z, float feetY);
    bool lineOfSight(float ax, float az, float bx, float bz);
    // The light-occlusion grid the shader marches, n x n cells from (originI,
    // originK), four bytes a cell: your storey, the one below, the one above, spare.
    // bit 0: opaque wall on the north edge; bit 1: on the west edge; bit 2: pillar;
    // bit 3: no fitting at this cell's min corner (it would hang in an opening);
    // bit 4: no ceiling here, so the storey above's tubes light it; bit 5: a hole
    // touches that corner, so the fitting there lights the storey below; bit 6: one
    // of bits 3-5 is set within reach of the fittings the shader sums here. Bit 6
    // must never be clear where a missing fitting matters. `out` has 2n rows: n of
    // cells, then n of fitting masks (buildOccupancy).
    void buildOccupancy(int originI, int originK, int n, unsigned char *out);
    // Can a body walk from cell (ci,ck) into the adjacent (ni,nk)? Furniture and
    // pillars fill their cell; only a doorway opens a walled edge.
    bool canStep(int ci, int ck, int ni, int nk);
    // BFS from (si,sk) toward (ti,tk) within 16 cells; the first cell to move to.
    // False if there is no route (fall back to a beeline).
    bool pathStep(int si, int sk, int ti, int tk, int &outI, int &outK);
    // Level 0 only: is this cell inside the Manila Room?
    bool manilaAt(int ci, int ck);
    // The centre (world metres) of a loaded Manila Room in this point's chunk or
    // its eight neighbours. Rooms are at least a chunk apart.
    bool manilaNear(float x, float z, float &rx, float &rz);
    // Level 0: a rare rotten floor patch
    bool softAt(int ci, int ck);
    // How far the rotten patch has sunk at (x,z), metres. The mesher and groundAt
    // both use it, so the dip drawn is the dip walked.
    float softDip(float x, float z);
    // Red Halls only: a standpipe with a shut-off wheel. The mesher builds it;
    // the game runs the valve puzzle.
    bool valveAt(int ci, int ck);
    // Exits that lead to the Red Halls instead of onward.
    bool cursedExit(int ci, int ck);
    Vec2 findOpenSpot(float x, float z);
    void unloadFar(int pcx, int pcz, int radius);
    void unloadAll();
    // Edges that have become walls behind the player (Game::shiftAWall picks
    // edges out of sight). Read in wallNVal/wallWVal, like unlockedDoors, so every
    // system sees them.
    std::unordered_set<uint64_t> shifted;
    // Doors the player has unlocked, in the same edge-key space.
    std::unordered_set<uint64_t> unlockedDoors;
    // Edges are per storey: the edge directly above is a different edge. Storey
    // 0 keys as a one-storey world did.
    uint64_t edgeKey(int ci, int ck, bool west) const {
        return ((key(ci, ck) << 1) | (west ? 1ull : 0ull)) ^ ((uint64_t)(uint32_t)qs * 0xD6E8FEB86659FD93ULL);
    }
    void shiftEdge(int ci, int ck, bool west);   // wall it off, and rebake the chunk that owns it
    void unlockEdge(int ci, int ck, bool west);  // open a locked door for good, and rebake
    // Is there a key lying loose in this cell? One per chunk at most.
    bool keyAt(int ci, int ck);
    void rebuildChunk(int cx, int cz);           // report its geometry stale (staleChunks)
    // Chunks whose geometry went stale: unloaded (unloadFar, unloadAll) or
    // rebuilt after a wall changed (rebuildChunk). Core only appends; a renderer
    // drains it before using any baked chunk (ChunkMeshCache), so baked meshes live
    // exactly as long as their chunk data.
    std::vector<ChunkRef> staleChunks;
};

// Point the accessors at another storey for a scope (meshing the storey
// below, looking down a hole, building the grid of the storey above).
struct StoreyScope {
    World &w; int prev;
    StoreyScope(World &world, int s) : w(world), prev(world.qs) { w.qs = s; }
    ~StoreyScope() { w.qs = prev; }
    StoreyScope(const StoreyScope &) = delete;
    StoreyScope &operator=(const StoreyScope &) = delete;
};
