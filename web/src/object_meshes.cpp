#include "object_meshes.h"
#include "mesh_upload.h"
#include "port/mesh_builder.h"
#include "util.h"
#include <cmath>

// One almond water can, base on y = 0. UVs index makeAlmondWrapTex: the barrel
// takes the label strip once round, the caps the lid and base squares below it.
Mesh buildCanMesh() {
    MB b;
    const int N = 24;
    const float R = 0.033f, H = 0.122f;          // 66mm across, 122mm tall
    const float SV = 128.0f / 192.0f;            // the label strip ends here in v
    // alpha 254: opaque but below the shader's relief threshold; the relief is
    // fixed in world space and a moving object would swim through it
    const Rgba w = { 255, 255, 255, 254 };
    // the barrel as a stack of rings: base roll, straight wall, shoulder
    const float ry[4] = { 0.0f,        H * 0.035f, H * 0.90f, H };
    const float rr[4] = { R * 0.90f,   R,          R,         R * 0.86f };
    const float rv[4] = { SV,          SV * 0.96f, SV * 0.07f, 0.0f };
    for (int i = 0; i < N; i++) {
        float a0 = i * TAU / N, a1 = (i + 1) * TAU / N;
        // u runs backwards round the barrel, or the wordmark reads mirrored on the
        // face turned toward you
        float u0 = 1.0f - i / (float)N, u1 = 1.0f - (i + 1) / (float)N;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        for (int k = 0; k < 3; k++) {
            Vec3 p00 = { c0 * rr[k],     ry[k],     s0 * rr[k] };
            Vec3 p10 = { c1 * rr[k],     ry[k],     s1 * rr[k] };
            Vec3 p11 = { c1 * rr[k + 1], ry[k + 1], s1 * rr[k + 1] };
            Vec3 p01 = { c0 * rr[k + 1], ry[k + 1], s0 * rr[k + 1] };
            // Smooth geometric normals, including the shoulder slope.
            float ny=(rr[k]-rr[k+1])/(ry[k+1]-ry[k]);
            float inv=1/sqrtf(1+ny*ny);
            b.quad(p00,p10,p11,p01,{c0*inv,ny*inv,s0*inv},
                   {u0,rv[k]},{u1,rv[k]},{u1,rv[k+1]},{u0,rv[k+1]},w);
            Vec3 normals[4]={{c0*inv,ny*inv,s0*inv},{c1*inv,ny*inv,s1*inv},
                                {c1*inv,ny*inv,s1*inv},{c0*inv,ny*inv,s0*inv}};
            size_t start=b.n.size()-12;
            for(int j=0;j<4;++j) {b.n[start+j*3]=normals[j].x;b.n[start+j*3+1]=normals[j].y;b.n[start+j*3+2]=normals[j].z;}

        }
    }
    // caps. UVs inset so bilinear filtering cannot reach the next square.
    const float IN = 1.5f / 192.0f;
    auto capUV = [&](float ox, float ang) {
        float u = 0.5f + 0.5f * cosf(ang) * 0.94f, vv = 0.5f + 0.5f * sinf(ang) * 0.94f;
        return Vec2{ ox + IN + u * (64.0f / 192.0f - 2 * IN),
                        SV + IN + vv * (64.0f / 192.0f - 2 * IN) };
    };
    for (int i = 0; i < N; i++) {
        float a0 = i * TAU / N, a1 = (i + 1) * TAU / N;
        // lid, facing up: the first square in the atlas
        b.tri({ 0, H, 0 }, { cosf(a1) * rr[3], H, sinf(a1) * rr[3] },
              { cosf(a0) * rr[3], H, sinf(a0) * rr[3] }, { 0, 1, 0 },
              Vec2{32.0f/192.0f,160.0f/192.0f}, capUV(0.0f, a1), capUV(0.0f, a0), w);
        // base, facing down: wound the other way, and the second square
        b.tri({ 0, 0, 0 }, { cosf(a0) * rr[0], 0, sinf(a0) * rr[0] },
              { cosf(a1) * rr[0], 0, sinf(a1) * rr[0] }, { 0, -1, 0 },
              Vec2{96.0f/192.0f,160.0f/192.0f}, capUV(64.0f / 192.0f, a0),
              capUV(64.0f / 192.0f, a1), w);
    }
    return uploadMesh(b);
}


