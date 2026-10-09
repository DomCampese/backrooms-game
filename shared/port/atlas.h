#pragma once
// Where things sit in the Web build's fixtures atlas, which the painter
// (web/src/textures.cpp) draws and the chunk mesher (chunk_mesh.cpp) maps onto
// geometry. One table for both, so a fitting's paint and its quad cannot drift.
#include "../core/vec.h"
#include "../core/world.h"   // VEND_HW, VEND_Y1: the vending machine's body
#include "../core/level_rules.h"

// Where each fitting sits in the fixtures atlas, and how big it is on the wall.
//
// One table, shared by the atlas that draws them and the mesher that hangs
// them, because the quad stretches its cell to fit: draw a fitting at one
// aspect and hang it at another and it comes out squashed. A faceplate is a
// thing everybody has seen ten thousand times, so a wrong one reads as wrong
// immediately — the first version of this drew each fitting inside part of a
// square cell and hung the whole cell, and every outlet in the building came
// out a narrow vertical sliver.
// FIX_MANILA is not a fitting but a tile of the Manila Room's wallpaper, and
// FIX_NOTE one of the notes left on its table; both live here because the
// fixtures mesh is where the room's decals go.
enum FixtureId { FIX_OUTLET = 0, FIX_OUTLET_BROKEN, FIX_SWITCH, FIX_GRILLE, FIX_DIFFUSER, FIX_SIGN,
                 FIX_MANILA, FIX_NOTE, FIX_COUNT };
struct FixtureRect {
    float u0, v0, u1, v1;   // its cell in the atlas, normalised
    float halfW, halfH;     // and half its size on the wall, in metres
};
extern const FixtureRect FIXTURES[FIX_COUNT];
constexpr int FIX_ATLAS_W = 1024, FIX_ATLAS_H = 512;   // the fittings on the left half, the vending machine on the right

// The vending machine's front door, painted at its real size into the right
// half of the fixtures atlas and carved into pieces by the mesher (port/chunk_mesh.cpp,
// PROP_VENDING): the frame round the window, the backlit header, the coin and
// bill column, the delivery flap, and the lit back of the cabinet you see the
// cans against through the glass. Every rect below is in metres on the door
// (x across it from the middle, y up from the floor); `vendUV` turns a point
// into the atlas, so a piece of geometry and the paint under it cannot drift.
constexpr float VEND_Y0 = 0.06f;                 // door bottom, over the plinth; VEND_HW and VEND_Y1 are core's
constexpr int   VEND_PX[4] = { 520, 4, 250, 504 };  // x, y, w, h in the atlas: 0.88 x 1.77 m
struct VendRect { float x0, y0, x1, y1; };
constexpr VendRect VEND_WIN    = { -0.40f, 0.62f, 0.17f, 1.70f };   // the glass
constexpr VendRect VEND_BIN    = { -0.40f, 0.22f, 0.17f, 0.50f };   // the delivery flap
constexpr VendRect VEND_HEADER = { -0.44f, 1.70f, 0.44f, 1.83f };   // backlit sign across the top
constexpr VendRect VEND_DISP   = {  0.215f, 1.525f, 0.405f, 1.605f };  // the selection display
constexpr int   VEND_ROWS = 6, VEND_COLS = 5;    // what the spirals hold: six shelves of five
constexpr float VEND_ROW0 = 0.645f, VEND_ROWP = 0.176f;   // top of the lowest shelf, and the pitch
// One price strip per shelf, clipped into the shelf's front lip: at (x, y + r * 12)
constexpr int   VEND_STRIP_PX[4] = { 780, 4, 240, 10 };
inline Vec2 vendUV(float x, float y) {
    return { (VEND_PX[0] + (x + VEND_HW) / (2 * VEND_HW) * VEND_PX[2]) / FIX_ATLAS_W,
             (VEND_PX[1] + (VEND_Y1 - y) / (VEND_Y1 - VEND_Y0) * VEND_PX[3]) / FIX_ATLAS_H };
}

// Metres per texture repeat. Floors and ceilings map (x, z) / FLOOR_TILE_M;
// walls run WALL_TILE_M across and wallTileV(level) down, which is the wall
// height on Level 1, whose concrete carries a tide line at a real height.
constexpr float FLOOR_TILE_M = 2.0f, WALL_TILE_M = 3.0f;
inline float wallTileV(int level) { return level == 1 ? LEVEL_RULES[1].wallH : WALL_TILE_M; }
