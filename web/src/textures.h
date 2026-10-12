#pragma once
// Procedural textures — surfaces are composed at startup, including embedded CC0 object tiles.
#include "raylib.h"
#include "core/world.h"   // VEND_HW, VEND_Y1: the vending machine's body
#include "core/level_rules.h"
#include "port/sheets.h"
#include "port/atlas.h"

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
// The sheets' frame counts, frame sizes and rows are port/sheets.h's.
Texture2D makeClarkTex();          // PIRATE CLARK, Level 0's hunter, ENT_FRAMES wide
Texture2D makeSmilerTex(bool glow);   // the Smiler: fog body, or its unlit eyes and grin; ENT_FRAMES wide
Texture2D makePartygoerTex();      // the thing that lives at the party
Texture2D makeScrawlTex();         // graffiti atlas: what earlier wanderers wrote
Texture2D makePropsTex();          // prop atlas: cardboard / cabinet / metal
Texture2D makeFixturesTex();       // fittings atlas: outlets, switch, grille, diffuser, exit sign, metal

// The fixtures atlas' layout (FIXTURES, FIX_*, the vending machine's door) is
// port/atlas.h's: the mesher reads it too.
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

// Every world surface, by slot. Levels share slots (the Red Halls reuse Level 1's
// slab; the pool ceiling is its floor tile), so a slot is generated once.
enum SurfSlot { SURF_CARPET, SURF_BOARDS, SURF_PAPER, SURF_SLAB, SURF_SOFFIT, SURF_CONCWALL,
                SURF_POOLFLOOR, SURF_POOLWALL, SURF_BRICK, SURF_BANQUET, SURF_PARTYCEIL,
                SURF_PARTYWALL, SURF_COUNT };
// texdump's file names, and the Unreal import's.
extern const char *const SURF_NAMES[SURF_COUNT];
Surface makeSurface(SurfSlot slot);
// What each level puts on its floors, ceilings and walls.
struct LevelSurfaces { SurfSlot floor, ceiling, walls; };
extern const LevelSurfaces LEVEL_SURFACES[NLEVELS];
// The tile sizes (FLOOR_TILE_M, WALL_TILE_M, wallTileV) are port/atlas.h's.
Texture2D makeAOStripTex();        // gradient strip for baked contact-shadow decals
Texture2D makeDogTex();            // THE RED HALLS: whatever the pack is, seen side-on
Texture2D makeAlmondWrapTex();     // almond water can, unwrapped: label strip + lid + base
Texture2D makeDeckTex();           // the tape player: top / body / front / reel, in one atlas

Texture2D makeParticleTex(); // soft procedural disc for smoke and muzzle flash

Texture2D makePropDetail(Texture2D albedo);
