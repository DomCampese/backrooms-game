#pragma once
// A port mesh builder's arrays as a raylib mesh on the GPU.
#include "raylib.h"
#include "port/mesh_builder.h"
#include <cstring>

// An empty builder gives an empty Mesh (vertexCount 0), which nothing draws.
inline Mesh uploadMesh(const MB &mb) {
    Mesh m = {};
    if (mb.idx.empty()) return m;
    m.vertexCount = (int)(mb.v.size() / 3);
    m.triangleCount = (int)(mb.idx.size() / 3);
    m.vertices = (float *)MemAlloc((unsigned)(mb.v.size() * sizeof(float)));
    m.texcoords = (float *)MemAlloc((unsigned)(mb.uv.size() * sizeof(float)));
    m.normals = (float *)MemAlloc((unsigned)(mb.n.size() * sizeof(float)));
    m.colors = (unsigned char *)MemAlloc((unsigned)mb.c.size());
    m.indices = (unsigned short *)MemAlloc((unsigned)(mb.idx.size() * sizeof(unsigned short)));
    memcpy(m.vertices, mb.v.data(), mb.v.size() * sizeof(float));
    memcpy(m.texcoords, mb.uv.data(), mb.uv.size() * sizeof(float));
    memcpy(m.normals, mb.n.data(), mb.n.size() * sizeof(float));
    memcpy(m.colors, mb.c.data(), mb.c.size());
    memcpy(m.indices, mb.idx.data(), mb.idx.size() * sizeof(unsigned short));
    UploadMesh(&m, false);
    return m;
}
