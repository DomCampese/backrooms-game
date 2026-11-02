#pragma once
// The infinite maze: deterministic chunk generation, storeys, collision, line
// of sight, pathfinding and the light-occlusion grid. Chunks are generated on
// demand and unloaded behind the player. Standard library only: the raylib
// mesher is web/src/world_mesh.cpp.
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
    // A doorway: the opening between DOOR_LO and DOOR_HI along the edge, under
    // a header at DOOR_HEAD, with a frame and a threshold. Passable, but its
    // jambs are solid (gatherCellAABBs).
    WALL_DOOR   = 4,
    // A doorway with a shut, locked leaf. Blocks bodies and light until
    // World::unlockEdge turns it into a WALL_DOOR through the wall overlay.
    WALL_LOCKED = 5,
    // A guard at the edge of a drop: round a stair opening or an atrium, and up
    // the open side of a flight. Stops a body at any height, but not light or
    // sight.
    WALL_RAIL   = 6,
    // A doorway whose leaf the player has shut. Never stored in a chunk: the
    // shutDoors overlay turns a WALL_DOOR into this in wallNVal/wallWVal. Blocks
    // bodies, light and sight like a locked door.
    WALL_SHUT   = 7,
};

// A doorway's opening (and an exit's): from DOOR_LO to DOOR_HI along its edge,
// 1.3 m, under a header DOOR_HEAD above the floor. Collision and the mesher
// both read these.
constexpr float DOOR_LO = 0.35f, DOOR_HI = CELL - DOOR_LO;
constexpr float DOOR_HEAD = 2.3f;
// A pillar fills the middle of its cell, PILLAR_LO to PILLAR_HI on both axes.
constexpr float PILLAR_LO = 0.42f, PILLAR_HI = CELL - PILLAR_LO;

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
    PROP_SEATS,          // 17 a row of four cinema seats facing the screen (the multiplex)
    PROP_CONCESSION,     // 18 a concession counter, run along x (the multiplex)
};

constexpr int PROP_KINDS = PROP_CONCESSION + 1;

// A prop's collision box, as offsets from its cell's min corner, and its top
// above the floor it stands on. The mesher builds each piece to this top and
// the sim stands a carton on it where `shelf` is set, so the three cannot
// disagree. Cartons (their top varies with the hash), the vending machine
// (vendFootprint) and the rubble of a fallen tile (walked over) are not boxed
// from here.
struct PropShape {
    bool solid;
    float x0, z0, x1, z1, top;
    bool shelf;
};
constexpr PropShape PROP_SHAPES[PROP_KINDS] = {
    {},                                                    // PROP_NONE
    {},                                                    // PROP_BOXES
    { true, 0.60f, 0.60f, 1.40f, 1.40f, 1.32f, true },    // PROP_CABINET
    { true, 0.32f, 0.32f, 1.68f, 1.68f, 0.72f, true },    // PROP_TABLE
    {},                                                    // PROP_FALLEN_TILE
    { true, 0.20f, 0.38f, 1.80f, 1.62f, 0.44f, false },   // PROP_COUCH: the seat
    { true, 0.55f, 0.62f, 1.45f, 1.38f, 1.90f, false },   // PROP_ARMOIRE
    { true, 0.82f, 0.82f, 1.18f, 1.18f, 1.62f, false },   // PROP_LAMP
    { true, 0.66f, 0.66f, 1.34f, 1.34f, 0.60f, true },    // PROP_NIGHTSTAND
    { true, 0.30f, 0.15f, 1.70f, 1.85f, 0.46f, false },   // PROP_BED: the mattress
    {},                                                    // PROP_VENDING
    { true, 0.40f, 0.40f, 1.60f, 1.60f, 0.74f, true },    // PROP_PARTY_TABLE
    { true, 0.30f, 0.50f, 1.70f, 1.50f, 0.74f, true },    // PROP_DESK
    { true, 0.36f, 0.70f, 1.64f, 1.30f, 1.80f, false },   // PROP_SHELVING
    { true, 0.78f, 0.78f, 1.22f, 1.22f, 0.94f, false },   // PROP_COOLER: the body, under the bottle
    { true, 0.80f, 0.80f, 1.20f, 1.20f, 0.32f, false },   // PROP_PLANT: the pot's rim
    {},                                                    // PROP_MANILA_TABLE: the room's furniture is not boxed
    {},                                                    // PROP_SEATS: turned, so gatherCellAABBs boxes it (seatBox)
    { true, 0.00f, 0.55f, 2.00f, 1.45f, 1.05f, true },    // PROP_CONCESSION: full length, so a run is one counter
};
// Where things lie on the Manila Room's table: its notes and its can, 2 mm
// over the top (0.785) so they do not fight it for depth.
constexpr float MANILA_TABLE_REST = 0.787f;

