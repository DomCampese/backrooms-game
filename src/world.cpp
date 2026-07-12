#include "world.h"
#include "util.h"
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

// mesh builder: accumulate textured quads, bake to a raylib Mesh
struct MB {
    std::vector<float> v, uv, n;
    std::vector<unsigned char> c;
    std::vector<unsigned short> idx;
    void quad(Vector3 a, Vector3 b, Vector3 cc, Vector3 d, Vector3 nn,
              Vector2 ta, Vector2 tb, Vector2 tc, Vector2 td, Color col) {
        unsigned short base = (unsigned short)(v.size() / 3);
        const Vector3 P[4] = { a, b, cc, d };
        const Vector2 T[4] = { ta, tb, tc, td };
        for (int i = 0; i < 4; i++) {
            v.push_back(P[i].x); v.push_back(P[i].y); v.push_back(P[i].z);
            uv.push_back(T[i].x); uv.push_back(T[i].y);
            n.push_back(nn.x); n.push_back(nn.y); n.push_back(nn.z);
            c.push_back(col.r); c.push_back(col.g); c.push_back(col.b); c.push_back(col.a);
        }
        const unsigned short q[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; i++) idx.push_back(base + q[i]);
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

ChunkData &World::data(int cx, int cz) {
    uint64_t k = key(cx, cz);
    auto it = chunks.find(k);
    if (it != chunks.end()) return it->second;
    ChunkData &d = chunks[k];
    generate(d, cx, cz);
    return d;
}

void World::generate(ChunkData &d, int cx, int cz) {
    memset(d.wallN, 0, sizeof(d.wallN));
    memset(d.wallW, 0, sizeof(d.wallW));
    memset(d.pillar, 0, sizeof(d.pillar));
    memset(d.pool, 0, sizeof(d.pool));
    memset(d.elev, 0, sizeof(d.elev));
    uint64_t k = key(cx, cz);
    Rng rng(hash64(k ^ ((uint64_t)seed + (uint64_t)level * 0x51ED270Bu) * 0x9E3779B97F4A7C15ULL));
    bool openChunk = (hash64(k ^ 0xA11CEULL ^ (uint64_t)seed) & 7) == 0;   // occasional open plaza
    int nseg = level == 0 ? 12 + rng.ri(0, 5) : level == 1 ? 7 + rng.ri(0, 4)
             : level == 4 ? 9 + rng.ri(0, 4) : 5 + rng.ri(0, 3);
    if (openChunk) nseg = 2 + rng.ri(0, 2);
    int lenBase = level == 0 ? 5 : 6;
    for (int s = 0; s < nseg; s++) {
        bool horiz = rng.next() & 1;
        int len = lenBase + rng.ri(0, 8);
        int a = rng.ri(0, CCELLS - 1), b = rng.ri(0, CCELLS - 1);
        int end = std::min(CCELLS - 1, a + len);
        int doorAt = (rng.f01() < (level == 0 ? 0.72f : 0.8f)) ? a + 1 + rng.ri(0, std::max(0, end - a - 2)) : -1;
        for (int i = a; i <= end; i++) {
            if (i == doorAt) continue;
            // rarely a window instead of blank wall; behind it, nothing
            uint8_t v = ((level == 0 || level == 3) && rng.f01() < 0.035f) ? 3 : 1;
            if (horiz) d.wallN[i][b] = v; else d.wallW[b][i] = v;
        }
    }
    int np = level == 0 ? 4 + rng.ri(0, 5) : level == 1 ? 10 + rng.ri(0, 8) : 2 + rng.ri(0, 3);
    for (int i = 0; i < np; i++) d.pillar[rng.ri(0, CCELLS - 1)][rng.ri(0, CCELLS - 1)] = 1;
    memset(d.prop, 0, sizeof(d.prop));
    memset(d.propRot, 0, sizeof(d.propRot));
    int npr = level == 0 ? 7 + rng.ri(0, 7) : level == 1 ? 8 + rng.ri(0, 8)
            : level == 3 ? 5 + rng.ri(0, 6) : level == 4 ? 9 + rng.ri(0, 7) : 0;
    for (int i = 0; i < npr; i++) {
        int a = rng.ri(0, CCELLS - 1), b = rng.ri(0, CCELLS - 1);
        if (d.pillar[a][b] || d.prop[a][b]) continue;
        float f = rng.f01();
        if (level == 1) d.prop[a][b] = f < 0.55f ? 1 : f < 0.78f ? 2 : f < 0.96f ? 3 : 10;  // warehouse
        else if (level == 3)                                               // red halls: someone's bedroom
            d.prop[a][b] = f < 0.30f ? 9 : f < 0.55f ? 6 : f < 0.80f ? 8 : 7;
        else if (level == 4)                                               // level fun: the party never ended
            d.prop[a][b] = f < 0.52f ? 11 : f < 0.70f ? 1 : f < 0.82f ? 5 : f < 0.93f ? 3 : 10;
        else   // L0: office clutter, plus furniture that has no business here
            d.prop[a][b] = f < 0.26f ? 1 : f < 0.40f ? 2 : f < 0.50f ? 3 : f < 0.62f ? 4 :
                           f < 0.74f ? 5 : f < 0.84f ? 6 : f < 0.90f ? 7 : f < 0.95f ? 8 :
                           f < 0.985f ? 9 : 10;
        d.propRot[a][b] = (uint8_t)rng.ri(0, 3);
        // boxes like company: sometimes a neighbouring stack
        if (d.prop[a][b] == 1 && a + 1 < CCELLS && rng.f01() < 0.4f && !d.pillar[a + 1][b] && !d.prop[a + 1][b]) {
            d.prop[a + 1][b] = 1; d.propRot[a + 1][b] = (uint8_t)rng.ri(0, 3);
        }
    }
    if (level == 2) {   // sunken pools where noise blobs say so, never under walls
        for (int i = 1; i < CCELLS - 1; i++) for (int kk = 1; kk < CCELLS - 1; kk++) {
            if (d.pillar[i][kk]) continue;
            if (d.wallN[i][kk] || d.wallW[i][kk] || d.wallN[i][kk + 1] || d.wallW[i + 1][kk]) continue;
            float gxc = (float)(cx * CCELLS + i), gzc = (float)(cz * CCELLS + kk);
            if (fbm2(gxc * 0.11f, gzc * 0.11f, seed ^ 0x77AAu, 3) > 0.565f) d.pool[i][kk] = 1;
        }
    }
    if (level == 0 || level == 1) {   // sunken lounges (L0) / loading docks (L1), never under walls
        for (int i = 1; i < CCELLS - 1; i++) for (int kk = 1; kk < CCELLS - 1; kk++) {
            if (d.pillar[i][kk]) continue;
            if (d.wallN[i][kk] || d.wallW[i][kk] || d.wallN[i][kk + 1] || d.wallW[i + 1][kk]) continue;
            float gxc = (float)(cx * CCELLS + i), gzc = (float)(cz * CCELLS + kk);
            if (fbm2(gxc * 0.09f, gzc * 0.09f, seed ^ (level == 0 ? 0x51ABu : 0xD0CCu), 3) > 0.60f)
                d.elev[i][kk] = level == 0 ? -5 : 6;
        }
    }
    // rare exit door carved into an existing wall run
    if (hash64(k ^ 0xE717ULL ^ (uint64_t)seed) % (exitTest ? 1 : 16) == 0) {
        bool placed = false;
        for (int i = 1; i < CCELLS - 1 && !placed; i++)
            for (int kk = 0; kk < CCELLS && !placed; kk++)
                if (d.wallN[i][kk] == 1 && d.wallN[i - 1][kk] == 1 && d.wallN[i + 1][kk] == 1) {
                    d.wallN[i][kk] = 2; placed = true;
                }
        for (int i = 0; i < CCELLS && !placed; i++)
            for (int kk = 1; kk < CCELLS - 1 && !placed; kk++)
                if (d.wallW[i][kk] == 1 && d.wallW[i][kk - 1] == 1 && d.wallW[i][kk + 1] == 1) {
                    d.wallW[i][kk] = 2; placed = true;
                }
    }
    if (cx == 0 && cz == 0) {   // clear spawn room
        for (int i = 5; i <= 10; i++) for (int kk = 5; kk <= 10; kk++) {
            d.wallN[i][kk] = d.wallW[i][kk] = d.pillar[i][kk] = d.prop[i][kk] = d.pool[i][kk] = 0;
            d.elev[i][kk] = 0;
        }
        if (exitTest) { d.wallN[6][11] = 1; d.wallN[7][11] = 2; d.wallN[8][11] = 1; }
    }
}

uint8_t World::wallNVal(int ci, int ck) {
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    return data(cx, cz).wallN[ci - cx * CCELLS][ck - cz * CCELLS];
}
uint8_t World::wallWVal(int ci, int ck) {
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    return data(cx, cz).wallW[ci - cx * CCELLS][ck - cz * CCELLS];
}
bool World::pillarAt(int ci, int ck) {
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    return data(cx, cz).pillar[ci - cx * CCELLS][ck - cz * CCELLS] != 0;
}
uint8_t World::propAt(int ci, int ck) {
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    return data(cx, cz).prop[ci - cx * CCELLS][ck - cz * CCELLS];
}
bool World::poolAt(int ci, int ck) {
    if (level != 2) return false;
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    return data(cx, cz).pool[ci - cx * CCELLS][ck - cz * CCELLS] != 0;
}
float World::floorY(int ci, int ck) {
    if (poolAt(ci, ck)) return -0.6f;
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    return data(cx, cz).elev[ci - cx * CCELLS][ck - cz * CCELLS] * 0.1f;
}

bool World::softAt(int ci, int ck) {
    if (level != 0) return false;
    if (abs(ci) <= 10 && abs(ck) <= 10) return false;   // never near where you wake up
    if (ih(ci, ck, (uint32_t)seed ^ 0x50F7u) % 523 != 0) return false;
    return !pillarAt(ci, ck) && propAt(ci, ck) == 0 && floorY(ci, ck) == 0.0f;
}

bool World::cursedExit(int ci, int ck) {
    return ih(ci, ck, (uint32_t)seed ^ 0xC0DEu) % 6 == 0;
}

// rotated prop box: 4 sides + top, one UV region for sides, another for the top
static void addPropBox(MB &mb, float cx, float cz, float yaw, float hx, float hz, float y0, float y1,
                       float u0, float v0, float u1, float v1,
                       float tu0, float tv0, float tu1, float tv1, Color tint = WHITE) {
    float ca = cosf(yaw), sa = sinf(yaw);
    auto pt = [&](float lx, float lz) { return Vector3{ cx + lx * ca - lz * sa, 0, cz + lx * sa + lz * ca }; };
    Vector3 corners[5] = { pt(-hx, -hz), pt(hx, -hz), pt(hx, hz), pt(-hx, hz), pt(-hx, -hz) };
    for (int f = 0; f < 4; f++) {
        Vector3 a = corners[f], b = corners[f + 1];
        Vector3 n = { b.z - a.z, 0, -(b.x - a.x) };
        float nl = sqrtf(n.x * n.x + n.z * n.z);
        n.x /= nl; n.z /= nl;
        float mx = (a.x + b.x) * 0.5f - cx, mz = (a.z + b.z) * 0.5f - cz;
        if (n.x * mx + n.z * mz < 0) { n.x = -n.x; n.z = -n.z; }
        mb.quad({a.x,y0,a.z},{b.x,y0,b.z},{b.x,y1,b.z},{a.x,y1,a.z}, n,
                {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tint);
    }
    mb.quad({corners[0].x,y1,corners[0].z},{corners[1].x,y1,corners[1].z},
            {corners[2].x,y1,corners[2].z},{corners[3].x,y1,corners[3].z},{0,1,0},
            {tu0,tv0},{tu1,tv0},{tu1,tv1},{tu0,tv1}, tint);
}

static void addBoxSides(MB &mb, float x0, float y0, float z0, float x1, float y1, float z1, bool bottomFace = false) {
    Color w = WHITE;
    float va = 1 - y0 / 3, vb = 1 - y1 / 3;
    mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{0,0,-1},{x0/3,va},{x1/3,va},{x1/3,vb},{x0/3,vb},w);
    mb.quad({x1,y0,z1},{x0,y0,z1},{x0,y1,z1},{x1,y1,z1},{0,0,1},{x1/3,va},{x0/3,va},{x0/3,vb},{x1/3,vb},w);
    mb.quad({x0,y0,z1},{x0,y0,z0},{x0,y1,z0},{x0,y1,z1},{-1,0,0},{z1/3,va},{z0/3,va},{z0/3,vb},{z1/3,vb},w);
    mb.quad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0},{1,0,0},{z0/3,va},{z1/3,va},{z1/3,vb},{z0/3,vb},w);
    if (bottomFace)
        mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},{0,-1,0},{x0/3,z0/3},{x1/3,z0/3},{x1/3,z1/3},{x0/3,z1/3},w);
}

