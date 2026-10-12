#pragma once
// The chunk mesher: turns core's floorplan and layout (core/layout.h) into
// textured triangles, one builder per material slot. Core decides what stands
// where; this file decides how it looks. Both builds draw from it: the Web
// build uploads each part as a raylib mesh (web/src/world_mesh.h), Unreal as a
// procedural mesh section.
#include "../core/world.h"
#include "mesh_builder.h"

// Slots in ChunkGeometry::parts. Each is built separately because each needs a
// different material or a different draw order (see Game::renderScene).
enum ChunkMesh {
    MESH_FLOOR = 0,
    MESH_CEILING,
    MESH_WALLS,
    MESH_PROPS,
    MESH_WATER,
    MESH_SCRAWL,      // graffiti decals, pressed just off the wall faces
    MESH_FIXTURES,    // outlets, grilles, diffusers, signs, conduit and sprinklers
    MESH_GLASS,       // window panes
    MESH_AO,          // baked contact-shadow gradients in every crease
    MESH_COUNT,
};

// One chunk's geometry, in its own storey's frame (y = 0 is that storey's
// floor); another storey's chunk is drawn translated by a pitch. Positions are
// metres, y up; UVs are in texture repeats of each slot's atlas or surface.
struct ChunkGeometry { MB parts[MESH_COUNT]; };

// Build chunk (cx, cz) of storey w.qs, generating it and its neighbours as needed.
void bakeChunkGeometry(World &w, int cx, int cz, ChunkGeometry &out);
