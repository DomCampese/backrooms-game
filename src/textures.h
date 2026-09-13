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
constexpr int ENT_FRAMES = 6;      // Clark and the partygoer, 128 px per frame
constexpr int DOG_FRAMES = 4;      // the pack, 192 px per frame

Texture2D makeEntityTex();         // PIRATE CLARK billboard sprite, ENT_FRAMES wide
Texture2D makePartygoerTex();      // the thing that lives at the party
Texture2D makeScrawlTex();         // graffiti atlas: what earlier wanderers wrote
Texture2D makePropsTex();          // prop atlas: cardboard / cabinet / metal
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