// The tape player, underside on y = 0. UVs index makeDeckTex's four tiles.
// Alpha 254, as the can.
Mesh buildDeckMesh() {
    MB b;
    const float HX = 0.059f, HZ = 0.038f, HY = 0.029f;   // 118 × 76 × 29 mm
    const Rgba w = { 255, 255, 255, 254 };
    // tile helpers: (0,0) top, (1,0) body, (0,1) front, (1,1) reel
    auto tile = [](int tx, int ty, float u, float v) {
        const float S = 0.5f, IN = 1.0f / 128.0f;        // inset: bilinear must not cross tiles
        return Vec2{ tx * S + IN + u * (S - 2 * IN), ty * S + IN + v * (S - 2 * IN) };
    };
    auto face = [&](Vec3 a, Vec3 b2, Vec3 c, Vec3 d, Vec3 nn, int tx, int ty) {
        b.quad(a, b2, c, d, nn, tile(tx, ty, 0, 1), tile(tx, ty, 1, 1),
               tile(tx, ty, 1, 0), tile(tx, ty, 0, 0), w);
    };

    // front face (+z) carries the grille and the buttons; the rest is body
    face({ -HX, 0, HZ }, { HX, 0, HZ }, { HX, HY * 2, HZ }, { -HX, HY * 2, HZ }, { 0, 0, 1 }, 0, 1);
    face({ HX, 0, -HZ }, { -HX, 0, -HZ }, { -HX, HY * 2, -HZ }, { HX, HY * 2, -HZ }, { 0, 0, -1 }, 1, 0);
    face({ -HX, 0, -HZ }, { -HX, 0, HZ }, { -HX, HY * 2, HZ }, { -HX, HY * 2, -HZ }, { -1, 0, 0 }, 1, 0);
    face({ HX, 0, HZ }, { HX, 0, -HZ }, { HX, HY * 2, -HZ }, { HX, HY * 2, HZ }, { 1, 0, 0 }, 1, 0);
    face({ -HX, 0, -HZ }, { HX, 0, -HZ }, { HX, 0, HZ }, { -HX, 0, HZ }, { 0, -1, 0 }, 1, 0);

    // The top is the cassette bay: four border strips, then walls down to a
    // recessed floor the reels sit on.
    const float TY = HY * 2, BY = TY - 0.008f;            // bay floor, 8 mm down
    const float BX = HX * 0.62f, BZ = HZ * 0.42f;         // the opening
    // border strips, UV'd from the same tile so the label and frame line up
    auto topStrip = [&](float x0, float x1, float z0, float z1) {
        auto uv = [&](float x, float z) {
            // u runs backwards, or the printed label reads mirrored
            return tile(0, 0, 1.0f - (x + HX) / (2 * HX), (z + HZ) / (2 * HZ));
        };
        b.quad({ x0, TY, z0 }, { x1, TY, z0 }, { x1, TY, z1 }, { x0, TY, z1 }, { 0, 1, 0 },
               uv(x0, z0), uv(x1, z0), uv(x1, z1), uv(x0, z1), w);
    };
    topStrip(-HX, HX, -HZ, -BZ);
    topStrip(-HX, HX, BZ, HZ);
    topStrip(-HX, -BX, -BZ, BZ);
    topStrip(BX, HX, -BZ, BZ);
    // the bay: four inner walls and a floor, all off the dark middle of the tile
    const Vec2 dk = tile(0, 0, 0.5f, 0.5f);            // solidly inside the window
    auto flat = [&](Vec3 a, Vec3 b2, Vec3 c, Vec3 d, Vec3 nn) {
        b.quad(a, b2, c, d, nn, dk, dk, dk, dk, w);
    };
    flat({ -BX, BY, -BZ }, { BX, BY, -BZ }, { BX, BY, BZ }, { -BX, BY, BZ }, { 0, 1, 0 });
    flat({ -BX, BY, -BZ }, { -BX, TY, -BZ }, { BX, TY, -BZ }, { BX, BY, -BZ }, { 0, 0, 1 });
    flat({ BX, BY, BZ }, { BX, TY, BZ }, { -BX, TY, BZ }, { -BX, BY, BZ }, { 0, 0, -1 });
    flat({ -BX, BY, BZ }, { -BX, TY, BZ }, { -BX, TY, -BZ }, { -BX, BY, -BZ }, { 1, 0, 0 });
    flat({ BX, BY, -BZ }, { BX, TY, -BZ }, { BX, TY, BZ }, { BX, BY, BZ }, { -1, 0, 0 });
    return uploadMesh(b);
}