void World::ensureMesh(int cx, int cz) {
    ChunkData &d = data(cx, cz);
    if (d.built) return;
    MB fl, ce, wa, pr, wt, scr, gl;
    float wx = cx * CHUNK, wz = cz * CHUNK;
    Color wcol = WHITE;
    if (level == 2) {   // per-cell floor: pool basins sit 0.6m down, with tiled skirts
        Color water = { 115, 190, 217, 128 };
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            float gx = wx + i * CELL, gz = wz + kk * CELL;
            float fy = d.pool[i][kk] ? -0.6f : 0.0f;
            fl.quad({gx,fy,gz},{gx+CELL,fy,gz},{gx+CELL,fy,gz+CELL},{gx,fy,gz+CELL},{0,1,0},
                    {gx/2,gz/2},{(gx+CELL)/2,gz/2},{(gx+CELL)/2,(gz+CELL)/2},{gx/2,(gz+CELL)/2},wcol);
            if (!d.pool[i][kk]) continue;
            auto dry = [&](int a, int b) { return a < 0 || a >= CCELLS || b < 0 || b >= CCELLS || !d.pool[a][b]; };
            if (dry(i, kk-1)) fl.quad({gx,0,gz},{gx+CELL,0,gz},{gx+CELL,-0.6f,gz},{gx,-0.6f,gz},{0,0,1},
                    {gx/2,0},{(gx+CELL)/2,0},{(gx+CELL)/2,0.3f},{gx/2,0.3f},wcol);
            if (dry(i, kk+1)) fl.quad({gx,0,gz+CELL},{gx+CELL,0,gz+CELL},{gx+CELL,-0.6f,gz+CELL},{gx,-0.6f,gz+CELL},{0,0,-1},
                    {gx/2,0},{(gx+CELL)/2,0},{(gx+CELL)/2,0.3f},{gx/2,0.3f},wcol);
            if (dry(i-1, kk)) fl.quad({gx,0,gz},{gx,0,gz+CELL},{gx,-0.6f,gz+CELL},{gx,-0.6f,gz},{1,0,0},
                    {gz/2,0},{(gz+CELL)/2,0},{(gz+CELL)/2,0.3f},{gz/2,0.3f},wcol);
            if (dry(i+1, kk)) fl.quad({gx+CELL,0,gz},{gx+CELL,0,gz+CELL},{gx+CELL,-0.6f,gz+CELL},{gx+CELL,-0.6f,gz},{-1,0,0},
                    {gz/2,0},{(gz+CELL)/2,0},{(gz+CELL)/2,0.3f},{gz/2,0.3f},wcol);
            // submerged step along dry edges — tiled bench to walk down into the water
            auto pstep = [&](float bx0, float bz0, float bx1, float bz1, float nx, float nz) {
                float mid = -0.3f, ox = nx * 0.32f, oz = nz * 0.32f;
                fl.quad({bx0,mid,bz0},{bx1,mid,bz1},{bx1+ox,mid,bz1+oz},{bx0+ox,mid,bz0+oz},{0,1,0},
                        {0,0},{1,0},{1,0.16f},{0,0.16f},wcol);
                fl.quad({bx0+ox,mid,bz0+oz},{bx1+ox,mid,bz1+oz},{bx1+ox,-0.6f,bz1+oz},{bx0+ox,-0.6f,bz0+oz},{nx,0,nz},
                        {0,0},{1,0},{1,0.15f},{0,0.15f},wcol);
            };
            if (dry(i, kk-1)) pstep(gx, gz, gx+CELL, gz, 0, 1);
            if (dry(i, kk+1)) pstep(gx, gz+CELL, gx+CELL, gz+CELL, 0, -1);
            if (dry(i-1, kk)) pstep(gx, gz, gx, gz+CELL, 1, 0);
            if (dry(i+1, kk)) pstep(gx+CELL, gz, gx+CELL, gz+CELL, -1, 0);
            wt.quad({gx,-0.12f,gz},{gx+CELL,-0.12f,gz},{gx+CELL,-0.12f,gz+CELL},{gx,-0.12f,gz+CELL},{0,1,0},
                    {0,0},{1,0},{1,1},{0,1}, water);
        }
    } else {
        // per-cell floor: sunken lounges (L0) and loading docks (L1) change height, with real steps
        auto stepEdge = [&](float bx0, float bz0, float bx1, float bz1, float hi, float lo, float nx, float nz) {
            fl.quad({bx0,hi,bz0},{bx1,hi,bz1},{bx1,lo,bz1},{bx0,lo,bz0},{nx,0,nz},
                    {(bx0+bz0)/2,0},{(bx1+bz1)/2,0},{(bx1+bz1)/2,(hi-lo)/2},{(bx0+bz0)/2,(hi-lo)/2},wcol);
            float mid = (hi + lo) * 0.5f, ox = nx * 0.35f, oz = nz * 0.35f;
            fl.quad({bx0,mid,bz0},{bx1,mid,bz1},{bx1+ox,mid,bz1+oz},{bx0+ox,mid,bz0+oz},{0,1,0},
                    {0,0},{1,0},{1,0.18f},{0,0.18f},wcol);
            fl.quad({bx0+ox,mid,bz0+oz},{bx1+ox,mid,bz1+oz},{bx1+ox,lo,bz1+oz},{bx0+ox,lo,bz0+oz},{nx,0,nz},
                    {0,0},{1,0},{1,(mid-lo)/2},{0,(mid-lo)/2},wcol);
            fl.quad({bx0,mid,bz0},{bx0+ox,mid,bz0+oz},{bx0+ox,lo,bz0+oz},{bx0,lo,bz0},{bz1-bz0,0,bx0-bx1},
                    {0,0},{0.18f,0},{0.18f,(mid-lo)/2},{0,(mid-lo)/2},wcol);   // end caps
            fl.quad({bx1,mid,bz1},{bx1+ox,mid,bz1+oz},{bx1+ox,lo,bz1+oz},{bx1,lo,bz1},{bz0-bz1,0,bx1-bx0},
                    {0,0},{0.18f,0},{0.18f,(mid-lo)/2},{0,(mid-lo)/2},wcol);
        };
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            float gx = wx + i * CELL, gz = wz + kk * CELL;
            float fy = d.elev[i][kk] * 0.1f;
            fl.quad({gx,fy,gz},{gx+CELL,fy,gz},{gx+CELL,fy,gz+CELL},{gx,fy,gz+CELL},{0,1,0},
                    {gx/2,gz/2},{(gx+CELL)/2,gz/2},{(gx+CELL)/2,(gz+CELL)/2},{gx/2,(gz+CELL)/2},wcol);
            if (level == 0 && softAt(cx * CCELLS + i, cz * CCELLS + kk)) {
                // the carpet has gone dark and soft here. don't linger.
                float mx3 = gx + 1.0f, mz3 = gz + 1.0f;
                fl.quad({mx3-0.85f,0.006f,mz3-0.85f},{mx3+0.85f,0.006f,mz3-0.85f},
                        {mx3+0.85f,0.006f,mz3+0.85f},{mx3-0.85f,0.006f,mz3+0.85f},{0,1,0},
                        {0.75f,0.75f},{0.75f,0.75f},{0.75f,0.75f},{0.75f,0.75f}, Color{ 16, 13, 10, 165 });
                fl.quad({mx3-0.5f,0.008f,mz3-0.5f},{mx3+0.5f,0.008f,mz3-0.5f},
                        {mx3+0.5f,0.008f,mz3+0.5f},{mx3-0.5f,0.008f,mz3+0.5f},{0,1,0},
                        {0.75f,0.75f},{0.75f,0.75f},{0.75f,0.75f},{0.75f,0.75f}, Color{ 10, 8, 6, 205 });
            }
            if (d.elev[i][kk] == 0) continue;
            auto hgt = [&](int a, int b) {
                return (a < 0 || a >= CCELLS || b < 0 || b >= CCELLS) ? 0.0f : d.elev[a][b] * 0.1f;
            };
            float hN = hgt(i, kk - 1), hS = hgt(i, kk + 1), hW = hgt(i - 1, kk), hE = hgt(i + 1, kk);
            // dock: steps drop into the lower neighbour; lounge: steps climb out into this cell
            if (hN < fy) stepEdge(gx, gz, gx + CELL, gz, fy, hN, 0, -1);
            else if (hN > fy) stepEdge(gx, gz, gx + CELL, gz, hN, fy, 0, 1);
            if (hS < fy) stepEdge(gx, gz + CELL, gx + CELL, gz + CELL, fy, hS, 0, 1);
            else if (hS > fy) stepEdge(gx, gz + CELL, gx + CELL, gz + CELL, hS, fy, 0, -1);
            if (hW < fy) stepEdge(gx, gz, gx, gz + CELL, fy, hW, -1, 0);
            else if (hW > fy) stepEdge(gx, gz, gx, gz + CELL, hW, fy, 1, 0);
            if (hE < fy) stepEdge(gx + CELL, gz, gx + CELL, gz + CELL, fy, hE, 1, 0);
            else if (hE > fy) stepEdge(gx + CELL, gz, gx + CELL, gz + CELL, hE, fy, -1, 0);
        }
    }
    ce.quad({wx,wallH,wz},{wx,wallH,wz+CHUNK},{wx+CHUNK,wallH,wz+CHUNK},{wx+CHUNK,wallH,wz},{0,-1,0},
            {wx/2,wz/2},{wx/2,(wz+CHUNK)/2},{(wx+CHUNK)/2,(wz+CHUNK)/2},{(wx+CHUNK)/2,wz/2},wcol);
    // light panels on the global grid (emissive: alpha=0); spacing varies per level
    Color panel = {255,255,255,0};
    float ls = level == 1 ? 12.0f : 8.0f;
    int g0x = (int)floorf(wx / ls), g1x = (int)floorf((wx + CHUNK) / ls);
    int g0z = (int)floorf(wz / ls), g1z = (int)floorf((wz + CHUNK) / ls);
    for (int gx = g0x; gx <= g1x; gx++)
        for (int gz = g0z; gz <= g1z; gz++) {
            float lx = gx * ls + ls * 0.5f, lz = gz * ls + ls * 0.5f, hp = 0.62f, yq = wallH - 0.02f;
            if (lx < wx || lx >= wx + CHUNK || lz < wz || lz >= wz + CHUNK) continue;
            ce.quad({lx-hp,yq,lz-hp},{lx-hp,yq,lz+hp},{lx+hp,yq,lz+hp},{lx+hp,yq,lz-hp},{0,-1,0},
                    {0,0},{0,1},{1,1},{1,0},panel);
        }
    ChunkData &dd = d;
    for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
        float gx = wx + i * CELL, gz = wz + kk * CELL;
        uint8_t nv = dd.wallN[i][kk];
        if (nv == 1) addBoxSides(wa, gx - WT, 0, gz - WT, gx + CELL + WT, wallH, gz + WT);
        else if (nv == 3) {   // window on x-running wall; behind the glass, nothing
            addBoxSides(wa, gx - WT, 0, gz - WT, gx + CELL + WT, 1.0f, gz + WT);
            addBoxSides(wa, gx - WT, 2.1f, gz - WT, gx + CELL + WT, wallH, gz + WT, true);
            addBoxSides(wa, gx - WT, 1.0f, gz - WT, gx + 0.45f, 2.1f, gz + WT);
            addBoxSides(wa, gx + 1.55f, 1.0f, gz - WT, gx + CELL + WT, 2.1f, gz + WT);
            wa.quad({gx-WT,1.0f,gz-WT},{gx+CELL+WT,1.0f,gz-WT},{gx+CELL+WT,1.0f,gz+WT},{gx-WT,1.0f,gz+WT},
                    {0,1,0},{0,0},{1,0},{1,0.1f},{0,0.1f}, WHITE);   // sill top
            // real glass now: translucent pane (alpha 100 -> glass branch), see the room beyond
            Color glass = { 20, 26, 32, 100 };
            gl.quad({gx+0.45f,1.0f,gz},{gx+1.55f,1.0f,gz},{gx+1.55f,2.1f,gz},{gx+0.45f,2.1f,gz},
                    {0,0,-1},{0,1},{1,1},{1,0},{0,0}, glass);
        }
        else if (nv == 2) {   // exit doorway on x-running wall
            addBoxSides(wa, gx - WT, 0, gz - WT, gx + 0.35f, wallH, gz + WT);
            addBoxSides(wa, gx + 1.65f, 0, gz - WT, gx + CELL + WT, wallH, gz + WT);
            addBoxSides(wa, gx + 0.35f, 2.3f, gz - WT, gx + 1.65f, wallH, gz + WT, true);
            // cursed exits glow red — they don't lead deeper, they lead to the Red Halls
            bool crs = cursedExit(cx * CCELLS + i, cz * CCELLS + kk);
            Color glow = crs ? Color{ 255, 60, 40, 70 } : Color{ 255, 248, 225, 70 };
            wa.quad({gx+0.35f,0,gz},{gx+1.65f,0,gz},{gx+1.65f,2.3f,gz},{gx+0.35f,2.3f,gz},{0,0,-1},
                    {0,1},{1,1},{1,0},{0,0},glow);
        }
        uint8_t wv = dd.wallW[i][kk];
        if (wv == 1) addBoxSides(wa, gx - WT, 0, gz - WT, gx + WT, wallH, gz + CELL + WT);
        else if (wv == 3) {   // window on z-running wall
            addBoxSides(wa, gx - WT, 0, gz - WT, gx + WT, 1.0f, gz + CELL + WT);
            addBoxSides(wa, gx - WT, 2.1f, gz - WT, gx + WT, wallH, gz + CELL + WT, true);
            addBoxSides(wa, gx - WT, 1.0f, gz - WT, gx + WT, 2.1f, gz + 0.45f);
            addBoxSides(wa, gx - WT, 1.0f, gz + 1.55f, gx + WT, 2.1f, gz + CELL + WT);
            wa.quad({gx-WT,1.0f,gz-WT},{gx+WT,1.0f,gz-WT},{gx+WT,1.0f,gz+CELL+WT},{gx-WT,1.0f,gz+CELL+WT},
                    {0,1,0},{0,0},{1,0},{1,0.1f},{0,0.1f}, WHITE);   // sill top
            Color glass = { 20, 26, 32, 100 };
            gl.quad({gx,1.0f,gz+0.45f},{gx,1.0f,gz+1.55f},{gx,2.1f,gz+1.55f},{gx,2.1f,gz+0.45f},
                    {1,0,0},{0,1},{1,1},{1,0},{0,0}, glass);
        }
        else if (wv == 2) {   // exit doorway on z-running wall
            addBoxSides(wa, gx - WT, 0, gz - WT, gx + WT, wallH, gz + 0.35f);
            addBoxSides(wa, gx - WT, 0, gz + 1.65f, gx + WT, wallH, gz + CELL + WT);
            addBoxSides(wa, gx - WT, 2.3f, gz + 0.35f, gx + WT, wallH, gz + 1.65f, true);
            bool crs = cursedExit(cx * CCELLS + i, cz * CCELLS + kk);
            Color glow = crs ? Color{ 255, 60, 40, 70 } : Color{ 255, 248, 225, 70 };
            wa.quad({gx,0,gz+0.35f},{gx,0,gz+1.65f},{gx,2.3f,gz+1.65f},{gx,2.3f,gz+0.35f},{1,0,0},
                    {0,1},{1,1},{1,0},{0,0},glow);
        }
        // wall scrawl: rarely, a solid wall carries a phrase left by an earlier
        // wanderer. one of eight, from the 2x4 scrawl atlas, drawn as a decal
        // pressed just off the wall face (level 2 is pristine tile — no scrawl)
        if (level != 2) {
            uint32_t gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            auto uvOf = [](int ph, float &u0, float &v0, float &u1, float &v1) {
                u0 = (ph & 1) * 0.5f; v0 = (ph >> 1) * 0.25f; u1 = u0 + 0.5f; v1 = v0 + 0.25f;
            };
            if (nv == 1) {
                uint32_t hs = ih(gi, gk, seed ^ 0x5C1Bu);
                if (hs % 7 == 0) {
                    float u0, v0, u1, v1; uvOf((hs >> 5) % 8, u0, v0, u1, v1);
                    float y0 = 0.95f + ((hs >> 9) & 3) * 0.12f, y1 = y0 + 0.66f;
                    float x0 = gx + 0.28f, x1 = gx + 1.72f;
                    float zf = (hs & 8) ? gz + WT + 0.006f : gz - WT - 0.006f;
                    if (hs & 8) scr.quad({x0,y0,zf},{x1,y0,zf},{x1,y1,zf},{x0,y1,zf},{0,0,1},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, WHITE);
                    else        scr.quad({x1,y0,zf},{x0,y0,zf},{x0,y1,zf},{x1,y1,zf},{0,0,-1},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, WHITE);
                }
            }
            if (wv == 1) {
                uint32_t hs = ih(gi, gk, seed ^ 0x5C2Du);
                if (hs % 7 == 0) {
                    float u0, v0, u1, v1; uvOf((hs >> 5) % 8, u0, v0, u1, v1);
                    float y0 = 0.95f + ((hs >> 9) & 3) * 0.12f, y1 = y0 + 0.66f;
                    float z0 = gz + 0.28f, z1 = gz + 1.72f;
                    float xf = (hs & 8) ? gx + WT + 0.006f : gx - WT - 0.006f;
                    if (hs & 8) scr.quad({xf,y0,z1},{xf,y0,z0},{xf,y1,z0},{xf,y1,z1},{1,0,0},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, WHITE);
                    else        scr.quad({xf,y0,z0},{xf,y0,z1},{xf,y1,z1},{xf,y1,z0},{-1,0,0},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, WHITE);
                }
            }
        }
        if (dd.pillar[i][kk]) {
            addBoxSides(wa, gx + 0.42f, 0, gz + 0.42f, gx + 1.58f, wallH, gz + 1.58f);
            wa.quad({gx+0.30f,0.004f,gz+0.30f},{gx+1.70f,0.004f,gz+0.30f},   // contact shadow
                    {gx+1.70f,0.004f,gz+1.70f},{gx+0.30f,0.004f,gz+1.70f},
                    {0,1,0},{0,0},{0.04f,0},{0.04f,0.04f},{0,0.04f}, Color{ 12, 12, 12, 160 });
        }
        if (dd.prop[i][kk]) {
            float pcx = gx + 1.0f, pcz = gz + 1.0f;
            float rot = dd.propRot[i][kk] * 1.5708f;
            float ey = dd.elev[i][kk] * 0.1f;   // furniture sits on the local floor
            uint32_t h = ih(cx * CCELLS + i, cz * CCELLS + kk, seed ^ 0xB0B5u);
            float r1 = (h & 0xFF) / 255.0f, r2 = ((h >> 8) & 0xFF) / 255.0f, r3 = ((h >> 16) & 0xFF) / 255.0f;
            // UV regions of the prop atlas
            const float CU0=0.02f, CV0=0.02f, CU1=0.48f, CV1=0.98f;       // cardboard
            const float FU0=0.52f, FV0=0.02f, FU1=0.98f, FV1=0.48f;       // cabinet front
            const float MU0=0.52f, MV0=0.52f, MU1=0.98f, MV1=0.98f;       // plain metal
            float ca = cosf(rot), sa = sinf(rot);
            // rotated sub-box placed relative to the prop centre
            auto part = [&](float ox, float oz, float hx2, float hz2, float y0, float y1,
                            bool wood, Color tint) {
                addPropBox(pr, pcx + ox * ca - oz * sa, pcz + ox * sa + oz * ca, rot, hx2, hz2, y0, y1,
                           wood ? CU0 : MU0, wood ? CV0 : MV0, wood ? CU1 : MU1, wood ? CV1 : MV1,
                           wood ? CU0 : MU0, wood ? CV0 : MV0, wood ? CU1 : MU1, wood ? CV1 : MV1, tint);
            };
            auto blob = [&](float hx2, float hz2) {   // soft contact shadow under the piece
                auto ptv = [&](float lx, float lz) {
                    return Vector3{ pcx + lx * ca - lz * sa, ey + 0.006f, pcz + lx * sa + lz * ca };
                };
                pr.quad(ptv(-hx2, -hz2), ptv(hx2, -hz2), ptv(hx2, hz2), ptv(-hx2, hz2), {0,1,0},
                        {0.75f,0.75f},{0.75f,0.75f},{0.75f,0.75f},{0.75f,0.75f}, Color{ 12, 12, 12, 160 });
            };
            switch (dd.prop[i][kk]) {
            case 1: {   // box stack — on LEVEL FUN they're wrapped like presents,
                        // and the packing tape reads as ribbon
                blob(0.58f, 0.58f);
                float bh = 0.55f + r1 * 0.2f, bhx = 0.34f + r2 * 0.08f;
                auto wrap = [&](int rot2) {
                    if (level != 4) return WHITE;
                    Color c = PARTY[(h >> rot2) % 5];
                    return Color{ cl8(c.r * 0.9f + 46), cl8(c.g * 0.9f + 46), cl8(c.b * 0.9f + 46), 255 };
                };
                addPropBox(pr, pcx + (r3 - 0.5f) * 0.5f, pcz + (r1 - 0.5f) * 0.5f, rot + r2,
                           bhx, bhx, ey, ey + bh, CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1, wrap(5));
                if (r2 > 0.35f)   // second box on top, skewed
                    addPropBox(pr, pcx + (r3 - 0.5f) * 0.5f + 0.06f, pcz + (r1 - 0.5f) * 0.5f - 0.05f,
                               rot + r2 + 0.5f, bhx * 0.8f, bhx * 0.8f, ey + bh, ey + bh + 0.5f,
                               CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1, wrap(9));
                if (r1 > 0.6f)    // third box beside
                    addPropBox(pr, pcx + 0.62f, pcz + 0.3f, rot + r3 * 2, 0.27f, 0.27f, ey, ey + 0.5f,
                               CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1, wrap(13));
                break;
            }
            case 2:     // filing cabinet
                blob(0.34f, 0.42f);
                addPropBox(pr, pcx, pcz, rot, 0.26f, 0.34f, ey, ey + 1.32f,
                           FU0, FV0, FU1, FV1, MU0, MV0, MU1, MV1);
                break;
            case 3: {   // folding table
                blob(0.68f, 0.46f);
                float ty = 0.72f;
                addPropBox(pr, pcx, pcz, rot, 0.62f, 0.40f, ey + ty - 0.04f, ey + ty,
                           MU0, MV0, MU1, MV1, MU0, MV0, MU1, MV1);
                for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2) {
                    float ox = lx * 0.54f, oz = lz * 0.32f;
                    addPropBox(pr, pcx + ox * ca - oz * sa, pcz + ox * sa + oz * ca, rot,
                               0.03f, 0.03f, ey, ey + ty - 0.04f, MU0, MV0, MU1, MV1, MU0, MV0, MU1, MV1);
                }
                break;
            }
            case 4: {   // collapsed ceiling: dark hole above, tile leaning below, debris
                Color hole = { 12, 11, 9, 51 };
                ce.quad({pcx-0.85f,2.994f,pcz-0.85f},{pcx-0.85f,2.994f,pcz+0.85f},
                        {pcx+0.85f,2.994f,pcz+0.85f},{pcx+0.85f,2.994f,pcz-0.85f},{0,-1,0},
                        {0,0},{0,1},{1,1},{1,0}, hole);
                float bx0 = pcx - 0.58f * ca, bz0 = pcz - 0.58f * sa;   // base edge on floor
                float tx = pcx + 0.35f * ca, tz = pcz + 0.35f * sa;     // top edge, lifted
                Vector3 a = { bx0 - 0.58f * sa, ey + 0.02f, bz0 + 0.58f * ca };
                Vector3 b = { bx0 + 0.58f * sa, ey + 0.02f, bz0 - 0.58f * ca };
                Vector3 c2 = { tx + 0.58f * sa, ey + 0.42f, tz - 0.58f * ca };
                Vector3 dq = { tx - 0.58f * sa, ey + 0.42f, tz + 0.58f * ca };
                ce.quad(a, b, c2, dq, { -ca * 0.5f, 0.87f, -sa * 0.5f },
                        {0.05f,0.45f},{0.45f,0.45f},{0.45f,0.05f},{0.05f,0.05f}, WHITE);
                Color deb = { 110, 105, 95, 255 };
                ce.quad({pcx+0.4f,ey+0.012f,pcz+0.5f},{pcx+0.75f,ey+0.012f,pcz+0.55f},
                        {pcx+0.7f,ey+0.012f,pcz+0.85f},{pcx+0.38f,ey+0.012f,pcz+0.8f},{0,1,0},
                        {0.1f,0.1f},{0.3f,0.1f},{0.3f,0.3f},{0.1f,0.3f}, deb);
                ce.quad({pcx-0.7f,ey+0.012f,pcz-0.35f},{pcx-0.45f,ey+0.012f,pcz-0.42f},
                        {pcx-0.4f,ey+0.012f,pcz-0.2f},{pcx-0.68f,ey+0.012f,pcz-0.15f},{0,1,0},
                        {0.3f,0.3f},{0.45f,0.3f},{0.45f,0.45f},{0.3f,0.45f}, deb);
                break;
            }
            case 5: {   // couch: mustard upholstery gone grey, facing nothing in particular
                blob(0.86f, 0.56f);
                Color uph = { 172, 152, 96, 255 };
                part(0, 0.10f, 0.78f, 0.42f, ey + 0.16f, ey + 0.44f, false, uph);   // seat
                part(0, -0.36f, 0.78f, 0.14f, ey + 0.16f, ey + 0.92f, false, uph);  // backrest
                part(-0.64f, 0.06f, 0.14f, 0.46f, ey, ey + 0.62f, false, uph);      // arms
                part( 0.64f, 0.06f, 0.14f, 0.46f, ey, ey + 0.62f, false, uph);
                part(0, 0.10f, 0.74f, 0.38f, ey, ey + 0.16f, false, Color{ 120, 106, 70, 255 });
                break;
            }
            case 6:     // armoire: a wardrobe looming where no bedroom is
                blob(0.54f, 0.46f);
                part(0, 0, 0.44f, 0.36f, ey, ey + 1.78f, true, Color{ 118, 82, 58, 255 });
                part(0, 0, 0.48f, 0.40f, ey + 1.78f, ey + 1.90f, true, Color{ 92, 63, 44, 255 });  // cornice
                part(0, 0.37f, 0.015f, 0.015f, ey + 0.85f, ey + 1.0f, false, Color{ 190, 170, 110, 255 }); // handles
                break;
            case 7:     // floor lamp, shade askew, never lit
                blob(0.22f, 0.22f);
                part(0, 0, 0.14f, 0.14f, ey, ey + 0.05f, false, Color{ 66, 62, 60, 255 });
                part(0, 0, 0.025f, 0.025f, ey, ey + 1.34f, false, Color{ 66, 62, 60, 255 });
                addPropBox(pr, pcx + 0.05f, pcz, rot + 0.3f, 0.17f, 0.17f, ey + 1.30f, ey + 1.60f,
                           CU0, CV0, CU1, CV1, CU0, CV0, CU1, CV1, Color{ 214, 190, 142, 255 });
                break;
            case 8:     // nightstand, nowhere near a bed. usually.
                blob(0.36f, 0.36f);
                part(0, 0, 0.26f, 0.26f, ey, ey + 0.55f, true, Color{ 126, 90, 62, 255 });
                part(0, 0, 0.30f, 0.30f, ey + 0.55f, ey + 0.60f, true, Color{ 104, 74, 50, 255 });
                break;
            case 9: {   // bed: bare stained mattress, headboard against nothing
                blob(0.60f, 1.02f);
                Color wd = { 110, 78, 54, 255 };
                part(0, 0, 0.52f, 0.92f, ey + 0.12f, ey + 0.26f, true, wd);          // frame
                part(0, 0.04f, 0.48f, 0.86f, ey + 0.26f, ey + 0.46f, true, Color{ 216, 208, 188, 255 }); // mattress
                part(0, -0.97f, 0.52f, 0.05f, ey, ey + 0.95f, true, wd);             // headboard
                break;
            }
            case 11: {  // party table: paper cloth, a cake nobody cut, cups nobody drank
                blob(0.62f, 0.62f);
                float ty = 0.74f;
                uint32_t th = ih(cx * CCELLS + i, cz * CCELLS + kk, seed ^ 0xCAFEu);
                Color cloth = PARTY[th % 5];
                part(0, 0, 0.55f, 0.55f, ey + ty - 0.05f, ey + ty, false, cloth);
                for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2)
                    part(lx * 0.44f, lz * 0.44f, 0.035f, 0.035f, ey, ey + ty - 0.05f,
                         false, Color{ 120, 118, 112, 255 });
                part(0, 0, 0.17f, 0.17f, ey + ty, ey + ty + 0.16f, false, Color{ 238, 232, 220, 255 });   // cake
                part(0, 0, 0.11f, 0.11f, ey + ty + 0.16f, ey + ty + 0.26f, false, Color{ 232, 152, 172, 255 });
                part(0, 0, 0.013f, 0.013f, ey + ty + 0.26f, ey + ty + 0.37f, false, Color{ 240, 226, 172, 255 }); // candle
                {   // paper cups set out around the cake, in party colours
                    int ncup = 3 + (th % 4);
                    for (int c = 0; c < ncup; c++) {
                        uint32_t ch = th * 2654435761u + (uint32_t)c * 40503u;
                        float ang = (ch & 0xFFFF) / 65535.0f * 6.2831853f;
                        float rad = 0.30f + ((ch >> 16) & 0xFF) / 255.0f * 0.15f;
                        part(cosf(ang) * rad, sinf(ang) * rad, 0.04f, 0.04f,
                             ey + ty, ey + ty + 0.09f, false, PARTY[(ch >> 5) % 5]);
                    }
                }
                {   // ...and the candle is still lit. nobody lit it. two crossed
                    // emissive fins make a little flame that survives blackouts
                    auto fpt = [&](float lx, float ly2, float lz) {
                        return Vector3{ pcx + lx * ca - lz * sa, ly2, pcz + lx * sa + lz * ca };
                    };
                    Color flame = { 255, 196, 110, 70 };   // alpha <0.4: raw emissive in the shader
                    float fy0 = ey + ty + 0.37f, fy1 = fy0 + 0.055f;
                    pr.quad(fpt(-0.022f, fy0, 0), fpt(0.022f, fy0, 0), fpt(0.013f, fy1, 0), fpt(-0.013f, fy1, 0),
                            { sa, 0, -ca }, {0,1},{1,1},{1,0},{0,0}, flame);
                    pr.quad(fpt(0, fy0, -0.022f), fpt(0, fy0, 0.022f), fpt(0, fy1, 0.013f), fpt(0, fy1, -0.013f),
                            { ca, 0, sa }, {0,1},{1,1},{1,0},{0,0}, flame);
                }
                break;
            }
            case 10: {  // vending machine: still stocked, still humming, takes doubloons
                blob(0.52f, 0.44f);
                part(0, 0, 0.44f, 0.36f, ey, ey + 1.85f, false, Color{ 148, 152, 158, 255 });
                auto ptv2 = [&](float lx, float ly2, float lz) {
                    return Vector3{ pcx + lx * ca - lz * sa, ly2, pcz + lx * sa + lz * ca };
                };
                Color panel = { 66, 90, 122, 60 };   // emissive front: soft cold glow
                pr.quad(ptv2(-0.28f, ey + 0.55f, -0.375f), ptv2(0.28f, ey + 0.55f, -0.375f),
                        ptv2(0.28f, ey + 1.68f, -0.375f), ptv2(-0.28f, ey + 1.68f, -0.375f),
                        { sa, 0, -ca }, {0,1},{1,1},{1,0},{0,0}, panel);
                break;
            }
            }
        }
    }
    if (level == 4) {   // crepe streamers sag from the ceiling, in pairs of quads
        Rng srng(hash64(key(cx, cz) ^ 0xFE57AULL ^ (uint64_t)seed));
        int ns = 3 + srng.ri(0, 3);
        for (int s = 0; s < ns; s++) {
            float ax = wx + srng.f01() * CHUNK, az = wz + srng.f01() * CHUNK;
            float bx2 = ax + (srng.f01() - 0.5f) * 9, bz2 = az + (srng.f01() - 0.5f) * 9;
            float mx2 = (ax + bx2) * 0.5f, mz2 = (az + bz2) * 0.5f;
            float ytop = wallH - 0.03f, ymid = wallH - 0.55f - srng.f01() * 0.35f;
            Color sc = PARTY[srng.ri(0, 4)];
            float dx2 = bx2 - ax, dz2 = bz2 - az, dl = sqrtf(dx2 * dx2 + dz2 * dz2) + 1e-4f;
            Vector3 nrm = { dz2 / dl, 0, -dx2 / dl };
            Vector2 uvp = { 0.75f, 0.75f };   // plain-metal corner of the prop atlas: flat colour
            pr.quad({ ax, ytop, az }, { mx2, ymid + 0.06f, mz2 }, { mx2, ymid, mz2 }, { ax, ytop - 0.06f, az },
                    nrm, uvp, uvp, uvp, uvp, sc);
            pr.quad({ mx2, ymid + 0.06f, mz2 }, { bx2, ytop, bz2 }, { bx2, ytop - 0.06f, bz2 }, { mx2, ymid, mz2 },
                    nrm, uvp, uvp, uvp, uvp, sc);
        }
    }
    d.meshes[0] = fl.bake();
    d.meshes[1] = ce.bake();
    d.meshes[2] = wa.bake();
    d.meshes[3] = pr.bake();
    d.meshes[4] = wt.bake();
    d.meshes[5] = scr.bake();
    d.meshes[6] = gl.bake();
    d.built = true;
}

