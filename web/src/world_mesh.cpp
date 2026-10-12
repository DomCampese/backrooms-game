#include "world_mesh.h"
#include "mesh_upload.h"

void bakeChunk(World &w, int cx, int cz, ChunkMeshes &out) {
    ChunkGeometry g;
    bakeChunkGeometry(w, cx, cz, g);
    for (int i = 0; i < MESH_COUNT; i++) out.meshes[i] = uploadMesh(g.parts[i]);
}

static void unloadMeshes(ChunkMeshes &c) {
    for (Mesh &m : c.meshes)
        if (m.vertexCount > 0) UnloadMesh(m);
}

void ChunkMeshCache::drain(World &w) {
    for (const ChunkRef &r : w.staleChunks) {
        auto s = baked.find(r.storey);
        if (s == baked.end()) continue;
        auto it = s->second.find(World::key(r.cx, r.cz));
        if (it == s->second.end()) continue;
        unloadMeshes(it->second);
        s->second.erase(it);
    }
    w.staleChunks.clear();
}

bool ChunkMeshCache::ensure(World &w, int cx, int cz) {
    drain(w);
    w.data(cx, cz);
    auto &m = baked[w.qs];
    uint64_t k = World::key(cx, cz);
    if (m.count(k)) return false;
    bakeChunk(w, cx, cz, m[k]);
    return true;
}

const ChunkMeshes *ChunkMeshCache::find(World &w, int s, int cx, int cz) {
    drain(w);
    auto st = baked.find(s);
    if (st == baked.end()) return nullptr;
    auto it = st->second.find(World::key(cx, cz));
    return it == st->second.end() ? nullptr : &it->second;
}

ChunkMeshes &ChunkMeshCache::slot(int s, int cx, int cz) { return baked[s][World::key(cx, cz)]; }
