#pragma once
// Mesh builder shared by the chunk mesher and the object meshes: accumulate
// textured quads and triangles, then upload them as one raylib Mesh.
#include "raylib.h"
#include <cstring>
#include <functional>
#include <vector>

struct MB {
    std::vector<float> v, uv, n;
    std::vector<unsigned char> c;
    std::vector<unsigned short> idx;
    // Optional colour field over the floor plan, multiplied into every vertex
    // this builder emits (alpha untouched, so the shader's alpha coding holds). Per
    // vertex, so a tint grades across a room instead of stepping per cell.
    std::function<Color(float, float)> tint;
    void quad(Vector3 a, Vector3 b, Vector3 cc, Vector3 d, Vector3 nn,
              Vector2 ta, Vector2 tb, Vector2 tc, Vector2 td, Color col) {
        unsigned short base = (unsigned short)(v.size() / 3);
        const Vector3 P[4] = { a, b, cc, d };
        const Vector2 T[4] = { ta, tb, tc, td };
        for (int i = 0; i < 4; i++) {
            v.push_back(P[i].x); v.push_back(P[i].y); v.push_back(P[i].z);
            uv.push_back(T[i].x); uv.push_back(T[i].y);
            n.push_back(nn.x); n.push_back(nn.y); n.push_back(nn.z);
            Color k = col;
            if (tint) {
                Color t = tint(P[i].x, P[i].z);
                k = { (unsigned char)(col.r * t.r / 255), (unsigned char)(col.g * t.g / 255),
                      (unsigned char)(col.b * t.b / 255), col.a };
            }
            c.push_back(k.r); c.push_back(k.g); c.push_back(k.b); c.push_back(k.a);
        }
        const unsigned short q[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; i++) idx.push_back(base + q[i]);
    }
    void tri(Vector3 a, Vector3 b, Vector3 cc, Vector3 nn,
             Vector2 ta, Vector2 tb, Vector2 tc, Color col) {
        unsigned short base = (unsigned short)(v.size() / 3);
        const Vector3 P[3] = { a, b, cc };
        const Vector2 T[3] = { ta, tb, tc };
        for (int i = 0; i < 3; i++) {
            v.push_back(P[i].x); v.push_back(P[i].y); v.push_back(P[i].z);
            uv.push_back(T[i].x); uv.push_back(T[i].y);
            n.push_back(nn.x); n.push_back(nn.y); n.push_back(nn.z);
            c.push_back(col.r); c.push_back(col.g); c.push_back(col.b); c.push_back(col.a);
        }
        for (int i = 0; i < 3; i++) idx.push_back(base + (unsigned short)i);
    }
    Mesh bake() {
        Mesh m = {};
        if (idx.empty()) return m;
        m.vertexCount = (int)(v.size() / 3);
        m.triangleCount = (int)(idx.size() / 3);
        m.vertices = (float *)MemAlloc((unsigned)(v.size() * sizeof(float)));
        m.texcoords = (float *)MemAlloc((unsigned)(uv.size() * sizeof(float)));
        m.normals = (float *)MemAlloc((unsigned)(n.size() * sizeof(float)));
        m.colors = (unsigned char *)MemAlloc((unsigned)c.size());
        m.indices = (unsigned short *)MemAlloc((unsigned)(idx.size() * sizeof(unsigned short)));
        memcpy(m.vertices, v.data(), v.size() * sizeof(float));
        memcpy(m.texcoords, uv.data(), uv.size() * sizeof(float));
        memcpy(m.normals, n.data(), n.size() * sizeof(float));
        memcpy(m.colors, c.data(), c.size());
        memcpy(m.indices, idx.data(), idx.size() * sizeof(unsigned short));
        UploadMesh(&m, false);
        return m;
    }
};

// Where the atlases keep plain opaque material: the props and fixtures atlases
// both keep a metal swatch at this UV, and flat-coloured geometry samples it.
// Move the swatch and every box drawn from it disappears.
constexpr Vector2 PLAIN_UV = { 0.375f, 0.75f };

// A box turned by `yaw` about (cx, cz): four sides from one UV region, the top
// from another. A bevel chamfers the vertical edges and the top rim.
void addPropBox(MB &mb, float cx, float cz, float yaw, float hx, float hz, float y0, float y1,
                float u0, float v0, float u1, float v1,
                float tu0, float tv0, float tu1, float tv1, Color tint = WHITE, float bevel = 0);
// An axis-aligned box in a flat colour; every face samples PLAIN_UV.
void addSolidBox(MB &mb, float x0, float y0, float z0, float x1, float y1, float z1, Color t);