int World::gatherCellAABBs(int ci, int ck, AABB *out, int cap, int cnt, bool includeProps) {
    float x0 = ci * CELL, z0 = ck * CELL;
    uint8_t nv = wallNVal(ci, ck), wv = wallWVal(ci, ck);
    if (cnt < cap && (nv == 1 || nv == 3)) out[cnt++] = { x0 - WT, z0 - WT, x0 + CELL + WT, z0 + WT, wallH };
    if (cnt < cap && (wv == 1 || wv == 3)) out[cnt++] = { x0 - WT, z0 - WT, x0 + WT, z0 + CELL + WT, wallH };
    if (cnt < cap && pillarAt(ci, ck)) out[cnt++] = { x0 + 0.42f, z0 + 0.42f, x0 + 1.58f, z0 + 1.58f, wallH };
    if (includeProps && cnt < cap) {
        uint8_t pv = propAt(ci, ck);
        if (pv) {
            float ey = floorY(ci, ck);
            uint32_t h = ih(ci, ck, seed ^ 0xB0B5u);   // same hash the mesher uses
            float r1 = (h & 0xFF) / 255.0f, r2 = ((h >> 8) & 0xFF) / 255.0f;
            switch (pv) {
            case 1: {   // boxes: top of the tallest stacked one
                float t = 0.55f + r1 * 0.2f;
                if (r2 > 0.35f) t += 0.5f;
                out[cnt++] = { x0 + 0.35f, z0 + 0.35f, x0 + 1.65f, z0 + 1.65f, ey + t }; break;
            }
            case 2: out[cnt++] = { x0 + 0.60f, z0 + 0.60f, x0 + 1.40f, z0 + 1.40f, ey + 1.32f }; break;  // cabinet
            case 3: out[cnt++] = { x0 + 0.32f, z0 + 0.32f, x0 + 1.68f, z0 + 1.68f, ey + 0.72f }; break;  // table
            case 5: out[cnt++] = { x0 + 0.20f, z0 + 0.38f, x0 + 1.80f, z0 + 1.62f, ey + 0.44f }; break;  // couch
            case 6: out[cnt++] = { x0 + 0.55f, z0 + 0.62f, x0 + 1.45f, z0 + 1.38f, ey + 1.90f }; break;  // armoire
            case 7: out[cnt++] = { x0 + 0.82f, z0 + 0.82f, x0 + 1.18f, z0 + 1.18f, ey + 1.62f }; break;  // lamp
            case 8: out[cnt++] = { x0 + 0.66f, z0 + 0.66f, x0 + 1.34f, z0 + 1.34f, ey + 0.60f }; break;  // nightstand
            case 9: out[cnt++] = { x0 + 0.30f, z0 + 0.15f, x0 + 1.70f, z0 + 1.85f, ey + 0.46f }; break;  // bed
            case 10: out[cnt++] = { x0 + 0.50f, z0 + 0.58f, x0 + 1.50f, z0 + 1.42f, ey + 1.85f }; break; // vending
            case 11: out[cnt++] = { x0 + 0.40f, z0 + 0.40f, x0 + 1.60f, z0 + 1.60f, ey + 0.74f }; break; // party table
            }
        }
    }
    return cnt;
}

