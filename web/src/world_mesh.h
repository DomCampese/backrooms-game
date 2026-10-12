#pragma once
// The chunk mesher's output (port/chunk_mesh.h) as raylib meshes, kept for as
// long as core keeps the chunk they were baked from.
#include "raylib.h"
#include "core/world.h"
#include "port/chunk_mesh.h"   // ChunkMesh, the slots
#include <cstdint>
#include <unordered_map>

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
