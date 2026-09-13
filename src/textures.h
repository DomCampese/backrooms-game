#pragma once
// Procedural textures — surfaces are composed at startup, including embedded CC0 object tiles.
#include "raylib.h"

Texture2D makeWallpaperTex();      // Level 0: yellowed stripes, stains, baseboard
Texture2D makeCarpetTex();         // Level 0: moist mustard carpet
Texture2D makeCeilingTex();        // office ceiling tiles (Level 0 + LEVEL FUN)
Texture2D makeEntityTex();         // PIRATE CLARK billboard sprite
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
enum FixtureId { FIX_OUTLET = 0, FIX_OUTLET_BROKEN, FIX_SWITCH, FIX_GRILLE, FIX_DIFFUSER, FIX_SIGN, FIX_COUNT };
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