// feetY: obstacles whose top is at or below your feet are walkable, not solid
void World::collideCircle(float &px, float &pz, float r, float feetY) {
    AABB boxes[48];
    int cnt = 0;
    int ci = cellOf(px), ck = cellOf(pz);
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            cnt = gatherCellAABBs(ci + dx, ck + dz, boxes, 48, cnt);
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < cnt; i++) {
            const AABB &b = boxes[i];
            if (feetY >= b.top - 0.02f) continue;
            float cx = clampf(px, b.minx, b.maxx), cz = clampf(pz, b.minz, b.maxz);
            float dx = px - cx, dz = pz - cz, d2 = dx * dx + dz * dz;
            if (d2 >= r * r) continue;
            if (d2 < 1e-8f) {   // centre inside box: push out along smallest penetration
                float dl = px - b.minx, dr = b.maxx - px, dn = pz - b.minz, ds = b.maxz - pz;
                float m = std::min(std::min(dl, dr), std::min(dn, ds));
                if (m == dl) px = b.minx - r; else if (m == dr) px = b.maxx + r;
                else if (m == dn) pz = b.minz - r; else pz = b.maxz + r;
            } else {
                float d = sqrtf(d2), push = (r - d) / d;
                px += dx * push; pz += dz * push;
            }
        }
}

