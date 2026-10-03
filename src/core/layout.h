#pragma once
// What a chunk holds besides its floorplan: furniture, fixtures on walls and
// ceilings, light fittings, and the openings in its walls. Core decides all of
// it from the seed; a renderer decides only how each thing looks. Positions are
// world metres in the storey's own frame (y = 0 is that storey's floor).
//
// Every list is in cell order, i outer and k inner, the order bakeChunk walks
// the cells. A renderer that emits each item where its old per-cell decision
// stood keeps its output order, which blending and depth ties depend on.
#include "world.h"
#include <cstdint>
#include <vector>

// How often the wall and ceiling fixtures turn up: one in N solid wall edges
// (each orientation hashed on its own) unless marked otherwise. Outlets are
// common on purpose: a thing of known size to judge a corridor by.
constexpr uint32_t OUTLET_RATE   = 5;
constexpr uint32_t OUTLET_BROKEN = 4;    // one outlet in this many has lost its cover
constexpr uint32_t SWITCH_RATE   = 23;
constexpr uint32_t GRILLE_RATE   = 27;
constexpr uint32_t EXITSIGN_RATE = 97;
constexpr uint32_t SPRINK_RATE   = 11;   // per ceiling cell
constexpr uint32_t DIFFUSER_RATE = 13;   // per ceiling cell
constexpr uint32_t CONDUIT_RUN   = 6;    // cells per conduit run, so runs are runs
// Wall scrawl: one in SCRAWL_RATE solid wall edges carries one of
// SCRAWL_PHRASES phrases. Must match makeScrawlTex's atlas (4 x 8), or walls
// show half of one phrase and half of another.
constexpr uint32_t SCRAWL_RATE = 40;
constexpr uint32_t SCRAWL_PHRASES = 32;

// A window's opening in its edge: WINDOW_LO to WINDOW_HI along it, from
// WINDOW_SILL to WINDOW_HEAD above the wall's base.
constexpr float WINDOW_LO = 0.45f, WINDOW_HI = 1.55f;
constexpr float WINDOW_SILL = 1.0f, WINDOW_HEAD = 2.1f;
// The luminous plane of a light fitting hangs this far below its ceiling. The
// shader's uLY and lightAtCPU use wallH - LIGHT_DROP.
constexpr float LIGHT_DROP = 0.12f;

struct PropPlacement {
    PropKind kind;
    uint8_t i, k;            // chunk-local cell
    int gi, gk;              // global cell
    float x, z;              // centre: the cell's, or a vending machine's from vendFootprint
    float floorY;            // the floor it stands on
    uint8_t turn;            // quarter turns (propRot & 3)
    float yaw;               // turn * PROP_TURN, radians
    bool againstWall;        // PROP_AGAINST_WALL: backed onto the solid wall behind it
    uint32_t hash;           // per-piece variation: ih(gi, gk, sseed() ^ PROP_HASH_SALT)
    uint32_t hash2;          // a second stream: the party table's cloth and cups
};

enum class FixtureKind : uint8_t {
    Outlet, BrokenOutlet, Switch, Grille, ExitSign,   // on a wall face
    Diffuser, Sprinkler,                               // in a ceiling
    Conduit,        // a steel run along the top of a wall
    Scrawl,         // a phrase written on a wall
    LiftDoor,       // shut lift doors, indicator and call button (Level 1)
    Spall,          // concrete blown off a wall, rebar showing (Level 1)
    PillarSpall,    // the same on a column
    Pipe,           // a service pipe under the ceiling, one cell long (Levels 1, 3)
    Valve,          // a standpipe with a shut-off wheel (Level 3)
    Streamer,       // a crepe streamer sagging between two ceiling points (Level 4)
    ManilaRoom,     // the Manila Room's furnishings (Level 0)
};

// One fixture. What each field means depends on the kind:
//
// | kind                  | pos                                   | end            | w, h               | other |
// |-----------------------|---------------------------------------|----------------|--------------------|-------|
// | Outlet .. ExitSign    | centre, on the wall face              |                |                    | |
// | Diffuser, Sprinkler   | cell centre, on the ceiling           |                |                    | |
// | Conduit               | one end of its underside, on the face | the other end  |                    | |
// | Scrawl                | centre, on the face                   |                | half extents       | angle: tilt; variant: phrase; tone: tint 0-3 |
// | LiftDoor              | the face at the edge's start, floor   |                |                    | |
// | Spall, PillarSpall    | centre of the scar, on the face       |                | half extents       | seed: its shape |
// | Pipe                  | centre of one end                     | the other end  | w: radius          | tone: 1 rusted; variant: 1 collar |
// | Valve                 | foot, at the cell centre              | top, ceiling   |                    | |
// | Streamer              | one end, under the ceiling            | the other end  | h: its lowest y    | variant: party colour |
// | ManilaRoom            | room centre; y is its ceiling         |                |                    | seed |
//
// `normal` points out of the face a fixture is on. A face is the wall's
// surface, WT off its centreline; decals stand off it by the renderer's own
// clearance.
struct Fixture {
    FixtureKind kind;
    int8_t i = -1, k = -1;   // chunk-local cell; -1 for one that belongs to the chunk
    Vec3 pos{}, end{}, normal{};   // zero where a kind leaves them unused
    float w = 0, h = 0, angle = 0;
    uint32_t seed = 0;
    uint8_t variant = 0, tone = 0;
};

