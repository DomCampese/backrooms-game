#include "world.h"
#include "util.h"
#include "levels.h"     // the light grid spacing per level
#include "textures.h"   // FIXTURES: where each fitting sits in the atlas, and how big it is
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <functional>

// Door trim: an architrave 24 mm proud of the wall face, and the threshold
// strip under the opening. Both sample the props atlas's plain metal at
// (0.375, 0.75), which is where addSolidBox looks.
static const float TRIM_T = 0.024f;
static const Color TRIM_COL = { 176, 170, 152, 254 };   // painted trim, no relief bump
static const Color SILL_COL = { 138, 136, 130, 254 };   // dulled metal threshold
// A locked door's leaf and its lock plate. Alpha 254: textured and opaque, but
// below the shader's relief threshold, because a flat painted slab with the
// world-space bump on it comes out looking like pebbledash (see AGENTS.md).
static const Color LEAF_COL = { 150, 128, 96, 254 };
static const Color LOCK_COL = { 206, 194, 140, 254 };
// Half the thickness of a rail's knee wall. Thinner than a partition (WT) so it
// reads as a guard and not as a wall someone forgot to finish.
static const float RAIL_T = 0.075f;

// mesh builder: accumulate textured quads, bake to a raylib Mesh
struct MB {
    std::vector<float> v, uv, n;
    std::vector<unsigned char> c;
    std::vector<unsigned short> idx;
    // Optional colour field over the floor plan, multiplied into every vertex
    // this builder emits (alpha untouched, so the shader's alpha coding is
    // unaffected). Per vertex rather than per quad, so a tint that changes
    // across a room grades smoothly instead of stepping cell by cell — which
    // is what the Red Rooms' bleed on Level 0 needs.
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

ChunkData &World::data(int cx, int cz) {
    uint64_t k = key(cx, cz);
    auto &m = layer(qs);
    auto it = m.find(k);
    if (it != m.end()) return it->second;
    ChunkData &d = m[k];
    generate(d, cx, cz);
    return d;
}

void World::setStorey(int s) {
    if (s == storey) return;
    layers[storey] = std::move(chunks);
    auto it = layers.find(s);
    if (it != layers.end()) { chunks = std::move(it->second); layers.erase(it); }
    else chunks.clear();
    storey = qs = s;
}

// ---- where the stairs go.
//
// One pure function decides, for each chunk and each pair of storeys (p, p+1),
// whether a flight or an opening joins them there and exactly where. Both
// storeys' generators call it and stamp their half, so the two halves agree by
// construction — neither storey ever reads the other's floorplan.
//
// Rising and arriving features must not overlap on the storey they share, so
// the pair's parity picks the half of the chunk it may use: pair p sits in the
// west half when p is even and the east half when it is odd, and storey s —
// which carries pair s rising and pair s-1 arriving — therefore always has the
// two in different halves. Footprints start on even cells: Level 0's 4 m tube
// grid puts a fitting on every odd cell corner, and an opening whose edge ran
// along one would take the fitting out and leave a dark stripe beside it.
bool World::manilaChunk(int cx, int cz, int s) {
    if (level != 0) return false;
    if (manilaTest) return s == 0 && cx == 1 && cz == 0;
    if (abs(cx) <= 1 && abs(cz) <= 1) return false;          // never near where you wake
    StoreyScope sc(*this, s);
    return (hash64(key(cx, cz) ^ 0x3A1111AULL ^ (uint64_t)sseed()
                   ^ ((uint64_t)visit * 0xA24BAED4963EE407ULL)) % MANILA_RATE) == 0;
}

bool World::pairFeature(int cx, int cz, int p, VertFeat &f) {
    if (storeyH <= 0.0f) return false;
    // The arrival: nothing may be stamped over the room you wake in.
    if (cx == 0 && cz == 0 && (p == 0 || p == -1)) return false;
    // A Manila Room fills the middle of its chunk on its own storey.
    if (manilaChunk(cx, cz, p) || manilaChunk(cx, cz, p + 1)) return false;
    uint64_t h = hash64(key(cx, cz) ^ ((uint64_t)seed * 0x9E3779B97F4A7C15ULL)
                        ^ ((uint64_t)(uint32_t)p * 0xC2B2AE3D27D4EB4FULL)
                        ^ ((uint64_t)level * 0x165667B19E3779F9ULL)
                        ^ ((uint64_t)visit * 0x27D4EB2F165667C5ULL) ^ 0x57A1Au);
    // A little over a third of chunks join each pair of storeys. Every storey
    // is in two pairs, so about two chunks in three have a way up or a way down
    // in them: "randomly segmented rooms, hallways, and stairs", not a building
    // with one stairwell you have to find.
    if (h % 100 >= 36) return false;
    uint32_t r = (uint32_t)(h >> 8);
    f = VertFeat{};
    f.lo = p;
    f.dir = (uint8_t)(r & 3);
    int kindRoll = (int)((r >> 2) % 100);
    if (kindRoll < 40)      { f.kind = VK_STAIRWELL; f.wu = 2; f.lv = 4; }
    else if (kindRoll < 72) { f.kind = VK_STAIR; f.wu = (r >> 9) & 1 ? 2 : 1; f.lv = 6; f.stairU = 0;
                              f.wallSide = (int8_t)((int)((r >> 10) % 3) - 1); }
    else if (kindRoll < 86) { f.kind = VK_ATRIUM; f.wu = 4; f.lv = 4; }
    else                    { f.kind = VK_ATRIUM; f.wu = 4; f.lv = 6; f.stairU = (r >> 11) & 1 ? 3 : 0; }
    // Footprint on the grid, and where in its half of the chunk it can go. The
    // room patch is cells 1..13 (the ring corridor has the rest), and every
    // footprint keeps a cleared margin cell all round, so footprints live in
    // 2..13: the west half 2..6, the east half 9..13. Not 2..7 and 8..13: two
    // footprints that touch share an edge, and the storey that carries both
    // stamps one over the other — a stairwell's wall where the flight beside
    // it has a rail — while the storeys either side carry only one of them and
    // keep the rail. The storeys then disagree about that edge, which the
    // regression's boundary check caught as a body pushed 3 cm differently
    // either side of the switch.
    int xlo = (p & 1) ? 9 : 2, xhi = (p & 1) ? 13 : 6;
    int zlo = 2, zhi = 13;
    // A flight in a room (lv 6) has one cell of approach before its opening,
    // so it is the opening's corner that wants the even cell, not the
    // footprint's. Prefer aligned spots; take any if there are none. A
    // footprint too long to lie across its half turns to lie along it.
    int lead = (f.lv == 6) ? 1 : 0;
    int cands[96][2], nc = 0;
    for (int turn = 0; turn < 2 && nc == 0; turn++) {
        if (turn) f.dir = (uint8_t)(f.dir ^ 2);
        int wx = (f.dir < 2) ? f.wu : f.lv, wz = (f.dir < 2) ? f.lv : f.wu;
        for (int pass = 0; pass < 2 && nc == 0; pass++)
            for (int x = xlo; x + wx - 1 <= xhi; x++)
                for (int z = zlo; z + wz - 1 <= zhi && nc < 96; z++) {
                    int ax = x + (f.dir >= 2 ? lead : 0), az = z + (f.dir < 2 ? lead : 0);
                    if (pass == 0 && ((ax & 1) || (az & 1))) continue;
                    cands[nc][0] = x; cands[nc][1] = z; nc++;
                }
    }
    if (nc == 0) return false;
    int pick = (int)((r >> 13) % (uint32_t)nc);
    f.x0 = (int8_t)cands[pick][0]; f.z0 = (int8_t)cands[pick][1];
    return true;
}

int World::featuresFor(int cx, int cz, int s, VertFeat *out, int cap) {
    int n = 0;
    VertFeat f;
    if (n < cap && pairFeature(cx, cz, s, f)) out[n++] = f;          // rising from this storey
    if (n < cap && pairFeature(cx, cz, s - 1, f)) out[n++] = f;      // arriving at it
    return n;
}

// The local frame: u across the rise, v along it, from the footprint corner.
// Four proper rotations of the grid, never a mirror, so a normal computed in
// the local frame survives the trip to the world one.
void World::featureLocal(const VertFeat &f, int cx, int cz, float x, float z, float &u, float &v) const {
    float fx = (cx * CCELLS + f.x0) * CELL, fz = (cz * CCELLS + f.z0) * CELL;
    float U = f.wu * CELL, V = f.lv * CELL;
    float lx = x - fx, lz = z - fz;
    switch (f.dir) {
    case 0:  u = lx;     v = lz;     break;
    case 1:  u = U - lx; v = V - lz; break;
    case 2:  v = lx;     u = U - lz; break;
    default: v = V - lx; u = lz;     break;
    }
}
Vector3 World::featureWorld(const VertFeat &f, int cx, int cz, float u, float y, float v) const {
    float fx = (cx * CCELLS + f.x0) * CELL, fz = (cz * CCELLS + f.z0) * CELL;
    float U = f.wu * CELL, V = f.lv * CELL;
    switch (f.dir) {
    case 0:  return { fx + u,     y, fz + v };
    case 1:  return { fx + U - u, y, fz + V - v };
    case 2:  return { fx + v,     y, fz + U - u };
    default: return { fx + V - v, y, fz + u };
    }
}

uint8_t World::vflagAt(int ci, int ck) {
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    return data(cx, cz).vflag[ci - cx * CCELLS][ck - cz * CCELLS];
}

// Storey qs's half of one feature. The lower storey (qs == f.lo) gets the
// flights and an open ceiling; the upper one gets the hole, its rails and the
// arrival. Walls that run up through both — a stairwell's shaft, the wall a
// flight is built against — are written on both, on the same edges, so the
// wall below and the wall above meet at the slab without a seam and collision
// agrees with itself on either side of the storey boundary.
void World::stampFeature(ChunkData &d, const VertFeat &f, int cx, int cz) {
    const bool lower = qs == f.lo;
    const int W = f.wu, L = f.lv;
    auto cellOfLocal = [&](int uc, int vc, int &i, int &k) {
        switch (f.dir) {
        case 0:  i = f.x0 + uc;           k = f.z0 + vc;           break;
        case 1:  i = f.x0 + (W - 1 - uc); k = f.z0 + (L - 1 - vc); break;
        case 2:  i = f.x0 + vc;           k = f.z0 + (W - 1 - uc); break;
        default: i = f.x0 + (L - 1 - vc); k = f.z0 + uc;           break;
        }
    };
    // Set the edge between two neighbouring local cells (either may lie just
    // outside the footprint) and protect it.
    auto edge = [&](int ua, int va, int ub, int vb, uint8_t val) {
        int i1, k1, i2, k2;
        cellOfLocal(ua, va, i1, k1); cellOfLocal(ub, vb, i2, k2);
        if (i1 == i2) { int k = std::max(k1, k2); d.wallN[i1][k] = val; d.prot[i1][k] |= 1; }
        else          { int i = std::max(i1, i2); d.wallW[i][k1] = val; d.prot[i][k1] |= 2; }
    };
    auto flag = [&](int uc, int vc, uint8_t fl) {
        int i, k; cellOfLocal(uc, vc, i, k);
        d.vflag[i][k] |= fl;
        d.vfeat[i][k] = (int8_t)d.nfeat;
    };
    // ---- the margin: the footprint and one cell round it are cleared of
    // furniture, pillars, sunken floors and whatever walls the room partition
    // ran through them, so a flight never lands in a desk and an opening never
    // has a wall standing in the middle of it. Edges another feature already
    // protected are left alone.
    int ia, ka, ib, kb;
    cellOfLocal(0, 0, ia, ka); cellOfLocal(W - 1, L - 1, ib, kb);
    int mx0 = std::min(ia, ib) - 1, mx1 = std::max(ia, ib) + 1;
    int mz0 = std::min(ka, kb) - 1, mz1 = std::max(ka, kb) + 1;
    for (int i = mx0; i <= mx1; i++) for (int k = mz0; k <= mz1; k++) {
        if (i < 0 || k < 0 || i >= CCELLS || k >= CCELLS) continue;
        d.pillar[i][k] = 0; d.prop[i][k] = PROP_NONE; d.elev[i][k] = 0; d.pool[i][k] = 0;
        if (k > mz0 && !(d.prot[i][k] & 1)) d.wallN[i][k] = WALL_NONE;
        if (i > mx0 && !(d.prot[i][k] & 2)) d.wallW[i][k] = WALL_NONE;
        if (!d.vflag[i][k]) d.vflag[i][k] = VF_KEEP;
    }
    for (int uc = 0; uc < W; uc++) for (int vc = 0; vc < L; vc++) flag(uc, vc, VF_KEEP);
    // Local edges: along a lane (v to v+1) and across lanes (u to u+1).
    auto sideU = [&](int uc, int vc, int side, uint8_t val) { edge(uc, vc, uc + side, vc, val); };
    auto endV  = [&](int uc, int vc, int side, uint8_t val) { edge(uc, vc, uc, vc + side, val); };
    switch (f.kind) {
    case VK_STAIRWELL: {
        // A dogleg in a shaft: lane A (u 0) climbs to the half landing across
        // the far end, lane B (u 1) climbs back to the top landing, which is
        // the upper storey's floor. Solid round the outside and up the middle
        // on both storeys — a stair core, not an open well — so the only way in
        // below is lane A's door and the only way out above is lane B's.
        for (int uc = 0; uc < 2; uc++) {
            endV(uc, 0, -1, WALL_SOLID);                 // the near end
            endV(uc, 3, +1, WALL_SOLID);                 // the far end, behind the half landing
        }
        for (int vc = 0; vc < 4; vc++) {
            sideU(0, vc, -1, WALL_SOLID);                // the two long sides
            sideU(1, vc, +1, WALL_SOLID);
            if (vc < 3) sideU(0, vc, +1, WALL_SOLID);    // the core wall between the flights
            else        sideU(0, vc, +1, WALL_NONE);     // ...open across the half landing
            if (vc < 3) { endV(0, vc, +1, WALL_NONE); endV(1, vc, +1, WALL_NONE); }
        }
        if (lower) {
            endV(0, 0, -1, WALL_DOOR);                   // in at the foot of lane A
            for (int uc = 0; uc < 2; uc++) for (int vc = 0; vc < 4; vc++) {
                uint8_t fl = VF_STAIR | VF_NOWALK;
                if (!(uc == 1 && vc == 0)) fl |= VF_OPENUP;   // lane B's top landing is the floor above
                // The foot of the stair is plain floor, and part of this
                // storey's floor graph: you walk in through the door and stand
                // here looking up the shaft.
                if (uc == 0 && vc == 0) fl = VF_OPENUP;
                flag(uc, vc, fl);
            }
        } else {
            endV(1, 0, -1, WALL_DOOR);                   // out at the head of lane B
            for (int uc = 0; uc < 2; uc++) for (int vc = 0; vc < 4; vc++)
                if (!(uc == 1 && vc == 0)) flag(uc, vc, VF_HOLE | VF_WALKHOLE | VF_NOWALK);
        }
        break;
    }
    case VK_STAIR:
    case VK_ATRIUM: {
        // A straight flight is lanes [s0, s1] of rows 1..4, rising from the
        // approach row 0 to arrive on row 5 of the floor above. An atrium is
        // an opening over every lane of its rows; with a flight, rows 1..4
        // only, the flight up one side.
        bool hasStair = f.kind == VK_STAIR || f.stairU >= 0;
        int s0 = f.kind == VK_STAIR ? 0 : f.stairU, s1 = f.kind == VK_STAIR ? W - 1 : f.stairU;
        int r0 = L == 6 ? 1 : 0, r1 = L == 6 ? 4 : L - 1;   // the rows the opening spans
        auto inStair = [&](int uc) { return hasStair && uc >= s0 && uc <= s1; };
        // Which long side of the flight is a wall. VK_STAIR says; an atrium's
        // flight is always against the atrium's outer edge.
        bool wallLo = f.kind == VK_STAIR ? f.wallSide == -1 : (hasStair && s0 == 0);
        bool wallHi = f.kind == VK_STAIR ? f.wallSide == +1 : (hasStair && s1 == W - 1);
        for (int vc = r0; vc <= r1; vc++) {
            for (int uc = 0; uc < W; uc++) {
                bool st = inStair(uc);
                // across to the next lane, or out of the footprint
                if (uc == 0) sideU(uc, vc, -1, st && wallLo ? WALL_SOLID : (st || !lower) ? WALL_RAIL : WALL_NONE);
                if (uc == W - 1) sideU(uc, vc, +1, st && wallHi ? WALL_SOLID : (st || !lower) ? WALL_RAIL : WALL_NONE);
                if (uc < W - 1) {
                    bool st2 = inStair(uc + 1);
                    // flight beside open floor: a balustrade below, and above
                    // a rail between the flight's hole and the void's
                    sideU(uc, vc, +1, st != st2 ? WALL_RAIL : WALL_NONE);
                }
            }
        }
        for (int uc = 0; uc < W; uc++) {
            bool st = inStair(uc);
            // the near end of the opening
            if (r0 > 0) endV(uc, r0, -1, lower ? WALL_NONE : WALL_RAIL);
            else endV(uc, r0, -1, lower ? WALL_NONE : WALL_RAIL);
            // the far end: the flight runs into the wall under its arrival
            // below, and arrives through it above; the void is railed off
            if (st) endV(uc, r1, +1, lower ? WALL_SOLID : WALL_NONE);
            else    endV(uc, r1, +1, lower ? WALL_NONE : WALL_RAIL);
            for (int vc = r0; vc < r1; vc++) endV(uc, vc, +1, WALL_NONE);
            for (int vc = r0; vc <= r1; vc++) {
                if (lower) flag(uc, vc, st ? (VF_STAIR | VF_OPENUP | VF_NOWALK) : VF_OPENUP);
                else       flag(uc, vc, st ? (VF_HOLE | VF_WALKHOLE | VF_NOWALK) : (VF_HOLE | VF_NOWALK));
            }
        }
        break;
    }
    default: break;
    }
    if (d.nfeat < 2) d.feats[d.nfeat++] = f;
}

bool World::linksStorey(int cx, int cz, int rel) {
    if (storeyH <= 0.0f || rel == 0) return false;
    VertFeat f;
    // rel +1: a feature rising from qs; rel -1: one arriving at qs from below.
    return pairFeature(cx, cz, rel > 0 ? qs : qs - 1, f);
}

void World::generate(ChunkData &d, int cx, int cz) {
    memset(d.wallN, 0, sizeof(d.wallN));
    memset(d.wallW, 0, sizeof(d.wallW));
    memset(d.pillar, 0, sizeof(d.pillar));
    memset(d.pool, 0, sizeof(d.pool));
    memset(d.elev, 0, sizeof(d.elev));
    uint64_t k = key(cx, cz);
    Rng rng(hash64(k ^ ((uint64_t)sseed() + (uint64_t)level * 0x51ED270Bu
                        + (uint64_t)visit * 0x2545F4914F6CDD1DULL) * 0x9E3779B97F4A7C15ULL));
    // Open plazas were one chunk in eight. They are most of what is left of
    // the old wall-less world and they dominate the enclosure average, so cap
    // them at one in sixteen: still a room to break out into, half as often.
    //
    // They also have to move with the rest of the maze when you come back down
    // to a level (BUG-05), or every revisit has its open rooms in the same
    // places and the maze still feels like the one you just left.
    bool openChunk = (hash64(k ^ 0xA11CEULL ^ (uint64_t)sseed()
                             ^ ((uint64_t)visit * 0xD1B54A32D192ED03ULL)) & 15) == 0;
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
            if (i == doorAt) {   // the gap in a wall run is a door, not an absence
                if (horiz) d.wallN[i][b] = WALL_DOOR; else d.wallW[b][i] = WALL_DOOR;
                continue;
            }
            // rarely a window instead of blank wall; behind it, nothing. Only the
            // Red Halls: Level 0 is canonically windowless (the original photo's
            // caption says as much), so it still draws the number — keeping the
            // rng stream where it was — and throws the answer away.
            bool win = (level == 0 || level == 3) && rng.f01() < 0.035f;
            uint8_t v = (win && level == 3) ? WALL_WINDOW : WALL_SOLID;
            if (horiz) d.wallN[i][b] = v; else d.wallW[b][i] = v;
        }
    }
    // ---- rooms.
    //
    // The segments above leave wall *stubs*. Measured over Level 0 with
    // tools/mapdump: 13.9% of edges solid and 55% of cells with no wall on any
    // side, so you are almost never inside anything. Three systems starve on
    // that — the light-occlusion shadowing has nothing to occlude so the shafts
    // through doorways rarely happen, Clark's around-a-corner routing almost
    // never fires because line of sight is rarely broken at chase range, and
    // there is little to hide behind.
    //
    // So partition a patch of the chunk into rooms that share their walls,
    // rather than scattering rectangles and hoping: a BSP down to rooms a few
    // cells across, with a doorway punched into every wall as it is cut.
    // Scattering independent rooms instead was tried and took the reachable
    // share of the world from 92% to 84%, a pocket per room.
    //
    // Chunks are generated blind of each other, so the partition stays one cell
    // off the chunk boundary. A room allowed to sit on the edge walls it, the
    // neighbour does the same, and the two chunks seal apart. That free ring is
    // also the corridor the rooms open onto — and because each chunk leaves
    // one, every seam is a two-cell corridor, which is where the long sightlines
    // that survive this change come from.
    auto roomEdge = [&](uint8_t &e, uint8_t v) {
        // Never overwrite something that is already a way through. A room wall
        // laid across an existing doorway would seal a corridor the segment
        // pass had already opened.
        if (e == WALL_NONE || e == WALL_SOLID) e = v;
    };
    if (!openChunk) {
        // Smallest room side, in cells. Level 2 is a bathhouse, not an office:
        // its pools only form on cells with no wall on any side, so partitioning
        // it as tightly as Level 0 drains it.
        const int MINR = level == 2 ? 5 : 3;
        const int SPLIT = 2 * MINR;              // a side this wide still has room for two
        const float HALL = 0.06f;                // chance a splittable rect is left whole
        const int px0 = HALL_LO, pz0 = HALL_LO,
                  px1 = CCELLS - 1 - HALL_HI, pz1 = CCELLS - 1 - HALL_HI;
        struct Rect { int x0, z0, x1, z1; };
        Rect stack[64];
        int sp = 0;
        stack[sp++] = { px0, pz0, px1, pz1 };
        // The patch's outer wall is what separates the rooms from the ring
        // corridor. Several doors per side, not one: a single door makes the
        // whole block hang off it, so a prop dropped in that one cell strands
        // everything behind it.
        for (int x = px0; x <= px1; x++) {
            roomEdge(d.wallN[x][pz0], WALL_SOLID);
            roomEdge(d.wallN[x][pz1 + 1], WALL_SOLID);
        }
        for (int z = pz0; z <= pz1; z++) {
            roomEdge(d.wallW[px0][z], WALL_SOLID);
            roomEdge(d.wallW[px1 + 1][z], WALL_SOLID);
        }
        for (int i = 0; i < 3; i++) {
            d.wallN[px0 + rng.ri(0, px1 - px0)][pz0]     = WALL_DOOR;
            d.wallN[px0 + rng.ri(0, px1 - px0)][pz1 + 1] = WALL_DOOR;
            d.wallW[px0][pz0 + rng.ri(0, pz1 - pz0)]     = WALL_DOOR;
            d.wallW[px1 + 1][pz0 + rng.ri(0, pz1 - pz0)] = WALL_DOOR;
        }
        // Clear the ring along its own axis. The segment pass ran before this
        // and is free to drop a wall stub across the corridor; left there, the
        // ring stops being a route and the rooms are all you can see. Each
        // chunk leaves one, so the two either side of a seam make a two-cell
        // corridor, and those line up across chunks into the long runs that
        // keep the world from being nothing but small rooms.
        //
        // Leave them unbroken. Walling one cell of each per chunk was tried, to
        // stop a corridor running to the horizon: it moved the enclosure figures
        // by a third of a percent and took the longest measured sightline from
        // 184 m to 50 m. A 184 m axis-aligned run reads alarming in a number and
        // plays fine, because fog density 0.055 closes it long before the end.
        //
        // Three separate things have to go for a ring to be one wide corridor
        // rather than a set of narrow lanes, and only the first was going:
        //   1. the blockers *along* each lane,
        //   2. the dividers *between* this chunk's own lanes, and
        //   3. the seam edge, which divides this chunk's ring from the ring of
        //      the chunk across the seam.
        // (3) is why corridors used to narrow to 2 m without warning: a segment
        // laid across a seam was left there, and the connectivity pass punched
        // only three doors through the whole line, so the hall became a wall
        // with holes in it. The seam line lies entirely inside the ring, so
        // clearing it costs nothing but the hall.
        for (int i = 0; i < CCELLS; i++) {
            for (int w = 0; w < HALL_LO; w++) {                  // low-side lanes
                d.wallW[i][w] = WALL_NONE;
                d.wallN[w][i] = WALL_NONE;
            }
            for (int w = 0; w < HALL_HI; w++) {                  // high-side lanes
                d.wallW[i][CCELLS - 1 - w] = WALL_NONE;
                d.wallN[CCELLS - 1 - w][i] = WALL_NONE;
            }
            for (int w = 1; w < HALL_HI; w++) {                  // (2) between them
                d.wallN[i][CCELLS - w] = WALL_NONE;
                d.wallW[CCELLS - w][i] = WALL_NONE;
            }
            d.wallN[i][0] = WALL_NONE;                           // (3) the seams
            d.wallW[0][i] = WALL_NONE;
        }
        while (sp > 0) {
            Rect r = stack[--sp];
            int rw = r.x1 - r.x0 + 1, rh = r.z1 - r.z0 + 1;
            bool canX = rw >= SPLIT, canZ = rh >= SPLIT;
            if (!canX && !canZ) continue;        // small enough: it is a room
            bool splitX = canX && (!canZ || rw > rh || (rw == rh && (rng.next() & 1)));
            if (sp > 60) continue;
            // Stop early sometimes and leave the rect whole. A partition taken
            // all the way down is uniformly small rooms, which is as wrong in
            // the other direction as the wall-less world was: nowhere in the
            // building is there a hall, and every sightline is the width of one
            // room. These are what the long ones come off.
            if (rng.f01() < HALL) continue;
            if (splitX) {
                int cut = r.x0 + MINR + rng.ri(0, rw - 2 * MINR);
                for (int z = r.z0; z <= r.z1; z++) roomEdge(d.wallW[cut][z], WALL_SOLID);
                d.wallW[cut][r.z0 + rng.ri(0, rh - 1)] = WALL_DOOR;
                stack[sp++] = { r.x0, r.z0, cut - 1, r.z1 };
                stack[sp++] = { cut, r.z0, r.x1, r.z1 };
            } else {
                int cut = r.z0 + MINR + rng.ri(0, rh - 2 * MINR);
                for (int x = r.x0; x <= r.x1; x++) roomEdge(d.wallN[x][cut], WALL_SOLID);
                d.wallN[r.x0 + rng.ri(0, rw - 1)][cut] = WALL_DOOR;
                stack[sp++] = { r.x0, r.z0, r.x1, cut - 1 };
                stack[sp++] = { r.x0, cut, r.x1, r.z1 };
            }
        }
    }
    int np = level == 0 ? 4 + rng.ri(0, 5) : level == 1 ? 10 + rng.ri(0, 8) : 2 + rng.ri(0, 3);
    for (int i = 0; i < np; i++) d.pillar[rng.ri(0, CCELLS - 1)][rng.ri(0, CCELLS - 1)] = 1;
    memset(d.prop, 0, sizeof(d.prop));
    memset(d.propRot, 0, sizeof(d.propRot));
    int npr = level == 0 ? 10 + rng.ri(0, 8) : level == 1 ? 9 + rng.ri(0, 8)
            : level == 3 ? 5 + rng.ri(0, 6) : level == 4 ? 9 + rng.ri(0, 7) : 0;
    for (int i = 0; i < npr; i++) {
        int a = rng.ri(0, CCELLS - 1), b = rng.ri(0, CCELLS - 1);
        if (d.pillar[a][b] || d.prop[a][b]) continue;
        float f = rng.f01();
        if (level == 1)                                                    // warehouse
            d.prop[a][b] = f < 0.45f ? PROP_BOXES : f < 0.65f ? PROP_CABINET : f < 0.80f ? PROP_TABLE
                         : f < 0.96f ? PROP_SHELVING : PROP_VENDING;
        else if (level == 3)                                               // red halls: someone's bedroom
            d.prop[a][b] = f < 0.30f ? PROP_BED : f < 0.55f ? PROP_ARMOIRE : f < 0.80f ? PROP_NIGHTSTAND
                         : PROP_LAMP;
        else if (level == 4)                                               // level fun: the party never ended
            d.prop[a][b] = f < 0.52f ? PROP_PARTY_TABLE : f < 0.70f ? PROP_BOXES : f < 0.82f ? PROP_COUCH
                         : f < 0.93f ? PROP_TABLE : PROP_VENDING;
        else   // L0: "randomly segmented EMPTY rooms" — see below
            // Level 0 used to be furnished like a flat someone had moved out of:
            // desks, beds, couches, armoires, vending machines, plants. Every
            // version of the lore says the opposite — the 4chan post's "randomly
            // segmented empty rooms", the wiki's "barren, sprawling maze", the
            // 2002 photo itself, which is a stripped retail back room with
            // nothing in it. The emptiness is the horror; a couch in the corner
            // is somewhere to sit. What survives is what a back room that was
            // cleared out would still have: the odd stack of cartons nobody
            // took, and ceiling tiles that have come down (the Threshold
            // article lists falling tiles among the level's hazards). The
            // cartons are also the only cover Clark's level offers, so they
            // stay common enough to find.
            d.prop[a][b] = f < 0.30f ? PROP_BOXES : f < 0.46f ? PROP_FALLEN_TILE : PROP_NONE;
        d.propRot[a][b] = (uint8_t)rng.ri(0, 3);
        // boxes like company: sometimes a neighbouring stack
        if (d.prop[a][b] == PROP_BOXES && a + 1 < CCELLS && rng.f01() < 0.4f &&
            !d.pillar[a + 1][b] && !d.prop[a + 1][b]) {
            d.prop[a + 1][b] = PROP_BOXES; d.propRot[a + 1][b] = (uint8_t)rng.ri(0, 3);
        }
    }
    if (level == 2) {
        // THE POOLROOMS, after Level 37 "Sublimity": interconnected rooms and
        // corridors of identical white tile, flooded to varying depths, "ranging
        // from uniform pools and hallways to more open, abnormally shaped areas".
        // It used to be one layout stamped on every chunk (a cross of arched
        // partitions over four identical stepped basins), which is why it read
        // as boring: every 32 m looked like the last 32 m.
        //
        // Now a chunk is either one grand hall, or the arched cross with each
        // quarter drawn from a set of lore room types. Cells 0 and 15 stay a dry
        // walkway so chunks generated blind of each other always meet; the
        // connectivity pass below still punches through anything sealed, and
        // its doorways become bare tiled openings further down.
        memset(d.wallN, 0, sizeof(d.wallN));
        memset(d.wallW, 0, sizeof(d.wallW));
        memset(d.pillar, 0, sizeof(d.pillar));
        memset(d.pool, 0, sizeof(d.pool));
        memset(d.elev, 0, sizeof(d.elev));
        auto setPool = [&](int x, int z, int e) { d.pool[x][z] = e < 0; d.elev[x][z] = (int8_t)e; };
        // Stepped basin over a rectangle: a wading shelf, a waist-deep tread,
        // then swimming depth. Pools are exempt from the riser rule, so these
        // treads are what make the edge read as a pool rather than a pit.
        auto basin = [&](int x0, int z0, int x1, int z1, bool grove, uint32_t h) {
            for (int x = x0; x <= x1; ++x) for (int z = z0; z <= z1; ++z) {
                int edge = std::min({x - x0, z - z0, x1 - x, z1 - z});
                setPool(x, z, edge == 0 ? -6 : edge == 1 ? -12 : -28);
                if (grove && edge >= 2 && ((x - x0) % 3 == 1) && ((z - z0) % 3 == 1)) d.pillar[x][z] = 1;
            }
            if (h % 4 == 0) {   // a tiled island in the deep end, reached by swimming
                int mx = (x0 + x1) / 2, mz = (z0 + z1) / 2;
                setPool(mx, mz, 0); d.pillar[mx][mz] = 0;
            }
        };
        uint32_t ch = ih(cx, cz, sseed() ^ 0x37C0u);
        bool grandHall = ch % 4 == 0 && !(cx == 0 && cz == 0);
        if (grandHall) {
            // One unnaturally large room that serves no purpose: a single basin
            // under a colonnade, pillars marching through the deep water.
            basin(1, 1, CCELLS - 2, CCELLS - 2, true, ch >> 3);
        } else {
            // The arched cross. Arches (mesher) span cells 3-5 and 11-13; the
            // ends at 0 and 15 stay open so the perimeter walkway is continuous.
            for (int t = 1; t < CCELLS - 1; ++t) {
                bool opening = (t >= 3 && t <= 5) || (t >= 11 && t <= 13);
                if (!opening) d.wallN[t][8] = d.wallW[8][t] = WALL_SOLID;
            }
            for (int qa = 0; qa < 2; ++qa) for (int qb = 0; qb < 2; ++qb) {
                int x0 = qa ? 8 : 1, x1 = qa ? CCELLS - 2 : 7;
                int z0 = qb ? 8 : 1, z1 = qb ? CCELLS - 2 : 7;
                uint32_t qh = ih(cx * 2 + qa, cz * 2 + qb, sseed() ^ 0x37D1u);
                Rng qr(hash64(((uint64_t)qh << 1) ^ 0x5EA5ULL));
                int kind = (int)(qh % 10);   // 0-2 bath, 3-4 tunnels, 5-6 stairs, 7 gallery, 8 islands, 9 tubs
                if (kind <= 2) {
                    basin(x0, z0, x1, z1, qh % 3 == 0, qh >> 5);
                } else if (kind <= 4) {
                    // Flooded tiled tunnels: a braided maze of one-cell corridors.
                    // Half are wading depth, half are submerged passages you swim.
                    int depth = (qh >> 7) & 1 ? -20 : -6;
                    int w = x1 - x0 + 1, hgt = z1 - z0 + 1;
                    for (int x = x0; x <= x1; ++x) for (int z = z0; z <= z1; ++z) {
                        setPool(x, z, depth);
                        if (x > x0) d.wallW[x][z] = WALL_SOLID;
                        if (z > z0) d.wallN[x][z] = WALL_SOLID;
                    }
                    bool seen[CCELLS][CCELLS] = {};
                    int sx[CCELLS * CCELLS], sz[CCELLS * CCELLS], sp = 0;
                    sx[sp] = x0 + qr.ri(0, w - 1); sz[sp] = z0 + qr.ri(0, hgt - 1);
                    seen[sx[sp]][sz[sp]] = true; ++sp;
                    while (sp) {
                        int x = sx[sp - 1], z = sz[sp - 1];
                        int opts[4], n = 0;
                        if (x > x0 && !seen[x - 1][z]) opts[n++] = 0;
                        if (x < x1 && !seen[x + 1][z]) opts[n++] = 1;
                        if (z > z0 && !seen[x][z - 1]) opts[n++] = 2;
                        if (z < z1 && !seen[x][z + 1]) opts[n++] = 3;
                        if (!n) { --sp; continue; }
                        int o = opts[qr.ri(0, n - 1)];
                        int nx = x + (o == 1) - (o == 0), nz = z + (o == 3) - (o == 2);
                        if (o == 0) d.wallW[x][z] = WALL_NONE;
                        if (o == 1) d.wallW[x + 1][z] = WALL_NONE;
                        if (o == 2) d.wallN[x][z] = WALL_NONE;
                        if (o == 3) d.wallN[x][z + 1] = WALL_NONE;
                        seen[nx][nz] = true; sx[sp] = nx; sz[sp] = nz; ++sp;
                    }
                    // Braid: knock out some dead ends so the tunnels loop back
                    // on themselves instead of reading as a puzzle maze.
                    for (int x = x0; x <= x1; ++x) for (int z = z0; z <= z1; ++z) {
                        if (qr.f01() > 0.22f) continue;
                        if (x > x0 && qr.f01() < 0.5f) d.wallW[x][z] = WALL_NONE;
                        else if (z > z0) d.wallN[x][z] = WALL_NONE;
                    }
                } else if (kind <= 6) {
                    // A broad staircase descending into deep water from one side,
                    // real 0.4 m treads all the way down. The underwater stairs
                    // are the lore's hint that the level was not always flooded.
                    int dir = (int)((qh >> 9) & 3);
                    for (int x = x0; x <= x1; ++x) for (int z = z0; z <= z1; ++z) {
                        int step = dir == 0 ? x - x0 : dir == 1 ? x1 - x : dir == 2 ? z - z0 : z1 - z;
                        setPool(x, z, -4 * std::min(step + 1, 7));
                    }
                    // flanking walls turn it into a stairwell rather than a ramp
                    for (int t = 0; t <= 6; ++t) {
                        if (t == 0 || t == 6) continue;
                        if (dir <= 1) { d.wallN[x0 + t][z0 + 1] = WALL_SOLID; d.wallN[x0 + t][z1] = WALL_SOLID; }
                        else          { d.wallW[x0 + 1][z0 + t] = WALL_SOLID; d.wallW[x1][z0 + t] = WALL_SOLID; }
                    }
                } else if (kind == 7) {
                    // A dry gallery with rows of windows looking out into light
                    // (the mesher draws Poolrooms windows as glowing panes, not
                    // glass), pillars, and a small deep pool in the middle.
                    for (int x = x0 + 2; x <= x1 - 2; ++x) for (int z = z0 + 2; z <= z1 - 2; ++z)
                        setPool(x, z, (x == x0 + 2 || x == x1 - 2 || z == z0 + 2 || z == z1 - 2) ? -6 : -18);
                    bool alongX = (qh >> 11) & 1;
                    for (int t = 0; t < 7; ++t) {
                        if (t == 3) continue;   // a gap mid-run to walk through
                        if (alongX) { d.wallN[x0 + t][z0 + 1] = WALL_WINDOW; d.wallN[x0 + t][z1] = WALL_WINDOW; }
                        else        { d.wallW[x0 + 1][z0 + t] = WALL_WINDOW; d.wallW[x1][z0 + t] = WALL_WINDOW; }
                    }
                    d.pillar[x0 + 1][z0 + 1] = d.pillar[x1 - 1][z0 + 1] = 1;
                    d.pillar[x0 + 1][z1 - 1] = d.pillar[x1 - 1][z1 - 1] = 1;
                } else if (kind == 8) {
                    // Stepping stones: dry tiled platforms standing in deep water.
                    for (int x = x0; x <= x1; ++x) for (int z = z0; z <= z1; ++z) {
                        bool stone = ((x - x0) % 2 == 0 && (z - z0) % 2 == 0 && qr.f01() < 0.55f);
                        setPool(x, z, stone ? 0 : -24);
                    }
                } else {
                    // Private baths: small tiled rooms, each with its own deep tub.
                    int mx = x0 + 3, mz = z0 + 3;
                    for (int t = x0; t <= x1; ++t) if (t != x0 + 1 && t != x1 - 1) d.wallN[t][mz] = WALL_SOLID;
                    for (int t = z0; t <= z1; ++t) if (t != z0 + 1 && t != z1 - 1) d.wallW[mx][t] = WALL_SOLID;
                    int tubs[4][2] = { { x0 + 1, z0 + 1 }, { x1 - 1, z0 + 1 }, { x0 + 1, z1 - 1 }, { x1 - 1, z1 - 1 } };
                    for (auto &tb : tubs) {
                        int tx = tb[0], tz = tb[1];
                        setPool(tx, tz, -16);
                        int ox = tx < mx ? tx + 1 : tx - 1;
                        setPool(ox, tz, -16);
                    }
                }
            }
        }
    }
    if (level == 0 || level == 1) {   // sunken lounges (L0) / loading docks (L1), never under walls
        for (int i = 1; i < CCELLS - 1; i++) for (int kk = 1; kk < CCELLS - 1; kk++) {
            if (d.pillar[i][kk]) continue;
            if (d.wallN[i][kk] || d.wallW[i][kk] || d.wallN[i][kk + 1] || d.wallW[i + 1][kk]) continue;
            float gxc = (float)(cx * CCELLS + i), gzc = (float)(cz * CCELLS + kk);
            if (fbm2(gxc * 0.09f, gzc * 0.09f, sseed() ^ (level == 0 ? 0x51ABu : 0xD0CCu), 3) > 0.60f)
                d.elev[i][kk] = level == 0 ? -5 : 6;
            if (level == 1 && d.elev[i][kk] == 6 &&   // some docks carry a second tier
                fbm2(gxc * 0.09f, gzc * 0.09f, sseed() ^ 0xD0CCu, 3) > 0.655f) d.elev[i][kk] = 12;
        }
        // L0: rare grand atria — the floor falls away in broad carpeted terraces,
        // half a metre per ring, down to a hall two and a half metres below the
        // office. Every terrace edge gets real stairs; the walls above become
        // balconies. Think of the movie: there was always another floor.
        if (level == 0) {
            for (int i = 1; i < CCELLS - 1; i++) for (int kk = 1; kk < CCELLS - 1; kk++) {
                if (d.pillar[i][kk]) continue;
                if (d.wallN[i][kk] || d.wallW[i][kk] || d.wallN[i][kk + 1] || d.wallW[i + 1][kk]) continue;
                float gxc = (float)(cx * CCELLS + i), gzc = (float)(cz * CCELLS + kk);
                float n = fbm2(gxc * 0.042f, gzc * 0.042f, sseed() ^ 0xA7B1u, 3);
                if (n <= 0.615f) continue;
                int ring = 1 + (int)((n - 0.615f) / 0.028f);   // deeper toward the middle
                d.elev[i][kk] = (int8_t)(-5 * std::min(ring, 5));
            }
        }
    }
    // ---- storeys: the stairs and openings that join this floor to the ones
    // above and below. Stamped here — after the rooms, the furniture and the
    // sunken floors, before the connectivity flood — so the flood routes round
    // them and punches its doorways elsewhere. Their edges are marked in
    // d.prot, and every pass after this one leaves a protected edge alone:
    // door thinning, the locked door and the noclip walls all had a way of
    // turning a stairwell's wall into a doorway, and the storey above would
    // not have agreed.
    d.nfeat = 0;
    memset(d.vflag, 0, sizeof(d.vflag));
    memset(d.vfeat, -1, sizeof(d.vfeat));
    memset(d.prot, 0, sizeof(d.prot));
    {
        VertFeat fs[2];
        int nf = featuresFor(cx, cz, qs, fs, 2);
        for (int q = 0; q < nf; q++) stampFeature(d, fs[q], cx, cz);
    }
    // ---- the Manila Room. Stamped before the connectivity flood so the flood
    // sees it, and stamped again at the end (below) so nothing after the flood
    // — door thinning, the locked door, the exit — can move its four doors.
    d.manila = manilaChunk(cx, cz, qs);
    auto stampManila = [&]() {
        // Walls of the room: solid all round, one doorway in each side.
        for (int t = MANILA_LO; t <= MANILA_HI; t++) {
            d.wallN[t][MANILA_LO] = d.wallN[t][MANILA_HI + 1] = WALL_SOLID;
            d.wallW[MANILA_LO][t] = d.wallW[MANILA_HI + 1][t] = WALL_SOLID;
        }
        d.wallN[7][MANILA_LO] = d.wallN[8][MANILA_HI + 1] = WALL_DOOR;
        d.wallW[MANILA_LO][8] = d.wallW[MANILA_HI + 1][7] = WALL_DOOR;
    };
    if (d.manila) {
        // A cleared margin round it, so every door opens onto floor and the
        // room stands on its own in the maze rather than sharing walls with
        // whatever the partition cut there.
        for (int i = MANILA_LO - 2; i <= MANILA_HI + 2; i++)
            for (int kk = MANILA_LO - 2; kk <= MANILA_HI + 2; kk++) {
                d.pillar[i][kk] = 0; d.prop[i][kk] = PROP_NONE; d.elev[i][kk] = 0;
                if (kk > MANILA_LO - 2) d.wallN[i][kk] = WALL_NONE;
                if (i > MANILA_LO - 2) d.wallW[i][kk] = WALL_NONE;
            }
        stampManila();
        d.prop[7][7] = PROP_MANILA_TABLE; d.propRot[7][7] = 0;
    }
    // ---- connectivity.
    //
    // Walls, pillars and props are placed by passes that cannot see each other:
    // a segment laid across a room cuts it in two, and a desk dropped in a
    // room's only doorway strands everything behind it. Constraining every
    // placement pass against every other one is a losing game, so instead flood
    // the finished chunk once and punch a doorway across any edge that still
    // has two regions either side of it.
    //
    // This is not decoration. Before it existed the enclosure work above left
    // 1002 sealed pockets in a 129-cell sample and took the reachable share of
    // the world from 92% down to 85%; most were one or two cells, wedged
    // between a room wall and a segment, and no amount of tuning the room sizes
    // made them go away — they are what happens when independent passes share a
    // grid.
    {
        const int N = CCELLS * CCELLS;
        int parent[N];
        for (int i = 0; i < N; i++) parent[i] = i;
        auto find = [&](int a) { while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
        auto join = [&](int a, int b) { a = find(a); b = find(b); if (a == b) return false; parent[a] = b; return true; };
        // A pillar or a prop fills its cell outright — canStep() says so — so
        // those cells are not part of the graph and are not worth opening to.
        auto solidCell = [&](int x, int z) { return d.pillar[x][z] != 0 || d.prop[x][z] != PROP_NONE ||
                                                (d.vflag[x][z] & VF_NOWALK) != 0; };
        auto cellId = [](int x, int z) { return x * CCELLS + z; };
        for (int x = 0; x < CCELLS; x++) for (int z = 0; z < CCELLS; z++) {
            if (solidCell(x, z)) continue;
            if (z > 0 && !solidCell(x, z - 1) && !blocksEdge(d.wallN[x][z])) join(cellId(x, z), cellId(x, z - 1));
            if (x > 0 && !solidCell(x - 1, z) && !blocksEdge(d.wallW[x][z])) join(cellId(x, z), cellId(x - 1, z));
        }
        for (int x = 0; x < CCELLS; x++) for (int z = 0; z < CCELLS; z++) {
            if (solidCell(x, z)) continue;
            // A stairwell's walls are the storey above's walls too; a doorway
            // punched here would be a doorway into a shaft on one floor only.
            if (z > 0 && !solidCell(x, z - 1) && !(d.prot[x][z] & 1) &&
                join(cellId(x, z), cellId(x, z - 1))) d.wallN[x][z] = WALL_DOOR;
            if (x > 0 && !solidCell(x - 1, z) && !(d.prot[x][z] & 2) &&
                join(cellId(x, z), cellId(x - 1, z))) d.wallW[x][z] = WALL_DOOR;
        }
        // The chunk owns the wall line along its west and north seams, and the
        // neighbour across each seam owns the other two, so opening these two
        // opens all four. Nothing else can: a neighbour generated blind cannot
        // put a hole in a wall it does not store.
        for (int side = 0; side < 2; side++) {
            int opened = 0;
            for (int t = 0; t < CCELLS && opened < 3; t++) {
                int i = (t * 7 + (int)rng.ri(0, CCELLS - 1)) % CCELLS;
                uint8_t &e = side ? d.wallW[0][i] : d.wallN[i][0];
                if (side ? solidCell(0, i) : solidCell(i, 0)) continue;
                if (!blocksEdge(e)) { opened++; continue; }
                e = WALL_DOOR;
                opened++;
            }
        }
    }
    // ---- doorways, thinned.
    //
    // Three passes punch doorways and none of them can see the others — the
    // segment runs, the room partition, and the connectivity flood above — so
    // they regularly leave two and three openings side by side in one wall.
    // What that builds is not a room with doors in it: a 1.3 m opening in a 2 m
    // cell leaves 0.7 m of plasterboard standing between each pair, and a wall
    // of alternating holes and piers reads as unfinished geometry rather than
    // as a building. It is the single thing that makes the rooms look sloppy
    // from inside one, and no floorplan shows it — it took a screenshot.
    //
    // So close one of every adjacent pair. Only ever *close*, and only where
    // the chunk stays exactly as reachable as the flood above left it, so this
    // can take away a second doorway and can never take away the only one.
    {
        auto solidCell = [&](int x, int z) { return d.pillar[x][z] != 0 || d.prop[x][z] != PROP_NONE ||
                                                (d.vflag[x][z] & VF_NOWALK) != 0; };
        // Is there still a way from one side of this edge to the other?
        //
        // That is the whole test, and it is exact: closing a single edge can
        // split the floor into at most two pieces, and it does so precisely
        // when its own two cells end up in different ones. Counting how many
        // cells one flood reaches instead is NOT the same test and quietly gets
        // it wrong — a chunk whose floor is already in two pieces (both reached
        // from the world through different seams) has one of them outside the
        // count, so every closure inside that piece looks free. Measured, that
        // shipped 24 newly stranded cells and took the largest cut-off pocket
        // from 2 cells to 8.
        auto joined = [&](int ax, int az, int bx, int bz) {
            bool seen[CCELLS][CCELLS] = {};
            int qx[CCELLS * CCELLS], qz[CCELLS * CCELLS], head = 0, tail = 0;
            qx[tail] = ax; qz[tail] = az; tail++; seen[ax][az] = true;
            while (head < tail) {
                int x = qx[head], z = qz[head]; head++;
                if (x == bx && z == bz) return true;
                const int dxs[4] = { 0, 0, -1, 1 }, dzs[4] = { -1, 1, 0, 0 };
                for (int e = 0; e < 4; e++) {
                    int nx = x + dxs[e], nz = z + dzs[e];
                    if (nx < 0 || nz < 0 || nx >= CCELLS || nz >= CCELLS) continue;
                    if (seen[nx][nz] || solidCell(nx, nz)) continue;
                    uint8_t edge = e == 0 ? d.wallN[x][z] : e == 1 ? d.wallN[x][z + 1]
                                 : e == 2 ? d.wallW[x][z] : d.wallW[x + 1][z];
                    if (blocksEdge(edge)) continue;
                    // The same riser rule canStep enforces, so this agrees with
                    // the pathfinder about what a route is. Pools are exempt on
                    // both sides, exactly as there.
                    if (!d.pool[x][z] && !d.pool[nx][nz] &&
                        abs((int)d.elev[nx][nz] - (int)d.elev[x][z]) > MAX_STEP_UNITS) continue;
                    seen[nx][nz] = true; qx[tail] = nx; qz[tail] = nz; tail++;
                }
            }
            return false;
        };
        // Never a seam edge (k == 0 on a north wall, i == 0 on a west one).
        // Those are shared with a chunk generated blind of this one, so nothing
        // here can tell whether closing or moving one strands the far side.
        //
        // Closing is only half of it. Most adjacent pairs are two rooms off the
        // same corridor, each reached through its own door, so neither door is
        // spare and refusing to close them leaves the wall exactly as it was —
        // which is what the first version of this did, and the screenshot was
        // unchanged. Give the door somewhere else to be instead: carve it into
        // blank wall on the same line, as near its old place as will take it,
        // and keep the move only if the room it served is still reachable.
        auto tidyLine = [&](auto edgeAt, auto protAt, auto sideA, auto sideB, int n) {
            for (int t = 1; t < n; t++) {
                if (edgeAt(t) != WALL_DOOR || edgeAt(t - 1) != WALL_DOOR) continue;
                if (protAt(t)) continue;                     // a stairwell's door stays where the stamp put it
                int ax, az, bx, bz;
                sideA(t, ax, az); sideB(t, bx, bz);
                if (solidCell(ax, az) || solidCell(bx, bz)) continue;
                edgeAt(t) = WALL_SOLID;
                if (joined(ax, az, bx, bz)) continue;        // spare: the wall is better without it
                bool moved = false;
                for (int off = 2; off < n && !moved; off++)
                    for (int sgn = -1; sgn <= 1 && !moved; sgn += 2) {
                        int u = t + sgn * off;
                        if (u < 0 || u >= n) continue;
                        if (edgeAt(u) != WALL_SOLID || protAt(u)) continue;   // only into blank wall
                        // ...and not straight back into the problem
                        if ((u > 0 && edgeAt(u - 1) == WALL_DOOR) ||
                            (u + 1 < n && edgeAt(u + 1) == WALL_DOOR)) continue;
                        int cx2, cz2, dx2, dz2;
                        sideA(u, cx2, cz2); sideB(u, dx2, dz2);
                        if (solidCell(cx2, cz2) || solidCell(dx2, dz2)) continue;
                        edgeAt(u) = WALL_DOOR;
                        if (joined(ax, az, bx, bz)) moved = true;
                        else edgeAt(u) = WALL_SOLID;
                    }
                if (!moved) edgeAt(t) = WALL_DOOR;           // nowhere better; leave it alone
            }
        };
        for (int k = 1; k < CCELLS; k++)
            tidyLine([&](int t) -> uint8_t & { return d.wallN[t][k]; },
                     [&](int t) { return (d.prot[t][k] & 1) != 0; },
                     [&](int t, int &x, int &z) { x = t; z = k; },
                     [&](int t, int &x, int &z) { x = t; z = k - 1; }, CCELLS);
        for (int i = 1; i < CCELLS; i++)
            tidyLine([&](int t) -> uint8_t & { return d.wallW[i][t]; },
                     [&](int t) { return (d.prot[i][t] & 2) != 0; },
                     [&](int t, int &x, int &z) { x = i;     z = t; },
                     [&](int t, int &x, int &z) { x = i - 1; z = t; }, CCELLS);
    }
    // ---- one door that is actually shut, and the key to it.
    //
    // Every other "door" in the building is an empty frame. About a third of
    // chunks get one with a leaf in it, locked, and the key lying loose within
    // sight of it. Two rules make that a small find rather than a wall:
    //
    //   - the key goes on the side of the door you can already reach. Which
    //     side that is cannot be hashed, because it depends on the floorplan
    //     the passes above happened to build, so the flood works it out and the
    //     answer is stored in the chunk.
    //   - what the door shuts off stays small. Locking a bridge that strands
    //     half a chunk is a wall with extra steps; locking one that strands a
    //     closet is a cupboard worth opening. A door that strands nothing at
    //     all is a shortcut, which is also fine.
    if (level != 2 && (hash64(k ^ 0x10CCEDULL ^ (uint64_t)sseed()
                ^ ((uint64_t)visit * 0x9E3779B97F4A7C15ULL)) % 3) == 0) {
        auto solidCell = [&](int x, int z) { return d.pillar[x][z] != 0 || d.prop[x][z] != PROP_NONE ||
                                                (d.vflag[x][z] & VF_NOWALK) != 0; };
        // Cells reachable from (sx,sz) with the walls exactly as they stand.
        auto flood = [&](int sx, int sz, bool (&seen)[CCELLS][CCELLS]) {
            memset(seen, 0, sizeof(seen));
            int qx[CCELLS * CCELLS], qz[CCELLS * CCELLS], head = 0, tail = 0, n = 0;
            qx[tail] = sx; qz[tail] = sz; tail++; seen[sx][sz] = true;
            while (head < tail) {
                int x = qx[head], z = qz[head]; head++; n++;
                const int dxs[4] = { 0, 0, -1, 1 }, dzs[4] = { -1, 1, 0, 0 };
                for (int e = 0; e < 4; e++) {
                    int nx = x + dxs[e], nz = z + dzs[e];
                    if (nx < 0 || nz < 0 || nx >= CCELLS || nz >= CCELLS) continue;
                    if (seen[nx][nz] || solidCell(nx, nz)) continue;
                    uint8_t edge = e == 0 ? d.wallN[x][z] : e == 1 ? d.wallN[x][z + 1]
                                 : e == 2 ? d.wallW[x][z] : d.wallW[x + 1][z];
                    if (blocksEdge(edge)) continue;
                    if (!d.pool[x][z] && !d.pool[nx][nz] &&
                        abs((int)d.elev[nx][nz] - (int)d.elev[x][z]) > MAX_STEP_UNITS) continue;
                    seen[nx][nz] = true; qx[tail] = nx; qz[tail] = nz; tail++;
                }
            }
            return n;
        };
        const int CLOSET_MAX = 12;               // cells a locked door may shut off
        // Interior doorways only. A seam edge is shared with a chunk generated
        // blind of this one, and a door locked there would be a wall the
        // neighbour has no idea about.
        int bi[128], bk[128], bw[128], nb = 0;
        for (int x = 1; x < CCELLS && nb < 128; x++)
            for (int z = 1; z < CCELLS && nb < 128; z++) {
                // Never a stairwell's door: locked from the floor above, it
                // would shut a wanderer coming up the stairs into a closet
                // with the key on the far side of the door.
                if (d.wallN[x][z] == WALL_DOOR && !(d.prot[x][z] & 1) && !solidCell(x, z) && !solidCell(x, z - 1)) {
                    bi[nb] = x; bk[nb] = z; bw[nb] = 0; nb++;
                }
                if (nb < 128 && d.wallW[x][z] == WALL_DOOR && !(d.prot[x][z] & 2) && !solidCell(x, z) && !solidCell(x - 1, z)) {
                    bi[nb] = x; bk[nb] = z; bw[nb] = 1; nb++;
                }
            }
        for (int attempt = 0; attempt < 12 && nb > 0 && d.lockI < 0; attempt++) {
            int c = rng.ri(0, nb - 1);
            int x = bi[c], z = bk[c], west = bw[c];
            // the Manila Room's doors are open by definition: it is where
            // wanderers can meet, and a locked one would make it a closet
            if (d.manila && x >= MANILA_LO && x <= MANILA_HI + 1 && z >= MANILA_LO && z <= MANILA_HI + 1) continue;
            int ax = x, az = z, bx2 = west ? x - 1 : x, bz2 = west ? z : z - 1;
            uint8_t &e = west ? d.wallW[x][z] : d.wallN[x][z];
            e = WALL_LOCKED;
            static bool seenA[CCELLS][CCELLS], seenB[CCELLS][CCELLS];
            int na = flood(ax, az, seenA);
            if (seenA[bx2][bz2]) {
                // Not a bridge: the door is a shortcut. The key goes on either
                // side, since both are the same side.
                d.lockI = (int8_t)x; d.lockK = (int8_t)z; d.lockWest = (uint8_t)west;
            } else {
                int nbb = flood(bx2, bz2, seenB);
                // Lock it only if one side is a closet rather than half the
                // chunk, and put the key in the other one.
                bool aCloset = na <= CLOSET_MAX, bCloset = nbb <= CLOSET_MAX;
                if (!aCloset && !bCloset) { e = WALL_DOOR; continue; }
                d.lockI = (int8_t)x; d.lockK = (int8_t)z; d.lockWest = (uint8_t)west;
                // the open side is the one that is NOT the closet
                if (aCloset) memcpy(seenA, seenB, sizeof(seenA));
            }
            // The key: the nearest cell on the open side that has nothing else
            // in it. Near, because a key you cannot associate with its door is
            // just another pickup.
            int best = 1 << 30;
            for (int qx2 = 0; qx2 < CCELLS; qx2++)
                for (int qz2 = 0; qz2 < CCELLS; qz2++) {
                    if (!seenA[qx2][qz2] || solidCell(qx2, qz2) || d.pool[qx2][qz2]) continue;
                    int dd = (qx2 - x) * (qx2 - x) + (qz2 - z) * (qz2 - z);
                    if (dd == 0 || dd >= best || dd > 36) continue;   // within 6 cells
                    best = dd; d.keyI = (int8_t)qx2; d.keyK = (int8_t)qz2;
                }
            if (d.keyI < 0) { e = WALL_DOOR; d.lockI = d.lockK = -1; }   // no room for the key
        }
    }
    // rare exit door carved into an existing wall run
    // (Never in a stairwell's walls: those run up through two storeys, and a
    // wall you can noclip through on one floor would be solid on the next.)
    if (hash64(k ^ 0xE717ULL ^ (uint64_t)sseed()) % (exitTest ? 1 : 16) == 0) {
        bool placed = false;
        for (int i = 1; i < CCELLS - 1 && !placed; i++)
            for (int kk = 0; kk < CCELLS && !placed; kk++)
                if (d.wallN[i][kk] == WALL_SOLID && d.wallN[i - 1][kk] == WALL_SOLID &&
                    d.wallN[i + 1][kk] == WALL_SOLID && !(d.prot[i][kk] & 1)) {
                    d.wallN[i][kk] = WALL_EXIT; placed = true;
                }
        for (int i = 0; i < CCELLS && !placed; i++)
            for (int kk = 1; kk < CCELLS - 1 && !placed; kk++)
                if (d.wallW[i][kk] == WALL_SOLID && d.wallW[i][kk - 1] == WALL_SOLID &&
                    d.wallW[i][kk + 1] == WALL_SOLID && !(d.prot[i][kk] & 2)) {
                    d.wallW[i][kk] = WALL_EXIT; placed = true;
                }
    }
    if (d.manila) stampManila();   // see above: its doors are its own
    if (level == 2) {
        // Tiled halls have no door frames. Whatever the connectivity pass and
        // the thinning punched through, open it as a plain gap in the tile.
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            if (d.wallN[i][kk] == WALL_DOOR) d.wallN[i][kk] = WALL_NONE;
            if (d.wallW[i][kk] == WALL_DOOR) d.wallW[i][kk] = WALL_NONE;
        }
    }
    if (cx == 0 && cz == 0 && level != 2 && qs == 0) {   // clear spawn room (you wake on storey 0)
        for (int i = 5; i <= 10; i++) for (int kk = 5; kk <= 10; kk++) {
            d.wallN[i][kk] = d.wallW[i][kk] = d.pillar[i][kk] = d.prop[i][kk] = d.pool[i][kk] = 0;
            d.elev[i][kk] = 0;
        }
        if (exitTest) { d.wallN[6][11] = WALL_SOLID; d.wallN[7][11] = WALL_EXIT; d.wallN[8][11] = WALL_SOLID; }
    }

    if (level==2 && cx==0 && cz==0) {
        // Arrival opens onto the baths, rather than facing the corner of the
        // central partition. A dry landing leaves room to learn the controls.
        for (int i=7;i<=9;++i) for (int z=7;z<=9;++z) {
            d.pool[i][z]=0;d.elev[i][z]=0;
            d.wallN[i][z]=d.wallW[i][z]=WALL_NONE;
        }
    }

    // No edge of walkable terrain may be taller than one step. A 0.5 m lounge
    // lip or a 0.6 m dock face used to be strolled up like a ramp; with
    // MAX_STEP enforced they would instead be sealed — and a sunken lounge you
    // can fall into but not climb out of is a softlock, since there are no
    // stair meshes yet (WORLD-06). So the generator gives every region a tread:
    // each pass pulls any cell that overhangs its most moderate neighbour back
    // toward it, which leaves plateau interiors at their authored depth and
    // turns the boundary ring into a step.
    //
    // This runs LAST, after the spawn-room clear above, which zeroes a block of
    // cells and would otherwise leave a 0.5 m face round the spawn room that
    // nothing had smoothed. Both directions move cells toward zero, so it
    // always terminates; the chunk's border ring is elev 0 (the placement loops
    // start at 1), so chunk seams are flat and no pass ever needs to look into
    // the neighbouring chunk.
    for (int pass = 0; level != 2 && pass < 10; pass++) {
        bool changed = false;
        for (int i = 1; i < CCELLS - 1; i++) for (int kk = 1; kk < CCELLS - 1; kk++) {
            int e = d.elev[i][kk];
            if (e == 0) continue;
            int nb[4] = { d.elev[i - 1][kk], d.elev[i + 1][kk],
                          d.elev[i][kk - 1], d.elev[i][kk + 1] };
            if (e > 0) {   // a rise: no higher than one step above its lowest neighbour
                int lo = nb[0];
                for (int q = 1; q < 4; q++) lo = std::min(lo, nb[q]);
                if (e > lo + MAX_STEP_UNITS) { d.elev[i][kk] = (int8_t)(lo + MAX_STEP_UNITS); changed = true; }
            } else {       // a drop: no lower than one step below its highest neighbour
                int hi = nb[0];
                for (int q = 1; q < 4; q++) hi = std::max(hi, nb[q]);
                if (e < hi - MAX_STEP_UNITS) { d.elev[i][kk] = (int8_t)(hi - MAX_STEP_UNITS); changed = true; }
            }
        }
        if (!changed) break;
    }
}

