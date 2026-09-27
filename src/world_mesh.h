#pragma once
// The chunk mesher: turns core's floorplan into raylib meshes, and keeps them
// for as long as core keeps the chunk they were baked from.
#include "raylib.h"
#include "core/world.h"
#include <cstdint>
#include <unordered_map>

// How rare a phrase on a wall is, and how many there are to find. The rate is
// per solid wall edge and applies to both orientations, so a corridor of ten
// cells offers about twenty chances. SCRAWL_PHRASES must match the atlas built
// by makeScrawlTex (4 columns x 8 rows) — change one and change the other.
// How often the building's fittings turn up. These are rarities in the same
// sense the scrawl is, except an outlet is meant to be ordinary: canon names
// "scattered electrical outlets" and the eye needs something of known size to
// measure a corridor against, so they are common and everything else is not.
// All are per solid wall edge, both orientations.
constexpr uint32_t OUTLET_RATE   = 5;
constexpr uint32_t OUTLET_BROKEN = 4;    // one outlet in this many has lost its cover
constexpr uint32_t SWITCH_RATE   = 23;
constexpr uint32_t GRILLE_RATE   = 27;
constexpr uint32_t EXITSIGN_RATE = 97;
constexpr uint32_t SPRINK_RATE   = 11;   // per ceiling cell
constexpr uint32_t DIFFUSER_RATE = 13;   // per ceiling cell
constexpr uint32_t CONDUIT_RUN   = 6;    // cells per conduit run, so runs are runs

constexpr uint32_t SCRAWL_RATE = 40;
constexpr uint32_t SCRAWL_PHRASES = 32;

// Slots in ChunkMeshes::meshes. Each is baked separately because each needs a
// different material or a different draw order (see Game::renderScene).
enum ChunkMesh {
    MESH_FLOOR = 0,
    MESH_CEILING,
    MESH_WALLS,
    MESH_PROPS,
    MESH_WATER,
    MESH_SCRAWL,      // graffiti decals, pressed just off the wall faces
    MESH_FIXTURES,    // outlets, grilles, diffusers, signs — and the conduit/sprinkler bodies
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
