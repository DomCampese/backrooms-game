#pragma once
// Procedural textures — surfaces are composed at startup, including embedded CC0 object tiles.
#include "raylib.h"

Texture2D makeWallpaperTex();      // Level 0: yellowed stripes, stains, baseboard
Texture2D makeCarpetTex();         // Level 0: moist mustard carpet
Texture2D makeCeilingTex();        // office ceiling tiles (Level 0 + LEVEL FUN)
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
Texture2D makeConcreteWallTex();   // Level 1
Texture2D makeConcreteFloorTex();  // Level 1 (+ Red Halls)
Texture2D makeConcreteCeilTex();   // Level 1 (+ Red Halls)
Texture2D makeRedBrickTex();       // Red Halls
Texture2D makeTileTex();           // Poolrooms: white ceramic everywhere
Texture2D makePartyWallTex();      // LEVEL FUN =): bunting, confetti, smileys
Texture2D makePartyCarpetTex();    // LEVEL FUN =): deep red party carpet
Texture2D makePartyCeilTex();      // LEVEL FUN =): near-black party-hall ceiling
Texture2D makeAOStripTex();        // gradient strip for baked contact-shadow decals
Texture2D makeDogTex();            // THE RED HALLS: whatever the pack is, seen side-on
Texture2D makeAlmondWrapTex();     // almond water can, unwrapped: label strip + lid + base
Texture2D makeDeckTex();           // the tape player: top / body / front / reel, in one atlas

// Packed tangent slopes (RG), gloss mask (B). Generated once, mipmapped, no assets.
Texture2D makeSurfaceDetail(Texture2D albedo, bool ceramic, float strength);

Texture2D makeParticleTex(); // soft procedural disc for smoke and muzzle flash

Texture2D makePropDetail(Texture2D albedo);