// propRot bit 2: the prop stands pushed back against the solid wall behind it.
// Only vending machines. World::generate sets it after every pass that changes
// an edge, so the mesher and collision (vendFootprint) agree.
constexpr uint8_t PROP_AGAINST_WALL = 4;
// propRot's quarter turn, radians, as authored (not TAU / 4: the mesher and
// collision both use this value, and changing it moves every prop).
constexpr float PROP_TURN = 1.5708f;
// Per-piece variation of a prop: ih(gi, gk, sseed() ^ PROP_HASH_SALT). The
// mesher builds from it and collision reads the same heights.
constexpr uint32_t PROP_HASH_SALT = 0xB0B5u;

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
// Level 1's lift doors: which north edges carry one. chunkLayout places them;
// the vending pass keeps machines from backing onto them.
bool liftHash(int gi, int gk, unsigned s);


// The Manila Room (Level 0): 8 x 8 m (room cells MANILA_LO..MANILA_HI on both
// axes), thick walls, a door in each side. One chunk in MANILA_RATE, never in
// the chunks round spawn.
constexpr uint32_t MANILA_RATE = 20;
constexpr int MANILA_LO = 6, MANILA_HI = 9;
// The room's centre is the min corner of chunk cell (MANILA_MID, MANILA_MID).
constexpr int MANILA_MID = MANILA_LO + 2;

// The multiplex (level MULTIPLEX). Most chunks fill the room patch with one
// auditorium, AUD_N cells square: u runs across it, v back from the screen.
// The seats are rows AUD_ROW0..AUD_ROW1, each AUD_RISE decimetres above the
// row in front, so the back cross aisle (v = AUD_N - 1) is at floor 0 and the
// front of the room is sunk AUD_DEPTH below it. A sunken cell keeps the room's
// ceiling, so the ceiling stays flat. v = 0 is a stage AUD_STAGE above the
// front floor. Aisles run at u = 0, AUD_N / 2 and AUD_N - 1.
constexpr int AUD_LO = HALL_LO;
constexpr int AUD_N = CCELLS - HALL_LO - HALL_HI;
constexpr int AUD_ROW0 = 2, AUD_ROW1 = AUD_N - 2;
constexpr int AUD_RISE = 3;
constexpr int AUD_DEPTH = AUD_RISE * (AUD_N - 1 - AUD_ROW0);
constexpr int AUD_STAGE = 4;
// One chunk in AUD_LOBBY_RATE is a lobby instead, and so is the spawn chunk.
constexpr uint32_t AUD_LOBBY_RATE = 4;
// The screen: AUD_SCREEN_W m across, from AUD_SCREEN_LIFT above the stage up to
// AUD_SCREEN_TOP below the ceiling, standing AUD_SCREEN_OFF off the wall face.
constexpr float AUD_SCREEN_W = 16.0f, AUD_SCREEN_LIFT = 0.6f, AUD_SCREEN_TOP = 0.4f;
constexpr float AUD_SCREEN_OFF = 0.05f;
// Chunk-local cell of auditorium cell (u, v), for an auditorium whose screen
// is on its -z (dir 0), +z (1), -x (2) or +x (3) side. One step outside the
// room in u or v gives the corridor cell beyond its wall.
inline void audCell(int dir, int u, int v, int &x, int &z) {
    const int m = AUD_N - 1;
    switch (dir) {
    case 0:  x = AUD_LO + u;     z = AUD_LO + v;     break;
    case 1:  x = AUD_LO + m - u; z = AUD_LO + m - v; break;
    case 2:  x = AUD_LO + v;     z = AUD_LO + m - u; break;
    default: x = AUD_LO + m - v; z = AUD_LO + u;     break;
    }
}
// Floor of auditorium row v, decimetres.
inline int audElev(int v) {
    if (v <= 0) return -AUD_DEPTH + AUD_STAGE;
    if (v < AUD_ROW0) return -AUD_DEPTH;
    return -AUD_RISE * (AUD_N - 1 - v);
}
// propRot of a seat row facing the screen: turn r faces -z, +x, +z, -x.
inline uint8_t audFacing(int dir) { return (uint8_t)(dir == 0 ? 0 : dir == 1 ? 2 : dir == 2 ? 3 : 1); }
// The collision box of a seat row in cell (x0, z0)'s min corner: the seats
// fill the back of the cell (away from the screen), and the front is legroom
// a body can walk along.
void seatBox(uint8_t rotByte, float x0, float z0, float &bx0, float &bz0, float &bx1, float &bz1);
constexpr float SEAT_TOP = 1.0f;             // the seat backs
constexpr float SEAT_FRONT = 0.95f, SEAT_BACK = 1.90f;   // seat depth, metres from the cell's front edge

