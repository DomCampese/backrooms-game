#pragma once
// Procedural textures — surfaces are composed at startup, including embedded CC0 object tiles.
#include "raylib.h"

Texture2D makeWallpaperTex();      // Level 0: yellowed stripes, stains, baseboard
Texture2D makeCarpetTex();         // Level 0: moist mustard carpet
Texture2D makeCeilingTex();        // office ceiling tiles (Level 0 + LEVEL FUN)
// Sprite-sheet geometry for the two humanoids and for the pack. The billboard
// draws in render.cpp index these, so a frame count that disagrees with the
// generator shows up as a sliver of the neighbouring frame down one edge.
//   ENT_FRAMES: one full stride, NOT half of one mirrored — Clark has a real
//   leg and a peg leg, so the halves of his gait genuinely differ.
//   ENT_ROWS:   0 = head down the corridor, 1 = head come round onto you.
constexpr int ENT_FRAMES = 6;
constexpr int ENT_ROWS   = 2;
constexpr int DOG_FRAMES = 4;

Texture2D makeEntityTex();         // PIRATE CLARK billboard sprite sheet
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