// floor height here, counting prop tops at or below your feet (so you can stand on furniture)
float World::groundAt(float x, float z, float feetY) {
    float g = floorY(cellOf(x), cellOf(z));
    AABB boxes[48];
    int cnt = 0;
    int ci = cellOf(x), ck = cellOf(z);
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            cnt = gatherCellAABBs(ci + dx, ck + dz, boxes, 48, cnt);
    for (int i = 0; i < cnt; i++)
        if (x > boxes[i].minx && x < boxes[i].maxx && z > boxes[i].minz && z < boxes[i].maxz &&
            boxes[i].top <= feetY + 0.05f && boxes[i].top > g) g = boxes[i].top;
    return g;
}

bool World::lineOfSight(float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az;
    float dist = sqrtf(dx * dx + dz * dz);
    int steps = (int)(dist / 0.22f) + 1;
    for (int s = 1; s < steps; s++) {
        float t = (float)s / steps, x = ax + dx * t, z = az + dz * t;
        AABB boxes[48];
        int cnt = 0, ci = cellOf(x), ck = cellOf(z);
        for (int ddx = -1; ddx <= 1; ddx++)
            for (int ddz = -1; ddz <= 1; ddz++)
                cnt = gatherCellAABBs(ci + ddx, ck + ddz, boxes, 48, cnt, false);  // props don't block sight
        for (int i = 0; i < cnt; i++)
            if (x > boxes[i].minx && x < boxes[i].maxx && z > boxes[i].minz && z < boxes[i].maxz) return false;
    }
    return true;
}