// One reel: a flat disc in the XZ plane about its centre, drawn twice and
// spun by the deck.
Mesh buildReelMesh() {
    MB b;
    const int N = 16;
    const float R = 0.0145f;
    const Rgba w = { 255, 255, 255, 254 };
    auto uv = [](float ang, float rad) {
        const float S = 0.5f, IN = 1.0f / 128.0f;
        float u = 0.5f + 0.5f * cosf(ang) * rad, v = 0.5f + 0.5f * sinf(ang) * rad;
        return Vec2{ S + IN + u * (S - 2 * IN), S + IN + v * (S - 2 * IN) };
    };
    for (int i = 0; i < N; i++) {
        float a0 = i * TAU / N, a1 = (i + 1) * TAU / N;
        b.tri({ 0, 0, 0 }, { cosf(a1) * R, 0, sinf(a1) * R }, { cosf(a0) * R, 0, sinf(a0) * R },
              { 0, 1, 0 }, uv(0, 0), uv(a1, 0.97f), uv(a0, 0.97f), w);
    }
    return uploadMesh(b);
}

// The record lamp. Alpha 51 is the shader's raw emissive branch: it burns its
// own colour, and shows in a blackout.
Mesh buildDeckLampMesh() {
    MB b;
    const Rgba glow = { 255, 66, 48, 51 };
    // On the lid in front of the bay and down the front edge, so it reads from
    // above and side-on. Oversized so it still covers a pixel at 4 m. 1.5 mm proud
    // of the shell, or it z-fights and vanishes beyond a couple of metres. The UV
    // only has to be legal: the emissive branch ignores the texture.
    const float x0 = 0.024f, x1 = 0.044f, y = 0.0595f, z0 = 0.019f, z1 = 0.031f;
    const Vec2 t = { 0.25f, 0.25f };
    b.quad({ x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 }, { 0, 1, 0 }, t, t, t, t, glow);
    b.quad({ x0, 0.044f, 0.0395f }, { x1, 0.044f, 0.0395f },
           { x1, y, 0.0395f }, { x0, y, 0.0395f }, { 0, 0, 1 }, t, t, t, t, glow);
    return uploadMesh(b);
}

// An axial tube with round sides. Ring end faces leave a bore, so a muzzle is
// a recess.
static void weaponTube(MB &b, float y, float z0, float z1, float radius,
                       float bore, Rgba metal, int sides = 24) {
    Vec2 uv = PLAIN_UV;
    for (int i = 0; i < sides; ++i) {
        float a = TAU * i / sides, c = TAU * (i + 1) / sides;
        Vec3 n0{cosf(a), sinf(a), 0}, n1{cosf(c), sinf(c), 0};
        Vec3 p0{radius*n0.x, y+radius*n0.y, z0}, p1{radius*n1.x, y+radius*n1.y, z0};
        Vec3 p2{p1.x, p1.y, z1}, p3{p0.x, p0.y, z1};
        Vec3 normal{cosf((a+c)*0.5f), sinf((a+c)*0.5f), 0};
        b.quad(p0,p1,p2,p3,normal,uv,uv,uv,uv,metal);
        // Interpolated radial normals keep a low-poly cylinder looking round.
        const Vec3 radial[4] = {n0,n1,n1,n0};
        size_t start = b.n.size()-12;
        for (int j=0;j<4;++j) {
            b.n[start+j*3]=radial[j].x; b.n[start+j*3+1]=radial[j].y; b.n[start+j*3+2]=0;
        }
        Vec3 q0{bore*n0.x,y+bore*n0.y,z1}, q1{bore*n1.x,y+bore*n1.y,z1};
        b.quad(p3,p2,q1,q0,{0,0,1},uv,uv,uv,uv,metal);
        Rgba inside{12,12,13,254};
        b.quad(q0,q1,{q1.x,q1.y,z0},{q0.x,q0.y,z0},
               {-normal.x,-normal.y,0},uv,uv,uv,uv,inside);
        b.tri({0,y,z0},p1,p0,{0,0,-1},uv,uv,uv,metal);
    }
}

