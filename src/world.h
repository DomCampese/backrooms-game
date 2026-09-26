#pragma once
// The infinite maze: deterministic chunk generation, mesh baking, collision,
// line of sight. Chunks stream in around the player and unload behind them.
#include "raylib.h"
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

constexpr float WATER_Y = -0.12f;       // shared surface for rendering and swimming
constexpr float CELL = 2.0f;           // metres per grid cell
constexpr int   CCELLS = 16;           // cells per chunk side
// How rare a phrase on a wall is, and how many there are to find. The rate is
// per solid wall edge and applies to both orientations, so a corridor of ten
// cells offers about twenty chances. SCRAWL_PHRASES must match the atlas built
// by makeScrawlTex (4 columns x 8 rows) — change one and change the other.
// How often the building's fittings turn up. These are rarities in the same
// sense the scrawl is, except an outlet is meant to be ordinary: canon names
// "scattered electrical outlets" and the eye needs something of known size to
// measure a corridor against, so they are common and everything else is not.
// All are per solid wall edge, both orientations.
constexpr uint32_t OUTLET_RATE   = 5;
constexpr uint32_t OUTLET_BROKEN = 4;    // one outlet in this many has lost its cover
constexpr uint32_t SWITCH_RATE   = 23;
constexpr uint32_t GRILLE_RATE   = 27;
constexpr uint32_t EXITSIGN_RATE = 97;
constexpr uint32_t SPRINK_RATE   = 11;   // per ceiling cell
constexpr uint32_t DIFFUSER_RATE = 13;   // per ceiling cell
constexpr uint32_t CONDUIT_RUN   = 6;    // cells per conduit run, so runs are runs

constexpr uint32_t SCRAWL_RATE = 40;
constexpr uint32_t SCRAWL_PHRASES = 32;
constexpr float CHUNK = CELL * CCELLS;
constexpr float WT = 0.11f;            // wall half-thickness
// The corridor ring each chunk leaves around its rooms, in cells — one lane on
// its low sides, two on its high ones. Every seam pairs one chunk's high ring
// with the next one's low ring, so every corridor in the world comes out
// HALL_LO + HALL_HI cells wide: 3, or 6 m, against the 4 m a symmetric one-lane
// ring gave (and the 2 m it fell to wherever a segment crossed a seam).
//
// Asymmetric because the symmetric alternative doubles both sides at once. Two
// lanes each way is 8 m and eats 44% of the floor, which took `hidden at 20 m`
// from 98.4% to 93.4% — enclosure and sightlines are the same number, see
// AGENTS.md, so a hall that wide is paid for in Clark having nowhere to be
// unseen. The room patch is what is left: CCELLS - HALL_LO - HALL_HI square.
constexpr int HALL_LO = 1;
constexpr int HALL_HI = 2;

// What sits on one edge of a cell. Only WALL_SOLID and WALL_WINDOW are
// floor-to-ceiling blockers; a doorway is a hole you (and light) walk through.
enum WallKind : uint8_t {
    WALL_NONE   = 0,   // open floor, nothing on the edge
    WALL_SOLID  = 1,
    WALL_EXIT   = 2,   // a doorway out of this level
    WALL_WINDOW = 3,   // glass, with nothing behind it
    // A doorway between two spaces: a 1.3 m opening under a 2.3 m header, with
    // a frame and a threshold. The generator used to leave these edges simply
    // absent, which is indistinguishable from open floor — one marked doorway
    // turned up in 33,282 edges on Level 0, and that one was an exit. Passable,
    // so blocksEdge() says no; but its jambs are solid, so gatherCellAABBs
    // gives them boxes and you have to go through the opening.
    WALL_DOOR   = 4,
    // The same opening with a leaf in it, shut and locked. It blocks a body and
    // it blocks light — it is a door, not a doorway — so blocksEdge says yes and
    // so does blocksLight. World::unlockEdge turns one into a WALL_DOOR for
    // good, through the same overlay the shifted walls use, which is what makes
    // every system agree the moment it opens.
    WALL_LOCKED = 5,
    // A guard at the edge of a drop: the knee wall round a stair opening or an
    // atrium, and the balustrade up the open side of a flight. It stops a body
    // at any height (you cannot step off a landing into the storey below) but
    // it is waist high, so it stops neither light nor sight. blocksEdge says
    // yes, blocksLight says no — the opposite of a locked door.
    WALL_RAIL   = 6,
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
    // 16 the Manila Room's octagonal table, two chairs and the cupboard under
    // it. Anchored on room cell (7,7) but built centred on that cell's far
    // corner, which is the middle of the room — the lore has the table
    // "perfectly centered". See World::generate and addManilaRoom.
    PROP_MANILA_TABLE,
};