// Both wall lookups consult the shifted-edge overlay first, because every
// system that cares about walls — collision, the pathfinder, line of sight, the
// occupancy grid the shader marches, and the mesher — comes through here. Put
// the overlay anywhere else and the lighting and Clark end up disagreeing with
// the geometry the player can see. An empty set is the overwhelmingly common
// case and costs one hash lookup.
uint8_t World::wallNVal(int ci, int ck) {
    if (!shifted.empty() && shifted.count(edgeKey(ci, ck, false))) return WALL_SOLID;
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    uint8_t v = data(cx, cz).wallN[ci - cx * CCELLS][ck - cz * CCELLS];
    if (v == WALL_LOCKED && !unlockedDoors.empty() &&
        unlockedDoors.count(edgeKey(ci, ck, false))) return WALL_DOOR;
    return v;
}
uint8_t World::wallWVal(int ci, int ck) {
    if (!shifted.empty() && shifted.count(edgeKey(ci, ck, true))) return WALL_SOLID;
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    uint8_t v = data(cx, cz).wallW[ci - cx * CCELLS][ck - cz * CCELLS];
    if (v == WALL_LOCKED && !unlockedDoors.empty() &&
        unlockedDoors.count(edgeKey(ci, ck, true))) return WALL_DOOR;
    return v;
}

