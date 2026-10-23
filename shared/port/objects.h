#pragma once
// The loose and held objects, each built at life size with its base on y = 0.
// Both builds draw these: the Web build uploads each as a raylib mesh
// (web/src/object_meshes.h), Unreal as a procedural mesh section.
#include "mesh_builder.h"

// One almond water can. UVs index makeAlmondWrapTex.
MB canMesh();
// The tape player, and the two reels and record lamp that go on it. Separate
// meshes because the reels turn and the lamp only burns while the tape is
// running. UVs index makeDeckTex.
MB deckMesh();
MB reelMesh();
MB deckLampMesh();
// The flare in hand. UVs are PLAIN_UV in the props atlas.
MB flareMesh();
// A Level 1 supply crate and its lid (see Game::crateAt). UVs index makePropsTex.
MB crateMesh();
MB crateLidMesh();