// The flare: a tube with its cap and printed bands.
Mesh buildFlareMesh() {
    MB b;
    weaponTube(b,0,-0.075f,0.105f,0.016f,0,{177,43,26,254},16);
    weaponTube(b,0,0.072f,0.094f,0.017f,0,{216,204,173,254},16);
    weaponTube(b,0,0.105f,0.125f,0.0165f,0,{55,34,24,254},16);
    // printed safety bands
    weaponTube(b,0,-0.059f,-0.044f,0.0164f,0,{209,191,148,254},16);
    return uploadMesh(b);
}

// ---- a Level 1 supply crate: not in any chunk mesh. Game places crates
// (Game::crateAt) and draws this mesh at each, lid separate. Veneer planks, dark
// battens and a pale label, alpha 255 so the wood takes its relief.
static void crateBody(MB &mb) {
    const float WU0 = 0.51f, WV0 = 0.02f, WU1 = 0.99f, WV1 = 0.48f;
    // the shipping label on a carton's top in the props atlas (makePropsTex)
    const float CU0 = 150 / 1024.0f, CV0 = 332 / 512.0f, CU1 = 236 / 1024.0f, CV1 = 392 / 512.0f;
    const float H = 0.56f, R = 0.35f;
    Rgba plank = { 255, 226, 180, 255 }, batten = { 176, 138, 96, 255 };
    // the box as horizontal planks, each a little different in tone
    for (int i = 0; i < 4; i++) {
        float y0 = i * H / 4 + 0.004f, y1 = (i + 1) * H / 4 - 0.004f;
        float k = 0.88f + 0.06f * ((i * 7) % 3);
        Rgba t = { (unsigned char)(plank.r * k), (unsigned char)(plank.g * k), (unsigned char)(plank.b * k), 255 };
        addPropBox(mb, 0, 0, 0, R, R, y0, y1, WU0, WV0 + 0.1f * i, WU1, WV0 + 0.1f * i + 0.08f,
                   WU0, WV0, WU1, WV1, t, 0.0f);
    }
    addPropBox(mb, 0, 0, 0, R - 0.01f, R - 0.01f, 0, H, WU0, WV0, WU1, WV1, WU0, WV0, WU1, WV1,
               Rgba{ 60, 44, 30, 255 }, 0.0f);                              // what shows in the gaps
    for (int cx = -1; cx <= 1; cx += 2) for (int cz = -1; cz <= 1; cz += 2)   // corner battens
        addPropBox(mb, cx * (R - 0.02f), cz * (R - 0.02f), 0, 0.035f, 0.035f, 0, H + 0.004f,
                   WU0, WV0, WU0 + 0.1f, WV1, WU0, WV0, WU1, WV1, batten, 0.0f);
    for (int f = 0; f < 4; f++) {                                            // a diagonal brace per side
        float a = f * 1.5707963f;
        addPropBox(mb, cosf(a) * (R + 0.012f), sinf(a) * (R + 0.012f), a + 1.5707963f, R - 0.05f, 0.012f,
                   H * 0.44f, H * 0.56f, WU0, WV0, WU1, WV0 + 0.05f, WU0, WV0, WU1, WV1, batten, 0.0f);
    }
    // a shipping label on one face, just off it
    Rgba lab = { 226, 214, 184, 254 };
    mb.quad({ -0.14f, 0.16f, R + 0.016f }, { 0.14f, 0.16f, R + 0.016f }, { 0.14f, 0.34f, R + 0.016f },
            { -0.14f, 0.34f, R + 0.016f }, { 0, 0, 1 }, { CU0, CV1 }, { CU1, CV1 }, { CU1, CV0 }, { CU0, CV0 }, lab);
}
Mesh buildCrateMesh() { MB mb; crateBody(mb); return uploadMesh(mb); }
Mesh buildCrateLidMesh() {
    MB mb;
    const float WU0 = 0.51f, WV0 = 0.02f, WU1 = 0.99f, WV1 = 0.48f;
    addPropBox(mb, 0, 0, 0, 0.365f, 0.365f, 0, 0.04f, WU0, WV0, WU1, WV1, WU0, WV0, WU1, WV1,
               Rgba{ 246, 214, 170, 255 }, 0.0f);
    for (int i = -1; i <= 1; i += 2)
        addPropBox(mb, 0, i * 0.25f, 0, 0.365f, 0.035f, 0.04f, 0.06f, WU0, WV0, WU1, WV1, WU0, WV0, WU1, WV1,
                   Rgba{ 176, 138, 96, 255 }, 0.0f);
    return uploadMesh(mb);
}