// A key lies in at most one cell per chunk, and the generator put it on the
// side of that chunk's locked door you can already get to.
bool World::keyAt(int ci, int ck) {
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    ChunkData &d = data(cx, cz);
    if (d.keyI < 0) return false;
    // ...and it stops being there once the door it opens is open, so a key you
    // walked past cannot be picked up after it has nothing left to unlock.
    if (unlockedDoors.count(edgeKey(cx * CCELLS + d.lockI, cz * CCELLS + d.lockK, d.lockWest != 0)))
        return false;
    return ci - cx * CCELLS == d.keyI && ck - cz * CCELLS == d.keyK;
}

// Open one for good. Same two-chunk rebake shiftEdge needs and for the same
// reason: the chunk across the seam draws its half of a shared edge too.
void World::unlockEdge(int ci, int ck, bool west) {
    if (!unlockedDoors.insert(edgeKey(ci, ck, west)).second) return;
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    rebuildChunk(cx, cz);
    int nx = fdiv(west ? ci - 1 : ci, CCELLS), nz = fdiv(west ? ck : ck - 1, CCELLS);
    if (nx != cx || nz != cz) rebuildChunk(nx, nz);
}

// Drop a chunk's baked meshes so streamChunks rebuilds it from the current
// wall values. Everything else about the chunk — its cells, its props — is
// untouched; only the geometry is stale.
void World::rebuildChunk(int cx, int cz) {
    auto &m = layer(qs);
    auto it = m.find(key(cx, cz));
    if (it == m.end()) return;
    for (int i = 0; i < MESH_COUNT; i++)
        if (it->second.meshes[i].vertexCount > 0) {
            UnloadMesh(it->second.meshes[i]);
            it->second.meshes[i] = Mesh{};
        }
    it->second.built = false;
}

void World::shiftEdge(int ci, int ck, bool west) {
    if (!shifted.insert(edgeKey(ci, ck, west)).second) return;   // already shifted
    // The edge sits on the boundary of its own chunk, so the neighbour on the
    // far side draws its half of it too — rebake both or you get a wall that
    // exists from one room and not from the other.
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    rebuildChunk(cx, cz);
    int nx = fdiv(west ? ci - 1 : ci, CCELLS), nz = fdiv(west ? ck : ck - 1, CCELLS);
    if (nx != cx || nz != cz) rebuildChunk(nx, nz);
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
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    return data(cx, cz).elev[ci - cx * CCELLS][ck - cz * CCELLS] * ELEV_UNIT;
}

float World::ceilY(int ci, int ck) {
    // Follow the floor *up*, never down. A raised deck has to carry its ceiling
    // with it or its floor comes through the slab — that is the whole point of
    // this being per cell. A sunken cell is a different thing: a pool basin and
    // a sunken lounge are depressions in the floor of the room they are in, and
    // they keep that room's ceiling. Dropping it with them instead hangs a
    // soffit round every pool in the Poolrooms at 0.6 m below the tile grid,
    // which reads as a broken mesh rather than as architecture. Telling a
    // depression from a genuine lower storey needs something the cell does not
    // store yet; until it does, up only.
    //
    // A cell open to the storey above has no ceiling of its own: its walls
    // run to the top of this storey, where the ones above take over, and the
    // mesher closes the slot between it and a neighbour's ceiling with the
    // bulkhead the soffit code already draws.
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    ChunkData &d = data(cx, cz);
    int li = ci - cx * CCELLS, lk = ck - cz * CCELLS;
    if (storeyH > 0.0f && (d.vflag[li][lk] & VF_OPENUP)) return storeyH;
    return std::max(d.elev[li][lk] * ELEV_UNIT, 0.0f) + wallH;
}

// The surface of a flight. Stepped: groundAt walks you up tread by tread, the
// same 180 mm risers the mesher draws, and the glide in the mover smooths each
// one into the small lift of a real stair. `ramp` gives the nosing line — the
// straight line a handrail follows — for the balustrade's top.
float World::stairY(float x, float z, bool ramp) {
    int ci = cellOf(x), ck = cellOf(z);
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    ChunkData &d = data(cx, cz);
    int li = ci - cx * CCELLS, lk = ck - cz * CCELLS;
    if (!(d.vflag[li][lk] & VF_STAIR) || d.vfeat[li][lk] < 0) return NAN;
    const VertFeat &f = d.feats[d.vfeat[li][lk]];
    float u, v;
    featureLocal(f, cx, cz, x, z, u, v);
    const float H = storeyH;
    const float R = H / 24.0f;                          // one riser: 24 to a storey
    // n risers over a run starting at v0, `going` apart; the i-th tread is at
    // (i+1) risers. The ramp passes through the nosings.
    auto flight = [&](float vv, float v0, float going, int n, float base) {
        if (vv < v0) return base;
        float t = (vv - v0) / going;
        if (ramp) return base + std::min((float)n, t + 0.5f) * R;
        return base + (float)std::min(n, (int)floorf(t) + 1) * R;
    };
    if (f.kind == VK_STAIRWELL) {
        // Lane A climbs 12 risers over 4 m to the half landing; lane B climbs
        // the other 12 back over the same 4 m to the top landing.
        const float G = 4.0f / 12.0f;
        if (v >= 6.0f) return 12 * R;                   // the half landing, both lanes
        if (u < CELL) return flight(v, 2.0f, G, 12, 0.0f);
        if (v < 2.0f) return H;                         // the top landing: the floor above
        return flight(6.0f - v, 0.0f, G, 12, 12 * R);  // lane B, climbing toward -v
    }
    // A straight flight: 24 risers over 8 m, from the approach row to the
    // arrival row of the floor above.
    return flight(v, 2.0f, 8.0f / 24.0f, 24, 0.0f);
}

bool World::manilaAt(int ci, int ck) {
    if (level != 0) return false;
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    int li = ci - cx * CCELLS, lk = ck - cz * CCELLS;
    if (li < MANILA_LO || li > MANILA_HI || lk < MANILA_LO || lk > MANILA_HI) return false;
    return data(cx, cz).manila;
}

bool World::manilaNear(float x, float z, float &rx, float &rz) {
    if (level != 0) return false;
    int pcx = fdiv(cellOf(x), CCELLS), pcz = fdiv(cellOf(z), CCELLS);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        auto &m = layer(qs);
        auto it = m.find(key(pcx + dx, pcz + dz));   // loaded chunks only: never generate from here
        if (it == m.end() || !it->second.manila) continue;
        rx = ((pcx + dx) * CCELLS + MANILA_HI + 1 - 2) * CELL;   // the corner between cells 7 and 8
        rz = ((pcz + dz) * CCELLS + MANILA_HI + 1 - 2) * CELL;
        return true;
    }
    return false;
}

bool World::softAt(int ci, int ck) {
    if (level != 0) return false;
    if (manilaAt(ci, ck)) return false;                  // the room has a wooden floor
    if (abs(ci) <= 10 && abs(ck) <= 10) return false;   // never near where you wake up
    if (ih(ci, ck, (uint32_t)sseed() ^ 0x50F7u) % 523 != 0) return false;
    // not on a stair or at the lip of an opening either: a floor that gives
    // way under you should be a floor
    return !pillarAt(ci, ck) && propAt(ci, ck) == PROP_NONE && floorY(ci, ck) == 0.0f && !vflagAt(ci, ck);
}

float World::softDip(float x, float z) {
    int ci = cellOf(x), ck = cellOf(z);
    if (!softAt(ci, ck)) return 0.0f;
    // Squared falloff rather than linear: it reaches the cell edge at exactly
    // zero *and* with zero slope, so the bowl meets its neighbours without a
    // crease, and the middle is flat enough to stand in.
    float ax = (x - ci * CELL) / (CELL * 0.5f) - 1.0f;
    float az = (z - ck * CELL) / (CELL * 0.5f) - 1.0f;
    float r2 = ax * ax + az * az;
    return r2 >= 1.0f ? 0.0f : SOFT_DEPTH * (1.0f - r2) * (1.0f - r2);
}

bool World::cursedExit(int ci, int ck) {
    return ih(ci, ck, (uint32_t)sseed() ^ 0xC0DEu) % 6 == 0;
}

// A shut-off wheel on a floor-to-ceiling standpipe. Rare enough that finding
// three is a proper errand, and never inside a wall, pillar or furniture.
bool World::valveAt(int ci, int ck) {
    if (level != 3) return false;
    if (ih(ci, ck, (uint32_t)sseed() ^ 0x7A17u) % 149 != 0) return false;
    return !pillarAt(ci, ck) && propAt(ci, ck) == PROP_NONE &&
           wallNVal(ci, ck) != WALL_SOLID && wallWVal(ci, ck) != WALL_SOLID;
}

// Baked contact-shadow tint. The AO strip texture carries the falloff in its
// alpha channel, so wall creases and furniture shadows all share this one colour.
static const Color AO_TINT = { 10, 9, 9, 255 };

// Level 0 has no exit doors. "Exiting Level 0 is only possible by noclipping"
// — so an exit there is a stretch of ordinary wall that is not quite there:
// full geometry, no collision (WALL_EXIT never had any), and a vertex alpha of
// 250 that the world shader reads as "this wallpaper is tearing". It sits in
// the gap between the 0.62 textured cutoff and the 254 relief opt-out, so it
// is textured, unbumped, and matches no other code in the alpha table. A
// cursed one is 247: the Red Rooms are behind that wall, and its tears show
// crimson rather than light.
static Color noclipCol(bool cursed) {
    // Alpha, not colour, says which kind: the Red Rooms' tint (MB::tint) is
    // multiplied into everything near a cursed wall, so a cursed wall wears
    // the same red as its neighbours and colour could not tell the two apart.
    return { 255, 255, 255, (unsigned char)(cursed ? 247 : 250) };
}

// A rounded contact shadow shared by props and pillars. Only the footprint
// is solid; the skirt samples the AO gradient down to zero at its outer edge.
static void addContactShadow(MB &ao, float pcx, float pcz, float ey, float rot,
                             float hx2, float hz2) {
    float ca = cosf(rot), sa = sinf(rot);
    const float S = 0.24f;                    // how far the falloff reaches
    const float d = S * 0.7071f;              // the corner, cut across
    const Vector3 up = { 0, 1, 0 };
    auto P = [&](float lx, float lz) {
        return Vector3{ pcx + lx * ca - lz * sa, ey + 0.006f, pcz + lx * sa + lz * ca };
    };
    // core, right under the piece: darkest end of the gradient
    ao.quad(P(-hx2,-hz2), P(hx2,-hz2), P(hx2,hz2), P(-hx2,hz2), up,
            {0,0},{1,0},{1,0},{0,0}, AO_TINT);
    // four skirts fading outward. Each inner edge runs so the quad
    // stays wound the same way round as the core.
    const float sd[4][6] = {
        {  hx2,-hz2, -hx2,-hz2,  0,  -S },
        {  hx2, hz2,  hx2,-hz2,  S,   0 },
        { -hx2, hz2,  hx2, hz2,  0,   S },
        { -hx2,-hz2, -hx2, hz2, -S,   0 },
    };
    for (auto &e : sd)
        ao.quad(P(e[0], e[1]), P(e[2], e[3]),
                P(e[2] + e[4], e[3] + e[5]), P(e[0] + e[4], e[1] + e[5]), up,
                {0,0},{1,0},{1,1},{0,1}, AO_TINT);
    // and the corners, so the skirt closes instead of leaving notches
    const float cn[4][4] = {
        {  hx2,  hz2,  1,  1 }, { -hx2,  hz2, -1,  1 },
        { -hx2, -hz2, -1, -1 }, {  hx2, -hz2,  1, -1 },
    };
    for (auto &c2 : cn) {
        Vector3 inner = P(c2[0], c2[1]);
        Vector3 pxv = P(c2[0] + c2[2] * S, c2[1]);
        Vector3 pmv = P(c2[0] + c2[2] * d, c2[1] + c2[3] * d);
        Vector3 pzv = P(c2[0], c2[1] + c2[3] * S);
        bool xFirst = (c2[2] * c2[3]) > 0;    // keeps the winding consistent
        Vector3 a1 = xFirst ? pxv : pzv, b1 = xFirst ? pzv : pxv;
        ao.tri(inner, a1, pmv, up, {0,0},{0,1},{1,1}, AO_TINT);
        ao.tri(inner, pmv, b1, up, {0,0},{1,1},{0,1}, AO_TINT);
    }
}

// rotated prop box: 4 sides + top, one UV region for sides, another for the top
static void addPropBox(MB &mb, float cx, float cz, float yaw, float hx, float hz, float y0, float y1,
                       float u0, float v0, float u1, float v1,
                       float tu0, float tv0, float tu1, float tv1, Color tint = WHITE, float bevel = 0) {
    float ca = cosf(yaw), sa = sinf(yaw);
    auto pt = [&](float lx, float lz) { return Vector3{ cx + lx * ca - lz * sa, 0, cz + lx * sa + lz * ca }; };
    bevel=std::min(bevel,std::min(std::min(hx,hz)*0.25f,(y1-y0)*0.25f));
    if (bevel>0.0001f) {
        Vector2 outline[8]={{-hx+bevel,-hz},{hx-bevel,-hz},{hx,-hz+bevel},{hx,hz-bevel},
                            {hx-bevel,hz},{-hx+bevel,hz},{-hx,hz-bevel},{-hx,-hz+bevel}};
        auto topUV=[&](Vector2 p) {return Vector2{tu0+(p.x/hx+1)*0.5f*(tu1-tu0),tv0+(p.y/hz+1)*0.5f*(tv1-tv0)};};
        for(int i=0;i<8;++i) {
            Vector2 a=outline[i],b=outline[(i+1)%8];
            Vector3 aa=pt(a.x,a.y),bb=pt(b.x,b.y);
            Vector3 n{bb.z-aa.z,0,aa.x-bb.x};float len=sqrtf(n.x*n.x+n.z*n.z);n.x/=len;n.z/=len;
            Vector3 at=pt(a.x*(hx-bevel)/hx,a.y*(hz-bevel)/hz);
            Vector3 bt=pt(b.x*(hx-bevel)/hx,b.y*(hz-bevel)/hz);
            mb.quad({aa.x,y0,aa.z},{bb.x,y0,bb.z},{bb.x,y1-bevel,bb.z},{aa.x,y1-bevel,aa.z},n,
                    {u0,v1},{u1,v1},{u1,v0},{u0,v0},tint);
            mb.quad({aa.x,y1-bevel,aa.z},{bb.x,y1-bevel,bb.z},{bt.x,y1,bt.z},{at.x,y1,at.z},
                    {n.x*0.7071f,0.7071f,n.z*0.7071f},topUV(a),topUV(b),topUV(b),topUV(a),tint);
            mb.tri({cx,y1,cz},{at.x,y1,at.z},{bt.x,y1,bt.z},{0,1,0},
                   {(tu0+tu1)*0.5f,(tv0+tv1)*0.5f},topUV(a),topUV(b),tint);
        }
        return;
    }
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

// A plain axis-aligned solid in a flat colour. Samples the props atlas' blank
// metal corner, so the tint is the whole look — pipework, collars, standpipes.
static void addSolidBox(MB &mb, float x0, float y0, float z0, float x1, float y1, float z1, Color t) {
    const Vector2 u = { 0.375f, 0.75f };
    mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{0,0,-1},u,u,u,u,t);
    mb.quad({x1,y0,z1},{x0,y0,z1},{x0,y1,z1},{x1,y1,z1},{0,0,1},u,u,u,u,t);
    mb.quad({x0,y0,z1},{x0,y0,z0},{x0,y1,z0},{x0,y1,z1},{-1,0,0},u,u,u,u,t);
    mb.quad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0},{1,0,0},u,u,u,u,t);
    mb.quad({x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1},{0,1,0},u,u,u,u,t);
    mb.quad({x0,y0,z1},{x1,y0,z1},{x1,y0,z0},{x0,y0,z0},{0,-1,0},u,u,u,u,t);
}

// skip bits: 1 = -z face, 2 = +z face, 4 = -x face, 8 = +x face. Wall runs overlap
// their neighbours by WT so corners close, which buries the end caps inside the
// next box — and an end cap meeting the neighbour's front face at exactly the same
// depth z-fights into a vertical seam. Skipping buried caps removes the seam.
// How many metres of wall one texture tile spans vertically. 3 everywhere the
// wall texture is a repeating pattern; Level 1's concrete instead runs floor
// to ceiling exactly once, because it carries things that live at a height —
// the damp band and its tide line at the foot of the wall, and the pour joints
// between concrete lifts — and a 3 m repeat on a 4.2 m wall would draw a
// second tide line under the ceiling. Set per chunk bake by ensureMesh.
static float gWallV = 3.0f;
// The wallpaper's V at a height. One tile is one wall's height, with the
// baseboard, the damp and the grime baked in at the heights they live at — so a
// wall that climbs past it (up a stair shaft, round an opening, to the floor
// above) must not simply repeat, or a second baseboard runs round the shaft at
// three metres. Above the tile it carries on from the tile's clean middle, a
// whole number of pattern repeats down, so the chevrons do not jump.
// Only on a storeyed level: everywhere else a wall taller than its tile is a
// deliberate repeat (the Poolrooms' tile, Level 1's docks) and stays one.
static bool gTallPaper = false;
static float wallV(float y) { return y <= gWallV + 1e-4f ? 1 - y / gWallV : 1 - (y - gWallV * 0.5f) / gWallV; }
// A face over a void — an upper storey's wall seen from the shaft below it —
// stands on no floor, so it must show no baseboard at all. It takes the
// tile's clean middle, 0.75 m to 2.25 m, repeating by whole chevrons, which
// means splitting the face wherever that band wraps.
static void voidFace(MB &mb, Vector3 a0, Vector3 a1, Vector3 n, float ua, float ub, float y0, float y1, Color w) {
    auto T = [](float y) { return 0.75f + fmodf(y + 0.57f + 150.0f, 1.5f); };
    float y = y0;
    while (y < y1 - 1e-4f) {
        float next = y + (2.25f - T(y));             // where the band wraps
        float yb = std::min(y1, next);
        float va = 1 - T(y) / gWallV, vb = 1 - (T(y) + (yb - y)) / gWallV;
        mb.quad({a0.x,y,a0.z},{a1.x,y,a1.z},{a1.x,yb,a1.z},{a0.x,yb,a0.z}, n, {ua,va},{ub,va},{ub,vb},{ua,vb}, w);
        y = yb;
    }
}
// voidFaces: which of the four faces look into a hole (same bits as skip).
static void addBoxSides(MB &mb, float x0, float y0, float z0, float x1, float y1, float z1,
                        bool bottomFace = false, int skip = 0, Color w = WHITE, int voidFaces = 0) {
    if (gTallPaper && voidFaces) {
        if (!(skip & 1) && (voidFaces & 1)) voidFace(mb, {x0,0,z0}, {x1,0,z0}, {0,0,-1}, x0/3, x1/3, y0, y1, w);
        if (!(skip & 2) && (voidFaces & 2)) voidFace(mb, {x1,0,z1}, {x0,0,z1}, {0,0,1}, x1/3, x0/3, y0, y1, w);
        if (!(skip & 4) && (voidFaces & 4)) voidFace(mb, {x0,0,z1}, {x0,0,z0}, {-1,0,0}, z1/3, z0/3, y0, y1, w);
        if (!(skip & 8) && (voidFaces & 8)) voidFace(mb, {x1,0,z0}, {x1,0,z1}, {1,0,0}, z0/3, z1/3, y0, y1, w);
        addBoxSides(mb, x0, y0, z0, x1, y1, z1, bottomFace, skip | voidFaces, w, 0);
        return;
    }
    if (!gTallPaper) {
        float va = 1 - y0 / gWallV, vb = 1 - y1 / gWallV;
        if (!(skip & 1))
            mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{0,0,-1},{x0/3,va},{x1/3,va},{x1/3,vb},{x0/3,vb},w);
        if (!(skip & 2))
            mb.quad({x1,y0,z1},{x0,y0,z1},{x0,y1,z1},{x1,y1,z1},{0,0,1},{x1/3,va},{x0/3,va},{x0/3,vb},{x1/3,vb},w);
        if (!(skip & 4))
            mb.quad({x0,y0,z1},{x0,y0,z0},{x0,y1,z0},{x0,y1,z1},{-1,0,0},{z1/3,va},{z0/3,va},{z0/3,vb},{z1/3,vb},w);
        if (!(skip & 8))
            mb.quad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0},{1,0,0},{z0/3,va},{z1/3,va},{z1/3,vb},{z0/3,vb},w);
        if (bottomFace)
            mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},{0,-1,0},{x0/3,z0/3},{x1/3,z0/3},{x1/3,z1/3},{x0/3,z1/3},w);
        return;
    }
    if (y0 < gWallV - 0.01f && y1 > gWallV + 0.01f) {   // straddles the tile: split it there
        addBoxSides(mb, x0, y0, z0, x1, gWallV, z1, bottomFace, skip, w, 0);
        addBoxSides(mb, x0, gWallV, z0, x1, y1, z1, false, skip, w, 0);
        return;
    }
    float va = wallV(y0), vb = wallV(y1);
    if (y0 >= gWallV - 0.01f) { va = 1 - (y0 - gWallV * 0.5f) / gWallV; vb = 1 - (y1 - gWallV * 0.5f) / gWallV; }
    if (!(skip & 1))
        mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{0,0,-1},{x0/3,va},{x1/3,va},{x1/3,vb},{x0/3,vb},w);
    if (!(skip & 2))
        mb.quad({x1,y0,z1},{x0,y0,z1},{x0,y1,z1},{x1,y1,z1},{0,0,1},{x1/3,va},{x0/3,va},{x0/3,vb},{x1/3,vb},w);
    if (!(skip & 4))
        mb.quad({x0,y0,z1},{x0,y0,z0},{x0,y1,z0},{x0,y1,z1},{-1,0,0},{z1/3,va},{z0/3,va},{z0/3,vb},{z1/3,vb},w);
    if (!(skip & 8))
        mb.quad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0},{1,0,0},{z0/3,va},{z1/3,va},{z1/3,vb},{z0/3,vb},w);
    if (bottomFace)
        mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},{0,-1,0},{x0/3,z0/3},{x1/3,z0/3},{x1/3,z1/3},{x0/3,z1/3},w);
}

// ---- rails. A guard round an opening, or up the open side of a flight, built
// the way everything else here is: a knee wall papered like the partitions,
// under a stained timber cap. The top runs from top0 to top1 along the edge,
// so the same builder does the level rail round a hole and the balustrade that
// climbs with a stair's nosing line. `bottom` closes the underside, which is
// only ever seen looking up through the opening it guards.
static const float RAIL_H = 1.0f;            // top of the cap above what you stand on
static const Color RAIL_CAP = { 104, 80, 46, 254 };
static void addRailRun(MB &wa, MB &pr, float ax, float az, float bx, float bz,
                       float base, float top0, float top1, bool bottom, int voidSide = 0) {
    float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz);
    if (len < 1e-4f) return;
    float ux = dx / len, uz = dz / len, nx = -uz, nz = ux;
    const float cap = 0.055f, capW = RAIL_T + 0.02f;
    bool alongX = fabsf(ux) > 0.5f;
    auto P = [&](float a, float side, float y) { return Vector3{ ax + ux * a + nx * side, y, az + uz * a + nz * side }; };
    auto U = [&](float a) { return (alongX ? ax + ux * a : az + uz * a) / 3.0f; };
    auto V = [&](float y) { return 1.0f - y / gWallV; };
    float w0 = top0 - cap, w1 = top1 - cap;   // the paper stops under the cap
    for (int sg = -1; sg <= 1; sg += 2) {
        float sd = sg * RAIL_T;
        // The face over a hole is the top of the bulkhead below it, not a
        // wall standing on a floor: no baseboard, and the paper picks up
        // where the bulkhead's left off (see the soffit in ensureMesh).
        auto Vs = [&](float y) { return sg == voidSide ? 1.0f - (y + 2.0f) / gWallV : V(y); };
        wa.quad(P(0, sd, base), P(len, sd, base), P(len, sd, w1), P(0, sd, w0), { nx * sg, 0, nz * sg },
                { U(0), Vs(base) }, { U(len), Vs(base) }, { U(len), Vs(w1) }, { U(0), Vs(w0) }, WHITE);
    }
    for (int e = 0; e < 2; e++) {   // the two ends, square across
        float a = e ? len : 0, w = e ? w1 : w0, sg = e ? 1.0f : -1.0f;
        wa.quad(P(a, -RAIL_T, base), P(a, RAIL_T, base), P(a, RAIL_T, w), P(a, -RAIL_T, w), { ux * sg, 0, uz * sg },
                { 0, V(base) }, { 0.05f, V(base) }, { 0.05f, V(w) }, { 0, V(w) }, WHITE);
    }
    if (bottom)
        wa.quad(P(0, -RAIL_T, base), P(len, -RAIL_T, base), P(len, RAIL_T, base), P(0, RAIL_T, base), { 0, -1, 0 },
                { U(0), 0 }, { U(len), 0 }, { U(len), 0.05f }, { U(0), 0.05f }, WHITE);
    // The cap: a timber slab a little proud of both faces, sloping with the top.
    const Vector2 m = { 0.375f, 0.75f };
    float slope = (top1 - top0) / len, sn = 1.0f / sqrtf(1 + slope * slope);
    Vector3 up = { -ux * slope * sn, sn, -uz * slope * sn };
    pr.quad(P(0, -capW, top0), P(len, -capW, top1), P(len, capW, top1), P(0, capW, top0), up, m, m, m, m, RAIL_CAP);
    for (int sg = -1; sg <= 1; sg += 2) {
        float sd = sg * capW;
        pr.quad(P(0, sd, w0), P(len, sd, w1), P(len, sd, top1), P(0, sd, top0), { nx * sg, 0, nz * sg }, m, m, m, m, RAIL_CAP);
    }
    pr.quad(P(0, -capW, w0), P(len, -capW, w1), P(len, capW, w1), P(0, capW, w0), { -up.x, -up.y, -up.z }, m, m, m, m, RAIL_CAP);
    for (int e = 0; e < 2; e++) {
        float a = e ? len : 0, w = e ? w1 : w0, t = e ? top1 : top0, sg = e ? 1.0f : -1.0f;
        pr.quad(P(a, -capW, w), P(a, capW, w), P(a, capW, t), P(a, -capW, t), { ux * sg, 0, uz * sg }, m, m, m, m, RAIL_CAP);
    }
}

// ---- stairs. One step of a flight: a solid block from the floor to its tread,
// so the flight is a stair and not a ramp of paper-thin treads, with carpet on
// the tread and up the riser — Level 0's carpet goes wherever the floor goes —
// and an aluminium nosing on the edge you would catch your heel on. Built in
// the feature's local frame (u across, v along, y up) through `to`, which is
// one of four rotations, so the normals come through unmirrored.
static const Color NOSING_COL = { 168, 160, 138, 254 };
template <class ToWorld>
static void addStep(MB &fl, MB &pr, ToWorld to, float u0, float u1, float va, float vb, float y0, float y1,
                    bool risesPlusV, bool sides) {
    // va..vb along the rise; the riser is on the downhill face
    float vr = risesPlusV ? va : vb;            // where the riser stands
    float vs = risesPlusV ? -1.0f : 1.0f;       // which way it faces, in v
    auto W = [&](float u, float y, float v) { return to(u, y, v); };
    auto uvOf = [](Vector3 p) { return Vector2{ p.x / 2, p.z / 2 }; };
    Vector3 t0 = W(u0, y1, va), t1 = W(u1, y1, va), t2 = W(u1, y1, vb), t3 = W(u0, y1, vb);
    fl.quad(t0, t1, t2, t3, { 0, 1, 0 }, uvOf(t0), uvOf(t1), uvOf(t2), uvOf(t3), WHITE);
    Vector3 n0 = W(0, 0, 0), nv = W(0, 0, vs);
    Vector3 rn = { nv.x - n0.x, 0, nv.z - n0.z };
    Vector3 r0 = W(u0, y0, vr), r1 = W(u1, y0, vr), r2 = W(u1, y1, vr), r3 = W(u0, y1, vr);
    fl.quad(r0, r1, r2, r3, rn, { u0 / 2, y0 / 2 }, { u1 / 2, y0 / 2 }, { u1 / 2, y1 / 2 }, { u0 / 2, y1 / 2 }, WHITE);
    if (sides) {
        for (int e = 0; e < 2; e++) {
            float u = e ? u1 : u0;
            Vector3 nu = W(e ? 1.0f : -1.0f, 0, 0);
            Vector3 sn = { nu.x - n0.x, 0, nu.z - n0.z };
            Vector3 s0 = W(u, y0, va), s1 = W(u, y0, vb), s2 = W(u, y1, vb), s3 = W(u, y1, va);
            fl.quad(s0, s1, s2, s3, sn, { va / 2, y0 / 2 }, { vb / 2, y0 / 2 }, { vb / 2, y1 / 2 }, { va / 2, y1 / 2 }, WHITE);
        }
    }
    // the nosing: 40 mm of aluminium over the front edge of the tread
    if (y1 > 0.01f) {
        float a = vr, b = vr - vs * 0.04f;
        Vector3 q0 = W(u0 + 0.02f, y1 - 0.02f, std::min(a, b)), q1 = W(u1 - 0.02f, y1 + 0.004f, std::max(a, b));
        addSolidBox(pr, std::min(q0.x, q1.x), y1 - 0.02f, std::min(q0.z, q1.z),
                    std::max(q0.x, q1.x), y1 + 0.004f, std::max(q0.z, q1.z), NOSING_COL);
    }
}

// Where a piece of furniture goes, and the handful of numbers that make each
// one of a kind slightly different from the next.
struct PropSite {
    float cx, cz;    // cell centre, in world metres
    float floorY;    // the floor under this cell — furniture stands on it
    float rot;       // yaw, from ChunkData::propRot
    int gi, gk;      // global cell coordinates: the seed for this piece's variations
};

