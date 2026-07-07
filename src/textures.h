#pragma once
// Procedural textures — every surface in the game is synthesized at startup.
#include "raylib.h"

Texture2D makeWallpaperTex();      // Level 0: yellowed stripes, stains, baseboard
Texture2D makeCarpetTex();         // Level 0: moist mustard carpet
Texture2D makeCeilingTex();        // office ceiling tiles (Level 0 + LEVEL FUN)
Texture2D makeEntityTex();         // PIRATE CLARK billboard sprite
Texture2D makePartygoerTex();      // the thing that lives at the party
Texture2D makeScrawlTex();         // graffiti atlas: what earlier wanderers wrote
Texture2D makePropsTex();          // prop atlas: cardboard / cabinet / metal
Texture2D makeConcreteWallTex();   // Level 1
Texture2D makeConcreteFloorTex();  // Level 1 (+ Red Halls)
Texture2D makeConcreteCeilTex();   // Level 1 (+ Red Halls)
Texture2D makeRedBrickTex();       // Red Halls
Texture2D makeTileTex();           // Poolrooms: white ceramic everywhere
Texture2D makePartyWallTex();      // LEVEL FUN =): bunting, confetti, smileys
Texture2D makePartyCarpetTex();    // LEVEL FUN =): confetti trodden into carpet