bool World::canStep(int ci, int ck, int ni, int nk) {
    if (pillarAt(ni, nk) || propAt(ni, nk) != 0) return false;   // furniture and pillars are solid
    // a wall (1) or window (3) on the shared edge blocks it; a doorway (2) is open
    if (nk == ck - 1)      { uint8_t v = wallNVal(ci, ck);     if (v == 1 || v == 3) return false; }
    else if (nk == ck + 1) { uint8_t v = wallNVal(ci, ck + 1); if (v == 1 || v == 3) return false; }
    else if (ni == ci - 1) { uint8_t v = wallWVal(ci, ck);     if (v == 1 || v == 3) return false; }
    else if (ni == ci + 1) { uint8_t v = wallWVal(ci + 1, ck); if (v == 1 || v == 3) return false; }
    return true;
}

bool World::pathStep(int si, int sk, int ti, int tk, int &outI, int &outK) {
    const int R = 16, W = 2 * R + 1;
    auto idx = [&](int ci, int ck) -> int {
        int lx = ci - si + R, lz = ck - sk + R;
        return (lx < 0 || lx >= W || lz < 0 || lz >= W) ? -1 : lz * W + lx;
    };
    std::vector<int> prev(W * W, -2);   // -2 unvisited, -1 = start, else predecessor local index
    std::vector<int> q; q.reserve(256);
    int s = idx(si, sk);
    prev[s] = -1; q.push_back(s);
    int found = -1;
    const int dirs[4][2] = { {0,-1}, {0,1}, {-1,0}, {1,0} };
    for (size_t head = 0; head < q.size(); head++) {
        int cur = q[head], clx = cur % W, clz = cur / W;
        int ci = si + clx - R, ck = sk + clz - R;
        if (ci == ti && ck == tk) { found = cur; break; }
        for (auto &d : dirs) {
            int ni = ci + d[0], nk = ck + d[1], nl = idx(ni, nk);
            if (nl < 0 || prev[nl] != -2 || !canStep(ci, ck, ni, nk)) continue;
            prev[nl] = cur; q.push_back(nl);
        }
    }
    if (found < 0) return false;
    int cur = found;                          // walk back to the first cell after the start
    while (prev[cur] != s && prev[cur] != -1) cur = prev[cur];
    outI = si + (cur % W) - R; outK = sk + (cur / W) - R;
    return true;
}

