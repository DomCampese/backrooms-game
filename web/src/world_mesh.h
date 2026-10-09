#pragma once
// The chunk mesher: turns core's floorplan and layout (core/layout.h) into
// raylib meshes, and keeps them for as long as core keeps the chunk they were
// baked from. Core decides what stands where; this file decides how it looks.
#include "raylib.h"
#include "core/world.h"
#include <cstdint>
#include <unordered_map>

// Slots in ChunkMeshes::meshes. Each is baked separately because each needs a
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

// One chunk's baked geometry, in its own storey's frame (y = 0 is that
// storey's floor); another storey's chunk is drawn translated by a pitch.
struct ChunkMeshes { Mesh meshes[MESH_COUNT] = {}; };

// Bake chunk (cx, cz) of storey w.qs, generating it and its neighbours as needed.
void bakeChunk(World &w, int cx, int cz, ChunkMeshes &out);

// Baked chunks, keyed by storey and chunk. Every call first drains
// World::staleChunks and unloads what it names, so a chunk's meshes go when
// core unloads or rebuilds the chunk. Meshes are not unloaded on destruction:
// the cache can outlive the GL context.
class ChunkMeshCache {
public:
    // Bake chunk (cx, cz) of storey w.qs unless it is baked. True when it baked.
    bool ensure(World &w, int cx, int cz);
    // The meshes of chunk (cx, cz) on storey s, or null when it is not baked.
    const ChunkMeshes *find(World &w, int s, int cx, int cz);
    // The entry for a chunk, created empty: for tests that place hand-built meshes.
    ChunkMeshes &slot(int s, int cx, int cz);

private:
    void drain(World &w);
    std::unordered_map<int, std::unordered_map<uint64_t, ChunkMeshes>> baked;
};
