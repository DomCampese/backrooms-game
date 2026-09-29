#include "../core/fp_strict.h"
#include "greybox.h"
#include <algorithm>
#include <cmath>

namespace {

// Stair cells are drawn as this many treads a side, each at stairY of its middle.
constexpr int STAIR_SUB = 8;
// A light panel's half extent: the shader's PANEL_HALF.
constexpr float PANEL_HALF = 0.62f;
// A locked door's leaf, and a window's glass, half thickness.
constexpr float LEAF_T = 0.03f;

Vec3 sub3(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 cross3(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
Vec3 unit3(Vec3 v) {
    float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 0 ? Vec3{ v.x / l, v.y / l, v.z / l } : v;
}

struct Builder {
    GreyboxMesh &m;

    // a, b, c, d in order round the quad. Both windings, each with its own normal.
    void quad(GreySurface s, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        GreyboxMesh::Section &sec = m.sections[(int)s];
        Vec3 n = unit3(cross3(sub3(b, a), sub3(c, a)));
        if (n.x == 0 && n.y == 0 && n.z == 0) return;   // degenerate
        for (int side = 0; side < 2; side++) {
            uint32_t base = (uint32_t)sec.pos.size();
            Vec3 sn = side ? Vec3{ -n.x, -n.y, -n.z } : n;
            for (Vec3 p : { a, b, c, d }) { sec.pos.push_back(p); sec.normal.push_back(sn); }
            const uint32_t FRONT[6] = { 0, 1, 2, 0, 2, 3 }, BACK[6] = { 0, 2, 1, 0, 3, 2 };
            const uint32_t *tri = side ? BACK : FRONT;
            for (int t = 0; t < 6; t++) sec.index.push_back(base + tri[t]);
        }
    }

    void box(GreySurface s, float x0, float y0, float z0, float x1, float y1, float z1) {
        if (x1 <= x0 || y1 <= y0 || z1 <= z0) return;
        quad(s, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 });   // top
        quad(s, { x0, y0, z0 }, { x0, y0, z1 }, { x1, y0, z1 }, { x1, y0, z0 });   // bottom
        quad(s, { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 });   // -z
        quad(s, { x0, y0, z1 }, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y0, z1 });   // +z
        quad(s, { x0, y0, z0 }, { x0, y1, z0 }, { x0, y1, z1 }, { x0, y0, z1 });   // -x
        quad(s, { x1, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x1, y1, z0 });   // +x
    }

    // A box on an edge: `from`..`to` along it, `half` either side of `line`.
    void edgeBox(GreySurface s, bool west, float line, float half, float from, float to, float y0, float y1) {
        if (west) box(s, line - half, y0, from, line + half, y1, to);
        else box(s, from, y0, line - half, to, y1, line + half);
    }

    // A vertical face on an edge, from..to along it, between two heights.
    void edgeFace(GreySurface s, bool west, float line, float from, float to, float y0, float y1) {
        if (y1 <= y0) return;
        if (west) quad(s, { line, y0, from }, { line, y1, from }, { line, y1, to }, { line, y0, to });
        else quad(s, { from, y0, line }, { to, y0, line }, { to, y1, line }, { from, y1, line });
    }

    // A rail on an edge: a knee wall whose cap runs from top0 at e0 to top1 at e0 + CELL.
    void rail(bool west, float line, float e0, float base, float top0, float top1) {
        GreySurface s = GreySurface::Rail;
        float e1 = e0 + CELL, lo = line - RAIL_T, hi = line + RAIL_T;
        auto P = [&](float along, float y, float across) { return west ? Vec3{ across, y, along } : Vec3{ along, y, across }; };
        quad(s, P(e0, base, lo), P(e1, base, lo), P(e1, top1, lo), P(e0, top0, lo));
        quad(s, P(e0, base, hi), P(e0, top0, hi), P(e1, top1, hi), P(e1, base, hi));
        quad(s, P(e0, top0, lo), P(e1, top1, lo), P(e1, top1, hi), P(e0, top0, hi));
        quad(s, P(e0, base, lo), P(e0, top0, lo), P(e0, top0, hi), P(e0, base, hi));
        quad(s, P(e1, base, lo), P(e1, base, hi), P(e1, top1, hi), P(e1, top1, lo));
    }
};

// A cell's floor: flat, a flight in treads, or nothing over a hole; and the
// water over a pool.
void floorOf(Builder &b, World &w, int gi, int gk) {
    float x0 = gi * CELL, z0 = gk * CELL;
    uint8_t vf = w.vflagAt(gi, gk);
    if (vf & VF_STAIR) {
        const float s = CELL / STAIR_SUB;
        float y[STAIR_SUB][STAIR_SUB];
        for (int a = 0; a < STAIR_SUB; a++)
            for (int c = 0; c < STAIR_SUB; c++) {
                float t = w.stairY(x0 + (a + 0.5f) * s, z0 + (c + 0.5f) * s);
                y[a][c] = std::isnan(t) ? w.floorY(gi, gk) : t;
            }
        for (int a = 0; a < STAIR_SUB; a++)
            for (int c = 0; c < STAIR_SUB; c++) {
                float xa = x0 + a * s, za = z0 + c * s, h = y[a][c];
                b.quad(GreySurface::Stair, { xa, h, za }, { xa, h, za + s }, { xa + s, h, za + s }, { xa + s, h, za });
                if (a + 1 < STAIR_SUB) {
                    float o = y[a + 1][c];
                    b.edgeFace(GreySurface::Stair, true, xa + s, za, za + s, std::min(h, o), std::max(h, o));
                }
                if (c + 1 < STAIR_SUB) {
                    float o = y[a][c + 1];
                    b.edgeFace(GreySurface::Stair, false, za + s, xa, xa + s, std::min(h, o), std::max(h, o));
                }
            }
        return;
    }
    if (vf & VF_HOLE) return;
    float y = w.floorY(gi, gk), x1 = x0 + CELL, z1 = z0 + CELL;
    b.quad(GreySurface::Floor, { x0, y, z0 }, { x0, y, z1 }, { x1, y, z1 }, { x1, y, z0 });
    if (w.poolAt(gi, gk))
        b.quad(GreySurface::Water, { x0, WATER_Y, z0 }, { x0, WATER_Y, z1 }, { x1, WATER_Y, z1 }, { x1, WATER_Y, z0 });
}

void ceilingOf(Builder &b, World &w, int gi, int gk) {
    if (!cellHasCeiling(w, gi, gk)) return;
    float x0 = gi * CELL, z0 = gk * CELL, x1 = x0 + CELL, z1 = z0 + CELL, y = w.ceilY(gi, gk);
    b.quad(GreySurface::Ceiling, { x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 });
}

// Where two cells' floors or ceilings differ across an edge with no wall, the
// step between them.
void stepsOn(Builder &b, World &w, int gi, int gk, bool west) {
    int oi = west ? gi - 1 : gi, ok = west ? gk : gk - 1;
    float line = west ? gi * CELL : gk * CELL, e0 = west ? gk * CELL : gi * CELL;
    if (cellHasFloor(w, gi, gk) && cellHasFloor(w, oi, ok)) {
        float a = w.floorY(gi, gk), c = w.floorY(oi, ok);
        b.edgeFace(GreySurface::Step, west, line, e0, e0 + CELL, std::min(a, c), std::max(a, c));
    }
    if (cellHasCeiling(w, gi, gk) && cellHasCeiling(w, oi, ok)) {
        float a = w.ceilY(gi, gk), c = w.ceilY(oi, ok);
        b.edgeFace(GreySurface::Wall, west, line, e0, e0 + CELL, std::min(a, c), std::max(a, c));
    }
}

void solidWall(Builder &b, World &w, int gi, int gk, bool west) {
    int oi = west ? gi - 1 : gi, ok = west ? gk : gk - 1;
    float line = west ? gi * CELL : gk * CELL, e0 = west ? gk * CELL : gi * CELL;
    float base = std::min(w.floorY(oi, ok), w.floorY(gi, gk));
    float top = std::max(w.ceilY(oi, ok), w.ceilY(gi, gk));
    b.edgeBox(GreySurface::Wall, west, line, WT, e0, e0 + CELL, base, top);
}

void openingWall(Builder &b, const Opening &o) {
    const float e1 = o.e0 + CELL;
    if (o.kind == OpeningKind::Rail) {
        if (!o.overVoid) b.rail(o.west, o.line, o.e0, o.base, o.railTop0, o.railTop1);
        return;
    }
    // Jambs either side of the opening, and the wall over it.
    b.edgeBox(GreySurface::Wall, o.west, o.line, WT, o.e0, o.a0, o.base, o.top);
    b.edgeBox(GreySurface::Wall, o.west, o.line, WT, o.a1, e1, o.base, o.top);
    b.edgeBox(GreySurface::Wall, o.west, o.line, WT, o.a0, o.a1, o.headY, o.top);
    switch (o.kind) {
    case OpeningKind::Window:
        b.edgeBox(GreySurface::Wall, o.west, o.line, WT, o.a0, o.a1, o.base, o.sillY);
        b.edgeBox(GreySurface::Glass, o.west, o.line, LEAF_T, o.a0, o.a1, o.sillY, o.headY);
        break;
    case OpeningKind::LockedDoor:
        b.edgeBox(GreySurface::Door, o.west, o.line, LEAF_T, o.a0, o.a1, o.floorY, o.headY);
        break;
    case OpeningKind::Exit:
    case OpeningKind::CursedExit:
        b.edgeBox(o.kind == OpeningKind::Exit ? GreySurface::Exit : GreySurface::CursedExit, o.west, o.line, LEAF_T,
                  o.a0, o.a1, o.floorY, o.headY);
        break;
    default: break;
    }
}

}  // namespace

