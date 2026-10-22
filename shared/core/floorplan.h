#pragma once
// What a chunk's floorplan is made of: each cell's floor and ceiling, what
// stands on each edge and how high, the risers where floors step and the
// soffits where ceilings do, the ceiling as merged rectangles, the flights, and
// the Poolrooms' arches. chunkLayout says what stands in a chunk; this says
// what the chunk is. The mesher (shared/port/chunk_mesh.cpp) builds from it and
// decides nothing about where a floor, wall or ceiling goes; a port dresses the
// same pieces with its own meshes.
//
// Positions are world metres in the storey's own frame (y = 0 is its floor).
// Edge values follow Opening (layout.h): a north edge is the line z = line from
// x = e0 to e0 + CELL, a west edge the line x = line from z = e0. Its low side
// is the -z (north edge) or -x (west edge) cell, its high side cell (i, k).
// Lists are in cell order, i outer and k inner, and inside a cell in the order
// the mesher emits them, which blending and depth ties depend on.
#include "layout.h"
#include <cstdint>
#include <vector>

// The Poolrooms' vaults over the openings in the central partitions: the
// soffit is an ellipse from ARCH_SPRING up to ARCH_SPRING + ARCH_RISE over
// the 6 m opening, and the wall above it runs to wallH. ARCH_SEGMENTS
// straight pieces make the curve.
constexpr float ARCH_SPRING = 4.6f;
constexpr float ARCH_RISE = 2.4f;
constexpr int ARCH_SEGMENTS = 24;

enum class FloorKind : uint8_t {
    Flat,     // level at y
    Soft,     // Level 0's rotten patch: a bowl World::softDip deep under y
    Boards,   // inside the Manila Room, whose furnishings lay their own floorboards
    Stair,    // a flight or landing: a Flight's steps (or the storey above) cover it
    Hole,     // no floor: the storey below shows through
};

// What stands on one edge of a cell.
struct FloorEdge {
    uint8_t wall;            // WallKind through wallNVal/wallWVal, so the overlays count
    // A wall here runs from the lower floor either side to the higher ceiling,
    // and everything fixed to it is measured off that base.
    float base, top;
    // The edge before this one on its line (west of a north edge, north of a
    // west edge) is wall that buries this wall's end (bit 0); the edge after it
    // (bit 1). A rail buries nothing: it is thinner than the wall.
    uint8_t buried;
    uint8_t voidFaces;       // the low side (bit 0) or the high side (bit 1) is a hole in the floor
    // Either side's floor and ceiling, and whether it has them (cellHasFloor,
    // cellHasCeiling), for skirting and creases.
    float floorLo, floorHi, ceilLo, ceilHi;
    bool hasFloorLo, hasFloorHi, hasCeilLo, hasCeilHi;
};

struct FloorCell {
    FloorKind floor;
    float y;                 // floorY
    bool pool;               // Level 2: water at WATER_Y over this cell
    bool ceiling;            // cellHasCeiling: false where it opens into the storey above
    float ceilY;             // World::ceilY: the storey's pitch where there is no ceiling
    bool pillar;
    FloorEdge north, west;
};

// A vertical face where the floor steps between two cells, listed once, by the
// cell that draws it. (x0, z0) to (x1, z1) is the edge in the order its quad is
// wound; it runs from `hi` down to `lo` and faces (nx, 0, nz).
struct Riser {
    uint8_t i, k;
    float x0, z0, x1, z1;
    float hi, lo;
    float nx, nz;
};

// Level 1: the safety edging along a loading dock's lip, on the high cell, from
// (x0, z0) to (x1, z1), its width running in toward (inx, inz).
struct Lip {
    uint8_t i, k;
    float x0, z0, x1, z1;
    float inx, inz;
};

// A rectangle of ceiling at one height. Equal neighbours are merged (along x
// first, then whole rows down z), so a flat chunk is one rectangle.
struct CeilingRect {
    float x0, z0, x1, z1, y;
};

// Where a neighbour's ceiling is higher than cell (i, k)'s, the face that closes
// the slot, from `lo` (this cell's ceiling) up to `hi`, along (ax, az) to
// (bx, bz). A bulkhead is the edge of an opening into the storey above: papered,
// as a wall is, up past the void to the floor above.
struct Soffit {
    uint8_t i, k;
    float ax, az, bx, bz;
    float lo, hi;
    bool bulkhead;
};

// One step of a flight, in its feature's frame (World::featureLocal): u across
// from u0 to u1, v along the rise from va to vb, a block from y0 up to its
// tread at y1. `risesPlusV`: the flight climbs toward +v, so the riser is at va.
struct FlightStep {
    float u0, u1, va, vb, y0, y1;
    bool risesPlusV;
};

// The flights rising from this storey. A storey draws the steps it stands on;
// the storey above draws the hole, rails and arrival floor.
struct Flight {
    VertFeat feat;
    std::vector<FlightStep> steps;
    // An enclosed stairwell's half landing, after its first twelve steps: the
    // rectangle u 0..landingU1, v landingV0..landingV1 at landingY.
    bool landing = false;
    float landingU1 = 0, landingV0 = 0, landingV1 = 0, landingY = 0;
    int landingAfter = 0;    // how many steps come before it
};

// A Poolrooms vault: over the opening from start * CELL to start * CELL + 6 m
// in the partition along chunk-local x = 8 cells (axis 1) or z = 8 (axis 0).
struct Arch {
    uint8_t axis, start;
};

struct ChunkFloorplan {
    int cx = 0, cz = 0, storey = 0;
    FloorCell cells[CCELLS][CCELLS];
    std::vector<Riser> risers;
    std::vector<Lip> lips;
    std::vector<CeilingRect> ceiling;
    std::vector<Soffit> soffits;
    std::vector<Flight> flights;
    std::vector<Arch> arches;

    template <class T> struct Span {
        const T *b, *e;
        const T *begin() const { return b; }
        const T *end() const { return e; }
    };
    Span<Riser> risersIn(int i, int k) const;
    Span<Lip> lipsIn(int i, int k) const;

    // Cell (i, k)'s risers are risers[riserFirst[i*CCELLS + k] .. riserFirst[i*CCELLS + k + 1]).
    uint16_t riserFirst[CCELLS * CCELLS + 1] = {};
    uint16_t lipFirst[CCELLS * CCELLS + 1] = {};
};

// The floorplan of chunk (cx, cz) on storey w.qs, generating it and its
// neighbours as needed.
ChunkFloorplan chunkFloorplan(World &w, int cx, int cz);