// The Manila Room (Level 0): "an isolated eight-by-eight-meter room" — 4x4
// cells — "with thick walls", manila wallpaper, wooden floorboards, one
// octagonal table, two chairs and "a wooden entrance door on each wall". It is
// found by "walking an almighty distance in any direction", so it is rare and
// never in the chunks around where you wake. One chunk in MANILA_RATE carries
// one, in room cells MANILA_LO..MANILA_HI on both axes.
constexpr uint32_t MANILA_RATE = 20;
constexpr int MANILA_LO = 6, MANILA_HI = 9;

// Slots in ChunkData::meshes. Each is baked separately because each needs a
// different material or a different draw order (see Game::renderScene).
enum ChunkMesh {
    MESH_FLOOR = 0,
    MESH_CEILING,
    MESH_WALLS,
    MESH_PROPS,
    MESH_WATER,
    MESH_SCRAWL,      // graffiti decals, pressed just off the wall faces
    MESH_FIXTURES,    // outlets, grilles, diffusers, signs — and the conduit/sprinkler bodies
    MESH_GLASS,       // window panes
    MESH_AO,          // baked contact-shadow gradients in every crease
    MESH_COUNT,
};

// ChunkData::elev is stored in decimetres so it fits in an int8_t.
constexpr float ELEV_UNIT = 0.1f;
// The tallest rise a body can walk up, and the tallest drop it can walk down
// without leaving the floor. Anything taller is terrain you have to go around:
// gatherCellAABBs puts a full-height blocker on the riser, and the mover falls
// off it the other way instead of gliding down. Without this the 2.5 m terraces
// of a Level 0 atrium and the Level 1 loading docks were walkable vertical
// faces — you strolled up them like ramps.
constexpr float MAX_STEP = 0.45f;
constexpr int   MAX_STEP_UNITS = (int)(MAX_STEP / ELEV_UNIT);   // 4 decimetres, in elev units
constexpr float SOFT_DEPTH = 0.085f;   // how far a rotten patch has sagged at its middle, metres

// ---- storeys.
//
// The Threshold article has wanderers stumbling "through mile after mile of
// randomly segmented rooms, hallways, and stairs", and the photograph it all
// began with is of a building's *second floor*. For most of this game's life
// the maze was one floorplan extruded to a ceiling height — every system in the
// engine (collision, the pathfinder, line of sight, the shader's shadow march)
// is built on there being exactly one floor under any point. So the vertical is
// added without taking that away: a level with a storey pitch is a *stack* of
// floorplans, each one a whole maze of its own, joined by stairs and openings.
//
// The storey you are standing on is always the one at y = 0. Everything that
// reasons in two dimensions keeps doing so, on that storey, in local
// coordinates; the storeys above and below are drawn offset by one pitch, and
// crossing the middle of a flight re-bases the player — and everything else
// with a position — by one storey (Game::changeStorey). A floating origin, with
// the float being a whole floor.
//
// Vertical features are pure functions of (chunk, pair of storeys), never of
// either storey's generated contents, so the storey below and the storey above
// build the same stairwell without ever seeing each other — the same trick
// that lets neighbouring chunks meet at a seam. See World::featuresFor.
enum VertKind : uint8_t {
    VK_NONE = 0,
    VK_STAIRWELL,   // an enclosed dogleg: two flights and a half landing in a 4 x 8 m shaft
    VK_STAIR,       // one straight flight in a room, under an opening in the ceiling
    VK_ATRIUM,      // a double-height hall: the floor above is cut away and railed round
};
// One vertical feature. Local frame: u runs across the rise, v along it, both
// in metres from the footprint's corner; `dir` turns that frame onto the grid.
struct VertFeat {
    uint8_t kind = VK_NONE;
    uint8_t dir = 0;          // 0 rises toward +z, 1 toward -z, 2 toward +x, 3 toward -x
    int8_t x0 = 0, z0 = 0;    // chunk-local cell of the footprint's min corner
    int8_t wu = 0, lv = 0;    // footprint across and along the rise, in cells
    int8_t stairU = -1;       // which lane carries a flight (VK_STAIR / an atrium's stair); -1 none
    int8_t wallSide = 0;      // VK_STAIR: -1 wall on the u=0 side, +1 on the far side, 0 rails both
    int lo = 0;               // the storey whose floor it rises from; it opens into lo + 1
};
// What a cell is, vertically. Set by the generator on the storey it belongs to.
enum VertFlag : uint8_t {
    VF_STAIR    = 1,    // this storey's floor here is a flight or a landing (World::stairY)
    VF_OPENUP   = 2,    // no ceiling: the space runs on up into the storey above
    VF_HOLE     = 4,    // no floor: you look, and fall, into the storey below
    VF_WALKHOLE = 8,    // ...but a flight comes up through it, so it can be walked down
    VF_KEEP     = 16,   // a feature or its margin: nothing else is placed here
    VF_NOWALK   = 32,   // not part of this storey's 2D floor graph (connectivity, flood fills)
};
// Deepest a groundAt will look through holes for something to stand on.
constexpr int STOREY_REACH = 6;
// Height an AABB reports for something that stops a body at any height: walls,
// jambs, pillars, rails. Walls around an opening run up a whole storey, and an
// actor on a flight is well above a 3 m wall-top, so wallH is no longer "tall
// enough to never step over".
constexpr float FULL_H = 1.0e4f;

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
    // At most one locked door per chunk, and the cell its key lies in. Both are
    // -1 when the chunk has neither. They are stored rather than hashed because
    // the two have to agree about something no hash knows: the key must be on
    // the side of the door you can already reach, and only the flood inside
    // generate() can say which side that is.
    int8_t lockI = -1, lockK = -1;   // cell owning the locked edge, chunk-local
    uint8_t lockWest = 0;            // 0: its north edge, 1: its west edge
    int8_t keyI = -1, keyK = -1;     // where the key for it lies, chunk-local
    bool manila = false;             // this chunk holds the Manila Room (Level 0)
    // Storeys: what each cell is vertically (VertFlag), which of `feats` it
    // belongs to, and which of its two edges (bit 0 north, bit 1 west) belong
    // to a stairwell and must not be touched by the passes that punch, move,
    // lock or noclip doorways after the stamp.
    uint8_t vflag[CCELLS][CCELLS] = {};
    int8_t vfeat[CCELLS][CCELLS] = {};
    uint8_t prot[CCELLS][CCELLS] = {};
    VertFeat feats[2];
    int nfeat = 0;
    bool built = false;
    Mesh meshes[MESH_COUNT] = {};
};
struct AABB {
    float minx, minz, maxx, maxz, top;   // top: height you can stand on
    bool seeThrough = false;             // a rail: stops a body, not a look
};