size_t GreyboxMesh::triangles() const {
    size_t n = 0;
    for (const Section &s : sections) n += s.index.size() / 3;
    return n;
}

GreyboxMesh greyboxChunk(World &w, int cx, int cz, uint32_t meshedProps) {
    GreyboxMesh m;
    m.storey = w.qs;
    m.cx = cx;
    m.cz = cz;
    Builder b{ m };
    const ChunkLayout L = chunkLayout(w, cx, cz);
    for (int i = 0; i < CCELLS; i++)
        for (int kk = 0; kk < CCELLS; kk++) {
            int gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            floorOf(b, w, gi, gk);
            ceilingOf(b, w, gi, gk);
            for (int west = 0; west < 2; west++) {
                uint8_t v = west ? w.wallWVal(gi, gk) : w.wallNVal(gi, gk);
                if (v == WALL_NONE) stepsOn(b, w, gi, gk, west != 0);
                else if (v == WALL_SOLID) solidWall(b, w, gi, gk, west != 0);
                else if (const Opening *o = L.opening(i, kk, west != 0)) openingWall(b, *o);
            }
            float fy = w.floorY(gi, gk);
            if (w.pillarAt(gi, gk))
                b.box(GreySurface::Pillar, gi * CELL + PILLAR_LO, fy, gk * CELL + PILLAR_LO, gi * CELL + PILLAR_HI,
                      w.ceilY(gi, gk), gk * CELL + PILLAR_HI);
            // Props are drawn as the boxes collision gives them, which gatherCellAABBs
            // appends after everything else in the cell.
            AABB boxes[MAX_NEARBY_AABBS];
            int fixed = w.gatherCellAABBs(gi, gk, boxes, MAX_NEARBY_AABBS, 0, false);
            int all = w.gatherCellAABBs(gi, gk, boxes, MAX_NEARBY_AABBS, 0, true);
            if (meshedProps & (1u << w.propAt(gi, gk))) all = fixed;
            for (int q = fixed; q < all; q++)
                b.box(GreySurface::Prop, boxes[q].minx, fy, boxes[q].minz, boxes[q].maxx, boxes[q].top, boxes[q].maxz);
        }
    for (const LightFitting &f : L.fittings) {
        if (f.gap != FittingGap::None) continue;
        float x0 = f.pos.x - PANEL_HALF, x1 = f.pos.x + PANEL_HALF, z0 = f.pos.z - PANEL_HALF, z1 = f.pos.z + PANEL_HALF;
        b.quad(f.dead ? GreySurface::DeadLight : GreySurface::Light, { x0, f.pos.y, z0 }, { x1, f.pos.y, z0 },
               { x1, f.pos.y, z1 }, { x0, f.pos.y, z1 });
    }
    return m;
}
