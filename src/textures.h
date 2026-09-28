#pragma once
// Procedural textures — surfaces are composed at startup, including embedded CC0 object tiles.
#include "raylib.h"
#include "core/world.h"   // VEND_HW, VEND_Y1: the vending machine's body

// A world surface: its colour, and the detail map the shader reads beside it
// (texture1: packed tangent slopes in RG, gloss mask in B, alpha 255). Both
// come out of one generator, because the relief is authored as a height field
// in metres alongside the paint rather than guessed from the paint afterwards:
// a printed chevron is flat, a groove between two ceiling boards is not, and
// the old luminance-derived relief could not tell them apart.
struct Surface { Texture2D albedo{}, detail{}; };
// Upload an image; `tiled` mipmaps it with trilinear + anisotropic filtering and
// repeat wrapping (see the note in textures.cpp about raylib's filter order).
Texture2D finishTexture(Image img, bool tiled);
Surface makeWallpaperSurface();    // Level 0: mono-yellow chevron vinyl, roll seams, skirting
Surface makeCarpetSurface();       // Level 0: heathered level-loop contract carpet
Surface makeCeilingSurface();      // Level 0: 600 mm fissured mineral board on white T-bar
// Walk-cycle frames, laid out left to right in one sheet. A billboard picks
// two adjacent frames and cross-fades between them, so the count is also the
// resolution of the gait: six is enough that adjacent frames differ by a few
// pixels and the fade reads as motion blur rather than as a dissolve.
constexpr int ENT_FRAMES = 6;      // Clark, the Smiler and the partygoer, 128 px per frame
constexpr int DOG_FRAMES = 4;      // the pack, 192 px per frame

// Rows of the entity sheet, 256 px each. A billboard always faces you, so
// neither a head turn nor a lean can come from the camera — both have to be in
// the sprite.
//
// Rows 0-2 are how far round his head is: at row 0 he is looking away and there
// is no eyeshine at all, at row 2 he is looking straight at you and the eye is
// lit. The eye appearing is the tell.
//
// Rows 3-4 are the same front-on head, sheared: he tips into the direction he is
// running. This is a shear in the sprite rather than a rotation at the draw call
// because raylib's DrawBillboardPro does not place a billboard the way
// DrawBillboardRec does — see AGENTS.md.
enum EntRow { ENT_ROW_AWAY = 0, ENT_ROW_HALF, ENT_ROW_FACE, ENT_ROW_LEAN_L, ENT_ROW_LEAN_R, ENT_ROWS };

Texture2D makeClarkTex();          // PIRATE CLARK, Level 0's hunter, ENT_FRAMES wide
Texture2D makeSmilerTex(bool glow);   // the Smiler: fog body, or its unlit eyes and grin; ENT_FRAMES wide
Texture2D makePartygoerTex();      // the thing that lives at the party
Texture2D makeScrawlTex();         // graffiti atlas: what earlier wanderers wrote
Texture2D makePropsTex();          // prop atlas: cardboard / cabinet / metal
Texture2D makeFixturesTex();       // fittings atlas: outlets, switch, grille, diffuser, exit sign, metal

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
// half of the fixtures atlas and carved into pieces by the mesher (world_mesh.cpp,
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
inline Vector2 vendUV(float x, float y) {
    return { (VEND_PX[0] + (x + VEND_HW) / (2 * VEND_HW) * VEND_PX[2]) / FIX_ATLAS_W,
             (VEND_PX[1] + (VEND_Y1 - y) / (VEND_Y1 - VEND_Y0) * VEND_PX[3]) / FIX_ATLAS_H };
}
Surface makeConcreteWallSurface();   // Level 1: plywood-formed walls, one tile floor to slab
Surface makeConcreteFloorSurface();  // Level 1 (+ Red Halls): power-trowelled slab
Surface makeConcreteCeilSurface();   // Level 1 (+ Red Halls): formed soffit
Surface makeRedBrickSurface();       // Red Halls: real-size brick in stretcher bond
// Poolrooms: white glazed tile at one pitch everywhere. Floors and ceilings map
// 2 m to a texture repeat and walls 3 m, so they need two textures to put the
// grout lines at the same spacing — and then they meet where wall meets floor.
Surface makeTileSurface(bool wall);
Surface makePartyWallSurface();      // LEVEL FUN =): party-print vinyl, bunting, smileys
Surface makePartyCarpetSurface();    // LEVEL FUN =): patterned banquet-hall carpet
Surface makePartyCeilSurface();      // LEVEL FUN =): black acoustic board on black grid
Texture2D makeAOStripTex();        // gradient strip for baked contact-shadow decals
Texture2D makeDogTex();            // THE RED HALLS: whatever the pack is, seen side-on
Texture2D makeAlmondWrapTex();     // almond water can, unwrapped: label strip + lid + base
Texture2D makeDeckTex();           // the tape player: top / body / front / reel, in one atlas

Texture2D makeParticleTex(); // soft procedural disc for smoke and muzzle flash

Texture2D makePropDetail(Texture2D albedo);