Vector2 World::findOpenSpot(float x, float z) {
    int ci0 = cellOf(x), ck0 = cellOf(z);
    for (int r = 0; r < 14; r++)
        for (int dx = -r; dx <= r; dx++)
            for (int dz = -r; dz <= r; dz++) {
                if (std::max(abs(dx), abs(dz)) != r) continue;
                if (!pillarAt(ci0 + dx, ck0 + dz) && propAt(ci0 + dx, ck0 + dz) == 0 && !poolAt(ci0 + dx, ck0 + dz))
                    return { (ci0 + dx) * CELL + 1.0f, (ck0 + dz) * CELL + 1.0f };
            }
    return { x, z };
}

void World::unloadFar(int pcx, int pcz, int radius) {
    for (auto it = chunks.begin(); it != chunks.end();) {
        int cx = (int)(int32_t)(it->first >> 32), cz = (int)(int32_t)(it->first & 0xFFFFFFFF);
        if (abs(cx - pcx) > radius || abs(cz - pcz) > radius) {
            if (it->second.built)
                for (int i = 0; i < 7; i++)
                    if (it->second.meshes[i].vertexCount > 0) UnloadMesh(it->second.meshes[i]);
            it = chunks.erase(it);
        } else ++it;
    }
}

void World::unloadAll() {
    for (auto &kv : chunks)
        if (kv.second.built)
            for (int i = 0; i < 7; i++)
                if (kv.second.meshes[i].vertexCount > 0) UnloadMesh(kv.second.meshes[i]);
    chunks.clear();
}
