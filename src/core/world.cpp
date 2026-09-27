#include "world.h"
#include "hash.h"
#include "level_rules.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

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
    // A few chunks are tall courts: one aligned void through 3, 5, or 7
    // floors. Reserve the entire eight-storey band, including its caps, so
    // ordinary pair features cannot overlap the court on an end floor.
    int band=fdiv(p,8), base=band*8;
    unsigned tower=ih(cx,cz,seed ^ (unsigned)visit*0x9E3779B9u ^ 0xA771u);
    if (level==0 && (cx!=0 || cz!=0) && tower%8==0) {
        int floors=3+2*((tower>>8)%3);
        for(int st=base;st<base+8;++st) if(manilaChunk(cx,cz,st)) return false;
        if(p>=base+floors-1) return false;
        f=VertFeat{};f.lo=p;f.kind=VK_ATRIUM;f.wu=4;f.lv=4;
        f.x0=4;f.z0=4;f.dir=0;
        return true;
    }
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
Vec3 World::featureWorld(const VertFeat &f, int cx, int cz, float u, float y, float v) const {
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

bool liftHash(int gi, int gk, unsigned s) { return ih(gi, gk, s ^ 0xE1E7u) % 71 == 0; }

static constexpr float VEND_BACK_OFFSET = CELL * 0.5f - WT - 0.08f - VEND_DEPTH_BACK;
void vendFootprint(uint8_t rotByte, float cx, float cz, float &px, float &pz,
                   float &x0, float &z0, float &x1, float &z1) {
    float yaw = (rotByte & 3) * 1.5708f, ca = cosf(yaw), sa = sinf(yaw);
    float o = (rotByte & PROP_AGAINST_WALL) ? VEND_BACK_OFFSET : 0.0f;
    px = cx - o * sa; pz = cz + o * ca;                   // local +z is the back
    x0 = z0 = 1e9f; x1 = z1 = -1e9f;
    for (int c = 0; c < 4; c++) {
        float lx = (c & 1) ? VEND_HW : -VEND_HW, lz = (c & 2) ? VEND_DEPTH_BACK : -VEND_DEPTH_FRONT;
        float wx = px + lx * ca - lz * sa, wz = pz + lx * sa + lz * ca;
        x0 = std::min(x0, wx); x1 = std::max(x1, wx); z0 = std::min(z0, wz); z1 = std::max(z1, wz);
    }
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
                d.wallW[cut][r.z0 + rh / 4] = WALL_DOOR;
                d.wallW[cut][r.z1 - rh / 4] = WALL_DOOR;
                stack[sp++] = { r.x0, r.z0, cut - 1, r.z1 };
                stack[sp++] = { cut, r.z0, r.x1, r.z1 };
            } else {
                int cut = r.z0 + MINR + rng.ri(0, rh - 2 * MINR);
                for (int x = r.x0; x <= r.x1; x++) roomEdge(d.wallN[x][cut], WALL_SOLID);
                d.wallN[r.x0 + rw / 4][cut] = WALL_DOOR;
                d.wallN[r.x1 - rw / 4][cut] = WALL_DOOR;
                stack[sp++] = { r.x0, r.z0, r.x1, cut - 1 };
                stack[sp++] = { r.x0, cut, r.x1, r.z1 };
            }
        }
    }
    // Break the seam highways with shared-axis baffles. Neighbours agree on
    // the cut, so neither half of a seam provides a straight bypass. Paired
    // entrances turn the rooms into through-routes on both sides of the baffle.
    if (!openChunk && level != 2) {
        int cutX = 6 + ih(cx, 0, sseed() ^ 0xBAFFu) % 4;
        int cutZ = 6 + ih(0, cz, sseed() ^ 0xBAFFu) % 4;
        for (int t=0; t<CCELLS; ++t) {
            if (t < HALL_LO || t >= CCELLS-HALL_HI) {
                d.wallW[cutX][t] = WALL_SOLID;
                d.wallN[t][cutZ] = WALL_SOLID;
            }
        }
        for (int offset : {-2, 2}) {
            d.wallN[cutX+offset][HALL_LO] = WALL_DOOR;
            d.wallN[cutX+offset][CCELLS-HALL_HI] = WALL_DOOR;
            d.wallW[HALL_LO][cutZ+offset] = WALL_DOOR;
            d.wallW[CCELLS-HALL_HI][cutZ+offset] = WALL_DOOR;
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
            for (int kk = 1; kk < CCELLS && !placed; kk++)
                if (d.wallN[i][kk] == WALL_SOLID && d.wallN[i - 1][kk] == WALL_SOLID &&
                    d.wallN[i + 1][kk] == WALL_SOLID && !(d.prot[i][kk] & 1)) {
                    d.wallN[i][kk] = WALL_EXIT; placed = true;
                    d.pillar[i][kk]=d.pillar[i][kk-1]=0;
                    d.prop[i][kk]=d.prop[i][kk-1]=PROP_NONE;
                }
        for (int i = 1; i < CCELLS && !placed; i++)
            for (int kk = 1; kk < CCELLS - 1 && !placed; kk++)
                if (d.wallW[i][kk] == WALL_SOLID && d.wallW[i][kk - 1] == WALL_SOLID &&
                    d.wallW[i][kk + 1] == WALL_SOLID && !(d.prot[i][kk] & 2)) {
                    d.wallW[i][kk] = WALL_EXIT; placed = true;
                    d.pillar[i][kk]=d.pillar[i-1][kk]=0;
                    d.prop[i][kk]=d.prop[i-1][kk]=PROP_NONE;
                }
    }
    if (d.manila) stampManila();   // see above: its doors are its own
    // Vending machines are plugged in, so they stand with their backs to a
    // wall, facing the room. Turn each to put a solid wall behind it and open
    // floor in front, starting from the turn it was dealt so the choice stays
    // a pure function of the chunk; edges owned by the neighbouring chunk are
    // unknown here and count as open in front, never as a wall behind. This
    // runs after every pass that opens or closes an edge. A machine with no
    // wall to stand against keeps its turn and stays in the middle of its cell.
    for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
        if (d.prop[i][kk] != PROP_VENDING) continue;
        int gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
        // north, east, south, west edges of the cell; -1 where not ours
        int edge[4] = { d.wallN[i][kk], i + 1 < CCELLS ? d.wallW[i + 1][kk] : -1,
                        kk + 1 < CCELLS ? d.wallN[i][kk + 1] : -1, d.wallW[i][kk] };
        bool lift[4] = { level == 1 && liftHash(gi, gk, sseed()), false,
                         level == 1 && liftHash(gi, gk + 1, sseed()), false };
        // turn r faces -z, +x, +z, -x (the mesher's local -z is the front)
        const int FRONT[4] = { 0, 1, 2, 3 }, BACK[4] = { 2, 3, 0, 1 };
        int r0 = d.propRot[i][kk] & 3;
        for (int t = 0; t < 4; t++) {
            int r = (r0 + t) & 3;
            if (edge[BACK[r]] != WALL_SOLID || lift[BACK[r]] || edge[FRONT[r]] == WALL_SOLID) continue;
            d.propRot[i][kk] = (uint8_t)(r | PROP_AGAINST_WALL);
            break;
        }
    }
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