// Scratch capacity for "everything solid in the 3x3 cells around a point" —
// three walls and a prop per cell would be 36, so 48 leaves headroom.
constexpr int MAX_NEARBY_AABBS = 48;

// A wall or a window stops you getting through a cell edge; a doorway does not.
// Collision, pathfinding, line of sight and the mesher all share this test.
// Light is the exception and does NOT use it — buildOccupancy checks for
// WALL_SOLID on its own, because glass blocks a body but not a fluorescent.
inline bool blocksEdge(uint8_t wall) {
    return wall == WALL_SOLID || wall == WALL_WINDOW || wall == WALL_LOCKED || wall == WALL_RAIL;
}
// What stops a fluorescent, which is not the same list: glass and a doorway
// both let light through a body cannot pass, and a shut door does the reverse.
// buildOccupancy used to test WALL_SOLID inline; it goes through here now so
// the locked doors cast the shadow the leaf in them obviously should.
inline bool blocksLight(uint8_t wall) { return wall == WALL_SOLID || wall == WALL_LOCKED; }

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
    // Which visit to this level, this descent — 0 the first time you arrive.
    // Mixed into the chunk seed so coming back gives you a genuinely different
    // maze rather than the one you already stripped. The chunk seed was
    // seed + level*K alone, so Level 0 regenerated identically every time you
    // returned to it and the exit loop 0 -> 1 -> 2 -> 4 -> 0 put every pickup
    // back where it was. At visit 0 the mix is a no-op, so a fresh descent at a
    // given seed still produces exactly the maze it always did.
    unsigned visit = 0;
    float wallH = 3.0f;
    bool exitTest = false;   // BACKROOMS_EXITS env: exits everywhere, for visual testing
    bool manilaTest = false; // BACKROOMS_MANILA env: a Manila Room in the chunk east of spawn
    // ---- storeys (see the note above VertKind). storeyH is the floor-to-floor
    // pitch, 0 on a level that is one floorplan. `storey` is the one you are
    // on, and `chunks` always holds its chunks, so every caller that iterates
    // the chunks it can see keeps seeing the floor it is standing on. Every
    // other storey's chunks live in `layers`.
    //
    // `qs` is the storey the accessors below actually read. It is `storey`
    // except inside a StoreyScope — meshing the floor below, asking what is
    // under a hole — and it is what data(), generate() and the mesher consult,
    // so no accessor needed a new parameter and none can forget one.
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
    // Move the player's storey. The chunk maps swap rather than rebuild: the
    // floor you just climbed to was already generated and meshed to be drawn
    // through the stairwell, and its meshes are in its own local frame.
    void setStorey(int s);
    // The seed for storey qs. Storey 0 gets the bare seed, so a fresh descent
    // at a given seed still opens on exactly the maze it always did; every
    // other storey is its own building.
    unsigned sseed() const {
        return qs == 0 ? seed : seed ^ (uint32_t)((uint32_t)qs * 0x9E3779B1u + 0x7F4A7C15u);
    }
    // Features touching storey s in chunk (cx,cz): the ones rising from it and
    // the ones arriving at it. A pure function of the seed, the level, the visit
    // and the coordinates — never of anything generated — so both storeys agree.
    int featuresFor(int cx, int cz, int s, VertFeat *out, int cap);
    bool pairFeature(int cx, int cz, int p, VertFeat &out);   // the one feature joining p and p + 1, if any
    bool manilaChunk(int cx, int cz, int s);                  // does storey s hold a Manila Room here
    uint8_t vflagAt(int ci, int ck);                           // VertFlag bits of a cell on storey qs
    // The walking surface of a flight or landing at (x,z), in storey qs's frame,
    // or NAN where the cell has no stair. Stepped, not a ramp: the tread you see
    // is the tread you stand on. `ramp` gives the nosing line instead — what a
    // handrail follows.
    float stairY(float x, float z, bool ramp = false);
    // A feature's local frame: world point -> (u, v) metres, and back.
    void featureLocal(const VertFeat &f, int cx, int cz, float x, float z, float &u, float &v) const;
    Vector3 featureWorld(const VertFeat &f, int cx, int cz, float u, float y, float v) const;
    // Do any chunks around this one link storey qs to storey qs + rel?
    bool linksStorey(int cx, int cz, int rel);
    // An enclosed stairwell's light: a batten on the end wall over the half
    // landing, in the frame of the storey the stairwell rises from. The shader
    // has one spare point light (uLamp); Game hands it the nearest of these.
    Vector3 landingLamp(const VertFeat &f, int cx, int cz) const {
        return featureWorld(f, cx, cz, CELL, storeyH * 0.5f + 2.25f, 4 * CELL - WT - 0.07f);
    }
    // Write storey qs's half of a feature into a chunk being generated: its
    // cleared margin, its cell flags, its walls, rails and doors.
    void stampFeature(ChunkData &d, const VertFeat &f, int cx, int cz);
    uint8_t wallNVal(int ci, int ck);
    uint8_t wallWVal(int ci, int ck);
    bool pillarAt(int ci, int ck);
    uint8_t propAt(int ci, int ck);
    bool poolAt(int ci, int ck);
    float floorY(int ci, int ck);
    // The ceiling sits one wall height above *this cell's* floor, not at a
    // fixed y. A raised deck carries its ceiling up with it; without that it
    // would push its floor through a slab that never moved.
    float ceilY(int ci, int ck);
    void ensureMesh(int cx, int cz);
    int gatherCellAABBs(int ci, int ck, AABB *out, int cap, int cnt, bool includeProps = true);
    // feetY: obstacles whose top is at or below your feet are walkable, not solid
    void collideCircle(float &px, float &pz, float r, float feetY = 0.0f);
    // floor height here, counting prop tops at or below your feet (so you can stand on furniture)
    float groundAt(float x, float z, float feetY);
    bool lineOfSight(float ax, float az, float bx, float bz);
    // Snapshot the local floorplan into a grid the shader marches for light
    // occlusion: bit0 = solid wall on this cell's north edge, bit1 = on its west
    // edge, bit2 = a pillar fills the cell. Only opaque blockers go in —
    // doorways, window glass, rails and furniture all let light through.
    // bit3 = the ceiling panel centred on this cell's min corner is not there
    // (it would hang in an opening), bit4 = this cell has no ceiling, so the
    // tubes of the storey above light it too, bit5 = a hole in this storey's
    // floor touches that corner, so the panel there shines down through it.
    // bit6 = one of bits 3-5 is set within reach of the fittings the shader
    // sums for a point in this cell; where it is clear the shader skips the
    // per-fitting lookups, so it must never be clear where one would matter.
    // Four bytes a cell: the storey you are on, the one below, the one above,
    // and a spare — the shader picks the byte by which storey the fragment is on.
    // `out` holds 2n rows: after the n of cells, n of fitting masks (which of
    // the nine fittings the shader sums in each light block exist, per storey).
    void buildOccupancy(int originI, int originK, int n, unsigned char *out);
    // can the hunter walk from cell (ci,ck) into the adjacent cell (ni,nk)?
    // Furniture and pillars are solid; only a doorway opens a walled edge.
    bool canStep(int ci, int ck, int ni, int nk);
    // BFS the cell grid from (si,sk) toward (ti,tk); fills the next cell to move
    // to in (outI,outK). false if no route within budget (fall back to a beeline).
    bool pathStep(int si, int sk, int ti, int tk, int &outI, int &outK);
    // Level 0 only: is this cell inside the Manila Room?
    bool manilaAt(int ci, int ck);
    // The centre (world metres) of the Manila Room in this point's chunk or
    // one of its eight neighbours, if there is one. Rooms are at least a chunk
    // apart, so there is never more than one in reach of the player.
    bool manilaNear(float x, float z, float &rx, float &rz);
    // Level 0 only: a rare patch of carpet that has stopped being a floor
    bool softAt(int ci, int ck);
    // How far the rotten patch in this cell has sunk at a continuous point in
    // it, as a positive depth below the cell's floor. The floor mesher shapes
    // the bowl out of this and groundAt walks the player down into it, so the
    // dip you see and the dip you stand in cannot drift apart.
    float softDip(float x, float z);
    // Red Halls only: a standpipe with a shut-off wheel on it. The mesher builds
    // the pipe, the game logic runs the puzzle, so both ask this.
    bool valveAt(int ci, int ck);
    // some exit doors glow red and were never going anywhere good
    bool cursedExit(int ci, int ck);
    Vector2 findOpenSpot(float x, float z);
    void unloadFar(int pcx, int pcz, int radius);
    void unloadAll();
    // ---- PAC-03: the place does not stay where you left it.
    //
    // The Backrooms is canonically non-Euclidean and this was a fixed grid that
    // was perfectly, deterministically consistent — the one thing the world
    // model actively worked against. `shifted` is an overlay of edges that have
    // become walls since you last looked at them: a doorway you walked through
    // is a blank wall when you turn round.
    //
    // It lands in wallNVal/wallWVal deliberately. Occupancy (and therefore the
    // lighting), the pathfinder, collision and the mesher all read the walls
    // through those two functions, so putting it anywhere else would have Clark
    // and the shadows disagreeing with the geometry. Game::shiftAWall is what
    // decides when, and only ever picks an edge you cannot currently see.
    std::unordered_set<uint64_t> shifted;
    // Doors the player has unlocked, in the same edge-key space. Read in
    // wallNVal/wallWVal for exactly the reason `shifted` is: collision, the
    // pathfinder, line of sight, the light's occupancy grid and the mesher all
    // come through those two, and a door that has opened for the player but not
    // for Clark is worse than one that never opened.
    std::unordered_set<uint64_t> unlockedDoors;
    // Storey-aware: a doorway walled off, or a door unlocked, on one floor is
    // not the same edge as the one directly over it. Storey 0 keys exactly as
    // it always did.
    uint64_t edgeKey(int ci, int ck, bool west) const {
        return ((key(ci, ck) << 1) | (west ? 1ull : 0ull)) ^ ((uint64_t)(uint32_t)qs * 0xD6E8FEB86659FD93ULL);
    }
    void shiftEdge(int ci, int ck, bool west);   // wall it off, and rebake the chunk that owns it
    void unlockEdge(int ci, int ck, bool west);  // open a locked door for good, and rebake
    // Is there a key lying loose in this cell? One per chunk at most.
    bool keyAt(int ci, int ck);
    void rebuildChunk(int cx, int cz);           // drop its meshes so streamChunks bakes it again
};

// Point the world's accessors at another storey for the length of a scope.
// Everything World reads goes through data(), and data() reads qs, so this is
// the one switch — meshing the floor below, looking down a hole for a floor,
// building the shadow grid of the storey above.
struct StoreyScope {
    World &w; int prev;
    StoreyScope(World &world, int s) : w(world), prev(world.qs) { w.qs = s; }
    ~StoreyScope() { w.qs = prev; }
    StoreyScope(const StoreyScope &) = delete;
    StoreyScope &operator=(const StoreyScope &) = delete;
};


Mesh buildFlareMesh();
// A Level 1 supply crate and its lid, base on y = 0 (see Game::crateAt).
Mesh buildCrateMesh();
Mesh buildCrateLidMesh();