// Build one piece of furniture into a chunk's meshes. Most of a prop is boxes
// in the props mesh; the collapsed-tile prop also cuts a hole in the ceiling
// mesh, and every piece lays a contact shadow into the AO mesh.
//
// Each piece is deliberately a handful of boxes rather than a model: everything
// in this game is generated, and furniture that reads correctly at corridor
// range is worth far more than furniture that reads correctly close up.
static void addProp(uint8_t kind, const PropSite &site, unsigned seed, int level,
                    MB &pr, MB &ce, MB &ao) {
    const float pcx = site.cx, pcz = site.cz, rot = site.rot, ey = site.floorY;
    uint32_t h = ih(site.gi, site.gk, seed ^ 0xB0B5u);
    float r1 = (h & 0xFF) / 255.0f, r2 = ((h >> 8) & 0xFF) / 255.0f, r3 = ((h >> 16) & 0xFF) / 255.0f;
    // UV regions of the prop atlas
    const float CU0=0.01f, CV0=0.02f, CU1=0.24f, CV1=0.98f;       // cardboard
    const float FU0=0.26f, FV0=0.02f, FU1=0.49f, FV1=0.48f;       // cabinet front
    const float MU0=0.26f, MV0=0.52f, MU1=0.49f, MV1=0.98f;       // plain metal
    enum class Surface { Metal, Wood, Fabric, Cardboard };
    float ca = cosf(rot), sa = sinf(rot);
    // rotated sub-box placed relative to the prop centre
    auto part = [&](float ox, float oz, float hx2, float hz2, float y0, float y1,
                    Surface surface, Color tint) {
        float u0=MU0,v0=MV0,u1=MU1,v1=MV1;
        if (surface==Surface::Wood) {u0=0.51f;v0=0.02f;u1=0.99f;v1=0.48f;}
        if (surface==Surface::Fabric) {u0=0.51f;v0=0.52f;u1=0.99f;v1=0.98f;}
        if (surface==Surface::Cardboard) {u0=CU0;v0=CV0;u1=CU1;v1=CV1;}
        addPropBox(pr, pcx+ox*ca-oz*sa, pcz+ox*sa+oz*ca, rot, hx2,hz2,y0,y1,
                   u0,v0,u1,v1,u0,v0,u1,v1,tint,
                   surface==Surface::Fabric ? 0.016f : surface==Surface::Wood ? 0.006f : 0.0f);
    };
    auto roundPart = [&](float ox,float oz,float r0,float r1,float y0,float y1,Color tint) {
        const Vector2 uv{0.375f,0.75f};
        float cx=pcx+ox*ca-oz*sa,cz=pcz+ox*sa+oz*ca;
        for (int i=0;i<16;++i) {
            float a=TAU*i/16,b=TAU*(i+1)/16;
            Vector3 p0{cx+r0*cosf(a),y0,cz+r0*sinf(a)},p1{cx+r0*cosf(b),y0,cz+r0*sinf(b)};
            Vector3 p2{cx+r1*cosf(b),y1,cz+r1*sinf(b)},p3{cx+r1*cosf(a),y1,cz+r1*sinf(a)};
            float ny=(r0-r1)/std::max(0.001f,y1-y0),inv=1/sqrtf(1+ny*ny);
            pr.quad(p0,p1,p2,p3,{cosf((a+b)/2)*inv,ny*inv,sinf((a+b)/2)*inv},uv,uv,uv,uv,tint);
            pr.tri({cx,y1,cz},p3,p2,{0,1,0},uv,uv,uv,tint);
        }
    };
    auto blob = [&](float hx, float hz) {
        addContactShadow(ao, pcx, pcz, ey, rot, hx, hz);
    };
    switch (kind) {
    case PROP_BOXES: {   // box stack — on LEVEL FUN they're wrapped like presents,
                // and the packing tape reads as ribbon
        blob(0.40f, 0.40f);
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
    case PROP_CABINET:     // filing cabinet
        blob(0.34f, 0.42f);
        addPropBox(pr, pcx, pcz, rot, 0.26f, 0.34f, ey, ey + 1.32f,
                   FU0, FV0, FU1, FV1, MU0, MV0, MU1, MV1);
        for (int i=0;i<3;++i)
            part(0,0.343f,0.075f,0.008f,ey+0.23f+i*0.40f,ey+0.25f+i*0.40f,Surface::Metal,{187,191,186,254});
        break;
    case PROP_TABLE: {   // folding table
        blob(0.58f, 0.40f);
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
    case PROP_FALLEN_TILE: {   // collapsed ceiling: dark hole above, tile leaning below, debris
        // 2.994 is just under a 3 m ceiling. Only Level 0 generates this prop,
        // and Level 0 is the 3 m one — see LEVELS in levels.cpp.
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
    case PROP_COUCH: {   // couch: mustard upholstery gone grey, facing nothing in particular
        blob(0.74f, 0.48f);
        Color uph = { 172, 152, 96, 255 };
        part(0, 0.10f, 0.78f, 0.42f, ey + 0.16f, ey + 0.44f, Surface::Fabric, uph);   // seat
        part(0, -0.36f, 0.78f, 0.14f, ey + 0.16f, ey + 0.92f, Surface::Fabric, uph);  // backrest
        part(-0.64f, 0.06f, 0.14f, 0.46f, ey, ey + 0.62f, Surface::Fabric, uph);      // arms
        part( 0.64f, 0.06f, 0.14f, 0.46f, ey, ey + 0.62f, Surface::Fabric, uph);
        part(0, 0.10f, 0.74f, 0.38f, ey, ey + 0.16f, Surface::Fabric, Color{ 120, 106, 70, 255 });
        // Three separate seat cushions and piping, inside the same collision box.
        for (int i=-1;i<=1;++i) {
            part(i*0.41f,0.12f,0.196f,0.34f,ey+0.44f,ey+0.49f,Surface::Fabric,uph);
            part(i*0.41f,0.454f,0.192f,0.006f,ey+0.452f,ey+0.461f,Surface::Fabric,{213,191,131,255});
        }
        break;
    }
    case PROP_ARMOIRE:     // armoire: a wardrobe looming where no bedroom is
        blob(0.54f, 0.46f);
        part(0, 0, 0.44f, 0.36f, ey, ey + 1.78f, Surface::Wood, Color{ 118, 82, 58, 255 });
        part(0, 0, 0.48f, 0.40f, ey + 1.78f, ey + 1.90f, Surface::Wood, Color{ 92, 63, 44, 255 });  // cornice
        part(0, 0.37f, 0.015f, 0.015f, ey + 0.85f, ey + 1.0f, Surface::Metal, Color{ 190, 170, 110, 255 }); // handles
        for (int i=-1;i<=1;i+=2)
            part(i*0.218f,0.362f,0.193f,0.003f,ey+0.15f,ey+1.67f,Surface::Wood,{155,113,79,255});
        break;
    case PROP_LAMP:     // floor lamp, shade askew, never lit
        blob(0.22f, 0.22f);
        part(0, 0, 0.14f, 0.14f, ey, ey + 0.05f, Surface::Metal, Color{ 66, 62, 60, 255 });
        part(0, 0, 0.025f, 0.025f, ey, ey + 1.34f, Surface::Metal, Color{ 66, 62, 60, 255 });
        roundPart(0.05f,0,0.17f,0.10f,ey+1.30f,ey+1.60f,{214,190,142,254});
        break;
    case PROP_NIGHTSTAND:     // nightstand, nowhere near a bed. usually.
        blob(0.36f, 0.36f);
        part(0, 0, 0.26f, 0.26f, ey, ey + 0.55f, Surface::Wood, Color{ 126, 90, 62, 255 });
        part(0, 0, 0.30f, 0.30f, ey + 0.55f, ey + 0.60f, Surface::Wood, Color{ 104, 74, 50, 255 });
        break;
    case PROP_BED: {   // bed: bare stained mattress, headboard against nothing
        blob(0.60f, 1.02f);
        Color wd = { 110, 78, 54, 255 };
        part(0, 0, 0.52f, 0.92f, ey + 0.12f, ey + 0.26f, Surface::Wood, wd);          // frame
        part(0, 0.04f, 0.48f, 0.86f, ey + 0.26f, ey + 0.46f, Surface::Fabric, Color{ 216, 208, 188, 255 }); // mattress
        part(0, -0.97f, 0.52f, 0.05f, ey, ey + 0.95f, Surface::Wood, wd);             // headboard
        break;
    }
    case PROP_PARTY_TABLE: {  // party table: paper cloth, a cake nobody cut, cups nobody drank
        blob(0.52f, 0.52f);
        float ty = 0.74f;
        uint32_t th = ih(site.gi, site.gk, seed ^ 0xCAFEu);
        Color cloth = PARTY[th % 5];
        part(0, 0, 0.55f, 0.55f, ey + ty - 0.05f, ey + ty, Surface::Fabric, cloth);
        for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2)
            part(lx * 0.44f, lz * 0.44f, 0.035f, 0.035f, ey, ey + ty - 0.05f,
                 Surface::Metal, Color{ 120, 118, 112, 255 });
        part(0, 0, 0.17f, 0.17f, ey + ty, ey + ty + 0.16f, Surface::Metal, Color{ 238, 232, 220, 255 });   // cake
        part(0, 0, 0.11f, 0.11f, ey + ty + 0.16f, ey + ty + 0.26f, Surface::Metal, Color{ 232, 152, 172, 255 });
        part(0, 0, 0.013f, 0.013f, ey + ty + 0.26f, ey + ty + 0.37f, Surface::Metal, Color{ 240, 226, 172, 255 }); // candle
        {   // paper cups set out around the cake, in party colours
            int ncup = 3 + (th % 4);
            for (int c = 0; c < ncup; c++) {
                uint32_t ch = th * 2654435761u + (uint32_t)c * 40503u;
                float ang = (ch & 0xFFFF) / 65535.0f * TAU;
                float rad = 0.30f + ((ch >> 16) & 0xFF) / 255.0f * 0.15f;
                part(cosf(ang) * rad, sinf(ang) * rad, 0.04f, 0.04f,
                     ey + ty, ey + ty + 0.09f, Surface::Metal, PARTY[(ch >> 5) % 5]);
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
    case PROP_VENDING: {  // vending machine: still stocked, still humming, takes doubloons
        blob(0.52f, 0.44f);
        part(0, 0, 0.44f, 0.36f, ey, ey + 1.85f, Surface::Metal, Color{ 148, 152, 158, 255 });
        auto ptv2 = [&](float lx, float ly2, float lz) {
            return Vector3{ pcx + lx * ca - lz * sa, ly2, pcz + lx * sa + lz * ca };
        };
        Color panel = { 66, 90, 122, 60 };   // emissive front: soft cold glow
        pr.quad(ptv2(-0.28f, ey + 0.55f, -0.375f), ptv2(0.28f, ey + 0.55f, -0.375f),
                ptv2(0.28f, ey + 1.68f, -0.375f), ptv2(-0.28f, ey + 1.68f, -0.375f),
                { sa, 0, -ca }, {0,1},{1,1},{1,0},{0,0}, panel);
        break;
    }
    case PROP_DESK: {  // office desk: chair shoved back, monitor long dead. someone worked here
        blob(0.72f, 0.52f);
        Color wd = { 104, 80, 56, 255 };
        part(0, -0.12f, 0.62f, 0.34f, ey + 0.70f, ey + 0.74f, Surface::Wood, wd);      // desktop
        part(-0.46f, -0.12f, 0.14f, 0.30f, ey, ey + 0.70f, Surface::Wood, wd);         // pedestals
        part( 0.46f, -0.12f, 0.14f, 0.30f, ey, ey + 0.70f, Surface::Wood, wd);
        part(0.08f, -0.22f, 0.19f, 0.035f, ey + 0.76f, ey + 1.08f, Surface::Metal, Color{ 30, 30, 34, 255 }); // monitor
        part(0.08f, -0.14f, 0.06f, 0.06f, ey + 0.74f, ey + 0.77f, Surface::Metal, Color{ 38, 38, 42, 255 });  // its foot
        part(-0.30f, -0.14f, 0.11f, 0.08f, ey + 0.74f, ey + 0.765f, Surface::Metal, Color{ 200, 196, 186, 255 }); // papers
        part(0.02f + r1 * 0.1f, 0.44f, 0.20f, 0.20f, ey + 0.40f, ey + 0.46f, Surface::Fabric, Color{ 52, 50, 54, 255 }); // chair seat
        part(0.02f + r1 * 0.1f, 0.62f, 0.20f, 0.04f, ey + 0.46f, ey + 0.96f, Surface::Fabric, Color{ 52, 50, 54, 255 }); // backrest
        part(0.02f + r1 * 0.1f, 0.44f, 0.035f, 0.035f, ey, ey + 0.40f, Surface::Metal, Color{ 72, 72, 76, 255 });        // post
        break;
    }
    case PROP_SHELVING: {  // steel shelving, half-emptied in a hurry
        blob(0.68f, 0.32f);
        Color mt = { 132, 136, 142, 255 };
        for (int s2 = 0; s2 <= 3; s2++)
            part(0, 0, 0.60f, 0.24f, ey + 0.08f + s2 * 0.55f, ey + 0.12f + s2 * 0.55f, Surface::Metal, mt);
        // corner posts, not full-depth panels — side-on you see *through* the rack
        for (int ux = -1; ux <= 1; ux += 2) for (int uz = -1; uz <= 1; uz += 2)
            part(ux * 0.575f, uz * 0.215f, 0.03f, 0.03f, ey, ey + 1.80f, Surface::Metal, mt);
        part(-0.25f, 0.0f, 0.16f, 0.16f, ey + 0.12f, ey + 0.44f, Surface::Cardboard, Color{ 168, 138, 100, 255 });  // what's left
        part( 0.30f, 0.02f, 0.14f, 0.14f, ey + 0.67f, ey + 0.94f, Surface::Cardboard, Color{ 150, 122, 88, 255 });
        if (r2 > 0.4f)
            part(-0.06f, -0.02f, 0.12f, 0.12f, ey + 1.22f, ey + 1.44f, Surface::Cardboard, Color{ 174, 146, 106, 255 });
        break;
    }
    case PROP_COOLER: {  // water cooler. the water is not almond
        blob(0.30f, 0.30f);
        part(0, 0, 0.19f, 0.19f, ey, ey + 0.94f, Surface::Metal, Color{ 204, 206, 210, 255 });   // body
        roundPart(0,0,0.10f,0.125f,ey+0.94f,ey+1.24f,{150,186,214,254});
        roundPart(0,0,0.125f,0.10f,ey+1.24f,ey+1.28f,{150,186,214,254});
        for (int i=0;i<3;++i) roundPart(0,0,0.128f,0.128f,ey+1.01f+i*0.07f,ey+1.025f+i*0.07f,{179,206,223,254});
        part(0, 0.205f, 0.05f, 0.02f, ey + 0.58f, ey + 0.66f, Surface::Metal, Color{ 88, 90, 94, 255 });  // tap
        break;
    }
    case PROP_PLANT: {  // potted plant. still green. nobody waters it
        blob(0.26f, 0.26f);
        part(0, 0, 0.17f, 0.17f, ey, ey + 0.09f, Surface::Metal, Color{ 120, 70, 48, 255 });    // saucer
        roundPart(0,0,0.105f,0.145f,ey+0.02f,ey+0.30f,{146,88,58,254});
        roundPart(0,0,0.153f,0.153f,ey+0.285f,ey+0.32f,{172,104,70,254});
        part(0, 0, 0.032f, 0.032f, ey + 0.32f, ey + 0.88f, Surface::Metal, Color{ 76, 66, 44, 255 });   // stem
        for (int i=0;i<10;++i) {
            float angle=rot+i*2.39996f,yy=ey+0.58f+(i%4)*0.13f;
            float dx=cosf(angle)*0.28f,dz=sinf(angle)*0.28f;
            Vector3 base{pcx,yy,pcz},tip{pcx+dx,yy+0.17f,pcz+dz};
            Vector3 left{pcx+dx*0.55f-dz*0.20f,yy+0.14f,pcz+dz*0.55f+dx*0.20f};
            Vector3 right{pcx+dx*0.55f+dz*0.20f,yy+0.14f,pcz+dz*0.55f-dx*0.20f};
            Vector2 uv{0.375f,0.75f};
            pr.tri(base,left,tip,{0,1,0},uv,uv,uv,{64,111,53,254});
            pr.tri(base,tip,right,{0,1,0},uv,uv,uv,{48,88,39,254});
        }
        break;
    }
    }
}

// Spalled concrete with the rebar showing — "exposed rebar" is in the first
// line of Level 1's description, and a warehouse of poured concrete gets it
// wherever water has got at the steel and the cover has blown off. A ragged
// cavity pressed just off the face (two fans, the deeper one darker), and the
// bars themselves as real geometry standing proud of it: two verticals and a
// tie or two across, rust-coloured. The face is axis-aligned, so `u` (the
// horizontal tangent) is always x or z and the bars stay boxes.
static void addSpall(MB &fx, MB &pr, Vector3 c, Vector3 n, Vector3 u, float rw, float rh, uint32_t h) {
    Rng r(((uint64_t)h << 1) ^ 0x5BA11ULL);
    const Vector2 uv = { 0.375f, 0.75f };                // the fixtures atlas's plain metal, darkened
    auto ring = [&](float scale, float off, Color col) {
        const int N = 11;
        Vector3 pts[N];
        for (int i = 0; i < N; i++) {
            float a = TAU * i / N, k = scale * (0.62f + 0.38f * r.f01());
            float du = cosf(a) * rw * k, dv = sinf(a) * rh * k;
            pts[i] = { c.x + u.x * du + n.x * off, c.y + dv, c.z + u.z * du + n.z * off };
        }
        Vector3 m = { c.x + n.x * off, c.y, c.z + n.z * off };
        for (int i = 0; i < N; i++) {
            // wound to face along n whichever way the face points
            float cr = (u.x * n.z - u.z * n.x);
            if (cr > 0) fx.tri(m, pts[i], pts[(i + 1) % N], n, uv, uv, uv, col);
            else        fx.tri(m, pts[(i + 1) % N], pts[i], n, uv, uv, uv, col);
        }
    };
    ring(1.0f, 0.0025f, Color{ 104, 98, 90, 254 });
    ring(0.62f, 0.0035f, Color{ 62, 58, 52, 254 });
    Color rust = { 104, 58, 34, 254 };
    const float R = 0.0085f, out = 0.014f;
    for (int b = -1; b <= 1; b += 2) {                   // two verticals
        float du = b * rw * (0.25f + 0.12f * r.f01());
        float x0 = c.x + u.x * du + n.x * out, z0 = c.z + u.z * du + n.z * out;
        addSolidBox(pr, x0 - R, c.y - rh * 0.75f, z0 - R, x0 + R, c.y + rh * 0.8f, z0 + R, rust);
    }
    int ties = 1 + (int)(r.f01() * 2);
    for (int t = 0; t < ties; t++) {                      // and the ties across them
        float y = c.y + (t - 0.5f * (ties - 1)) * rh * 0.55f;
        float ax = c.x - u.x * rw * 0.55f + n.x * (out + 0.012f), az = c.z - u.z * rw * 0.55f + n.z * (out + 0.012f);
        float bx = c.x + u.x * rw * 0.55f + n.x * (out + 0.012f), bz = c.z + u.z * rw * 0.55f + n.z * (out + 0.012f);
        addSolidBox(pr, std::min(ax, bx) - R, y - R, std::min(az, bz) - R,
                    std::max(ax, bx) + R, y + R, std::max(az, bz) + R, rust);
    }
}

// ---- a Level 1 exit. The article lists Level 1's ways out as doors, and the
// first of them is "doors with unique symbols at the end of corridors with
// fluorescent lights". So an exit here is a steel door — frame, a heavy leaf
// pinned open against the wall, the glow of wherever it goes in the opening —
// with a symbol painted over it that no other door has (strokes between
// points on a 3x3 lattice, chosen by the edge's hash, so every one differs),
// and a caged bulkhead lamp above that. Cursed ones get their symbol in red.
// `ax` is 0 for a wall running along x at z = w0, 1 for one along z at x = w0;
// `a0` is the cell's low corner along the wall and `base` the floor under it.
static void addSymbolDoor(MB &pr, MB &fx, int ax, float a0, float w0, float base, bool cursed, uint32_t h,
                          bool leafRoom) {
    auto P = [&](float a, float y, float n) {             // wall-local -> world
        return ax == 0 ? Vector3{ a, y, w0 + n } : Vector3{ w0 + n, y, a };
    };
    auto box = [&](MB &mb, float aL, float y0, float nL, float aH, float y1, float nH, Color c) {
        Vector3 lo = P(aL, y0, nL), hi = P(aH, y1, nH);
        addSolidBox(mb, std::min(lo.x, hi.x), y0, std::min(lo.z, hi.z), std::max(lo.x, hi.x), y1, std::max(lo.z, hi.z), c);
    };
    Color steel = { 84, 88, 86, 254 }, dark = { 50, 52, 52, 254 };
    const float o0 = a0 + 0.35f, o1 = a0 + 1.65f, T = 0.05f;
    for (int sd = -1; sd <= 1; sd += 2) {                 // the frame, proud of both faces
        float nf = sd * (WT + T * 0.5f);
        box(pr, o0 - 0.09f, base, nf - T * 0.5f, o0, base + 2.39f, nf + T * 0.5f, steel);
        box(pr, o1, base, nf - T * 0.5f, o1 + 0.09f, base + 2.39f, nf + T * 0.5f, steel);
        box(pr, o0 - 0.09f, base + 2.30f, nf - T * 0.5f, o1 + 0.09f, base + 2.39f, nf + T * 0.5f, steel);
    }
    if (leafRoom) {   // the leaf, swung back flat against the -side face beside the frame
        float nf = -(WT + 0.035f);
        box(pr, o1 + 0.10f, base + 0.02f, nf - 0.022f, o1 + 1.38f, base + 2.27f, nf + 0.022f, Color{ 96, 104, 100, 254 });
        box(pr, o1 + 0.22f, base + 1.00f, nf - 0.05f, o1 + 1.26f, base + 1.06f, nf - 0.02f, dark);   // push bar
        box(pr, o1 + 0.40f, base + 1.55f, nf - 0.03f, o1 + 1.08f, base + 1.95f, nf - 0.022f, Color{ 60, 70, 76, 254 });   // wired glass
    }
    // the symbol: strokes of paint between lattice points, both faces
    Rng r(((uint64_t)h << 1) ^ 0x51B01ULL);
    int pts[6], np = 4 + r.ri(0, 2);
    for (int i = 0; i < np; i++) pts[i] = r.ri(0, 8);
    Color paint = cursed ? Color{ 210, 40, 30, 254 } : Color{ 250, 238, 190, 254 };
    const Vector2 uv = { 0.375f, 0.75f };
    float sc = 0.30f, cA = (o0 + o1) * 0.5f, cY = base + 2.78f;
    for (int sd = -1; sd <= 1; sd += 2) {
        float nf = sd * (WT + 0.0025f);
        for (int i = 0; i + 1 < np; i++) {
            float pa = cA + ((pts[i] % 3) - 1) * sc, py = cY + ((pts[i] / 3) - 1) * sc;
            float qa = cA + ((pts[i + 1] % 3) - 1) * sc, qy = cY + ((pts[i + 1] / 3) - 1) * sc;
            if (pts[i] == pts[i + 1]) { qa += sc * 0.6f; }
            float da = qa - pa, dy = qy - py, L = sqrtf(da * da + dy * dy) + 1e-4f;
            float ta = -dy / L * 0.065f, ty = da / L * 0.065f;          // half a brush width, across
            Vector3 A = P(pa - ta, py - ty, nf), B = P(qa - ta, qy - ty, nf), C = P(qa + ta, qy + ty, nf), D = P(pa + ta, py + ty, nf);
            Vector3 nn = ax == 0 ? Vector3{ 0, 0, (float)sd } : Vector3{ (float)sd, 0, 0 };
            bool flip = (ax == 0) ? sd > 0 : sd < 0;
            if (flip) fx.quad(B, A, D, C, nn, uv, uv, uv, uv, paint);
            else      fx.quad(A, B, C, D, nn, uv, uv, uv, uv, paint);
        }
        // a dot where the stroke started: whoever painted it began there
        float pa = cA + ((pts[0] % 3) - 1) * sc, py = cY + ((pts[0] / 3) - 1) * sc;
        box(fx, pa - 0.04f, py - 0.04f, nf - 0.001f, pa + 0.04f, py + 0.04f, nf + 0.001f, paint);
        // the caged bulkhead over it all: a warm glass lens (raw emissive) in a wire cage
        float lf = sd * (WT + 0.07f);
        box(pr, cA - 0.13f, base + 3.18f, sd * WT, cA + 0.13f, base + 3.36f, lf, dark);
        box(pr, cA - 0.09f, base + 3.20f, lf - sd * 0.03f, cA + 0.09f, base + 3.34f, lf + sd * 0.012f,
            Color{ 255, 206, 140, 60 });
        for (int b = -1; b <= 1; b++)
            box(pr, cA + b * 0.07f - 0.006f, base + 3.18f, lf + sd * 0.012f, cA + b * 0.07f + 0.006f, base + 3.36f,
                lf + sd * 0.024f, dark);
    }
}

// ---- the Manila Room, built. Everything here follows the wiki entry and the
// renders made from it: wooden floorboards, walls papered the colour of a
// manila folder, "one octagonal table and two chairs", cupboards under the
// table, "a wooden entrance door on each wall", and — from the Level 0 article
// — "a table illuminated by a lone chandelier". rx/rz is the room's centre
// (a cell corner), cy its ceiling. Floor is 0: generate() keeps it flat.
static void addManilaRoom(MB &pr, MB &fx, MB &ao, float rx, float rz, float cy, uint32_t h) {
    const float R = 4.0f, IN = R - WT;       // half-size of the room, and of its inside
    const float WU0 = 0.51f, WV0 = 0.02f, WU1 = 0.99f, WV1 = 0.48f;   // props atlas: wood
    auto wood = [&](MB &mb, float cxp, float czp, float yaw, float hx, float hz, float y0, float y1, Color t) {
        addPropBox(mb, cxp, czp, yaw, hx, hz, y0, y1, WU0, WV0, WU1, WV1, WU0, WV0, WU1, WV1, t, 0.004f);
    };
    Rng r(((uint64_t)h << 1) ^ 0x3A11AULL);

    // Floorboards: 145 mm strips running east-west, broken at random lengths
    // and staggered row to row, over a dark underlay that shows as the gaps.
    // Alpha 255, so the props detail map gives the grain its relief, and the
    // wood's own gloss mask gives the varnish a sheen off the chandelier.
    {
        const Vector3 up = { 0, 1, 0 };
        const Vector2 u = { 0.375f, 0.75f };
        pr.quad({rx-R,0.002f,rz-R},{rx-R,0.002f,rz+R},{rx+R,0.002f,rz+R},{rx+R,0.002f,rz-R}, up,
                u, u, u, u, Color{ 34, 24, 18, 254 });
        const float PW = 0.145f;
        for (float z = rz - R; z < rz + R - 0.01f; z += PW) {
            float z1 = std::min(z + PW, rz + R);
            float x = rx - R - r.f01() * 1.2f;
            while (x < rx + R) {
                float len = 0.9f + r.f01() * 1.6f;
                float xa = std::max(x, rx - R), xb = std::min(x + len, rx + R);
                if (xb - xa > 0.05f) {
                    float ou = WU0 + r.f01() * (WU1 - WU0 - 0.26f), ov = WV0 + r.f01() * (WV1 - WV0 - 0.05f);
                    float k = 0.80f + 0.34f * r.f01();
                    Color t = { cl8(150 * k), cl8(104 * k), cl8(70 * k), 255 };
                    float ua = ou, ub = ou + (xb - xa) * 0.11f, va = ov, vb = ov + 0.03f;
                    pr.quad({xa+0.003f,0.005f,z+0.003f},{xa+0.003f,0.005f,z1-0.003f},
                            {xb-0.003f,0.005f,z1-0.003f},{xb-0.003f,0.005f,z+0.003f}, up,
                            {ua,va},{ua,vb},{ub,vb},{ub,va}, t);
                }
                x += len;
            }
        }
    }

    // Manila paper on the four inside faces, as 500 mm tiles pressed 1.5 mm
    // off the plaster, from the top of the skirting to the ceiling and round
    // each doorway. Tiles are anchored to the world grid so the lattice runs
    // unbroken across the cuts.
    const FixtureRect &M = FIXTURES[FIX_MANILA];
    auto tileRect = [&](int axis, float fixed, float nsgn, float a0, float a1, float y0, float y1) {
        const float T = 0.5f;
        for (float a = floorf(a0 / T) * T; a < a1 - 1e-4f; a += T)
            for (float y = floorf(y0 / T) * T; y < y1 - 1e-4f; y += T) {
                float qa0 = std::max(a, a0), qa1 = std::min(a + T, a1);
                float qy0 = std::max(y, y0), qy1 = std::min(y + T, y1);
                if (qa1 - qa0 < 1e-3f || qy1 - qy0 < 1e-3f) continue;
                float ua = M.u0 + (M.u1 - M.u0) * (qa0 - a) / T, ub = M.u0 + (M.u1 - M.u0) * (qa1 - a) / T;
                float va = M.v1 - (M.v1 - M.v0) * (qy0 - y) / T, vb = M.v1 - (M.v1 - M.v0) * (qy1 - y) / T;
                Color c = { 255, 255, 255, 254 };
                if (axis == 0) {   // face at z = fixed, running along x
                    Vector3 n = { 0, 0, nsgn };
                    if (nsgn > 0) fx.quad({qa1,qy0,fixed},{qa0,qy0,fixed},{qa0,qy1,fixed},{qa1,qy1,fixed}, n,
                                          {ub,va},{ua,va},{ua,vb},{ub,vb}, c);
                    else          fx.quad({qa0,qy0,fixed},{qa1,qy0,fixed},{qa1,qy1,fixed},{qa0,qy1,fixed}, n,
                                          {ua,va},{ub,va},{ub,vb},{ua,vb}, c);
                } else {           // face at x = fixed, running along z
                    Vector3 n = { nsgn, 0, 0 };
                    if (nsgn > 0) fx.quad({fixed,qy0,qa0},{fixed,qy0,qa1},{fixed,qy1,qa1},{fixed,qy1,qa0}, n,
                                          {ua,va},{ub,va},{ub,vb},{ua,vb}, c);
                    else          fx.quad({fixed,qy0,qa1},{fixed,qy0,qa0},{fixed,qy1,qa0},{fixed,qy1,qa1}, n,
                                          {ub,va},{ua,va},{ua,vb},{ub,vb}, c);
                }
            }
    };
    // Door openings, in room-local cells: north wall's doorway is in cell 7,
    // south's in 8, west's in 8, east's in 7 (see stampManila in generate).
    // Cell c's opening runs from its west/north edge + 0.35 to + 1.65.
    auto face = [&](int axis, float fixed, float nsgn, int doorCell) {
        float a0 = (axis == 0 ? rx : rz) - IN, a1 = (axis == 0 ? rx : rz) + IN;
        float o0 = (axis == 0 ? rx : rz) - R + (doorCell - MANILA_LO) * CELL + 0.35f, o1 = o0 + 1.30f;
        const float y0 = 0.135f, yo = 2.30f;
        tileRect(axis, fixed, nsgn, a0, o0 - 0.06f, y0, cy);    // left of the frame
        tileRect(axis, fixed, nsgn, o1 + 0.06f, a1, y0, cy);    // right of it
        tileRect(axis, fixed, nsgn, o0 - 0.06f, o1 + 0.06f, yo + 0.06f, cy);   // over the head
        // The door itself, opened flat back against the wall beside its frame:
        // a wooden leaf, two panels, and a brass knob. It is what the lore
        // means by "a wooden entrance door on each wall", and it is open
        // because this is the one place down here you are meant to walk into.
        float lc = o1 + 0.08f + 0.64f;                       // leaf centre along the wall
        float off = fixed + nsgn * 0.03f;                    // standing just off the face
        Color leaf = { 118, 78, 50, 255 }, panel = { 98, 64, 40, 255 };
        if (axis == 0) {
            wood(pr, lc, off, 0, 0.64f, 0.022f, 0.01f, 2.27f, leaf);
            wood(pr, lc, off + nsgn * 0.02f, 0, 0.48f, 0.006f, 0.25f, 1.05f, panel);
            wood(pr, lc, off + nsgn * 0.02f, 0, 0.48f, 0.006f, 1.25f, 2.05f, panel);
            addSolidBox(pr, lc + 0.50f, 0.98f, off + nsgn * 0.02f - 0.025f, lc + 0.56f, 1.04f,
                        off + nsgn * 0.02f + 0.025f, Color{ 200, 160, 70, 254 });
        } else {
            wood(pr, off, lc, 0, 0.022f, 0.64f, 0.01f, 2.27f, leaf);
            wood(pr, off + nsgn * 0.02f, lc, 0, 0.006f, 0.48f, 0.25f, 1.05f, panel);
            wood(pr, off + nsgn * 0.02f, lc, 0, 0.006f, 0.48f, 1.25f, 2.05f, panel);
            addSolidBox(pr, off + nsgn * 0.02f - 0.025f, 0.98f, lc + 0.50f, off + nsgn * 0.02f + 0.025f,
                        1.04f, lc + 0.56f, Color{ 200, 160, 70, 254 });
        }
    };
    const float D = 0.0015f;
    face(0, rz - IN + D, +1, 7);    // north wall, facing into the room (+z)
    face(0, rz + IN - D, -1, 8);    // south
    face(1, rx - IN + D, +1, 8);    // west
    face(1, rx + IN - D, -1, 7);    // east

    // The octagonal table, with its cupboard under the top (the lore keeps
    // "food, water, and more documents" in there), on a plinth.
    auto octo = [&](float r0, float y0, float y1, Color t, bool top) {
        const float rr = r0 / cosf(TAU / 16);            // r0 is flat-to-centre
        for (int i = 0; i < 8; i++) {
            float a = TAU * i / 8 + TAU / 16, b = TAU * (i + 1) / 8 + TAU / 16, m = (a + b) * 0.5f;
            Vector3 p0 = { rx + rr * cosf(a), y0, rz + rr * sinf(a) }, p1 = { rx + rr * cosf(b), y0, rz + rr * sinf(b) };
            Vector3 p2 = { p1.x, y1, p1.z }, p3 = { p0.x, y1, p0.z };
            float ua = WU0 + 0.05f * i, ub = ua + 0.05f;
            pr.quad(p0, p1, p2, p3, { cosf(m), 0, sinf(m) }, {ua,WV1}, {ub,WV1}, {ub,WV1-0.05f}, {ua,WV1-0.05f}, t);
            if (top) {
                Vector2 c = { (WU0 + WU1) * 0.5f, (WV0 + WV1) * 0.5f };
                auto tuv = [&](Vector3 p) { return Vector2{ c.x + (p.x - rx) * 0.35f, c.y + (p.z - rz) * 0.35f }; };
                pr.tri({ rx, y1, rz }, p3, p2, { 0, 1, 0 }, c, tuv(p3), tuv(p2), t);
            }
        }
    };
    Color top = { 132, 86, 54, 255 }, body = { 104, 68, 44, 255 };
    octo(0.48f, 0.00f, 0.07f, Color{ 70, 46, 30, 255 }, true);    // plinth
    octo(0.42f, 0.07f, 0.73f, body, true);                          // the cupboard
    octo(0.64f, 0.73f, 0.785f, top, true);                          // the top
    for (int i = 0; i < 8; i += 2) {                                // cupboard doors: a knob on alternate faces
        float m = TAU * i / 8 + TAU / 8;
        float kx = rx + 0.425f * cosf(m), kz = rz + 0.425f * sinf(m);
        addSolidBox(pr, kx - 0.015f, 0.44f, kz - 0.015f, kx + 0.015f, 0.47f, kz + 0.015f, Color{ 200, 160, 70, 254 });
    }
    addContactShadow(ao, rx, rz, 0.0f, 0.0f, 0.52f, 0.52f);

    // Two chairs, one either side, pulled up to it as if two people had sat
    // down to talk — the only place in Level 0 anyone ever can.
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float chx = rx + sgn * 1.02f, chz = rz;
        Color cw = { 112, 74, 46, 255 };
        wood(pr, chx, chz, 0, 0.21f, 0.21f, 0.43f, 0.47f, cw);                        // seat
        for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2)
            wood(pr, chx + lx * 0.18f, chz + lz * 0.18f, 0, 0.018f, 0.018f, 0.0f, 0.43f, cw);
        float bx = chx + sgn * 0.19f;                                                 // the back, away from the table
        for (int lz = -1; lz <= 1; lz += 2) wood(pr, bx, chz + lz * 0.18f, 0, 0.018f, 0.018f, 0.47f, 0.93f, cw);
        wood(pr, bx, chz, 0, 0.014f, 0.19f, 0.74f, 0.90f, cw);
        wood(pr, bx, chz, 0, 0.012f, 0.19f, 0.56f, 0.61f, cw);
        addContactShadow(ao, chx, chz, 0.0f, 0.0f, 0.22f, 0.22f);
    }

    // The notes. "Notes have been left on the table containing information
    // about the Backrooms and guides to no-clipping." E reads them (Game).
    const FixtureRect &N = FIXTURES[FIX_NOTE];
    for (int i = 0; i < 4; i++) {
        float a = r.f01() * TAU, d = 0.12f + r.f01() * 0.30f, rot = r.f01() * TAU;
        float nx = rx + cosf(a) * d, nz = rz + sinf(a) * d, y = 0.787f + 0.0012f * i;
        float c = cosf(rot), s2 = sinf(rot), hw = N.halfW, hh = N.halfH;
        auto P = [&](float u, float v) { return Vector3{ nx + u * c - v * s2, y, nz + u * s2 + v * c }; };
        fx.quad(P(-hw,-hh), P(-hw,hh), P(hw,hh), P(hw,-hh), { 0, 1, 0 },
                { N.u0, N.v0 }, { N.u0, N.v1 }, { N.u1, N.v1 }, { N.u1, N.v0 }, Color{ 255, 255, 255, 254 });
    }

    // The chandelier: a chain from the ceiling, a brass hub, six arms and six
    // warm bulbs. The bulbs are raw emissive (alpha 60); the light they throw
    // is the shader's uLamp, which Game points at this room while you are near.
    {
        Color brass = { 186, 146, 72, 254 };
        float hy = cy - 0.78f;
        addSolidBox(pr, rx - 0.008f, hy + 0.10f, rz - 0.008f, rx + 0.008f, cy, rz + 0.008f, Color{ 90, 80, 60, 254 });
        addSolidBox(pr, rx - 0.06f, hy - 0.05f, rz - 0.06f, rx + 0.06f, hy + 0.10f, rz + 0.06f, brass);
        addSolidBox(pr, rx - 0.10f, cy - 0.03f, rz - 0.10f, rx + 0.10f, cy, rz + 0.10f, brass);   // ceiling rose
        for (int i = 0; i < 6; i++) {
            float a = TAU * i / 6;
            float ex = rx + cosf(a) * 0.34f, ez = rz + sinf(a) * 0.34f;
            for (int sgm = 0; sgm < 4; sgm++) {                 // the arm, as a few short boxes out and up
                float t0 = sgm / 4.0f, t1 = (sgm + 1) / 4.0f;
                float ax = rx + cosf(a) * 0.34f * t1, az = rz + sinf(a) * 0.34f * t1;
                float ay = hy + 0.02f * sinf(t0 * 3.1416f) - 0.03f * t1;
                addSolidBox(pr, ax - 0.012f, ay - 0.012f, az - 0.012f, ax + 0.012f, ay + 0.012f, az + 0.012f, brass);
            }
            addSolidBox(pr, ex - 0.03f, hy - 0.05f, ez - 0.03f, ex + 0.03f, hy - 0.01f, ez + 0.03f, brass);   // cup
            addSolidBox(pr, ex - 0.018f, hy - 0.01f, ez - 0.018f, ex + 0.018f, hy + 0.07f, ez + 0.018f,
                        Color{ 255, 196, 120, 60 });                                                        // bulb
        }
    }
}

void World::ensureMesh(int cx, int cz) {
    ChunkData &d = data(cx, cz);
    if (d.built) return;
    MB fl, ce, wa, pr, wt, scr, gl, ao, fx;
    float wx = cx * CHUNK, wz = cz * CHUNK;
    gWallV = level == 1 ? wallH : 3.0f;
    gTallPaper = storeyH > 0.0f;
    Color wcol = WHITE;
    // The ceiling gets no world-space relief (alpha 254, not 255). It hangs level
    // with the light fittings, so every panel lights it edge-on — and a bump under
    // raking light swings the terminator far harder than the same bump lit
    // head-on. At 255 the relief field turned the whole ceiling into dark
    // mould-like blotches roughly a tile across. Its own texture carries the
    // fissures and speckle it needs.
    Color ccol = { 255, 255, 255, 254 };
    // ---- baked ambient occlusion: gradient decals hugging every crease where
    // geometry meets. The strip texture fades alpha from the crease (v=0)
    // outward (v=1), so walls sit *in* the room instead of on top of it.
    auto aoStrip = [&](Vector3 e0, Vector3 e1, Vector3 off, Vector3 nn, float v0) {
        ao.quad(e0, e1, { e1.x + off.x, e1.y + off.y, e1.z + off.z },
                { e0.x + off.x, e0.y + off.y, e0.z + off.z }, nn,
                { 0, v0 }, { 1, v0 }, { 1, 1 }, { 0, 1 }, AO_TINT);
    };
    const float AOW = 0.55f;   // reach across the floor / ceiling
    const float AOH = 0.48f;   // creep up / down the wall face
    const float AOC = 0.30f;   // ceiling creases start partway down the gradient (softer)
    // ---- the Red Rooms, bleeding through. On Level 0 a cursed noclip wall
    // leads to the red places, and the lore's warning signs are that the
    // colour shifts toward red and the paper starts peeling to crimson as you
    // get near one. So collect the cursed exits in this chunk and its eight
    // neighbours (an exit's pull reaches ~10 m, less than a chunk) and tint
    // every wall and floor cell by how close it stands to the nearest.
    struct RedSrc { float x, z; };
    RedSrc reds[16]; int nred = 0;
    if (level == 0) {
        for (int ncx = cx - 1; ncx <= cx + 1; ncx++) for (int ncz = cz - 1; ncz <= cz + 1; ncz++) {
            ChunkData &nd = data(ncx, ncz);
            for (int a = 0; a < CCELLS; a++) for (int b = 0; b < CCELLS; b++) {
                int gi = ncx * CCELLS + a, gk = ncz * CCELLS + b;
                bool n = nd.wallN[a][b] == WALL_EXIT, w = nd.wallW[a][b] == WALL_EXIT;
                if ((!n && !w) || !cursedExit(gi, gk) || nred >= 16) continue;
                reds[nred++] = { gi * CELL + (n ? 1.0f : 0.0f), gk * CELL + (n ? 0.0f : 1.0f) };
            }
        }
    }
    auto redAt = [&](float x, float z) {
        float best = 0;
        for (int r = 0; r < nred; r++) {
            float dx = x - reds[r].x, dz = z - reds[r].z;
            float t = 1.0f - sqrtf(dx * dx + dz * dz) / 11.0f;
            if (t > best) best = t;
        }
        return best * best * (3 - 2 * best);
    };
    // Multiplied into the texture, so it can only take colour away: the
    // yellow ground loses green and blue and goes to rust, then to the dull
    // crimson the Red Rooms are papered in. Alpha stays 255 so the relief does.
    auto redTint = [&](float x, float z) {
        float t = redAt(x, z);
        // rust at the edge of the pull, crimson and dim at its heart
        float t2 = t * t;
        return Color{ (unsigned char)(255 - 30 * t - 60 * t2), (unsigned char)(255 - 150 * t - 72 * t2),
                      (unsigned char)(255 - 110 * t - 90 * t2), 255 };
    };
    if (nred) { wa.tint = redTint; fl.tint = redTint; }
    if (level == 2) {
        Color water = { 72, 172, 162, 128 };
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            int ci = cx*CCELLS+i, ck = cz*CCELLS+kk;
            float gx = wx + i*CELL, gz = wz + kk*CELL, fy = floorY(ci,ck);
            fl.quad({gx,fy,gz},{gx+CELL,fy,gz},{gx+CELL,fy,gz+CELL},{gx,fy,gz+CELL},{0,1,0},
                    {gx/2,gz/2},{(gx+CELL)/2,gz/2},{(gx+CELL)/2,(gz+CELL)/2},{gx/2,(gz+CELL)/2},wcol);
            // Emit only the high side of a riser. Cross-chunk lookups avoid
            // false walls at seams; physics reads these same terrace heights.
            auto skirt = [&](float x0,float z0,float x1,float z1,float low,Vector3 n) {
                if (low >= fy) return;
                fl.quad({x0,fy,z0},{x1,fy,z1},{x1,low,z1},{x0,low,z0},n,
                        {(x0+z0)/2,-fy/2},{(x1+z1)/2,-fy/2},
                        {(x1+z1)/2,-low/2},{(x0+z0)/2,-low/2},wcol);
            };
            skirt(gx,gz,gx+CELL,gz,floorY(ci,ck-1),{0,0,-1});
            skirt(gx+CELL,gz+CELL,gx,gz+CELL,floorY(ci,ck+1),{0,0,1});
            skirt(gx,gz+CELL,gx,gz,floorY(ci-1,ck),{-1,0,0});
            skirt(gx+CELL,gz,gx+CELL,gz+CELL,floorY(ci+1,ck),{1,0,0});
            if (d.pool[i][kk])
                wt.quad({gx,WATER_Y,gz},{gx+CELL,WATER_Y,gz},{gx+CELL,WATER_Y,gz+CELL},{gx,WATER_Y,gz+CELL},{0,1,0},
                        {0,0},{1,0},{1,1},{0,1},water);
        }
        // Elliptical vaults spanning the openings in the central partitions.
        // The lowest point is 4.6 m above the deck: all collision lives in the
        // full-height piers already represented by the wall grid.
        for (int axis=0; axis<2; ++axis) for (int start : {3,11}) {
            // Only where the partition is actually there with this opening in
            // it: grand halls have no partition, and an arch left hanging over
            // open water reads as a bug, not a ruin.
            auto wallAt = [&](int t) { return axis ? d.wallW[8][t] : d.wallN[t][8]; };
            if (wallAt(start - 1) != WALL_SOLID || wallAt(start + 3) != WALL_SOLID) continue;
            auto pos = [&](float t,float y,float depth) -> Vector3 {
                return axis ? Vector3{wx+16+depth,y,wz+t} : Vector3{wx+t,y,wz+16+depth};
            };
            for (int n=0;n<24;++n) {
                float t0=start*CELL+6.0f*n/24, t1=start*CELL+6.0f*(n+1)/24;
                auto archY = [&](float t) { float u=(t-(start*CELL+3))/3;
                    return 4.6f+2.4f*sqrtf(std::max(0.0f,1-u*u)); };
                float y0=archY(t0),y1=archY(t1);
                for (float side : {-WT,WT}) {
                    Vector3 normal=axis ? Vector3{side/WT,0,0}:Vector3{0,0,side/WT};
                    wa.quad(pos(t0,y0,side),pos(t1,y1,side),pos(t1,wallH,side),pos(t0,wallH,side),normal,
                            {t0/2,-y0/2},{t1/2,-y1/2},{t1/2,-wallH/2},{t0/2,-wallH/2},wcol);
                }
                Vector3 normal=axis ? Vector3{0,-1,(y1-y0)/(t1-t0)}:Vector3{(y1-y0)/(t1-t0),-1,0};
                wa.quad(pos(t0,y0,-WT),pos(t1,y1,-WT),pos(t1,y1,WT),pos(t0,y0,WT),normal,
                        {t0/2,0},{t1/2,0},{t1/2,WT},{t0/2,WT},wcol);
            }
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
            float fy = d.elev[i][kk] * ELEV_UNIT;
            // the Manila Room lays its own floorboards (addManilaRoom)
            if (d.manila && i >= MANILA_LO && i <= MANILA_HI && kk >= MANILA_LO && kk <= MANILA_HI) continue;
            // A hole has no floor, and a flight builds its own treads and
            // landings (buildFlights, below).
            if (d.vflag[i][kk] & (VF_HOLE | VF_STAIR)) continue;
            if (level == 0 && softAt(cx * CCELLS + i, cz * CCELLS + kk)) {
                // A rotten patch, and the one thing on Level 0 that will drop you
                // a floor. It used to be two flat decal quads: a black rectangle
                // with a blacker rectangle inside it, which you can certainly see
                // but which reads as a hole already there, or as a rug — not as a
                // floor that is about to stop being one.
                //
                // So it is geometry. The cell is built as a shallow bowl and the
                // dip catches the ceiling light along its far rim the way a real
                // sag does; the darkening is damp carpet over a backing that has
                // gone, not a painted-on square. It still reads across a room.
                //
                // SOFT_DEPTH and the falloff live in World::softDip, because
                // groundAt walks the player down the same bowl and the two must
                // not drift.
                const int SOFTSUB = 24;
                auto dipAt = [&](float u, float v) { return softDip(gx + u * CELL, gz + v * CELL); };
                for (int a = 0; a < SOFTSUB; a++) for (int b = 0; b < SOFTSUB; b++) {
                    float u0 = a / (float)SOFTSUB, u1 = (a + 1) / (float)SOFTSUB;
                    float v0 = b / (float)SOFTSUB, v1 = (b + 1) / (float)SOFTSUB;
                    float x0 = gx + u0 * CELL, x1 = gx + u1 * CELL;
                    float z0 = gz + v0 * CELL, z1 = gz + v1 * CELL;
                    float y00 = fy - dipAt(u0, v0), y10 = fy - dipAt(u1, v0);
                    float y11 = fy - dipAt(u1, v1), y01 = fy - dipAt(u0, v1);
                    // The bowl has to shade as a bowl, so take the normal from the
                    // height field's own slope across this patch rather than
                    // leaving every quad pointing at (0,1,0).
                    float dydx = ((y10 + y11) - (y00 + y01)) / (2 * (x1 - x0));
                    float dydz = ((y01 + y11) - (y00 + y10)) / (2 * (z1 - z0));
                    float nl = sqrtf(dydx * dydx + 1 + dydz * dydz);
                    Vector3 n = { -dydx / nl, 1 / nl, -dydz / nl };
                    // Damp and dark toward the middle, where the backing has gone.
                    // MB::quad carries one colour and one normal per quad, so both
                    // the tint ramp and the bowl's shading are banded at the
                    // subdivision. 8 across two metres came out as a visible
                    // chequerboard — worse than the flat decal it replaced — and
                    // 12 still quilted. 24 puts the step at 8 cm, under the noise
                    // in the carpet, at 576 quads on a patch that occurs once per
                    // 2660 m2.
                    float md = 1.0f - dipAt((u0 + u1) * 0.5f, (v0 + v1) * 0.5f) / SOFT_DEPTH;
                    float k2 = 0.34f + 0.66f * md * md;
                    Color sc = { (unsigned char)(wcol.r * k2), (unsigned char)(wcol.g * k2),
                                 (unsigned char)(wcol.b * k2), wcol.a };
                    fl.quad({x0,y00,z0},{x1,y10,z0},{x1,y11,z1},{x0,y01,z1}, n,
                            {x0/2,z0/2},{x1/2,z0/2},{x1/2,z1/2},{x0/2,z1/2}, sc);
                }
            } else
            fl.quad({gx,fy,gz},{gx+CELL,fy,gz},{gx+CELL,fy,gz+CELL},{gx,fy,gz+CELL},{0,1,0},
                    {gx/2,gz/2},{(gx+CELL)/2,gz/2},{(gx+CELL)/2,(gz+CELL)/2},{gx/2,(gz+CELL)/2},wcol);
            if (d.elev[i][kk] == 0) continue;
            // true cross-chunk heights, so terraces spanning a chunk border don't
            // grow phantom risers (the old lookup assumed 0 beyond the edge)
            auto hgt = [&](int a, int b) { return floorY(cx * CCELLS + a, cz * CCELLS + b); };
            float hN = hgt(i, kk - 1), hS = hgt(i, kk + 1), hW = hgt(i - 1, kk), hE = hgt(i + 1, kk);
            // each shared riser is drawn once: by this cell when the neighbour is
            // flat ground (elev 0 cells skip out above), otherwise by the lower
            // cell of the pair — terraced atria would double-draw it otherwise
            if (hN < fy && hN == 0.0f) stepEdge(gx, gz, gx + CELL, gz, fy, hN, 0, -1);
            else if (hN > fy) stepEdge(gx, gz, gx + CELL, gz, hN, fy, 0, 1);
            if (hS < fy && hS == 0.0f) stepEdge(gx, gz + CELL, gx + CELL, gz + CELL, fy, hS, 0, 1);
            else if (hS > fy) stepEdge(gx, gz + CELL, gx + CELL, gz + CELL, hS, fy, 0, -1);
            if (hW < fy && hW == 0.0f) stepEdge(gx, gz, gx, gz + CELL, fy, hW, -1, 0);
            else if (hW > fy) stepEdge(gx, gz, gx, gz + CELL, hW, fy, 1, 0);
            if (hE < fy && hE == 0.0f) stepEdge(gx + CELL, gz, gx + CELL, gz + CELL, fy, hE, 1, 0);
            else if (hE > fy) stepEdge(gx + CELL, gz, gx + CELL, gz + CELL, hE, fy, -1, 0);
            if (level == 1) {
                // Safety edging along every drop off a loading dock: yellow and
                // black, 100 mm wide, 250 mm to a stripe, painted just in from
                // the lip. A warehouse marks its edges; and in the fog, with the
                // floor the colour of the risers, it is the only thing that does.
                auto edging = [&](float ex0, float ez0, float ex1, float ez1, float inx, float inz) {
                    const Vector2 uv = { 0.375f, 0.75f };
                    float len = sqrtf((ex1 - ex0) * (ex1 - ex0) + (ez1 - ez0) * (ez1 - ez0));
                    int n = (int)(len / 0.25f);
                    for (int q = 0; q < n; q++) {
                        float t0 = q / (float)n, t1 = (q + 1) / (float)n;
                        Vector3 a = { ex0 + (ex1 - ex0) * t0, fy + 0.004f, ez0 + (ez1 - ez0) * t0 };
                        Vector3 b = { ex0 + (ex1 - ex0) * t1, fy + 0.004f, ez0 + (ez1 - ez0) * t1 };
                        Vector3 c = { b.x + inx * 0.10f, b.y, b.z + inz * 0.10f }, dd2 = { a.x + inx * 0.10f, a.y, a.z + inz * 0.10f };
                        Color col = (q & 1) ? Color{ 34, 32, 28, 254 } : Color{ 196, 160, 40, 254 };
                        // wound up whichever way round the edge runs
                        if ((ex1 - ex0) * inz - (ez1 - ez0) * inx < 0) fx.quad(a, b, c, dd2, {0,1,0}, uv, uv, uv, uv, col);
                        else                                           fx.quad(dd2, c, b, a, {0,1,0}, uv, uv, uv, uv, col);
                    }
                };
                if (hN < fy) edging(gx, gz, gx + CELL, gz, 0, 1);
                if (hS < fy) edging(gx, gz + CELL, gx + CELL, gz + CELL, 0, -1);
                if (hW < fy) edging(gx, gz, gx, gz + CELL, 1, 0);
                if (hE < fy) edging(gx + CELL, gz, gx + CELL, gz + CELL, -1, 0);
            }
        }
    }
    // Per-cell ceiling at this cell's own floor plus a wall height, in place of
    // one flat slab per chunk at a fixed y. The slab is what stopped the world
    // going upward: a raised deck pushed its floor through it and its walls
    // started below it. UVs stay world-space, so the tile grid runs across the
    // cell seams exactly as it did when this was one quad.
    //
    // Emitted by greedy meshing rather than one quad per cell. Elevation touches
    // about 5% of cells, so a naive per-cell ceiling replaces one quad per chunk
    // with 256 identical coplanar ones and costs 4% of the frame on the software
    // rasteriser for nothing; merging equal-height runs gives a flat chunk its
    // single quad back and only pays where the ceiling actually steps.
    {
        float cyc[CCELLS][CCELLS];
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++)
            cyc[i][kk] = ceilY(cx * CCELLS + i, cz * CCELLS + kk);
        bool done[CCELLS][CCELLS] = {};
        // A cell open to the storey above has no ceiling here: the ceiling you
        // see from it is the next storey's, drawn by that storey's chunk.
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++)
            if (d.vflag[i][kk] & VF_OPENUP) done[i][kk] = true;
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            if (done[i][kk]) continue;
            float cy = cyc[i][kk];
            int i1x = i;                                  // grow along x first
            while (i1x + 1 < CCELLS && !done[i1x + 1][kk] && cyc[i1x + 1][kk] == cy) i1x++;
            int k1z = kk;                                 // then take whole rows down z
            while (k1z + 1 < CCELLS) {
                bool ok = true;
                for (int a = i; a <= i1x && ok; a++) ok = !done[a][k1z + 1] && cyc[a][k1z + 1] == cy;
                if (!ok) break;
                k1z++;
            }
            for (int a = i; a <= i1x; a++) for (int b = kk; b <= k1z; b++) done[a][b] = true;
            float x0 = wx + i * CELL, x1 = wx + (i1x + 1) * CELL;
            float z0 = wz + kk * CELL, z1 = wz + (k1z + 1) * CELL;
            ce.quad({x0,cy,z0},{x0,cy,z1},{x1,cy,z1},{x1,cy,z0},{0,-1,0},
                    {x0/2,z0/2},{x0/2,z1/2},{x1/2,z1/2},{x1/2,z0/2},ccol);
        }
        // Where the neighbour's ceiling is higher, close the slot with a soffit.
        // Leave it out and you look straight along the gap and out of the
        // building. Only the *lower* cell of a pair draws it, so a shared edge
        // is drawn exactly once — including across a chunk seam, where both
        // sides read the same global heights.
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            float gx = wx + i * CELL, gz = wz + kk * CELL;
            int gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            float cy = cyc[i][kk];
            if (d.vflag[i][kk] & VF_OPENUP) continue;   // no ceiling of its own to close off
            auto soffit = [&](float ax, float az, float bx2, float bz2, float hi, int ni, int nk, uint8_t edge) {
                if (hi <= cy + 1e-4f) return;
                if (storeyH > 0.0f && (vflagAt(ni, nk) & VF_OPENUP)) {
                    // The edge of an opening into the storey above: the ceiling
                    // stops and a bulkhead runs up past the metre of dark void
                    // over the tiles to the floor above. Papered like the walls,
                    // because that is what it is. A wall or a door head on the
                    // same line already climbs that high and hides it.
                    if (blocksEdge(edge) && edge != WALL_RAIL) return;
                    if (edge == WALL_DOOR || edge == WALL_EXIT) return;
                    float along0 = (ax == bx2) ? az : ax, along1 = (ax == bx2) ? bz2 : bx2;
                    // From the tile's middle rather than its foot, so no
                    // baseboard runs round the underside of the floor above.
                    auto fv = [&](float y) { return 1 - (y - 2.25f) / gWallV; };
                    wa.quad({ax,cy,az},{bx2,cy,bz2},{bx2,hi,bz2},{ax,hi,az},
                            {(bz2-az)/CELL,0,(ax-bx2)/CELL},
                            {along0/3,fv(cy)},{along1/3,fv(cy)},{along1/3,fv(hi)},{along0/3,fv(hi)},wcol);
                    return;
                }
                ce.quad({ax,cy,az},{bx2,cy,bz2},{bx2,hi,bz2},{ax,hi,az},
                        {(bz2-az)/CELL,0,(ax-bx2)/CELL},
                        {(ax+az)/2,0},{(bx2+bz2)/2,0},{(bx2+bz2)/2,(hi-cy)/2},{(ax+az)/2,(hi-cy)/2},ccol);
            };
            soffit(gx, gz, gx + CELL, gz, ceilY(gi, gk - 1), gi, gk - 1, wallNVal(gi, gk));
            soffit(gx + CELL, gz + CELL, gx, gz + CELL, ceilY(gi, gk + 1), gi, gk + 1, wallNVal(gi, gk + 1));
            soffit(gx, gz + CELL, gx, gz, ceilY(gi - 1, gk), gi - 1, gk, wallWVal(gi, gk));
            soffit(gx + CELL, gz, gx + CELL, gz + CELL, ceilY(gi + 1, gk), gi + 1, gk, wallWVal(gi + 1, gk));
        }
    }
    // light panels on the global grid (emissive: alpha=0); spacing varies per level
    Color panel = {255,255,255,0};
    float ls = LEVELS[level].ls;   // the same grid the shader lights from (uLS)
    int g0x = (int)floorf(wx / ls), g1x = (int)floorf((wx + CHUNK) / ls);
    int g0z = (int)floorf(wz / ls), g1z = (int)floorf((wz + CHUNK) / ls);
    for (int gx = g0x; gx <= g1x; gx++)
        for (int gz = g0z; gz <= g1z; gz++) {
            float lx = gx * ls + ls * 0.5f, lz = gz * ls + ls * 0.5f, hp = 0.62f;
            if (lx < wx || lx >= wx + CHUNK || lz < wz || lz >= wz + CHUNK) continue;
            // No tubes in the Manila Room: it is lit by its chandelier, and the
            // shader masks these same panels dark through uRoomMask.
            if (d.manila) {
                float mx = wx + (MANILA_HI + 1 - 2) * CELL, mz = wz + (MANILA_HI + 1 - 2) * CELL;
                if (fabsf(lx - mx) < 4.0f && fabsf(lz - mz) < 4.0f) continue;
            }
            // No fitting where there is no ceiling to hang it in: a panel whose
            // tray would overhang an opening into the storey above is left out,
            // and buildOccupancy tells the shader the same (bit 3), so no light
            // comes out of the air where a panel is not.
            if (storeyH > 0.0f) {
                int pci2 = (int)floorf(lx / CELL + 0.5f), pck2 = (int)floorf(lz / CELL + 0.5f);   // the corner it is centred on
                if ((vflagAt(pci2, pck2) | vflagAt(pci2 - 1, pck2) | vflagAt(pci2, pck2 - 1) | vflagAt(pci2 - 1, pck2 - 1))
                    & VF_OPENUP) continue;
            }
            // The fitting hangs in the ceiling, so it goes wherever the ceiling
            // of the cell it is centred in went. The tray is 1.38 m across and a
            // cell is 2 m, so it can overhang a neighbour at another height;
            // that neighbour's soffit is what it meets, which is what a real
            // bulkhead beside a light does.
            float wallTop = ceilY((int)floorf(lx / CELL), (int)floorf(lz / CELL));
            float yq = wallTop - 0.12f;
            // Recessed diffuser inside a real metal tray. The luminous plane
            // now matches uLY instead of floating 10 cm above its own light.
            Color rim = level == 2 ? Color{230,232,223,254} : Color{156,153,140,254};
            const float outer = 0.69f, lip = 0.035f;
            if (level == 1) {
                // Level 1's fittings are warehouse battens, not office trays:
                // two bare tubes under a steel reflector, hung off the slab on
                // two rods. The tubes sit on the light plane (uLY), so the
                // light still comes from where the glow is; the shader shades
                // the fitting as its usual square, which at this pitch nobody
                // can tell apart. Every other one is turned a quarter, so the
                // grid does not read as rows of identical strips.
                bool alongX = (ih((int)floorf(lx / ls), (int)floorf(lz / ls), sseed() ^ 0xBA77u) & 1) != 0;
                auto box = [&](float a0, float y0, float b0, float a1, float y1, float b1, Color c) {
                    if (alongX) addSolidBox(pr, lx + a0, y0, lz + b0, lx + a1, y1, lz + b1, c);
                    else        addSolidBox(pr, lx + b0, y0, lz + a0, lx + b1, y1, lz + a1, c);
                };
                Color steel = { 132, 134, 128, 254 }, rod = { 70, 70, 68, 254 };
                float yt = yq;                                   // the tubes' underside
                box(-0.82f, yt + 0.035f, -0.16f, 0.82f, yt + 0.075f, 0.16f, steel);   // reflector
                box(-0.82f, yt - 0.005f, -0.165f, 0.82f, yt + 0.075f, -0.145f, steel); // its lips
                box(-0.82f, yt - 0.005f, 0.145f, 0.82f, yt + 0.075f, 0.165f, steel);
                box(-0.84f, yt - 0.01f, -0.16f, -0.78f, yt + 0.075f, 0.16f, rod);      // end caps
                box(0.78f, yt - 0.01f, -0.16f, 0.84f, yt + 0.075f, 0.16f, rod);
                box(-0.55f, yt + 0.075f, -0.008f, -0.534f, wallTop, 0.008f, rod);     // hanger rods
                box(0.534f, yt + 0.075f, -0.008f, 0.55f, wallTop, 0.008f, rod);
                for (int t = -1; t <= 1; t += 2) {                                     // the two tubes
                    float c0 = t * 0.07f - 0.028f, c1 = t * 0.07f + 0.028f;
                    Vector3 a, b, c, d2;
                    if (alongX) { a = {lx-0.78f,yt,lz+c0}; b = {lx-0.78f,yt,lz+c1}; c = {lx+0.78f,yt,lz+c1}; d2 = {lx+0.78f,yt,lz+c0}; }
                    else        { a = {lx+c0,yt,lz-0.78f}; b = {lx+c0,yt,lz+0.78f}; c = {lx+c1,yt,lz+0.78f}; d2 = {lx+c1,yt,lz-0.78f}; }
                    ce.quad(a, b, c, d2, {0,-1,0}, {0,0},{0,1},{1,1},{1,0}, panel);
                    // and their sides, so a tube seen edge-on down a long hall
                    // is still a line of light and not nothing
                    if (alongX) ce.quad({lx-0.78f,yt,lz+c0},{lx+0.78f,yt,lz+c0},{lx+0.78f,yt+0.03f,lz+c0},{lx-0.78f,yt+0.03f,lz+c0},
                                        {0,0,-1},{0,0},{1,0},{1,1},{0,1}, panel);
                    else        ce.quad({lx+c0,yt,lz+0.78f},{lx+c0,yt,lz-0.78f},{lx+c0,yt+0.03f,lz-0.78f},{lx+c0,yt+0.03f,lz+0.78f},
                                        {-1,0,0},{0,0},{1,0},{1,1},{0,1}, panel);
                }
                continue;
            }
            if (level == 0) {
                // Level 0's fittings are lay-in troffers, dropped into the tile
                // grid the way they are in the photograph: the diffuser sits
                // flush with the ceiling behind a hairline frame, not in a
                // box hanging under it. The light plane (uLY) stays 12 cm
                // down, which nobody can see and every light calculation in
                // both shader and CPU mirror already agrees on.
                float yf = wallTop - 0.010f;
                Color frame = { 186, 182, 166, 254 };
                addSolidBox(pr, lx-outer, yf-0.008f, lz-outer, lx-hp, wallTop, lz+outer, frame);
                addSolidBox(pr, lx+hp, yf-0.008f, lz-outer, lx+outer, wallTop, lz+outer, frame);
                addSolidBox(pr, lx-hp, yf-0.008f, lz-outer, lx+hp, wallTop, lz-hp, frame);
                addSolidBox(pr, lx-hp, yf-0.008f, lz+hp, lx+hp, wallTop, lz+outer, frame);
                ce.quad({lx-hp,yf,lz-hp},{lx-hp,yf,lz+hp},{lx+hp,yf,lz+hp},{lx+hp,yf,lz-hp},{0,-1,0},
                        {0,0},{0,1},{1,1},{1,0},panel);
                continue;
            }
            addSolidBox(pr, lx-outer, yq-lip, lz-outer, lx-hp, wallTop, lz+outer, rim);
            addSolidBox(pr, lx+hp, yq-lip, lz-outer, lx+outer, wallTop, lz+outer, rim);
            addSolidBox(pr, lx-hp, yq-lip, lz-outer, lx+hp, wallTop, lz-hp, rim);
            addSolidBox(pr, lx-hp, yq-lip, lz+hp, lx+hp, wallTop, lz+outer, rim);
            ce.quad({lx-hp,yq,lz-hp},{lx-hp,yq,lz+hp},{lx+hp,yq,lz+hp},{lx+hp,yq,lz-hp},{0,-1,0},
                    {0,0},{0,1},{1,1},{1,0},panel);
        }
    ChunkData &dd = d;
    // A rail on one cell edge. Its top follows whatever you would be standing
    // on beside it, whichever side is higher: level round an opening, and up
    // the nosing line beside a flight. Between two holes there is nothing to
    // stand on here — the flight that needs guarding is the storey below's,
    // and so is the balustrade you see — and the edge is collision only.
    auto railEdge = [&](int a, int b, bool west) {
        float ex0 = a * CELL, ez0 = b * CELL;
        float ex1 = west ? ex0 : ex0 + CELL, ez1 = west ? ez0 + CELL : ez0;
        int oa = west ? a - 1 : a, ob = west ? b : b - 1;          // the cell across the edge
        float inx = west ? 0.05f : 0.0f, inz = west ? 0.0f : 0.05f;   // a step into cell (a,b)
        bool any = false, hole = false;
        int voidSide = 0;
        float base = 1e9f;
        auto side = [&](int ca, int cb, float x, float z) {
            uint8_t f = vflagAt(ca, cb);
            if (f & VF_HOLE) {
                hole = true;
                // which face of the run looks into it (addRailRun's normal is
                // +z for a north edge and -x for a west one)
                bool own = ca == a && cb == b;
                voidSide = west ? (own ? -1 : 1) : (own ? 1 : -1);
                return -1e9f;
            }
            any = true;
            base = std::min(base, floorY(ca, cb));
            return (f & VF_STAIR) ? stairY(x, z, true) : floorY(ca, cb);
        };
        auto topAt = [&](float x, float z) {
            return std::max(side(a, b, x + inx, z + inz), side(oa, ob, x - inx, z - inz));
        };
        float t0 = topAt(ex0 + (ex1 - ex0) * 0.01f, ez0 + (ez1 - ez0) * 0.01f);
        float t1 = topAt(ex0 + (ex1 - ex0) * 0.99f, ez0 + (ez1 - ez0) * 0.99f);
        if (!any) return;
        addRailRun(wa, pr, ex0, ez0, ex1, ez1, base, t0 + RAIL_H, t1 + RAIL_H, hole, voidSide);
    };
    for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
        float gx = wx + i * CELL, gz = wz + kk * CELL;
        int gi0 = cx * CCELLS + i, gk0 = cz * CCELLS + kk;
        // A wall stands between two cells that may be at different heights. Base
        // it on the lower floor and take it to the higher ceiling: base it on
        // its own cell's floor instead and a step leaves a gap under the wall on
        // the low side and a slot over it on the high side, both of which you
        // see straight through. Everything fixed to the wall — sill, door head,
        // architrave — is measured off that same base, so a doorway in a step
        // has its head where a real one would.
        float fyc = floorY(gi0, gk0), cyc = ceilY(gi0, gk0);
        float nb = std::min(floorY(gi0, gk0 - 1), floorY(gi0, gk0));
        float nt = std::max(ceilY(gi0, gk0 - 1), ceilY(gi0, gk0));
        float wb = std::min(floorY(gi0 - 1, gk0), floorY(gi0, gk0));
        float wt2 = std::max(ceilY(gi0 - 1, gk0), ceilY(gi0, gk0));
        // Through the accessors, not out of the array. Every overlay that
        // changes what a wall IS lands in wallNVal/wallWVal — `shifted` for the
        // doorways the building closes behind you, `unlockedDoors` for a door
        // you have turned a key in — and reading the raw array here meant the
        // geometry was the one system that never saw them. Both ways round:
        // an unlocked door went on drawing its leaf while collision let you
        // walk through it, and a shifted doorway kept its opening on screen
        // while collision had already sealed it. AGENTS.md said the mesher came
        // through here; it did not, until now.
        uint8_t nv = wallNVal(gi0, gk0);
        // Storeys: which side of this cell's two edges has a floor to put a
        // skirting board, a crease and an outlet against, and which has a
        // ceiling to crease into. A hole has no floor, a flight buries the foot
        // of its walls, and an opening has no ceiling — trim or a shadow strip
        // drawn at floor level beside a hole hangs in the air over it.
        auto hasFloor = [&](int a, int b) { return storeyH <= 0.0f || !(vflagAt(a, b) & (VF_HOLE | VF_STAIR)); };
        auto hasCeil  = [&](int a, int b) { return storeyH <= 0.0f || !(vflagAt(a, b) & VF_OPENUP); };
        const bool flN = hasFloor(gi0, gk0), flS = hasFloor(gi0, gk0 - 1), flW = hasFloor(gi0 - 1, gk0);
        const bool clN = hasCeil(gi0, gk0),  clS = hasCeil(gi0, gk0 - 1),  clW = hasCeil(gi0 - 1, gk0);
        // A neighbour's end cap is buried in the next wall along, but not in a
        // rail, which is thinner than the wall it meets.
        auto buries = [](uint8_t v) { return blocksEdge(v) && v != WALL_RAIL; };
        if (nv == WALL_RAIL) railEdge(gi0, gk0, false);
        // Faces that look into a hole in this storey's floor (see voidFace).
        auto holeAt = [&](int a, int b) { return storeyH > 0.0f && (vflagAt(a, b) & VF_HOLE); };
        if (nv == WALL_SOLID) {
            int sk = (buries(wallNVal(gi0 - 1, gk0)) ? 4 : 0) | (buries(wallNVal(gi0 + 1, gk0)) ? 8 : 0);
            int vfN = (holeAt(gi0, gk0 - 1) ? 1 : 0) | (holeAt(gi0, gk0) ? 2 : 0);
            addBoxSides(wa, gx - WT, nb, gz - WT, gx + CELL + WT, nt, gz + WT, false, sk, WHITE, vfN);
        }
        else if (nv == WALL_WINDOW) {   // window on x-running wall; behind the glass, nothing
            addBoxSides(wa, gx - WT, nb, gz - WT, gx + CELL + WT, nb + 1.0f, gz + WT);
            addBoxSides(wa, gx - WT, nb + 2.1f, gz - WT, gx + CELL + WT, nt, gz + WT, true);
            addBoxSides(wa, gx - WT, nb + 1.0f, gz - WT, gx + 0.45f, nb + 2.1f, gz + WT);
            addBoxSides(wa, gx + 1.55f, nb + 1.0f, gz - WT, gx + CELL + WT, nb + 2.1f, gz + WT);
            wa.quad({gx-WT,nb+1.0f,gz-WT},{gx+CELL+WT,nb+1.0f,gz-WT},{gx+CELL+WT,nb+1.0f,gz+WT},{gx-WT,nb+1.0f,gz+WT},
                    {0,1,0},{0,0},{1,0},{1,0.1f},{0,0.1f}, WHITE);   // sill top
            if (level == 2) {
                // Level 37's windows look out into a light void: an emissive
                // pale pane (alpha 70 -> raw emissive), one per face.
                Color sky = { 226, 241, 246, 70 };
                wa.quad({gx+0.45f,nb+1.0f,gz-0.02f},{gx+1.55f,nb+1.0f,gz-0.02f},{gx+1.55f,nb+2.1f,gz-0.02f},{gx+0.45f,nb+2.1f,gz-0.02f},
                        {0,0,-1},{0,1},{1,1},{1,0},{0,0}, sky);
                wa.quad({gx+1.55f,nb+1.0f,gz+0.02f},{gx+0.45f,nb+1.0f,gz+0.02f},{gx+0.45f,nb+2.1f,gz+0.02f},{gx+1.55f,nb+2.1f,gz+0.02f},
                        {0,0,1},{0,1},{1,1},{1,0},{0,0}, sky);
            } else {
            // real glass now: translucent pane (alpha 100 -> glass branch), see the room beyond
            Color glass = { 20, 26, 32, 100 };
            gl.quad({gx+0.45f,nb+1.0f,gz},{gx+1.55f,nb+1.0f,gz},{gx+1.55f,nb+2.1f,gz},{gx+0.45f,nb+2.1f,gz},
                    {0,0,-1},{0,1},{1,1},{1,0},{0,0}, glass);
            }
        }
        else if (nv == WALL_EXIT && level == 0) {   // Level 0: a wall you can noclip through
            addBoxSides(wa, gx - WT, nb, gz - WT, gx + CELL + WT, nt, gz + WT, false, 0,
                        noclipCol(cursedExit(gi0, gk0)));
        }
        else if (nv == WALL_EXIT) {   // exit doorway on x-running wall
            addBoxSides(wa, gx - WT, nb, gz - WT, gx + 0.35f, nt, gz + WT);
            addBoxSides(wa, gx + 1.65f, nb, gz - WT, gx + CELL + WT, nt, gz + WT);
            addBoxSides(wa, gx + 0.35f, nb + 2.3f, gz - WT, gx + 1.65f, nt, gz + WT, true);
            // cursed exits glow red — they don't lead deeper, they lead to the Red Halls
            bool crs = cursedExit(cx * CCELLS + i, cz * CCELLS + kk);
            Color glow = crs ? Color{ 255, 60, 40, 70 } : Color{ 255, 248, 225, 70 };
            wa.quad({gx+0.35f,nb,gz},{gx+1.65f,nb,gz},{gx+1.65f,nb+2.3f,gz},{gx+0.35f,nb+2.3f,gz},{0,0,-1},
                    {0,1},{1,1},{1,0},{0,0},glow);
            if (level == 1)
                addSymbolDoor(pr, fx, 0, gx, gz, nb, crs, ih(gi0, gk0, sseed() ^ 0x51B0u),
                              wallNVal(gi0 + 1, gk0) == WALL_SOLID);
        }
        else if (nv == WALL_DOOR) {   // doorway on x-running wall
            // Two doorways in neighbouring cells leave 0.35 m of jamb each side
            // of the line between them: a 0.7 m sliver of plasterboard between
            // two 1.3 m holes, which is the thing that reads as unfinished
            // geometry rather than as a building. World::generate spaces
            // doorways out where the floorplan allows it, but a room reached
            // only through its own door cannot have that door moved — so where
            // two must stay adjacent, take the sliver out and let them be one
            // wide opening. The header still crosses it, so the wall above is
            // unbroken and the opening reads as deliberate.
            bool mW = wallNVal(gi0 - 1, gk0) == WALL_DOOR;
            bool mE = wallNVal(gi0 + 1, gk0) == WALL_DOOR;
            if (mW) addBoxSides(wa, gx - WT, nb + 2.3f, gz - WT, gx + 0.35f, nt, gz + WT, true);
            else    addBoxSides(wa, gx - WT, nb, gz - WT, gx + 0.35f, nt, gz + WT);
            if (mE) addBoxSides(wa, gx + 1.65f, nb + 2.3f, gz - WT, gx + CELL + WT, nt, gz + WT, true);
            else    addBoxSides(wa, gx + 1.65f, nb, gz - WT, gx + CELL + WT, nt, gz + WT);
            addBoxSides(wa, gx + 0.35f, nb + 2.3f, gz - WT, gx + 1.65f, nt, gz + WT, true);
            // The architrave is what makes it read as a door rather than a hole:
            // it stands proud of both faces, so you can see it is a way through
            // from either side and at a glancing angle. A merged side has no
            // jamb to trim, so its post goes and the head runs on to meet the
            // neighbour's.
            float tx0 = mW ? gx - WT : gx + 0.29f, tx1 = mE ? gx + CELL + WT : gx + 1.71f;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                float zf = (sgn < 0) ? gz - WT - TRIM_T : gz + WT;
                if (!mW) addSolidBox(pr, gx + 0.29f, nb, zf, gx + 0.35f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                if (!mE) addSolidBox(pr, gx + 1.65f, nb, zf, gx + 1.71f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                addSolidBox(pr, tx0, nb + 2.30f, zf, tx1, nb + 2.36f, zf + TRIM_T, TRIM_COL);
            }
            // and a threshold strip underfoot, worn by whoever came through
            float fy0 = floorY(cx * CCELLS + i, cz * CCELLS + kk);
            addSolidBox(pr, mW ? gx : gx + 0.35f, fy0, gz - 0.07f,
                        mE ? gx + CELL : gx + 1.65f, fy0 + 0.013f, gz + 0.07f, SILL_COL);
        }
        else if (nv == WALL_LOCKED) {   // a door with the leaf still in it
            addBoxSides(wa, gx - WT, nb, gz - WT, gx + 0.35f, nt, gz + WT);
            addBoxSides(wa, gx + 1.65f, nb, gz - WT, gx + CELL + WT, nt, gz + WT);
            addBoxSides(wa, gx + 0.35f, nb + 2.3f, gz - WT, gx + 1.65f, nt, gz + WT, true);
            float fy0 = floorY(cx * CCELLS + i, cz * CCELLS + kk);
            // The leaf fills the opening. It is what tells you at a glance that
            // this one is different from the hundred empty frames behind you,
            // so it is a slab you can see from both sides, not a decal.
            addSolidBox(pr, gx + 0.36f, fy0, gz - 0.025f, gx + 1.64f, nb + 2.28f, gz + 0.025f, LEAF_COL);
            for (int sgn = -1; sgn <= 1; sgn += 2) {   // architrave, as on an open one
                float zf = (sgn < 0) ? gz - WT - TRIM_T : gz + WT;
                addSolidBox(pr, gx + 0.29f, nb, zf, gx + 0.35f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                addSolidBox(pr, gx + 1.65f, nb, zf, gx + 1.71f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                addSolidBox(pr, gx + 0.29f, nb + 2.30f, zf, gx + 1.71f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                // handle and escutcheon, at 1.02 m on the latch side
                addSolidBox(pr, gx + 1.34f, fy0 + 0.97f, zf - 0.02f,
                            gx + 1.50f, fy0 + 1.07f, zf + TRIM_T, LOCK_COL);
            }
        }
        uint8_t wv = wallWVal(gi0, gk0);
        if (wv == WALL_RAIL) railEdge(gi0, gk0, true);
        if (wv == WALL_SOLID) {
            int sk = (buries(wallWVal(gi0, gk0 - 1)) ? 1 : 0) | (buries(wallWVal(gi0, gk0 + 1)) ? 2 : 0);
            int vfW = (holeAt(gi0 - 1, gk0) ? 4 : 0) | (holeAt(gi0, gk0) ? 8 : 0);
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, gz + CELL + WT, false, sk, WHITE, vfW);
        }
        else if (wv == WALL_WINDOW) {   // window on z-running wall
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wb + 1.0f, gz + CELL + WT);
            addBoxSides(wa, gx - WT, wb + 2.1f, gz - WT, gx + WT, wt2, gz + CELL + WT, true);
            addBoxSides(wa, gx - WT, wb + 1.0f, gz - WT, gx + WT, wb + 2.1f, gz + 0.45f);
            addBoxSides(wa, gx - WT, wb + 1.0f, gz + 1.55f, gx + WT, wb + 2.1f, gz + CELL + WT);
            wa.quad({gx-WT,wb+1.0f,gz-WT},{gx+WT,wb+1.0f,gz-WT},{gx+WT,wb+1.0f,gz+CELL+WT},{gx-WT,wb+1.0f,gz+CELL+WT},
                    {0,1,0},{0,0},{1,0},{1,0.1f},{0,0.1f}, WHITE);   // sill top
            if (level == 2) {   // see the x-running case: light void, both faces
                Color sky = { 226, 241, 246, 70 };
                wa.quad({gx+0.02f,wb+1.0f,gz+0.45f},{gx+0.02f,wb+1.0f,gz+1.55f},{gx+0.02f,wb+2.1f,gz+1.55f},{gx+0.02f,wb+2.1f,gz+0.45f},
                        {1,0,0},{0,1},{1,1},{1,0},{0,0}, sky);
                wa.quad({gx-0.02f,wb+1.0f,gz+1.55f},{gx-0.02f,wb+1.0f,gz+0.45f},{gx-0.02f,wb+2.1f,gz+0.45f},{gx-0.02f,wb+2.1f,gz+1.55f},
                        {-1,0,0},{0,1},{1,1},{1,0},{0,0}, sky);
            } else {
            Color glass = { 20, 26, 32, 100 };
            gl.quad({gx,wb+1.0f,gz+0.45f},{gx,wb+1.0f,gz+1.55f},{gx,wb+2.1f,gz+1.55f},{gx,wb+2.1f,gz+0.45f},
                    {1,0,0},{0,1},{1,1},{1,0},{0,0}, glass);
            }
        }
        else if (wv == WALL_EXIT && level == 0) {   // see the x-running case
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, gz + CELL + WT, false, 0,
                        noclipCol(cursedExit(gi0, gk0)));
        }
        else if (wv == WALL_EXIT) {   // exit doorway on z-running wall
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, gz + 0.35f);
            addBoxSides(wa, gx - WT, wb, gz + 1.65f, gx + WT, wt2, gz + CELL + WT);
            addBoxSides(wa, gx - WT, wb + 2.3f, gz + 0.35f, gx + WT, wt2, gz + 1.65f, true);
            bool crs = cursedExit(cx * CCELLS + i, cz * CCELLS + kk);
            Color glow = crs ? Color{ 255, 60, 40, 70 } : Color{ 255, 248, 225, 70 };
            wa.quad({gx,wb,gz+0.35f},{gx,wb,gz+1.65f},{gx,wb+2.3f,gz+1.65f},{gx,wb+2.3f,gz+0.35f},{1,0,0},
                    {0,1},{1,1},{1,0},{0,0},glow);
            if (level == 1)
                addSymbolDoor(pr, fx, 1, gz, gx, wb, crs, ih(gi0, gk0, sseed() ^ 0x51B1u),
                              wallWVal(gi0, gk0 + 1) == WALL_SOLID);
        }
        else if (wv == WALL_DOOR) {   // doorway on z-running wall
            // Merged the same way its x-running twin above is — see there.
            bool mN = wallWVal(gi0, gk0 - 1) == WALL_DOOR;
            bool mS = wallWVal(gi0, gk0 + 1) == WALL_DOOR;
            if (mN) addBoxSides(wa, gx - WT, wb + 2.3f, gz - WT, gx + WT, wt2, gz + 0.35f, true);
            else    addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, gz + 0.35f);
            if (mS) addBoxSides(wa, gx - WT, wb + 2.3f, gz + 1.65f, gx + WT, wt2, gz + CELL + WT, true);
            else    addBoxSides(wa, gx - WT, wb, gz + 1.65f, gx + WT, wt2, gz + CELL + WT);
            addBoxSides(wa, gx - WT, wb + 2.3f, gz + 0.35f, gx + WT, wt2, gz + 1.65f, true);
            float tz0 = mN ? gz - WT : gz + 0.29f, tz1 = mS ? gz + CELL + WT : gz + 1.71f;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                float xf = (sgn < 0) ? gx - WT - TRIM_T : gx + WT;
                if (!mN) addSolidBox(pr, xf, wb, gz + 0.29f, xf + TRIM_T, wb + 2.36f, gz + 0.35f, TRIM_COL);
                if (!mS) addSolidBox(pr, xf, wb, gz + 1.65f, xf + TRIM_T, wb + 2.36f, gz + 1.71f, TRIM_COL);
                addSolidBox(pr, xf, wb + 2.30f, tz0, xf + TRIM_T, wb + 2.36f, tz1, TRIM_COL);
            }
            float fy0 = floorY(cx * CCELLS + i, cz * CCELLS + kk);
            addSolidBox(pr, gx - 0.07f, fy0, mN ? gz : gz + 0.35f,
                        gx + 0.07f, fy0 + 0.013f, mS ? gz + CELL : gz + 1.65f, SILL_COL);
        }
        else if (wv == WALL_LOCKED) {   // a door with the leaf still in it
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, gz + 0.35f);
            addBoxSides(wa, gx - WT, wb, gz + 1.65f, gx + WT, wt2, gz + CELL + WT);
            addBoxSides(wa, gx - WT, wb + 2.3f, gz + 0.35f, gx + WT, wt2, gz + 1.65f, true);
            float fy0 = floorY(cx * CCELLS + i, cz * CCELLS + kk);
            addSolidBox(pr, gx - 0.025f, fy0, gz + 0.36f, gx + 0.025f, wb + 2.28f, gz + 1.64f, LEAF_COL);
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                float xf = (sgn < 0) ? gx - WT - TRIM_T : gx + WT;
                addSolidBox(pr, xf, wb, gz + 0.29f, xf + TRIM_T, wb + 2.36f, gz + 0.35f, TRIM_COL);
                addSolidBox(pr, xf, wb, gz + 1.65f, xf + TRIM_T, wb + 2.36f, gz + 1.71f, TRIM_COL);
                addSolidBox(pr, xf, wb + 2.30f, gz + 0.29f, xf + TRIM_T, wb + 2.36f, gz + 1.71f, TRIM_COL);
                addSolidBox(pr, xf - 0.02f, fy0 + 0.97f, gz + 1.34f,
                            xf + TRIM_T, fy0 + 1.07f, gz + 1.50f, LOCK_COL);
            }
        }
        if (level == 0 || level == 4) {
            // Thin timber trim catches grazing light. Keep the extrusion within
            // the collision clearance; no separate obstacle or draw call.
            Color trim = level == 0 ? Color{91, 71, 39, 254} : Color{67, 41, 34, 254};
            if (nv == WALL_SOLID || (level == 0 && nv == WALL_EXIT)) {
                float tS = floorY(gi0, gk0 - 1), tN = floorY(gi0, gk0);
                if (flS) addSolidBox(pr, gx, tS, gz-WT-0.025f, gx+CELL, tS + 0.13f, gz-WT, trim);
                if (flN) addSolidBox(pr, gx, tN, gz+WT, gx+CELL, tN + 0.13f, gz+WT+0.025f, trim);
            }
            if (wv == WALL_SOLID || (level == 0 && wv == WALL_EXIT)) {
                float tW = floorY(gi0 - 1, gk0), tE = floorY(gi0, gk0);
                if (flW) addSolidBox(pr, gx-WT-0.025f, tW, gz, gx-WT, tW + 0.13f, gz+CELL, trim);
                if (flN) addSolidBox(pr, gx+WT, tE, gz, gx+WT+0.025f, tE + 0.13f, gz+CELL, trim);
            }
        }
        // baked AO around this cell's walls: floor strip, ceiling strip, and a
        // wall-face strip on both sides (solid walls and windows; doorways stay clean)
        // A noclip wall has to be indistinguishable from its neighbours by
        // everything except the glitch, so it gets their creases too.
        // A rail is not a wall to crease against: it has its own contact shadow.
        bool nvWall = (blocksEdge(nv) && nv != WALL_RAIL) || (level == 0 && nv == WALL_EXIT);
        bool wvWall = (blocksEdge(wv) && wv != WALL_RAIL) || (level == 0 && wv == WALL_EXIT);
        if (nvWall) {
            float fyS = floorY(gi0, gk0 - 1) + 0.005f, fyN = floorY(gi0, gk0) + 0.005f;
            // Ceiling creases follow each side's own ceiling. Pinned to a fixed
            // wallH they detach the moment the floor moves, and a crease hanging
            // in clear air under a ceiling reads as a smear, not a shadow.
            float cyS = ceilY(gi0, gk0 - 1) - 0.005f, cyN = ceilY(gi0, gk0) - 0.005f;
            // span exactly one cell — neighbours butt up seamlessly, no double-blend overlap
            float x0 = gx, x1 = gx + CELL;
            if (flS) aoStrip({ x0, fyS, gz - WT }, { x1, fyS, gz - WT }, { 0, 0, -AOW }, { 0, 1, 0 }, 0);          // floor, -z side
            if (flN) aoStrip({ x0, fyN, gz + WT }, { x1, fyN, gz + WT }, { 0, 0, AOW }, { 0, 1, 0 }, 0);           // floor, +z side
            if (clS) aoStrip({ x0, cyS, gz - WT }, { x1, cyS, gz - WT }, { 0, 0, -AOW }, { 0, -1, 0 }, AOC);       // ceiling creases
            if (clN) aoStrip({ x0, cyN, gz + WT }, { x1, cyN, gz + WT }, { 0, 0, AOW }, { 0, -1, 0 }, AOC);
            if (flS) aoStrip({ x0, fyS, gz - WT - 0.006f }, { x1, fyS, gz - WT - 0.006f }, { 0, AOH, 0 }, { 0, 0, -1 }, 0);   // skirting shadow up the faces
            if (flN) aoStrip({ x0, fyN, gz + WT + 0.006f }, { x1, fyN, gz + WT + 0.006f }, { 0, AOH, 0 }, { 0, 0, 1 }, 0);
            if (clS) aoStrip({ x0, cyS + 0.005f, gz - WT - 0.006f }, { x1, cyS + 0.005f, gz - WT - 0.006f }, { 0, -AOH, 0 }, { 0, 0, -1 }, AOC);   // and down from the ceiling
            if (clN) aoStrip({ x0, cyN + 0.005f, gz + WT + 0.006f }, { x1, cyN + 0.005f, gz + WT + 0.006f }, { 0, -AOH, 0 }, { 0, 0, 1 }, AOC);
        }
        if (wvWall) {
            float fyW = floorY(gi0 - 1, gk0) + 0.005f, fyE = floorY(gi0, gk0) + 0.005f;
            float cyW = ceilY(gi0 - 1, gk0) - 0.005f, cyE = ceilY(gi0, gk0) - 0.005f;
            float z0 = gz, z1 = gz + CELL;
            if (flW) aoStrip({ gx - WT, fyW, z0 }, { gx - WT, fyW, z1 }, { -AOW, 0, 0 }, { 0, 1, 0 }, 0);
            if (flN) aoStrip({ gx + WT, fyE, z0 }, { gx + WT, fyE, z1 }, { AOW, 0, 0 }, { 0, 1, 0 }, 0);
            if (clW) aoStrip({ gx - WT, cyW, z0 }, { gx - WT, cyW, z1 }, { -AOW, 0, 0 }, { 0, -1, 0 }, AOC);
            if (clN) aoStrip({ gx + WT, cyE, z0 }, { gx + WT, cyE, z1 }, { AOW, 0, 0 }, { 0, -1, 0 }, AOC);
            if (flW) aoStrip({ gx - WT - 0.006f, fyW, z0 }, { gx - WT - 0.006f, fyW, z1 }, { 0, AOH, 0 }, { -1, 0, 0 }, 0);
            if (flN) aoStrip({ gx + WT + 0.006f, fyE, z0 }, { gx + WT + 0.006f, fyE, z1 }, { 0, AOH, 0 }, { 1, 0, 0 }, 0);
            if (clW) aoStrip({ gx - WT - 0.006f, cyW + 0.005f, z0 }, { gx - WT - 0.006f, cyW + 0.005f, z1 }, { 0, -AOH, 0 }, { -1, 0, 0 }, AOC);
            if (clN) aoStrip({ gx + WT + 0.006f, cyE + 0.005f, z1 }, { gx + WT + 0.006f, cyE + 0.005f, z0 }, { 0, -AOH, 0 }, { 1, 0, 0 }, AOC);
        }
        if (level == 1 && nv == WALL_SOLID && ih(gi0, gk0, sseed() ^ 0xE1E7u) % 71 == 0 &&
            wallNVal(gi0 - 1, gk0) == WALL_SOLID && wallNVal(gi0 + 1, gk0) == WALL_SOLID) {
            // A lift. The article gives Level 1 "staircases, elevators, isolated
            // rooms, and hallways"; the doors are shut and nobody has found the
            // car, but the call button is lit, which is worse than if it were not.
            float sgn = (ih(gi0, gk0, sseed() ^ 0xE1E8u) & 1) ? 1.0f : -1.0f;
            float zf = gz + sgn * WT;
            auto zb = [&](float x0, float y0, float x1, float y1, float d0, float d1, Color c) {
                addSolidBox(pr, x0, y0, std::min(zf + sgn * d0, zf + sgn * d1), x1, y1, std::max(zf + sgn * d0, zf + sgn * d1), c);
            };
            Color frame = { 92, 94, 92, 254 }, leaf = { 142, 146, 144, 254 }, seam = { 40, 42, 42, 254 };
            zb(gx + 0.30f, nb, gx + 1.70f, nb + 2.46f, 0.0f, 0.03f, frame);                       // surround
            zb(gx + 0.38f, nb, gx + 0.995f, nb + 2.38f, 0.03f, 0.045f, leaf);                     // two leaves
            zb(gx + 1.005f, nb, gx + 1.62f, nb + 2.38f, 0.03f, 0.045f, leaf);
            zb(gx + 0.995f, nb, gx + 1.005f, nb + 2.38f, 0.03f, 0.042f, seam);
            zb(gx + 0.62f, nb + 2.52f, gx + 1.38f, nb + 2.70f, 0.0f, 0.03f, seam);                 // floor indicator
            zb(gx + 0.93f, nb + 2.55f, gx + 1.07f, nb + 2.67f, 0.03f, 0.036f, Color{ 255, 120, 40, 60 });
            zb(gx + 1.86f, nb + 1.00f, gx + 1.96f, nb + 1.30f, 0.0f, 0.02f, frame);               // call plate
            zb(gx + 1.89f, nb + 1.12f, gx + 1.93f, nb + 1.18f, 0.02f, 0.03f, Color{ 255, 236, 180, 60 });
        }
        if (level == 1) {   // spalls on the walls too, rarer than on the columns
            uint32_t sn = ih(gi0, gk0, sseed() ^ 0x5BA2u), sw2 = ih(gi0, gk0, sseed() ^ 0x5BA3u);
            if (nv == WALL_SOLID && sn % 13 == 0) {
                float sgn = (sn >> 4) & 1 ? 1.0f : -1.0f;
                addSpall(fx, pr, { gx + 0.5f + ((sn >> 5) & 7) * 0.14f, nb + 0.5f + ((sn >> 8) & 15) * 0.16f, gz + sgn * WT },
                         { 0, 0, sgn }, { 1, 0, 0 }, 0.30f, 0.34f, sn);
            }
            if (wv == WALL_SOLID && sw2 % 13 == 0) {
                float sgn = (sw2 >> 4) & 1 ? 1.0f : -1.0f;
                addSpall(fx, pr, { gx + sgn * WT, wb + 0.5f + ((sw2 >> 8) & 15) * 0.16f, gz + 0.5f + ((sw2 >> 5) & 7) * 0.14f },
                         { sgn, 0, 0 }, { 0, 0, 1 }, 0.30f, 0.34f, sw2);
            }
        }
        // ---- the building's fittings. Decals pressed off the wall and ceiling
        // faces, plus real (if tiny) geometry for the conduit and sprinklers.
        //
        // All of it is alpha 254: textured and opaque, but below the relief
        // threshold. A faceplate is flat moulded plastic — giving it the
        // world-space relief bump would ripple it like the wall behind it.
        //
        // These sit in their own mesh rather than in the props mesh, which is
        // already the biggest one in a chunk and is indexed with 16-bit indices
        // that nothing checks for overflow.
        {
            uint32_t gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            const Color FIXC = { 255, 255, 255, 254 };
            // A decal on an x-running (north) wall, and on a z-running (west)
            // one. `plus` picks which of the two faces it hangs on; the UVs
            // mirror with it, so signage reads the right way round from the
            // room that can actually see it. Size comes from FIXTURES, which is
            // also what drew the cell — see textures.h.
            auto decalN = [&](float xc, float yc, float zf, bool plus, int id) {
                const FixtureRect &f = FIXTURES[id];
                float x0 = xc - f.halfW, x1 = xc + f.halfW, y0 = yc - f.halfH, y1 = yc + f.halfH;
                if (plus) fx.quad({x0,y0,zf},{x1,y0,zf},{x1,y1,zf},{x0,y1,zf},{0,0,1},
                                  {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
                else      fx.quad({x1,y0,zf},{x0,y0,zf},{x0,y1,zf},{x1,y1,zf},{0,0,-1},
                                  {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
            };
            auto decalW = [&](float zc, float yc, float xf, bool plus, int id) {
                const FixtureRect &f = FIXTURES[id];
                float z0 = zc - f.halfW, z1 = zc + f.halfW, y0 = yc - f.halfH, y1 = yc + f.halfH;
                if (plus) fx.quad({xf,y0,z1},{xf,y0,z0},{xf,y1,z0},{xf,y1,z1},{1,0,0},
                                  {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
                else      fx.quad({xf,y0,z0},{xf,y0,z1},{xf,y1,z1},{xf,y1,z0},{-1,0,0},
                                  {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
            };
            // Which fitting this wall edge carries, if any. Checked in order of
            // rarity, so a wall that qualifies for two gets the rarer one — an
            // exit sign beats a grille beats a switch beats an outlet, rather
            // than a pile of fittings on the one unlucky wall. The heights are
            // the ones a building actually uses: outlets at the skirting, a
            // switch at the handle, a return grille up near the ceiling.
            auto pick = [&](uint32_t h, float &yc, int &id) {
                if (level != 2 && h % EXITSIGN_RATE == 0) { yc = 2.44f; id = FIX_SIGN;   return true; }
                if (level != 2 && h % GRILLE_RATE == 0)   { yc = 2.10f; id = FIX_GRILLE; return true; }
                if (level != 2 && h % SWITCH_RATE == 0)   { yc = 1.22f; id = FIX_SWITCH; return true; }
                // A pool hall does not have mains sockets at ankle height, and
                // Level 2 is the one level meant to read as still maintained.
                if (level != 2 && h % OUTLET_RATE == 0) {
                    yc = 0.32f;
                    id = ((h >> 11) % OUTLET_BROKEN == 0) ? FIX_OUTLET_BROKEN : FIX_OUTLET;
                    return true;
                }
                return false;
            };
            if (nv == WALL_SOLID) {
                uint32_t h = ih(gi, gk, sseed() ^ 0x71F0u);
                float yc; int id;
                if (pick(h, yc, id) && ((h & 16) ? flN : flS)) {   // not on a face over a hole or a flight
                    bool plus = (h & 16) != 0;
                    float zf = plus ? gz + WT + 0.006f : gz - WT - 0.006f;
                    decalN(gx + 0.45f + ((h >> 7) & 7) * 0.155f, yc, zf, plus, id);
                }
            }
            if (wv == WALL_SOLID) {
                uint32_t h = ih(gi, gk, sseed() ^ 0x71F9u);
                float yc; int id;
                if (pick(h, yc, id) && ((h & 16) ? flN : flW)) {
                    bool plus = (h & 16) != 0;
                    float xf = plus ? gx + WT + 0.006f : gx - WT - 0.006f;
                    decalW(gz + 0.45f + ((h >> 7) & 7) * 0.155f, yc, xf, plus, id);
                }
            }
            // Ceiling: a supply diffuser lies flat in the tile grid, while a
            // sprinkler hangs below it on a dropper — flat-on-the-ceiling is
            // exactly wrong for a sprinkler, which you almost always see from
            // underneath and off to one side.
            uint32_t hc = ih(gi, gk, sseed() ^ 0x71E3u);
            float ccx = gx + CELL * 0.5f, ccz = gz + CELL * 0.5f;
            if (!clN) {
                // no ceiling here to put a diffuser or a sprinkler in
            } else if (level != 2 && hc % DIFFUSER_RATE == 0) {
                const FixtureRect &f = FIXTURES[FIX_DIFFUSER];
                float yq = cyc - 0.008f;
                fx.quad({ccx-f.halfW,yq,ccz-f.halfH},{ccx-f.halfW,yq,ccz+f.halfH},
                        {ccx+f.halfW,yq,ccz+f.halfH},{ccx+f.halfW,yq,ccz-f.halfH},{0,-1,0},
                        {f.u0,f.v0},{f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0}, FIXC);
            } else if (level != 2 && hc % SPRINK_RATE == 0) {
                const Color BRASS = { 158, 126, 66, 254 };
                addSolidBox(fx, ccx-0.016f, cyc-0.085f, ccz-0.016f, ccx+0.016f, cyc, ccz+0.016f, BRASS);
                addSolidBox(fx, ccx-0.033f, cyc-0.085f, ccz-0.033f, ccx+0.033f, cyc-0.070f, ccz+0.033f, BRASS);
                addSolidBox(fx, ccx-0.045f, cyc-0.100f, ccz-0.045f, ccx+0.045f, cyc-0.090f, ccz+0.045f, BRASS);
            }
            // Conduit runs along the top of a wall. Keyed on a bucket of cells
            // rather than a single one, so it comes out as a run of six with a
            // beginning and an end instead of a dotted line of stubs.
            // Conduit stands on a wall *face*, not inside the wall. The first
            // version ran it about the wall centreline, ±28 mm on a wall whose
            // half-thickness is 110, so every run in the building was sealed
            // inside the plasterboard and nothing was ever drawn. The run hash
            // picks the face as well as the run, so a run does not change sides
            // halfway along.
            const Color STEEL = { 138, 136, 130, 254 };
            const float CDY = 0.052f;          // how far it stands off the wall
            // Conduit runs under a ceiling; a wall climbing an opening has none.
            if (nv == WALL_SOLID && clN && clS) {
                float cy = nt - 0.155f;
                uint32_t hr = ih(gi / CONDUIT_RUN, gk, sseed() ^ 0x71C5u);
                if (hr % 7 == 0) {
                    float z0 = (hr & 32) ? gz + WT : gz - WT - CDY;
                    addSolidBox(fx, gx - WT, cy, z0, gx + CELL + WT, cy + 0.046f, z0 + CDY, STEEL);
                }
            }
            if (wv == WALL_SOLID && clN && clW) {
                float cy = wt2 - 0.155f;
                uint32_t hr = ih(gi, gk / CONDUIT_RUN, sseed() ^ 0x71CBu);
                if (hr % 7 == 0) {
                    float x0 = (hr & 32) ? gx + WT : gx - WT - CDY;
                    addSolidBox(fx, x0, cy, gz - WT, x0 + CDY, cy + 0.046f, gz + CELL + WT, STEEL);
                }
            }
        }
        // wall scrawl: rarely, a solid wall carries a phrase left by an earlier
        // wanderer. one of thirty-two, from the 4x8 scrawl atlas, drawn as a
        // decal pressed just off the wall face (level 2 is pristine tile — no
        // scrawl).
        //
        // SCRAWL_RATE is a rarity, not a decoration: at the old one-in-seven
        // every corridor had writing on it and the same eight phrases came back
        // within sight of each other, which reads as wallpaper. One in forty,
        // across thirty-two phrases, means seeing one is an event and seeing the
        // same one twice means something.
        if (level != 2) {
            uint32_t gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            auto uvOf = [](int ph, float &u0, float &v0, float &u1, float &v1) {
                u0 = (ph & 3) * 0.25f; v0 = (ph >> 2) * 0.125f; u1 = u0 + 0.25f; v1 = v0 + 0.125f;
            };
            // Nobody writes on a wall straight, and no two people picked up the
            // same pen. A small rotation and a tint per instance, so the same
            // atlas cell twice does not read as the same decal twice.
            auto tintOf = [](uint32_t hs) {
                static const Color T[4] = { { 255, 255, 255, 255 }, { 236, 228, 214, 255 },
                                            { 216, 210, 212, 255 }, { 248, 234, 208, 255 } };
                return T[(hs >> 17) & 3];   // alpha stays 255: see the shader's alpha coding
            };
            auto tiltOf = [](uint32_t hs) { return ((int)((hs >> 12) & 15) - 7.5f) * 0.0085f; };
            if (nv == WALL_SOLID) {
                uint32_t hs = ih(gi, gk, sseed() ^ 0x5C1Bu);
                if (hs % SCRAWL_RATE == 0 && ((hs & 8) ? flN : flS)) {
                    float u0, v0, u1, v1; uvOf((hs >> 5) % SCRAWL_PHRASES, u0, v0, u1, v1);
                    float y0 = nb + 0.95f + ((hs >> 9) & 3) * 0.12f, y1 = y0 + 0.66f;
                    float x0 = gx + 0.28f, x1 = gx + 1.72f;
                    float zf = (hs & 8) ? gz + WT + 0.006f : gz - WT - 0.006f;
                    float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
                    float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
                    float an = tiltOf(hs), cq = cosf(an), sq = sinf(an);
                    auto co = [&](float sx, float sy) {
                        return Vector3{ mx + sx * hw * cq - sy * hh * sq, my + sx * hw * sq + sy * hh * cq, zf };
                    };
                    Color tc = tintOf(hs);
                    if (hs & 8) scr.quad(co(-1,-1), co(1,-1), co(1,1), co(-1,1), {0,0,1},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
                    else        scr.quad(co(1,-1), co(-1,-1), co(-1,1), co(1,1), {0,0,-1},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
                }
            }
            if (wv == WALL_SOLID) {
                uint32_t hs = ih(gi, gk, sseed() ^ 0x5C2Du);
                if (hs % SCRAWL_RATE == 0 && ((hs & 8) ? flN : flW)) {
                    float u0, v0, u1, v1; uvOf((hs >> 5) % SCRAWL_PHRASES, u0, v0, u1, v1);
                    float y0 = wb + 0.95f + ((hs >> 9) & 3) * 0.12f, y1 = y0 + 0.66f;
                    float z0 = gz + 0.28f, z1 = gz + 1.72f;
                    float xf = (hs & 8) ? gx + WT + 0.006f : gx - WT - 0.006f;
                    float mz = (z0 + z1) * 0.5f, my = (y0 + y1) * 0.5f;
                    float hd = (z1 - z0) * 0.5f, hh = (y1 - y0) * 0.5f;
                    float an = tiltOf(hs), cq = cosf(an), sq = sinf(an);
                    auto co = [&](float sz, float sy) {
                        return Vector3{ xf, my + sz * hd * sq + sy * hh * cq, mz + sz * hd * cq - sy * hh * sq };
                    };
                    Color tc = tintOf(hs);
                    if (hs & 8) scr.quad(co(1,-1), co(-1,-1), co(-1,1), co(1,1), {1,0,0},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
                    else        scr.quad(co(-1,-1), co(1,-1), co(1,1), co(-1,1), {-1,0,0},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
                }
            }
        }
        if (dd.pillar[i][kk]) {
            addBoxSides(wa, gx + 0.42f, fyc, gz + 0.42f, gx + 1.58f, cyc, gz + 1.58f);
            uint32_t sh = ih(gi0, gk0, sseed() ^ 0x5BA1u);
            if (level == 1 && sh % 3 == 0) {   // a column with its cover blown off
                int f = (int)((sh >> 3) & 3);
                const Vector3 NS[4] = { {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0} };
                Vector3 n = NS[f], u = (f < 2) ? Vector3{ 1, 0, 0 } : Vector3{ 0, 0, 1 };
                float off = ((sh >> 6) & 1) ? 0.28f : -0.28f;   // toward a corner, where it goes first
                Vector3 c = { gx + 1.0f + n.x * 0.58f + u.x * off, fyc + 0.7f + ((sh >> 8) & 15) / 15.0f * 2.0f,
                              gz + 1.0f + n.z * 0.58f + u.z * off };
                addSpall(fx, pr, c, n, u, 0.24f, 0.30f + ((sh >> 12) & 7) * 0.03f, sh);
            }
            addContactShadow(ao, gx + 1.0f, gz + 1.0f, fyc, 0.0f, 0.58f, 0.58f);
            // AO up the pillar's feet and a ceiling crease around its head
            float pfy = fyc + 0.005f;
            float px0 = gx + 0.42f, px1 = gx + 1.58f, pz0 = gz + 0.42f, pz1 = gz + 1.58f, cy = cyc - 0.005f;
            aoStrip({ px0, pfy, pz0 - 0.006f }, { px1, pfy, pz0 - 0.006f }, { 0, AOH, 0 }, { 0, 0, -1 }, 0);
            aoStrip({ px0, pfy, pz1 + 0.006f }, { px1, pfy, pz1 + 0.006f }, { 0, AOH, 0 }, { 0, 0, 1 }, 0);
            aoStrip({ px0 - 0.006f, pfy, pz0 }, { px0 - 0.006f, pfy, pz1 }, { 0, AOH, 0 }, { -1, 0, 0 }, 0);
            aoStrip({ px1 + 0.006f, pfy, pz0 }, { px1 + 0.006f, pfy, pz1 }, { 0, AOH, 0 }, { 1, 0, 0 }, 0);
            aoStrip({ px0, cy, pz0 }, { px1, cy, pz0 }, { 0, 0, -AOW }, { 0, -1, 0 }, AOC);
            aoStrip({ px0, cy, pz1 }, { px1, cy, pz1 }, { 0, 0, AOW }, { 0, -1, 0 }, AOC);
            aoStrip({ px0, cy, pz0 }, { px0, cy, pz1 }, { -AOW, 0, 0 }, { 0, -1, 0 }, AOC);
            aoStrip({ px1, cy, pz0 }, { px1, cy, pz1 }, { AOW, 0, 0 }, { 0, -1, 0 }, AOC);
        }
        if (dd.prop[i][kk] != PROP_NONE) {
            PropSite site = { gx + 1.0f, gz + 1.0f,
                              dd.elev[i][kk] * ELEV_UNIT,     // furniture sits on the local floor
                              dd.propRot[i][kk] * 1.5708f,    // a quarter turn at a time
                              cx * CCELLS + i, cz * CCELLS + kk };
            addProp(dd.prop[i][kk], site, sseed(), level, pr, ce, ao);
        }
    }
    // ---- the flights that rise from this storey (stampFeature has the plan).
    // Steps belong to the storey they stand on; the storey above draws only
    // its hole, its rails and the floor you arrive on.
    for (int q = 0; q < d.nfeat; q++) {
        const VertFeat &f = d.feats[q];
        if (f.lo != qs) continue;
        auto to = [&](float u, float y, float v) { return featureWorld(f, cx, cz, u, y, v); };
        const float R = storeyH / 24.0f;
        if (f.kind == VK_STAIRWELL) {
            // Lane A: 12 risers from the foot of the stair (plain floor, drawn
            // with the rest) to the half landing.
            const float G = 4.0f / 12.0f;
            Vector3 a, b, c, e;
            for (int i = 0; i < 12; i++)
                addStep(fl, pr, to, 0, CELL, 2 + i * G, 2 + (i + 1) * G, 0, (i + 1) * R, true, false);
            // The half landing, right across the shaft at the far end.
            a = to(0, 12 * R, 6); b = to(2 * CELL, 12 * R, 6); c = to(2 * CELL, 12 * R, 8); e = to(0, 12 * R, 8);
            fl.quad(a, b, c, e, { 0, 1, 0 }, { a.x / 2, a.z / 2 }, { b.x / 2, b.z / 2 }, { c.x / 2, c.z / 2 },
                    { e.x / 2, e.z / 2 }, WHITE);
            // Lane B: 12 more, climbing back toward the door above. Its top
            // landing is the upper storey's floor, drawn by that storey.
            for (int j = 0; j < 12; j++)
                addStep(fl, pr, to, CELL, 2 * CELL, 6 - (j + 1) * G, 6 - j * G, 0, 12 * R + (j + 1) * R, false, false);
            // The landing light: a batten on the end wall, 2.25 m over the half
            // landing, where it lights both flights. Its steel body is here;
            // its tube is drawn by the renderer, because it goes out in a
            // blackout and a chunk mesh cannot. It is the shaft's only light:
            // the tray grid has no fitting over an opening.
            Vector3 lamp = landingLamp(f, cx, cz);
            Vector3 lo2 = to(CELL - 0.55f, lamp.y - 0.06f, 8 - WT - 0.13f), hi2 = to(CELL + 0.55f, lamp.y + 0.06f, 8 - WT);
            addSolidBox(pr, std::min(lo2.x, hi2.x), lamp.y - 0.06f, std::min(lo2.z, hi2.z),
                        std::max(lo2.x, hi2.x), lamp.y + 0.06f, std::max(lo2.z, hi2.z), Color{ 150, 150, 142, 254 });
        } else if (f.kind == VK_STAIR || f.stairU >= 0) {
            // A straight flight: 24 risers over rows 1..4, between its wall and
            // its balustrade (both of which cover the steps' ends).
            int s0 = f.kind == VK_STAIR ? 0 : f.stairU, s1 = f.kind == VK_STAIR ? f.wu - 1 : f.stairU;
            const float G = 8.0f / 24.0f;
            for (int i = 0; i < 24; i++)
                addStep(fl, pr, to, s0 * CELL, (s1 + 1) * CELL, 2 + i * G, 2 + (i + 1) * G, 0, (i + 1) * R, true, false);
        }
    }
    if (d.manila) {
        float mx = wx + (MANILA_HI + 1 - 2) * CELL, mz = wz + (MANILA_HI + 1 - 2) * CELL;
        addManilaRoom(pr, fx, ao, mx, mz, ceilY(cellOf(mx), cellOf(mz)), ih(cx, cz, sseed() ^ 0x3A11u));
    }
    if (level == 3 || level == 1) {
        // Service pipework — the Red Halls' plumbing, and Level 1's: a warehouse
        // with "a consistent supply of water and electricity" has to carry both
        // somewhere, and the ceiling is where it does. Runs are decided per *row* rather than per cell, so a
        // pipe follows a whole corridor the way a real service run does instead of
        // appearing in patches. Consecutive cells emit abutting segments, so the
        // run reads as one continuous pipe.
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            float gx = wx + i * CELL, gz = wz + kk * CELL;
            int gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            // pipes hug the walls they run beside
            // Through the accessors, like the wall geometry above: a pipe hugs
            // a wall, so an edge the building has closed behind you should grow
            // one on the rebake rather than stay bare.
            if (wallNVal(gi, gk) == WALL_SOLID) {
                uint32_t rh = ih(gk, 7717, sseed() ^ 0x9191u);
                if (rh % 4 == 0) {
                    float py = ceilY(gi, gk) - 0.22f - ((rh >> 5) & 3) * 0.09f;
                    float side = ((rh >> 9) & 1) ? 0.34f : -0.34f;
                    float r = 0.065f + ((rh >> 11) & 3) * 0.012f;
                    Color pc = ((rh >> 13) & 1) ? Color{ 78, 54, 40, 255 }   // rusted iron
                                                : Color{ 62, 60, 66, 255 };  // dull steel
                    addSolidBox(pr, gx, py - r, gz + side - r, gx + CELL, py + r, gz + side + r, pc);
                    if (((gi * 2654435761u) & 3) == 0)   // a collar every few metres
                        addSolidBox(pr, gx + 0.85f, py - r - 0.03f, gz + side - r - 0.03f,
                                    gx + 1.15f, py + r + 0.03f, gz + side + r + 0.03f,
                                    Color{ 96, 74, 56, 255 });
                }
            }
            if (wallWVal(gi, gk) == WALL_SOLID) {
                uint32_t rh = ih(gi, 3313, sseed() ^ 0x9292u);
                if (rh % 4 == 0) {
                    float py = ceilY(gi, gk) - 0.22f - ((rh >> 5) & 3) * 0.09f;
                    float side = ((rh >> 9) & 1) ? 0.34f : -0.34f;
                    float r = 0.065f + ((rh >> 11) & 3) * 0.012f;
                    Color pc = ((rh >> 13) & 1) ? Color{ 78, 54, 40, 255 }
                                                : Color{ 62, 60, 66, 255 };
                    addSolidBox(pr, gx + side - r, py - r, gz, gx + side + r, py + r, gz + CELL, pc);
                    if (((gk * 2654435761u) & 3) == 0)
                        addSolidBox(pr, gx + side - r - 0.03f, py - r - 0.03f, gz + 0.85f,
                                    gx + side + r + 0.03f, py + r + 0.03f, gz + 1.15f,
                                    Color{ 96, 74, 56, 255 });
                }
            }
            // valve station: a standpipe floor to ceiling, wheel drawn by the renderer
            if (valveAt(gi, gk)) {
                float vx = gx + 1.0f, vz = gz + 1.0f, fy = dd.elev[i][kk] * ELEV_UNIT;
                addSolidBox(pr, vx - 0.085f, fy, vz - 0.085f, vx + 0.085f, ceilY(gi, gk), vz + 0.085f,
                            Color{ 84, 60, 44, 255 });
                addSolidBox(pr, vx - 0.13f, fy + 1.02f, vz - 0.13f, vx + 0.13f, fy + 1.24f, vz + 0.13f,
                            Color{ 104, 80, 58, 255 });   // the body the wheel sits on
            }
        }
    }
    if (level == 4) {   // crepe streamers sag from the ceiling, in pairs of quads
        Rng srng(hash64(key(cx, cz) ^ 0xFE57AULL ^ (uint64_t)sseed()));
        int ns = 3 + srng.ri(0, 3);
        for (int s = 0; s < ns; s++) {
            float ax = wx + srng.f01() * CHUNK, az = wz + srng.f01() * CHUNK;
            float bx2 = ax + (srng.f01() - 0.5f) * 9, bz2 = az + (srng.f01() - 0.5f) * 9;
            float mx2 = (ax + bx2) * 0.5f, mz2 = (az + bz2) * 0.5f;
            float cA = ceilY((int)floorf(ax / CELL), (int)floorf(az / CELL));
            float cB = ceilY((int)floorf(bx2 / CELL), (int)floorf(bz2 / CELL));
            float ytop = std::min(cA, cB) - 0.03f, ymid = ytop - 0.52f - srng.f01() * 0.35f;
            Color sc = PARTY[srng.ri(0, 4)];
            float dx2 = bx2 - ax, dz2 = bz2 - az, dl = sqrtf(dx2 * dx2 + dz2 * dz2) + 1e-4f;
            Vector3 nrm = { dz2 / dl, 0, -dx2 / dl };
            Vector2 uvp = { 0.375f, 0.75f };   // plain-metal corner of the prop atlas: flat colour
            pr.quad({ ax, ytop, az }, { mx2, ymid + 0.06f, mz2 }, { mx2, ymid, mz2 }, { ax, ytop - 0.06f, az },
                    nrm, uvp, uvp, uvp, uvp, sc);
            pr.quad({ mx2, ymid + 0.06f, mz2 }, { bx2, ytop, bz2 }, { bx2, ytop - 0.06f, bz2 }, { mx2, ymid, mz2 },
                    nrm, uvp, uvp, uvp, uvp, sc);
        }
    }
    d.meshes[MESH_FLOOR]   = fl.bake();
    d.meshes[MESH_CEILING] = ce.bake();
    d.meshes[MESH_WALLS]   = wa.bake();
    d.meshes[MESH_PROPS]   = pr.bake();
    d.meshes[MESH_WATER]   = wt.bake();
    d.meshes[MESH_SCRAWL]  = scr.bake();
    d.meshes[MESH_FIXTURES] = fx.bake();
    d.meshes[MESH_GLASS]   = gl.bake();
    d.meshes[MESH_AO]      = ao.bake();
    d.built = true;
}

// Solid boxes in one cell, appended to out[]. Everything that has to know what
// is in the way — collision, standing height, line of sight — reads the world
// through this, so they all agree on where the furniture is.
int World::gatherCellAABBs(int ci, int ck, AABB *out, int cap, int cnt, bool includeProps) {
    float x0 = ci * CELL, z0 = ck * CELL;
    uint8_t nv = wallNVal(ci, ck), wv = wallWVal(ci, ck);
    // Full-height blockers report FULL_H, not wallH. Standing on something tall
    // used to be enough to step over a wall, which nothing could do — until
    // flights put bodies four metres up beside walls that climb a whole storey.
    // A rail is the same box, marked see-through: waist high, so it stops a
    // body at any height and stops no look at all.
    //
    // Unlike a wall's, a rail's box stops at the ends of its edge. A wall
    // overhangs by WT so corners close; a rail overhanging would reach into a
    // stairwell's wall beside it, and a body in that stairwell's corner would
    // brush a rail that exists on one storey and not the next — a jolt at the
    // very moment the frame changes (tools/regression.cpp checks for it).
    if (cnt < cap && nv == WALL_RAIL) out[cnt++] = { x0, z0 - RAIL_T, x0 + CELL, z0 + RAIL_T, FULL_H, true };
    else if (cnt < cap && blocksEdge(nv)) out[cnt++] = { x0 - WT, z0 - WT, x0 + CELL + WT, z0 + WT, FULL_H };
    if (cnt < cap && wv == WALL_RAIL) out[cnt++] = { x0 - RAIL_T, z0, x0 + RAIL_T, z0 + CELL, FULL_H, true };
    else if (cnt < cap && blocksEdge(wv)) out[cnt++] = { x0 - WT, z0 - WT, x0 + WT, z0 + CELL + WT, FULL_H };
    // A doorway is passable — blocksEdge says so, and pathfinding, line of sight
    // and the light all take it at that. Its jambs are not: without these two
    // boxes you walk through the frame, which is worse than the bare gap the
    // doorway replaced. The opening they leave is 1.3 m, comfortably wider than
    // the player's 0.34 m radius and Clark's 0.38 m.
    // The mesher drops the jamb between two doorways in neighbouring cells and
    // makes them one wide opening, so the jamb boxes have to go with it. Leave
    // them in and the player walks into a 0.7 m pier that is not there.
    if (nv == WALL_DOOR) {
        if (cnt < cap && wallNVal(ci - 1, ck) != WALL_DOOR)
            out[cnt++] = { x0 - WT, z0 - WT, x0 + 0.35f, z0 + WT, FULL_H };
        if (cnt < cap && wallNVal(ci + 1, ck) != WALL_DOOR)
            out[cnt++] = { x0 + 1.65f, z0 - WT, x0 + CELL + WT, z0 + WT, FULL_H };
    }
    if (wv == WALL_DOOR) {
        if (cnt < cap && wallWVal(ci, ck - 1) != WALL_DOOR)
            out[cnt++] = { x0 - WT, z0 - WT, x0 + WT, z0 + 0.35f, FULL_H };
        if (cnt < cap && wallWVal(ci, ck + 1) != WALL_DOOR)
            out[cnt++] = { x0 - WT, z0 + 1.65f, x0 + WT, z0 + CELL + WT, FULL_H };
    }
    if (cnt < cap && pillarAt(ci, ck)) out[cnt++] = { x0 + 0.42f, z0 + 0.42f, x0 + 1.58f, z0 + 1.58f, FULL_H };
    // A riser taller than one step is terrain, not a ramp. The box covers the
    // whole high cell and tops out at its floor, which is the same trick the
    // furniture above uses: collideCircle skips any box you are already standing
    // level with, so a body up here walks over it freely while a body down there
    // is stopped at the face and has to find another way round.
    //
    // Pools are exempt on both sides: submerged risers and assisted climbs
    // are handled by the poolAt branches in the mover, which
    // a blocker here would override. Today the generator relaxes every terrace
    // to within MAX_STEP, so nothing it produces trips this — it is here so the
    // drops WORLD-03..WORLD-10 want to add are solid the day they land, and
    // tools/regression.cpp exercises it directly rather than trusting that.
    if (cnt < cap && !poolAt(ci, ck)) {
        float h = floorY(ci, ck);
        const int NB[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        for (int q = 0; q < 4; q++) {
            int na = ci + NB[q][0], nb = ck + NB[q][1];
            if (poolAt(na, nb)) continue;
            if (h - floorY(na, nb) > MAX_STEP) {
                out[cnt++] = { x0, z0, x0 + CELL, z0 + CELL, h };
                break;
            }
        }
    }
    if (includeProps && cnt < cap) {
        uint8_t pv = propAt(ci, ck);
        if (pv) {
            float ey = floorY(ci, ck);
            uint32_t h = ih(ci, ck, sseed() ^ 0xB0B5u);   // same hash the mesher uses
            float r1 = (h & 0xFF) / 255.0f, r2 = ((h >> 8) & 0xFF) / 255.0f;
            // Tops here must match the heights addProp actually builds, and
            // Game::bottleShelfY quotes the same numbers again for the surfaces
            // you can find a carton standing on. Move one, move all three.
            // PROP_FALLEN_TILE is missing on purpose: rubble you walk over.
            switch (pv) {
            case PROP_BOXES: {   // top of the tallest stacked one
                float t = 0.55f + r1 * 0.2f;
                if (r2 > 0.35f) t += 0.5f;
                out[cnt++] = { x0 + 0.35f, z0 + 0.35f, x0 + 1.65f, z0 + 1.65f, ey + t }; break;
            }
            case PROP_CABINET:     out[cnt++] = { x0 + 0.60f, z0 + 0.60f, x0 + 1.40f, z0 + 1.40f, ey + 1.32f }; break;
            case PROP_TABLE:       out[cnt++] = { x0 + 0.32f, z0 + 0.32f, x0 + 1.68f, z0 + 1.68f, ey + 0.72f }; break;
            case PROP_COUCH:       out[cnt++] = { x0 + 0.20f, z0 + 0.38f, x0 + 1.80f, z0 + 1.62f, ey + 0.44f }; break;
            case PROP_ARMOIRE:     out[cnt++] = { x0 + 0.55f, z0 + 0.62f, x0 + 1.45f, z0 + 1.38f, ey + 1.90f }; break;
            case PROP_LAMP:        out[cnt++] = { x0 + 0.82f, z0 + 0.82f, x0 + 1.18f, z0 + 1.18f, ey + 1.62f }; break;
            case PROP_NIGHTSTAND:  out[cnt++] = { x0 + 0.66f, z0 + 0.66f, x0 + 1.34f, z0 + 1.34f, ey + 0.60f }; break;
            case PROP_BED:         out[cnt++] = { x0 + 0.30f, z0 + 0.15f, x0 + 1.70f, z0 + 1.85f, ey + 0.46f }; break;
            case PROP_VENDING:     out[cnt++] = { x0 + 0.50f, z0 + 0.58f, x0 + 1.50f, z0 + 1.42f, ey + 1.85f }; break;
            case PROP_PARTY_TABLE: out[cnt++] = { x0 + 0.40f, z0 + 0.40f, x0 + 1.60f, z0 + 1.60f, ey + 0.74f }; break;
            case PROP_DESK:        out[cnt++] = { x0 + 0.30f, z0 + 0.50f, x0 + 1.70f, z0 + 1.50f, ey + 0.74f }; break;
            case PROP_SHELVING:    out[cnt++] = { x0 + 0.36f, z0 + 0.70f, x0 + 1.64f, z0 + 1.30f, ey + 1.80f }; break;
            case PROP_COOLER:      out[cnt++] = { x0 + 0.78f, z0 + 0.78f, x0 + 1.22f, z0 + 1.22f, ey + 0.94f }; break;
            case PROP_PLANT:       out[cnt++] = { x0 + 0.80f, z0 + 0.80f, x0 + 1.20f, z0 + 1.20f, ey + 0.32f }; break;
            }
        }
    }
    return cnt;
}

// feetY: obstacles whose top is at or below your feet are walkable, not solid
void World::collideCircle(float &px, float &pz, float r, float feetY) {
    AABB boxes[MAX_NEARBY_AABBS];
    int cnt = 0;
    int ci = cellOf(px), ck = cellOf(pz);
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            cnt = gatherCellAABBs(ci + dx, ck + dz, boxes, MAX_NEARBY_AABBS, cnt);
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
    int ci = cellOf(x), ck = cellOf(z);
    uint8_t vf = storeyH > 0.0f ? vflagAt(ci, ck) : 0;
    if (vf & VF_HOLE) {
        // No floor on this storey: what you stand on is whatever is under the
        // hole, one storey down — the flight coming up through it, or the floor
        // of the hall an atrium opens onto. Asked in that storey's own frame and
        // brought back into this one. Bounded, so a mistake in the generator is
        // a long fall and not a stack overflow.
        if (qs <= storey - STOREY_REACH) return -STOREY_REACH * storeyH;
        StoreyScope sc(*this, qs - 1);
        return groundAt(x, z, feetY + storeyH) - storeyH;
    }
    // A rotten patch is dished in the mesh, so walk into it rather than across
    // the top of it: the give underfoot is the warning, and a player standing
    // level on a floor that is visibly bowed under them is not warned of
    // anything. Furniture tops below still win, as they always did.
    float g = (vf & VF_STAIR) ? stairY(x, z) : floorY(ci, ck) - softDip(x, z);
    AABB boxes[MAX_NEARBY_AABBS];
    int cnt = 0;
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            cnt = gatherCellAABBs(ci + dx, ck + dz, boxes, MAX_NEARBY_AABBS, cnt);
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
        AABB boxes[MAX_NEARBY_AABBS];
        int cnt = 0, ci = cellOf(x), ck = cellOf(z);
        for (int ddx = -1; ddx <= 1; ddx++)
            for (int ddz = -1; ddz <= 1; ddz++)
                cnt = gatherCellAABBs(ci + ddx, ck + ddz, boxes, MAX_NEARBY_AABBS, cnt, false);  // props don't block sight
        // Only full-height blockers stop a sight line. Props are already out via
        // the flag above; the elevation risers are the other short box, and a
        // knee-high terrace lip does not hide anything — treating one as opaque
        // would blind every actor standing on a terrace, itself included.
        // Storeys did not change this: each is its own floorplan, so a sight
        // line is always drawn on one of them, and the callers that care about
        // a flight's worth of height between two actors test it themselves.
        // A rail is full height to a body and waist height to an eye.
        for (int i = 0; i < cnt; i++)
            if (boxes[i].top >= wallH - 0.01f && !boxes[i].seeThrough &&
                x > boxes[i].minx && x < boxes[i].maxx && z > boxes[i].minz && z < boxes[i].maxz) return false;
    }
    return true;
}

// The maze is a 2D floorplan extruded floor-to-ceiling, so light occlusion is a
// 2D problem. Snapshot the cells around the player into a byte grid and the
// fragment shader can march it to answer "does this light reach this point".
// Only full-height blockers count: an exit doorway (2) is a hole, a window (3)
// is glass, and furniture is too short to seal a cell — all let light through.
void World::buildOccupancy(int originI, int originK, int n, unsigned char *out) {
    // The storey you are on, through the accessors, so the overlays — walls the
    // building closed behind you, doors you unlocked — shadow like the walls
    // they are.
    for (int z = 0; z < n; z++)
        for (int x = 0; x < n; x++) {
            int ci = originI + x, ck = originK + z;
            unsigned char v = 0;
            if (blocksLight(wallNVal(ci, ck))) v |= 1;
            if (blocksLight(wallWVal(ci, ck))) v |= 2;
            if (pillarAt(ci, ck)) v |= 4;
            if (storeyH > 0.0f) {
                // A fitting is centred on this cell's min corner (every panel
                // centre is a cell corner) and overhangs all four cells round
                // it; if any of them is open to the storey above, the fitting
                // would hang in the opening, so the mesher leaves it out and
                // the shader must not light from it.
                uint8_t own = vflagAt(ci, ck);
                uint8_t corner = own | vflagAt(ci - 1, ck) | vflagAt(ci, ck - 1) | vflagAt(ci - 1, ck - 1);
                if (own & VF_OPENUP) v |= 16;
                if (corner & VF_OPENUP) v |= 8;
                // ...and bit 5: a hole touches this fitting's corner, so its
                // light falls through into the storey below as well.
                if (corner & VF_HOLE) v |= 32;
            }
            unsigned char *o = out + (z * n + x) * 4;
            o[0] = v; o[1] = o[2] = o[3] = 0;
        }
    if (storeyH <= 0.0f) return;
    // The storeys below (byte 1) and above (byte 2): only ever seen through an
    // opening, and only their chunks near one are loaded, so read what is
    // there and generate nothing. Their overlays are left out: the building
    // only rearranges the floor you are on.
    for (int rel = -1; rel <= 1; rel += 2) {
        auto &m = layer(storey + rel);
        const int ch = rel < 0 ? 1 : 2;
        auto peek = [&](int ci, int ck) -> const ChunkData * {
            auto it = m.find(key(fdiv(ci, CCELLS), fdiv(ck, CCELLS)));
            return it == m.end() ? nullptr : &it->second;
        };
        auto flagOf = [&](int ci, int ck) -> uint8_t {
            const ChunkData *d = peek(ci, ck);
            return d ? d->vflag[ci - fdiv(ci, CCELLS) * CCELLS][ck - fdiv(ck, CCELLS) * CCELLS] : 0;
        };
        for (int z = 0; z < n; z++)
            for (int x = 0; x < n; x++) {
                int ci = originI + x, ck = originK + z;
                const ChunkData *d = peek(ci, ck);
                if (!d) continue;
                int li = ci - fdiv(ci, CCELLS) * CCELLS, lk = ck - fdiv(ck, CCELLS) * CCELLS;
                unsigned char v = 0;
                if (blocksLight(d->wallN[li][lk])) v |= 1;
                if (blocksLight(d->wallW[li][lk])) v |= 2;
                if (d->pillar[li][lk]) v |= 4;
                uint8_t corner = d->vflag[li][lk] | flagOf(ci - 1, ck) | flagOf(ci, ck - 1) | flagOf(ci - 1, ck - 1);
                if (d->vflag[li][lk] & VF_OPENUP) v |= 16;
                if (corner & VF_OPENUP) v |= 8;
                if (corner & VF_HOLE) v |= 32;
                out[(z * n + x) * 4 + ch] = v;
            }
    }
    // Bit 6: an opening is near enough that one of the nine fittings the
    // shader sums for a point in this cell could be missing, or this cell
    // could be under one. The shader only looks fittings up where it is set,
    // so everywhere else pays one fetch rather than ten. The nine sit within
    // 1.5 grid pitches of the point; one more cell for rounding and one for
    // the shadow lookup's 16 cm bias off the surface.
    const int R = (int)ceilf(0.75f * LEVELS[level].ls) + 2;
    std::vector<unsigned char> row((size_t)n * n);
    for (int ch = 0; ch < 3; ch++) {
        for (int z = 0; z < n; z++)          // along x
            for (int x = 0; x < n; x++) {
                unsigned char hit = 0;
                for (int d = -R; d <= R && !hit; d++) {
                    int xx = x + d;
                    if (xx >= 0 && xx < n && (out[(z * n + xx) * 4 + ch] & (8 | 16 | 32))) hit = 1;
                }
                row[z * n + x] = hit;
            }
        for (int z = 0; z < n; z++)          // then along z
            for (int x = 0; x < n; x++)
                for (int d = -R; d <= R; d++) {
                    int zz = z + d;
                    if (zz >= 0 && zz < n && row[zz * n + x]) { out[(z * n + x) * 4 + ch] |= 64; break; }
                }
    }
}

bool World::canStep(int ci, int ck, int ni, int nk) {
    if (pillarAt(ni, nk) || propAt(ni, nk) != PROP_NONE) return false;   // furniture and pillars are solid
    // Storeys: a hole with nothing coming up through it is a drop, not a
    // route. A flight and the hole it rises through are walkable, and their
    // heights are continuous along every edge the walls and rails leave open,
    // so the cell-height riser rule below — which reads the nominal floor, 0
    // on every stair cell — has nothing to say about them.
    uint8_t fa = storeyH > 0.0f ? vflagAt(ci, ck) : 0, fb = storeyH > 0.0f ? vflagAt(ni, nk) : 0;
    if ((fb & VF_HOLE) && !(fb & VF_WALKHOLE)) return false;
    bool vertical = ((fa | fb) & (VF_STAIR | VF_HOLE)) != 0;
    // A body cannot route up or down a face it cannot walk. The BFS used to
    // test walls alone, so the pack and Clark crossed a terrace edge as if it
    // were flat and stood 1.2 m inside a loading dock. Pools stay passable:
    // wading in and out of one is movement the mover handles, not a wall.
    if (!vertical && !poolAt(ci, ck) && !poolAt(ni, nk) &&
        fabsf(floorY(ni, nk) - floorY(ci, ck)) > MAX_STEP) return false;
    // the edge the two cells share: a wall or window blocks it, a doorway does not
    if (nk == ck - 1)      { if (blocksEdge(wallNVal(ci, ck)))     return false; }
    else if (nk == ck + 1) { if (blocksEdge(wallNVal(ci, ck + 1))) return false; }
    else if (ni == ci - 1) { if (blocksEdge(wallWVal(ci, ck)))     return false; }
    else if (ni == ci + 1) { if (blocksEdge(wallWVal(ci + 1, ck))) return false; }
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
                // never on a flight or over a hole: an arrival, a spawn or a
                // dropped doubloon wants a floor
                if (!pillarAt(ci0 + dx, ck0 + dz) && propAt(ci0 + dx, ck0 + dz) == PROP_NONE &&
                    !poolAt(ci0 + dx, ck0 + dz) &&
                    !(storeyH > 0.0f && (vflagAt(ci0 + dx, ck0 + dz) & (VF_STAIR | VF_HOLE))))
                    return { (ci0 + dx) * CELL + 1.0f, (ck0 + dz) * CELL + 1.0f };
            }
    return { x, z };
}

static void unloadChunkMeshes(ChunkData &c) {
    if (!c.built) return;
    for (int i = 0; i < MESH_COUNT; i++)
        if (c.meshes[i].vertexCount > 0) UnloadMesh(c.meshes[i]);
}

void World::unloadFar(int pcx, int pcz, int radius) {
    auto sweep = [&](std::unordered_map<uint64_t, ChunkData> &m) {
        for (auto it = m.begin(); it != m.end();) {
            int cx = (int)(int32_t)(it->first >> 32), cz = (int)(int32_t)(it->first & 0xFFFFFFFF);
            if (abs(cx - pcx) > radius || abs(cz - pcz) > radius) {
                unloadChunkMeshes(it->second);
                it = m.erase(it);
            } else ++it;
        }
    };
    sweep(chunks);
    // The storeys next to yours are drawn through the openings and kept by the
    // same radius; anything further up or down is a floor you have left behind
    // and will be regenerated identically if you ever climb back to it.
    for (auto it = layers.begin(); it != layers.end();) {
        if (abs(it->first - storey) > 2) {
            for (auto &kv : it->second) unloadChunkMeshes(kv.second);
            it = layers.erase(it);
        } else { sweep(it->second); ++it; }
    }
}

void World::unloadAll() {
    shifted.clear();   // a different floor is a different building; it has not moved on you yet
    for (auto &kv : chunks) unloadChunkMeshes(kv.second);
    chunks.clear();
    for (auto &lv : layers) for (auto &kv : lv.second) unloadChunkMeshes(kv.second);
    layers.clear();
}


// One almond water can, built at life size with its base on y=0 so a transform
// can just put the base where it belongs — on a table, or in your hand. UVs
// index makeAlmondWrapTex: the barrel takes the label strip once round, and the
// two caps take the lid and base squares below it. Alpha 255 puts it down the
// shader's textured branch, so the room lights it like everything else.
Mesh buildCanMesh() {
    MB b;
    const int N = 24;
    const float R = 0.033f, H = 0.122f;          // 66mm across, 122mm tall
    const float SV = 128.0f / 192.0f;            // the label strip ends here in v
    // alpha 254, not 255: textured and opaque, but out of the shader's
    // world-space relief bump, which has no business on a drinks can
    const Color w = { 255, 255, 255, 254 };
    // the barrel, as a stack of rings: a roll at the base, the straight wall,
    // then the shoulder drawing in to the lid
    const float ry[4] = { 0.0f,        H * 0.035f, H * 0.90f, H };
    const float rr[4] = { R * 0.90f,   R,          R,         R * 0.86f };
    const float rv[4] = { SV,          SV * 0.96f, SV * 0.07f, 0.0f };
    for (int i = 0; i < N; i++) {
        float a0 = i * TAU / N, a1 = (i + 1) * TAU / N;
        // u runs backwards round the barrel: on the face turned toward you,
        // increasing angle travels screen-left, so mapping u forwards puts the
        // wordmark on mirrored
        float u0 = 1.0f - i / (float)N, u1 = 1.0f - (i + 1) / (float)N;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        for (int k = 0; k < 3; k++) {
            Vector3 p00 = { c0 * rr[k],     ry[k],     s0 * rr[k] };
            Vector3 p10 = { c1 * rr[k],     ry[k],     s1 * rr[k] };
            Vector3 p11 = { c1 * rr[k + 1], ry[k + 1], s1 * rr[k + 1] };
            Vector3 p01 = { c0 * rr[k + 1], ry[k + 1], s0 * rr[k + 1] };
            // Smooth geometric normals, including the shoulder slope. The old
            // upward cant made the barrel glow like a flat label in side light.
            float ny=(rr[k]-rr[k+1])/(ry[k+1]-ry[k]);
            float inv=1/sqrtf(1+ny*ny);
            b.quad(p00,p10,p11,p01,{c0*inv,ny*inv,s0*inv},
                   {u0,rv[k]},{u1,rv[k]},{u1,rv[k+1]},{u0,rv[k+1]},w);
            Vector3 normals[4]={{c0*inv,ny*inv,s0*inv},{c1*inv,ny*inv,s1*inv},
                                {c1*inv,ny*inv,s1*inv},{c0*inv,ny*inv,s0*inv}};
            size_t start=b.n.size()-12;
            for(int j=0;j<4;++j) {b.n[start+j*3]=normals[j].x;b.n[start+j*3+1]=normals[j].y;b.n[start+j*3+2]=normals[j].z;}

        }
    }
    // caps. Inset the UVs a touch so bilinear can't drag one square into the next.
    const float IN = 1.5f / 192.0f;
    auto capUV = [&](float ox, float ang) {
        float u = 0.5f + 0.5f * cosf(ang) * 0.94f, vv = 0.5f + 0.5f * sinf(ang) * 0.94f;
        return Vector2{ ox + IN + u * (64.0f / 192.0f - 2 * IN),
                        SV + IN + vv * (64.0f / 192.0f - 2 * IN) };
    };
    for (int i = 0; i < N; i++) {
        float a0 = i * TAU / N, a1 = (i + 1) * TAU / N;
        // lid, facing up: the first square in the atlas
        b.tri({ 0, H, 0 }, { cosf(a1) * rr[3], H, sinf(a1) * rr[3] },
              { cosf(a0) * rr[3], H, sinf(a0) * rr[3] }, { 0, 1, 0 },
              Vector2{32.0f/192.0f,160.0f/192.0f}, capUV(0.0f, a1), capUV(0.0f, a0), w);
        // base, facing down: wound the other way, and the second square
        b.tri({ 0, 0, 0 }, { cosf(a0) * rr[0], 0, sinf(a0) * rr[0] },
              { cosf(a1) * rr[0], 0, sinf(a1) * rr[0] }, { 0, -1, 0 },
              Vector2{96.0f/192.0f,160.0f/192.0f}, capUV(64.0f / 192.0f, a0),
              capUV(64.0f / 192.0f, a1), w);
    }
    return b.bake();
}


// The tape player, at life size with its underside on y=0, so one transform
// puts it either on the floor or in your hand. UVs index makeDeckTex's four
// tiles. Alpha 254 like the can: textured and opaque, but under the shader's
// world-space relief threshold — relief is fixed in world space, and this is a
// small object that moves, so it would swim through the noise field.
Mesh buildDeckMesh() {
    MB b;
    const float HX = 0.059f, HZ = 0.038f, HY = 0.029f;   // 118 × 76 × 29 mm
    const Color w = { 255, 255, 255, 254 };
    // tile helpers: (0,0) top, (1,0) body, (0,1) front, (1,1) reel
    auto tile = [](int tx, int ty, float u, float v) {
        const float S = 0.5f, IN = 1.0f / 128.0f;        // inset: bilinear must not cross tiles
        return Vector2{ tx * S + IN + u * (S - 2 * IN), ty * S + IN + v * (S - 2 * IN) };
    };
    auto face = [&](Vector3 a, Vector3 b2, Vector3 c, Vector3 d, Vector3 nn, int tx, int ty) {
        b.quad(a, b2, c, d, nn, tile(tx, ty, 0, 1), tile(tx, ty, 1, 1),
               tile(tx, ty, 1, 0), tile(tx, ty, 0, 0), w);
    };

    // front face (+z) carries the grille and the buttons; the rest is body
    face({ -HX, 0, HZ }, { HX, 0, HZ }, { HX, HY * 2, HZ }, { -HX, HY * 2, HZ }, { 0, 0, 1 }, 0, 1);
    face({ HX, 0, -HZ }, { -HX, 0, -HZ }, { -HX, HY * 2, -HZ }, { HX, HY * 2, -HZ }, { 0, 0, -1 }, 1, 0);
    face({ -HX, 0, -HZ }, { -HX, 0, HZ }, { -HX, HY * 2, HZ }, { -HX, HY * 2, -HZ }, { -1, 0, 0 }, 1, 0);
    face({ HX, 0, HZ }, { HX, 0, -HZ }, { HX, HY * 2, -HZ }, { HX, HY * 2, HZ }, { 1, 0, 0 }, 1, 0);
    face({ -HX, 0, -HZ }, { HX, 0, -HZ }, { HX, 0, HZ }, { -HX, 0, HZ }, { 0, -1, 0 }, 1, 0);

    // The top is the cassette bay, so it is cut as a frame rather than a slab:
    // four border strips at full height, then walls dropping to a recessed floor
    // the reels sit on. Without the recess the reels read as stickers.
    const float TY = HY * 2, BY = TY - 0.008f;            // bay floor, 8 mm down
    const float BX = HX * 0.62f, BZ = HZ * 0.42f;         // the opening
    // border strips, UV'd from the same tile so the label and frame line up
    auto topStrip = [&](float x0, float x1, float z0, float z1) {
        auto uv = [&](float x, float z) {
            // u runs backwards: with the lid's +x to the viewer's right, mapping
            // u forwards puts the printed label on mirrored
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
    const Vector2 dk = tile(0, 0, 0.5f, 0.5f);            // solidly inside the window
    auto flat = [&](Vector3 a, Vector3 b2, Vector3 c, Vector3 d, Vector3 nn) {
        b.quad(a, b2, c, d, nn, dk, dk, dk, dk, w);
    };
    flat({ -BX, BY, -BZ }, { BX, BY, -BZ }, { BX, BY, BZ }, { -BX, BY, BZ }, { 0, 1, 0 });
    flat({ -BX, BY, -BZ }, { -BX, TY, -BZ }, { BX, TY, -BZ }, { BX, BY, -BZ }, { 0, 0, 1 });
    flat({ BX, BY, BZ }, { BX, TY, BZ }, { -BX, TY, BZ }, { -BX, BY, BZ }, { 0, 0, -1 });
    flat({ -BX, BY, BZ }, { -BX, TY, BZ }, { -BX, TY, -BZ }, { -BX, BY, -BZ }, { 1, 0, 0 });
    flat({ BX, BY, -BZ }, { BX, TY, -BZ }, { BX, TY, BZ }, { BX, BY, BZ }, { -1, 0, 0 });
    return b.bake();
}

// One reel: a flat disc in the XZ plane about its own centre, so the deck can
// draw it twice and spin it. Its own mesh rather than part of the body because
// the spin is the only thing that says the tape is actually running.
Mesh buildReelMesh() {
    MB b;
    const int N = 16;
    const float R = 0.0145f;
    const Color w = { 255, 255, 255, 254 };
    auto uv = [](float ang, float rad) {
        const float S = 0.5f, IN = 1.0f / 128.0f;
        float u = 0.5f + 0.5f * cosf(ang) * rad, v = 0.5f + 0.5f * sinf(ang) * rad;
        return Vector2{ S + IN + u * (S - 2 * IN), S + IN + v * (S - 2 * IN) };
    };
    for (int i = 0; i < N; i++) {
        float a0 = i * TAU / N, a1 = (i + 1) * TAU / N;
        b.tri({ 0, 0, 0 }, { cosf(a1) * R, 0, sinf(a1) * R }, { cosf(a0) * R, 0, sinf(a0) * R },
              { 0, 1, 0 }, uv(0, 0), uv(a1, 0.97f), uv(a0, 0.97f), w);
    }
    return b.bake();
}

// The record lamp on the front. Alpha 51 (0.2) drops it into the shader's raw
// emissive branch, so it burns its own colour instead of taking room light —
// which is the point: in a blackout it is the only thing you can see of it.
Mesh buildDeckLampMesh() {
    MB b;
    const Color glow = { 255, 66, 48, 51 };
    // On the lid in front of the bay, where a recorder's record lamp sits and
    // where your eye already is. Drawn on the lid *and* down the front edge, so
    // it still reads when the deck is lying on a floor below you and the lid is
    // side-on. Deliberately oversized for an indicator — at four metres the
    // honest 3 mm of it is under a pixel, and this has to say "still running".
    // The emissive branch ignores the texture, so the UV only has to be legal.
    // Stand it 1.5 mm proud of the shell, not the tenth of a millimetre it had:
    // flush against the lid the two surfaces z-fight, and the shell wins as soon
    // as the deck is more than a couple of metres off — so the lamp read fine in
    // your hand and vanished exactly when you needed it, lying on a dark floor.
    const float x0 = 0.024f, x1 = 0.044f, y = 0.0595f, z0 = 0.019f, z1 = 0.031f;
    const Vector2 t = { 0.25f, 0.25f };
    b.quad({ x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 }, { 0, 1, 0 }, t, t, t, t, glow);
    b.quad({ x0, 0.044f, 0.0395f }, { x1, 0.044f, 0.0395f },
           { x1, y, 0.0395f }, { x0, y, 0.0395f }, { 0, 0, 1 }, t, t, t, t, glow);
    return b.bake();
}

// Axial tube with genuinely round sides. Ring end faces leave a bore, so the
// muzzle is a recess rather than a black sticker on a solid cylinder.
static void weaponTube(MB &b, float y, float z0, float z1, float radius,
                       float bore, Color metal, int sides = 24) {
    Vector2 uv{0.375f, 0.75f};
    for (int i = 0; i < sides; ++i) {
        float a = TAU * i / sides, c = TAU * (i + 1) / sides;
        Vector3 n0{cosf(a), sinf(a), 0}, n1{cosf(c), sinf(c), 0};
        Vector3 p0{radius*n0.x, y+radius*n0.y, z0}, p1{radius*n1.x, y+radius*n1.y, z0};
        Vector3 p2{p1.x, p1.y, z1}, p3{p0.x, p0.y, z1};
        Vector3 normal{cosf((a+c)*0.5f), sinf((a+c)*0.5f), 0};
        b.quad(p0,p1,p2,p3,normal,uv,uv,uv,uv,metal);
        // Interpolated radial normals keep a low-poly cylinder looking round.
        const Vector3 radial[4] = {n0,n1,n1,n0};
        size_t start = b.n.size()-12;
        for (int j=0;j<4;++j) {
            b.n[start+j*3]=radial[j].x; b.n[start+j*3+1]=radial[j].y; b.n[start+j*3+2]=0;
        }
        Vector3 q0{bore*n0.x,y+bore*n0.y,z1}, q1{bore*n1.x,y+bore*n1.y,z1};
        b.quad(p3,p2,q1,q0,{0,0,1},uv,uv,uv,uv,metal);
        Color inside{12,12,13,254};
        b.quad(q0,q1,{q1.x,q1.y,z0},{q0.x,q0.y,z0},
               {-normal.x,-normal.y,0},uv,uv,uv,uv,inside);
        b.tri({0,y,z0},p1,p0,{0,0,-1},uv,uv,uv,metal);
    }
}

// Convex side profile with a chamfered perimeter. The bevel catches narrow
// highlights without subdividing the broad faces or adding another draw call.
Mesh buildFlareMesh() {
    MB b;
    weaponTube(b,0,-0.075f,0.105f,0.016f,0,{177,43,26,254},16);
    weaponTube(b,0,0.072f,0.094f,0.017f,0,{216,204,173,254},16);
    weaponTube(b,0,0.105f,0.125f,0.0165f,0,{55,34,24,254},16);
    // Printed safety bands, attached to the tube instead of screen-space boxes.
    weaponTube(b,0,-0.059f,-0.044f,0.0164f,0,{209,191,148,254},16);
    return b.bake();
}

// ---- a Level 1 supply crate. "Crates of supplies appear and disappear
// randomly within the Level" — so they are not part of any chunk's mesh: Game
// decides where they are this minute (Game::crateAt) and draws this one mesh
// at each, base on y = 0, lid separate so an opened crate can have it off.
// Planks from the props atlas's veneer, dark battens on the edges, and a pale
// shipping label, all alpha 255 so the wood takes its grain relief.
static void crateBody(MB &mb) {
    const float WU0 = 0.51f, WV0 = 0.02f, WU1 = 0.99f, WV1 = 0.48f;
    const float CU0 = 0.02f, CV0 = 0.30f, CU1 = 0.22f, CV1 = 0.50f;          // cardboard, for the label
    const float H = 0.56f, R = 0.35f;
    Color plank = { 255, 226, 180, 255 }, batten = { 176, 138, 96, 255 };
    // the box as horizontal planks, each a little different in tone
    for (int i = 0; i < 4; i++) {
        float y0 = i * H / 4 + 0.004f, y1 = (i + 1) * H / 4 - 0.004f;
        float k = 0.88f + 0.06f * ((i * 7) % 3);
        Color t = { (unsigned char)(plank.r * k), (unsigned char)(plank.g * k), (unsigned char)(plank.b * k), 255 };
        addPropBox(mb, 0, 0, 0, R, R, y0, y1, WU0, WV0 + 0.1f * i, WU1, WV0 + 0.1f * i + 0.08f,
                   WU0, WV0, WU1, WV1, t, 0.0f);
    }
    addPropBox(mb, 0, 0, 0, R - 0.01f, R - 0.01f, 0, H, WU0, WV0, WU1, WV1, WU0, WV0, WU1, WV1,
               Color{ 60, 44, 30, 255 }, 0.0f);                              // what shows in the gaps
    for (int cx = -1; cx <= 1; cx += 2) for (int cz = -1; cz <= 1; cz += 2)   // corner battens
        addPropBox(mb, cx * (R - 0.02f), cz * (R - 0.02f), 0, 0.035f, 0.035f, 0, H + 0.004f,
                   WU0, WV0, WU0 + 0.1f, WV1, WU0, WV0, WU1, WV1, batten, 0.0f);
    for (int f = 0; f < 4; f++) {                                            // a diagonal brace per side
        float a = f * 1.5707963f;
        addPropBox(mb, cosf(a) * (R + 0.012f), sinf(a) * (R + 0.012f), a + 1.5707963f, R - 0.05f, 0.012f,
                   H * 0.44f, H * 0.56f, WU0, WV0, WU1, WV0 + 0.05f, WU0, WV0, WU1, WV1, batten, 0.0f);
    }
    // a shipping label on one face, pressed a hair off it
    Color lab = { 226, 214, 184, 254 };
    mb.quad({ -0.14f, 0.16f, R + 0.016f }, { 0.14f, 0.16f, R + 0.016f }, { 0.14f, 0.34f, R + 0.016f },
            { -0.14f, 0.34f, R + 0.016f }, { 0, 0, 1 }, { CU0, CV1 }, { CU1, CV1 }, { CU1, CV0 }, { CU0, CV0 }, lab);
}
Mesh buildCrateMesh() { MB mb; crateBody(mb); return mb.bake(); }
Mesh buildCrateLidMesh() {
    MB mb;
    const float WU0 = 0.51f, WV0 = 0.02f, WU1 = 0.99f, WV1 = 0.48f;
    addPropBox(mb, 0, 0, 0, 0.365f, 0.365f, 0, 0.04f, WU0, WV0, WU1, WV1, WU0, WV0, WU1, WV1,
               Color{ 246, 214, 170, 255 }, 0.0f);
    for (int i = -1; i <= 1; i += 2)
        addPropBox(mb, 0, i * 0.25f, 0, 0.365f, 0.035f, 0.04f, 0.06f, WU0, WV0, WU1, WV1, WU0, WV0, WU1, WV1,
                   Color{ 176, 138, 96, 255 }, 0.0f);
    return mb.bake();
}