// What a renderer needs of one auditorium: its screen and the light it throws.
struct Auditorium {
    int cx, cz, dir;
    Vec3 screen;          // centre of the screen's face, world metres
    Vec3 normal;          // out of the screen, toward the seats
    Vec3 across;          // along the screen, u increasing
    float halfW, y0, y1;  // half its width; its bottom and top
    Vec3 lamp;            // where its light is centred (the shader's uLamp)
    Vec3 booth;           // the projection port in the back wall
    float rect[4];        // the room's floor, x0 z0 x1 z1: its tubes are masked
    uint32_t seed;        // per auditorium: what it is showing
};

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
// An enclosed stairwell's landing light: the point the shader's spare light
// shines from (World::landingLamp), and the batten's tube, its centre and
// whether it runs along x. The tube goes out in a blackout, so a renderer
// draws it, not the chunk mesh.
struct LandingLight {
    Vec3 lamp, tube;
    bool alongX;
};

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
    VF_HOLE     = 4,    // no floor: the storey below shows through, and a body falls into it
    VF_WALKHOLE = 8,    // a flight comes up through the hole, so it can be walked down
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

// The bits of a cell's byte in the occupancy grid (World::buildOccupancy).
// worldFragmentShader() writes them into the GLSL, and lightAtCPU reads them.
enum OccBit : uint8_t {
    OCC_WALL_N = 1,          // an opaque wall on the north edge
    OCC_WALL_W = 2,          // ... on the west edge
    OCC_PILLAR = 4,
    OCC_NO_FITTING = 8,      // no fitting at this cell's min corner: it would hang in an opening
    OCC_OPEN_ABOVE = 16,     // no ceiling here, so the storey above's tubes light it
    OCC_HOLE_CORNER = 32,    // a hole touches that corner, so its fitting lights the storey below
    OCC_NEAR_OPENING = 64,   // one of the three above is set within reach of the fittings the shader sums here
};

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
    int8_t audDir = -1;              // the multiplex: this chunk's auditorium faces dir (audCell); -1 none
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
    return wall == WALL_SOLID || wall == WALL_WINDOW || wall == WALL_LOCKED || wall == WALL_RAIL ||
           wall == WALL_SHUT;
}
// Does this edge stop light? Glass and doorways pass light; a locked door
// does not.
inline bool blocksLight(uint8_t wall) { return wall == WALL_SOLID || wall == WALL_LOCKED || wall == WALL_SHUT; }

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
    // The multiplex: chunk (cx, cz)'s auditorium, generating the chunk, or
    // false for a lobby or another level.
    bool auditorium(int cx, int cz, Auditorium &a);
    // The auditoriums in the 3 x 3 chunks round (pcx, pcz) that are already
    // generated, so a renderer never generates a chunk to light a frame.
    std::vector<Auditorium> auditoriums(int pcx, int pcz);
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
    // The landing lights of the enclosed stairwells in the generated chunks of
    // the 3 x 3 round chunk (pcx, pcz), in the frame of the storey you are on
    // (World::storey), chunk by chunk then feature by feature. Nothing is
    // generated to find them.
    std::vector<LandingLight> landingLights(int pcx, int pcz);
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
    // originK), four bytes a cell: your storey, the one below, the one above, spare,
    // each an OccBit set. OCC_NEAR_OPENING must never be clear where a missing
    // fitting matters. `out` has 2n rows: n of cells, then n of fitting masks
    // (buildOccupancy).
    void buildOccupancy(int originI, int originK, int n, unsigned char *out);
    // Can a body walk from cell (ci,ck) into the adjacent (ni,nk)? Furniture and
    // pillars fill their cell; only a doorway opens a walled edge.
    // throughShut: treat a shut door as open, for a body that can open one (the
    // hunter). The pack cannot.
    bool canStep(int ci, int ck, int ni, int nk, bool throughShut = false);
    // BFS from (si,sk) toward (ti,tk) within 16 cells; the first cell to move to.
    // False if there is no route (fall back to a beeline).
    bool pathStep(int si, int sk, int ti, int tk, int &outI, int &outK, bool throughShut = false);
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
    // Red Halls only: a standpipe with a shut-off wheel. chunkLayout places it;
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
    // Doorways whose leaf is shut (WALL_SHUT), in the same edge-key space.
    // Cleared with the level (unloadAll).
    std::unordered_set<uint64_t> shutDoors;
    // Edges are per storey: the edge directly above is a different edge. Storey
    // 0 keys as a one-storey world did.
    uint64_t edgeKey(int ci, int ck, bool west) const {
        return ((key(ci, ck) << 1) | (west ? 1ull : 0ull)) ^ ((uint64_t)(uint32_t)qs * 0xD6E8FEB86659FD93ULL);
    }
    void shiftEdge(int ci, int ck, bool west);   // wall it off, and rebake the chunk that owns it
    void unlockEdge(int ci, int ck, bool west);  // open a locked door for good, and rebake
    // Shut or open a doorway's leaf, and rebake both chunks that draw it.
    void shutEdge(int ci, int ck, bool west, bool shut);
    // Is the next edge along this one's wall line a doorway (open or shut)?
    // Two such doorways share one opening with no jamb between them, so neither
    // takes a leaf.
    bool doorJoined(int ci, int ck, bool west);
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