// Why a fitting on the grid is not there.
enum class FittingGap : uint8_t {
    None,
    UnderOpening,   // its tray would overhang an opening to the storey above (occupancy bit 3)
    ManilaRoom,     // inside the Manila Room, which the chandelier lights (uRoomMask)
};

// A light fitting on the level's grid (LevelRules::ls). The static state comes
// from the same tube hash as the shader's lightState() and lightAtCPU. Flicker,
// blackouts and the hunter's pool of dead light vary with time and stay in the
// renderer.
struct LightFitting {
    int gx, gz;              // grid index: centre at (g + 0.5) * ls
    Vec3 pos;                // centre of the luminous plane, ceilingY - LIGHT_DROP
    float ceilingY;          // the ceiling of the cell it is centred in
    FittingGap gap;
    bool turned;             // a Level 1 batten runs along x
    bool dead;               // hash below LevelRules::dead: never lit
    float output;            // 1 - vary * fract(hash * 53.7): part output
    bool faulty;             // hash above 1 - LevelRules::faulty: stutters
    float stutterSeed;       // fract(hash * 97.31): when a faulty tube stutters (tubeStutter)
};

enum class OpeningKind : uint8_t { Doorway, LockedDoor, Exit, CursedExit, Window, Rail };

// An edge that is neither open floor nor plain wall. Along-edge values are
// world x for a north edge and world z for a west one.
struct Opening {
    OpeningKind kind;
    uint8_t i, k;            // the cell whose north (or west) edge this is
    bool west;
    float line;              // the wall's centreline: z of a north edge, x of a west edge
    float e0;                // where the edge starts; it runs to e0 + CELL
    float a0, a1;            // the opening: e0 + DOOR_LO .. DOOR_HI, or WINDOW_LO .. HI
    // The wall: from the lower floor either side to the higher ceiling. A rail's
    // base is the lower floor of the sides that have one.
    float base, top;
    float headY;             // underside of the header: base + DOOR_HEAD, or the window head
    float sillY;             // a window's sill: base + WINDOW_SILL
    float floorY;            // the floor of cell (i, k), under a threshold or a leaf
    // Doorway: the next edge along the line on the low (high) side is a doorway
    // too, so there is no jamb between them and the header runs on.
    bool joinLo = false, joinHi = false;
    // Exit: the wall past the high jamb is solid, so a leaf can stand open
    // against it; and its glyph, strokes between points 0-8 of a 3 x 3 lattice.
    bool leafRoom = false;
    uint8_t glyph[6] = {}, glyphLen = 0;
    // Rail: the cap's height over e0 and over e0 + CELL (RAIL_H above what you
    // stand on beside it, level round an opening, climbing beside a flight).
    float railTop0 = 0, railTop1 = 0;
    bool overVoid = false;   // holes both sides: the storey below draws it
    bool underside = false;  // one side is a hole, so its underside shows
    int8_t voidSide = 0;     // which face looks into the hole: +1 the +z (north edge) or -x (west edge) face
};

struct ChunkLayout {
    int cx = 0, cz = 0, storey = 0;
    std::vector<PropPlacement> props;
    std::vector<Fixture> fixtures;
    std::vector<LightFitting> fittings;
    std::vector<Opening> openings;

    struct Span {
        const Fixture *b, *e;
        const Fixture *begin() const { return b; }
        const Fixture *end() const { return e; }
    };
    // The fixtures of cell (i, k), in the order they were decided.
    Span fixturesIn(int i, int k) const;
    // The fixtures that belong to the chunk rather than to a cell.
    Span chunkFixtures() const;
    const PropPlacement *propIn(int i, int k) const;
    const Opening *opening(int i, int k, bool west) const;

    // Cell (i, k)'s fixtures are fixtures[first[i*CCELLS + k] .. first[i*CCELLS + k + 1]);
    // the chunk's own follow from first[CCELLS * CCELLS].
    uint16_t first[CCELLS * CCELLS + 1] = {};
    int16_t propAt[CCELLS][CCELLS];
    int16_t openN[CCELLS][CCELLS], openW[CCELLS][CCELLS];
};

// The layout of chunk (cx, cz) on storey w.qs, generating it and its
// neighbours as needed.
ChunkLayout chunkLayout(World &w, int cx, int cz);

// Whether a cell has a floor to fix trim and fittings above (not a hole, and not
// a flight, which buries the foot of its walls) and a ceiling (not open to the
// storey above). Always true on a level that is one floorplan.
bool cellHasFloor(World &w, int gi, int gk);
bool cellHasCeiling(World &w, int gi, int gk);

// The tube hash the shader's lhash() and lightAtCPU use, 0..1.
float tubeHash(float gx, float gz);
// Each storey's tubes fail in their own places: the tube hash is offset per
// storey. Bounded, because the shader's sin() loses the hash at large
// arguments. Storey 0 is offset 0. Must match storeyOffset() in shaders.cpp.
float storeyHashOffset(int s);
// A faulty fitting's output at time t, seconds, as the shader's lightState
// stutters it: 1, or 0.62 for the moments its gate is open and its noise low.
// Change one, change both.
float tubeStutter(float stutterSeed, float t);
