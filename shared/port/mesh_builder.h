#pragma once
// Mesh builder shared by the chunk mesher and the object meshes: accumulate
// textured quads and triangles as plain arrays, which a renderer uploads (the
// Web build: web/src/mesh_upload.h; Unreal: a procedural mesh section).
#include "../core/vec.h"
#include <cstdint>
#include <functional>
#include <vector>

// A vertex colour, 0..255 a channel, laid out as raylib's Color. The alpha is
// the world shader's material code (AGENTS.md, "The shader's alpha coding").
struct Rgba { uint8_t r, g, b, a; };
// Not WHITE: raylib defines that as a macro, and the Web build's files see both.
constexpr Rgba RGBA_WHITE = { 255, 255, 255, 255 };

struct MB {
    std::vector<float> v, uv, n;
    std::vector<unsigned char> c;
    std::vector<unsigned short> idx;
    // Optional colour field over the floor plan, multiplied into every vertex
    // this builder emits (alpha untouched, so the shader's alpha coding holds). Per
    // vertex, so a tint grades across a room instead of stepping per cell.
    std::function<Rgba(float, float)> tint;
    void quad(Vec3 a, Vec3 b, Vec3 cc, Vec3 d, Vec3 nn,
              Vec2 ta, Vec2 tb, Vec2 tc, Vec2 td, Rgba col) {
        unsigned short base = (unsigned short)(v.size() / 3);
        const Vec3 P[4] = { a, b, cc, d };
        const Vec2 T[4] = { ta, tb, tc, td };
        for (int i = 0; i < 4; i++) {
            v.push_back(P[i].x); v.push_back(P[i].y); v.push_back(P[i].z);
            uv.push_back(T[i].x); uv.push_back(T[i].y);
            n.push_back(nn.x); n.push_back(nn.y); n.push_back(nn.z);
            Rgba k = col;
            if (tint) {
                Rgba t = tint(P[i].x, P[i].z);
                k = { (unsigned char)(col.r * t.r / 255), (unsigned char)(col.g * t.g / 255),
                      (unsigned char)(col.b * t.b / 255), col.a };
            }
            c.push_back(k.r); c.push_back(k.g); c.push_back(k.b); c.push_back(k.a);
        }
        const unsigned short q[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; i++) idx.push_back(base + q[i]);
    }
    void tri(Vec3 a, Vec3 b, Vec3 cc, Vec3 nn,
             Vec2 ta, Vec2 tb, Vec2 tc, Rgba col) {
        unsigned short base = (unsigned short)(v.size() / 3);
        const Vec3 P[3] = { a, b, cc };
        const Vec2 T[3] = { ta, tb, tc };
        for (int i = 0; i < 3; i++) {
            v.push_back(P[i].x); v.push_back(P[i].y); v.push_back(P[i].z);
            uv.push_back(T[i].x); uv.push_back(T[i].y);
            n.push_back(nn.x); n.push_back(nn.y); n.push_back(nn.z);
            c.push_back(col.r); c.push_back(col.g); c.push_back(col.b); c.push_back(col.a);
        }
        for (int i = 0; i < 3; i++) idx.push_back(base + (unsigned short)i);
    }
};

// Where the atlases keep plain opaque material: the props and fixtures atlases
// both keep a metal swatch at this UV, and flat-coloured geometry samples it.
// Move the swatch and every box drawn from it disappears.
constexpr Vec2 PLAIN_UV = { 0.375f, 0.75f };

// A box turned by `yaw` about (cx, cz): four sides from one UV region, the top
// from another. A bevel chamfers the vertical edges and the top rim.
void addPropBox(MB &mb, float cx, float cz, float yaw, float hx, float hz, float y0, float y1,
                float u0, float v0, float u1, float v1,
                float tu0, float tv0, float tu1, float tv1, Rgba tint = RGBA_WHITE, float bevel = 0);
// An axis-aligned box in a flat colour; every face samples PLAIN_UV.
void addSolidBox(MB &mb, float x0, float y0, float z0, float x1, float y1, float z1, Rgba t);
