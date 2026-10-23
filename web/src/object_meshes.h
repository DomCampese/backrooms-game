#pragma once
// The loose and held objects (port/objects.h) as raylib meshes.
#include "raylib.h"

// One almond water can. UVs index makeAlmondWrapTex.
Mesh buildCanMesh();
// The tape player, and the two reels and record lamp that go on it. Separate
// meshes because the reels turn and the lamp only burns while the tape is
// running. UVs index makeDeckTex.
Mesh buildDeckMesh();
Mesh buildReelMesh();
Mesh buildDeckLampMesh();
Mesh buildFlareMesh();
// A Level 1 supply crate and its lid (see Game::crateAt).
Mesh buildCrateMesh();
Mesh buildCrateLidMesh();