// Report a chunk's geometry stale so the renderer rebakes it from the current
// wall values. Its cells and props are untouched.
void World::rebuildChunk(int cx, int cz) {
    auto &m = layer(qs);
    if (m.find(key(cx, cz)) == m.end()) return;
    staleChunks.push_back({ qs, cx, cz });
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
uint8_t World::propRotAt(int ci, int ck) {
    int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    return data(cx, cz).propRot[ci - cx * CCELLS][ck - cz * CCELLS];
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

// Solid boxes in one cell, appended to out[]. Everything that has to know what
// is in the way — collision, standing height, line of sight — reads the world
// through this, so they all agree on where the furniture is.
int World::gatherCellAABBs(int ci, int ck, AABB *out, int cap, int cnt, bool includeProps) {
    float x0 = ci * CELL, z0 = ck * CELL;
    uint8_t nv = wallNVal(ci, ck), wv = wallWVal(ci, ck);
    // Full-height blockers report FULL_H, not wallH. Standing on something tall
    // used to be enough to step over a wall, which nothing could do — until
    // flights put bodies four metres up beside walls that climb a whole storey.
    // Balcony guards have their visible height so a jump can clear them.
    // Flight guards retain full-height collision across the storey rebase.
    //
    // Unlike a wall's, a rail's box stops at the ends of its edge. A wall
    // overhangs by WT so corners close; a rail overhanging would reach into a
    // stairwell's wall beside it, and a body in that stairwell's corner would
    // brush a rail that exists on one storey and not the next — a jolt at the
    // very moment the frame changes (tools/regression.cpp checks for it).
    auto railTop = [&](bool west) {
        int ai=west ? ci-1 : ci, ak=west ? ck : ck-1;
        uint8_t own=vflagAt(ci,ck), other=vflagAt(ai,ak);
        if ((own|other)&(VF_STAIR|VF_WALKHOLE)) return FULL_H;
        float top=-1e9f;
        if (!(own&VF_HOLE)) top=floorY(ci,ck);
        if (!(other&VF_HOLE)) top=std::max(top,floorY(ai,ak));
        return top+RAIL_H;
    };
    if (cnt < cap && nv == WALL_RAIL) out[cnt++] = { x0, z0 - RAIL_T, x0 + CELL, z0 + RAIL_T, railTop(false), true };
    else if (cnt < cap && blocksEdge(nv)) out[cnt++] = { x0 - WT, z0 - WT, x0 + CELL + WT, z0 + WT, FULL_H };
    if (cnt < cap && wv == WALL_RAIL) out[cnt++] = { x0 - RAIL_T, z0, x0 + RAIL_T, z0 + CELL, railTop(true), true };
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
            case PROP_VENDING: {   // wherever vendFootprint stood it
                float vx, vz, bx0, bz0, bx1, bz1;
                vendFootprint(propRotAt(ci, ck), x0 + 1.0f, z0 + 1.0f, vx, vz, bx0, bz0, bx1, bz1);
                out[cnt++] = { bx0, bz0, bx1, bz1, ey + VEND_Y1 };
                break;
            }
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
    memset(out + (size_t)n * n * 4, 0, (size_t)n * n * 4);   // the fitting masks, below
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
    // could be under one. The shader only reads the fitting masks (below)
    // where it is set, so everywhere else pays one fetch. The nine sit within
    // 1.5 grid pitches of the point; one more cell for rounding and one for
    // the shadow lookup's 16 cm bias off the surface.
    const int R = (int)ceilf(0.75f * LEVEL_RULES[level].ls) + 2;
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
    // The second n rows: for each light block, which of the nine fittings the
    // shader sums there exist, one byte per storey (bit (dx+1)*3 + (dz+1), the
    // shader's loop order) and the three ninth bits in the spare byte. A
    // fitting exists where bit 3 is clear at the cell corner under its centre.
    // Looking bit 3 up per fitting cost the shader nine fetches a fragment
    // near every opening, and nine more for the storey above: in a stair
    // shaft, where every fragment is near one, that was most of the frame.
    // Stored at every cell, for the block it lies in; the shader fetches the
    // block's first cell. Assumes the grid pitch is a whole number of cells,
    // which it is on every storeyed level.
    const float ls = LEVEL_RULES[level].ls;
    auto occByte = [&](int ci, int ck, int ch) -> int {
        int x = ci - originI, z = ck - originK;
        return (x < 0 || z < 0 || x >= n || z >= n) ? 0 : out[(z * n + x) * 4 + ch];
    };
    for (int z = 0; z < n; z++)
        for (int x = 0; x < n; x++) {
            int bx = (int)floorf((originI + x) * CELL / ls), bz = (int)floorf((originK + z) * CELL / ls);
            unsigned char *o = out + (size_t)n * n * 4 + (z * n + x) * 4;
            for (int ch = 0; ch < 3; ch++) {
                int mask = 0;
                for (int dx = -1; dx <= 1; dx++)
                    for (int dz = -1; dz <= 1; dz++) {
                        // the corner the shader looked up: floor(centre / CELL + 0.5)
                        int kx = (int)floorf(((bx + dx) * ls + ls * 0.5f) / CELL + 0.5f);
                        int kz = (int)floorf(((bz + dz) * ls + ls * 0.5f) / CELL + 0.5f);
                        if (!(occByte(kx, kz, ch) & 8)) mask |= 1 << ((dx + 1) * 3 + (dz + 1));
                    }
                o[ch] = (unsigned char)(mask & 255);
                o[3] |= (unsigned char)(((mask >> 8) & 1) << ch);
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

Vec2 World::findOpenSpot(float x, float z) {
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

// The chunk coordinates a map key was made from (World::key).
static int keyX(uint64_t k) { return (int)(int32_t)(k >> 32); }
static int keyZ(uint64_t k) { return (int)(int32_t)(k & 0xFFFFFFFF); }

void World::unloadFar(int pcx, int pcz, int radius) {
    auto sweep = [&](std::unordered_map<uint64_t, ChunkData> &m, int s) {
        for (auto it = m.begin(); it != m.end();) {
            int cx = keyX(it->first), cz = keyZ(it->first);
            if (abs(cx - pcx) > radius || abs(cz - pcz) > radius) {
                staleChunks.push_back({ s, cx, cz });
                it = m.erase(it);
            } else ++it;
        }
    };
    sweep(chunks, storey);
    // The storeys next to yours are drawn through the openings and kept by the
    // same radius; anything further up or down is a floor you have left behind
    // and will be regenerated identically if you ever climb back to it.
    for (auto it = layers.begin(); it != layers.end();) {
        if (abs(it->first - storey) > STOREY_REACH) {
            for (auto &kv : it->second) staleChunks.push_back({ it->first, keyX(kv.first), keyZ(kv.first) });
            it = layers.erase(it);
        } else { sweep(it->second, it->first); ++it; }
    }
}

void World::unloadAll() {
    shifted.clear();   // a different floor is a different building; it has not moved on you yet
    for (auto &kv : chunks) staleChunks.push_back({ storey, keyX(kv.first), keyZ(kv.first) });
    chunks.clear();
    for (auto &lv : layers)
        for (auto &kv : lv.second) staleChunks.push_back({ lv.first, keyX(kv.first), keyZ(kv.first) });
    layers.clear();
}
