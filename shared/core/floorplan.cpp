#include "fp_strict.h"
#include "floorplan.h"
#include <algorithm>

namespace {

// A neighbour's wall buries this wall's end cap, unless it is a rail, which is
// thinner than the wall.
bool buries(uint8_t v) { return blocksEdge(v) && v != WALL_RAIL; }

bool holeAt(World &w, int gi, int gk) { return w.storeyH > 0.0f && (w.vflagAt(gi, gk) & VF_HOLE); }

// The edge between cell (gi, gk) and its low-side neighbour (oi, ok).
FloorEdge edgeOf(World &w, int gi, int gk, bool west) {
    int oi = west ? gi - 1 : gi, ok = west ? gk : gk - 1;
    FloorEdge e;
    e.wall = west ? w.wallWVal(gi, gk) : w.wallNVal(gi, gk);
    e.floorLo = w.floorY(oi, ok);
    e.floorHi = w.floorY(gi, gk);
    e.ceilLo = w.ceilY(oi, ok);
    e.ceilHi = w.ceilY(gi, gk);
    e.base = std::min(e.floorLo, e.floorHi);
    e.top = std::max(e.ceilLo, e.ceilHi);
    if (west) e.buried = (buries(w.wallWVal(gi, gk - 1)) ? 1 : 0) | (buries(w.wallWVal(gi, gk + 1)) ? 2 : 0);
    else e.buried = (buries(w.wallNVal(gi - 1, gk)) ? 1 : 0) | (buries(w.wallNVal(gi + 1, gk)) ? 2 : 0);
    e.voidFaces = (holeAt(w, oi, ok) ? 1 : 0) | (holeAt(w, gi, gk) ? 2 : 0);
    e.hasFloorLo = cellHasFloor(w, oi, ok);
    e.hasFloorHi = cellHasFloor(w, gi, gk);
    e.hasCeilLo = cellHasCeiling(w, oi, ok);
    e.hasCeilHi = cellHasCeiling(w, gi, gk);
    return e;
}

FloorKind floorKindOf(World &w, const ChunkData &d, int i, int k, int gi, int gk) {
    if (d.manila && i >= MANILA_LO && i <= MANILA_HI && k >= MANILA_LO && k <= MANILA_HI) return FloorKind::Boards;
    if (d.vflag[i][k] & VF_STAIR) return FloorKind::Stair;
    if (d.vflag[i][k] & VF_HOLE) return FloorKind::Hole;
    if (w.level == 0 && w.softAt(gi, gk)) return FloorKind::Soft;
    return FloorKind::Flat;
}

// The risers cell (i, k) draws, and on Level 1 the edging along its lips.
void risersOf(ChunkFloorplan &P, World &w, int i, int k) {
    int gi = P.cx * CCELLS + i, gk = P.cz * CCELLS + k;
    float gx = P.cx * CHUNK + i * CELL, gz = P.cz * CHUNK + k * CELL;
    const FloorCell &c = P.cells[i][k];
    float fy = c.y;
    auto riser = [&](float x0, float z0, float x1, float z1, float hi, float lo, float nx, float nz) {
        P.risers.push_back({ (uint8_t)i, (uint8_t)k, x0, z0, x1, z1, hi, lo, nx, nz });
    };
    if (w.level == 2) {
        // Every cell draws the high side of each riser it has, so each is drawn
        // once. Neighbour heights come from the world, across chunk seams.
        auto skirt = [&](float x0, float z0, float x1, float z1, float low, float nx, float nz) {
            if (low < fy) riser(x0, z0, x1, z1, fy, low, nx, nz);
        };
        skirt(gx, gz, gx + CELL, gz, w.floorY(gi, gk - 1), 0, -1);
        skirt(gx + CELL, gz + CELL, gx, gz + CELL, w.floorY(gi, gk + 1), 0, 1);
        skirt(gx, gz + CELL, gx, gz, w.floorY(gi - 1, gk), -1, 0);
        skirt(gx + CELL, gz, gx + CELL, gz + CELL, w.floorY(gi + 1, gk), 1, 0);
        return;
    }
    // Elsewhere a cell with no floor of its own, or a flat one, draws none: each
    // shared riser is drawn by this cell when the neighbour is flat, otherwise by
    // the lower cell of the pair.
    if (c.floor == FloorKind::Boards || c.floor == FloorKind::Stair || c.floor == FloorKind::Hole) return;
    if (fy == 0.0f) return;
    float hN = w.floorY(gi, gk - 1), hS = w.floorY(gi, gk + 1), hW = w.floorY(gi - 1, gk), hE = w.floorY(gi + 1, gk);
    if (hN < fy && hN == 0.0f) riser(gx, gz, gx + CELL, gz, fy, hN, 0, -1);
    else if (hN > fy) riser(gx, gz, gx + CELL, gz, hN, fy, 0, 1);
    if (hS < fy && hS == 0.0f) riser(gx, gz + CELL, gx + CELL, gz + CELL, fy, hS, 0, 1);
    else if (hS > fy) riser(gx, gz + CELL, gx + CELL, gz + CELL, hS, fy, 0, -1);
    if (hW < fy && hW == 0.0f) riser(gx, gz, gx, gz + CELL, fy, hW, -1, 0);
    else if (hW > fy) riser(gx, gz, gx, gz + CELL, hW, fy, 1, 0);
    if (hE < fy && hE == 0.0f) riser(gx + CELL, gz, gx + CELL, gz + CELL, fy, hE, 1, 0);
    else if (hE > fy) riser(gx + CELL, gz, gx + CELL, gz + CELL, hE, fy, -1, 0);
    if (w.level != 1) return;
    auto lip = [&](float x0, float z0, float x1, float z1, float inx, float inz) {
        P.lips.push_back({ (uint8_t)i, (uint8_t)k, x0, z0, x1, z1, inx, inz });
    };
    if (hN < fy) lip(gx, gz, gx + CELL, gz, 0, 1);
    if (hS < fy) lip(gx, gz + CELL, gx + CELL, gz + CELL, 0, -1);
    if (hW < fy) lip(gx, gz, gx, gz + CELL, 1, 0);
    if (hE < fy) lip(gx + CELL, gz, gx + CELL, gz + CELL, -1, 0);
}

// The ceiling greedy-meshed: grow along x, then take whole rows down z.
void ceilingOf(ChunkFloorplan &P) {
    float wx = P.cx * CHUNK, wz = P.cz * CHUNK;
    bool done[CCELLS][CCELLS] = {};
    for (int i = 0; i < CCELLS; i++)
        for (int k = 0; k < CCELLS; k++)
            if (!P.cells[i][k].ceiling) done[i][k] = true;
    auto cy = [&](int i, int k) { return P.cells[i][k].ceilY; };
    for (int i = 0; i < CCELLS; i++)
        for (int k = 0; k < CCELLS; k++) {
            if (done[i][k]) continue;
            float y = cy(i, k);
            int i1 = i;
            while (i1 + 1 < CCELLS && !done[i1 + 1][k] && cy(i1 + 1, k) == y) i1++;
            int k1 = k;
            while (k1 + 1 < CCELLS) {
                bool ok = true;
                for (int a = i; a <= i1 && ok; a++) ok = !done[a][k1 + 1] && cy(a, k1 + 1) == y;
                if (!ok) break;
                k1++;
            }
            for (int a = i; a <= i1; a++)
                for (int b = k; b <= k1; b++) done[a][b] = true;
            P.ceiling.push_back({ wx + i * CELL, wz + k * CELL, wx + (i1 + 1) * CELL, wz + (k1 + 1) * CELL, y });
        }
}

// Where a neighbour's ceiling is higher, the face that closes the slot. Only
// the lower cell of a pair draws it, so a shared edge is drawn once, across
// chunk seams too.
void soffitsOf(ChunkFloorplan &P, World &w, int i, int k) {
    const FloorCell &c = P.cells[i][k];
    if (!c.ceiling) return;   // no ceiling of its own to close off
    int gi = P.cx * CCELLS + i, gk = P.cz * CCELLS + k;
    float gx = P.cx * CHUNK + i * CELL, gz = P.cz * CHUNK + k * CELL, cy = c.ceilY;
    auto soffit = [&](float ax, float az, float bx, float bz, float hi, int ni, int nk, uint8_t edge) {
        if (hi <= cy + 1e-4f) return;
        bool bulkhead = w.storeyH > 0.0f && (w.vflagAt(ni, nk) & VF_OPENUP);
        // A wall or door head on the same line already covers an opening's edge.
        if (bulkhead && ((blocksEdge(edge) && edge != WALL_RAIL) || edge == WALL_DOOR || edge == WALL_EXIT)) return;
        P.soffits.push_back({ (uint8_t)i, (uint8_t)k, ax, az, bx, bz, cy, hi, bulkhead });
    };
    soffit(gx, gz, gx + CELL, gz, w.ceilY(gi, gk - 1), gi, gk - 1, w.wallNVal(gi, gk));
    soffit(gx + CELL, gz + CELL, gx, gz + CELL, w.ceilY(gi, gk + 1), gi, gk + 1, w.wallNVal(gi, gk + 1));
    soffit(gx, gz + CELL, gx, gz, w.ceilY(gi - 1, gk), gi - 1, gk, w.wallWVal(gi, gk));
    soffit(gx + CELL, gz, gx + CELL, gz + CELL, w.ceilY(gi + 1, gk), gi + 1, gk, w.wallWVal(gi + 1, gk));
}

// The flights rising from this storey (stampFeature has the plan).
void flightsOf(ChunkFloorplan &P, World &w, const ChunkData &d) {
    for (int q = 0; q < d.nfeat; q++) {
        const VertFeat &f = d.feats[q];
        if (f.lo != w.qs) continue;
        const float R = w.storeyH / 24.0f;
        Flight fl;
        fl.feat = f;
        if (f.kind == VK_STAIRWELL) {
            // Lane A: 12 risers from the foot (plain floor) to the half landing
            // across the far end of the shaft; lane B 12 more back toward the door
            // above, whose top landing is the upper storey's floor.
            const float G = 4.0f / 12.0f;
            for (int i = 0; i < 12; i++) fl.steps.push_back({ 0, CELL, 2 + i * G, 2 + (i + 1) * G, 0, (i + 1) * R, true });
            fl.landing = true;
            fl.landingAfter = 12;
            fl.landingU1 = 2 * CELL;
            fl.landingV0 = 6;
            fl.landingV1 = 8;
            fl.landingY = 12 * R;
            for (int j = 0; j < 12; j++)
                fl.steps.push_back({ CELL, 2 * CELL, 6 - (j + 1) * G, 6 - j * G, 0, 12 * R + (j + 1) * R, false });
        } else if (f.kind == VK_STAIR || f.stairU >= 0) {
            // A straight flight: 24 risers over rows 1..4, between its wall and its
            // balustrade.
            int s0 = f.kind == VK_STAIR ? 0 : f.stairU, s1 = f.kind == VK_STAIR ? f.wu - 1 : f.stairU;
            const float G = 8.0f / 24.0f;
            for (int i = 0; i < 24; i++)
                fl.steps.push_back({ s0 * CELL, (s1 + 1) * CELL, 2 + i * G, 2 + (i + 1) * G, 0, (i + 1) * R, true });
        } else {
            continue;
        }
        P.flights.push_back(fl);
    }
}

}  // namespace

ChunkFloorplan::Span<Riser> ChunkFloorplan::risersIn(int i, int k) const {
    const Riser *b = risers.data();
    return { b + riserFirst[i * CCELLS + k], b + riserFirst[i * CCELLS + k + 1] };
}

ChunkFloorplan::Span<Lip> ChunkFloorplan::lipsIn(int i, int k) const {
    const Lip *b = lips.data();
    return { b + lipFirst[i * CCELLS + k], b + lipFirst[i * CCELLS + k + 1] };
}

ChunkFloorplan chunkFloorplan(World &w, int cx, int cz) {
    ChunkFloorplan P;
    P.cx = cx;
    P.cz = cz;
    P.storey = w.qs;
    ChunkData &d = w.data(cx, cz);
    for (int i = 0; i < CCELLS; i++)
        for (int k = 0; k < CCELLS; k++) {
            int gi = cx * CCELLS + i, gk = cz * CCELLS + k;
            FloorCell &c = P.cells[i][k];
            c.floor = floorKindOf(w, d, i, k, gi, gk);
            c.y = w.floorY(gi, gk);
            c.pool = w.poolAt(gi, gk);
            c.ceiling = cellHasCeiling(w, gi, gk);
            c.ceilY = w.ceilY(gi, gk);
            c.pillar = d.pillar[i][k] != 0;
            c.north = edgeOf(w, gi, gk, false);
            c.west = edgeOf(w, gi, gk, true);
        }
    for (int i = 0; i < CCELLS; i++)
        for (int k = 0; k < CCELLS; k++) {
            P.riserFirst[i * CCELLS + k] = (uint16_t)P.risers.size();
            P.lipFirst[i * CCELLS + k] = (uint16_t)P.lips.size();
            risersOf(P, w, i, k);
        }
    P.riserFirst[CCELLS * CCELLS] = (uint16_t)P.risers.size();
    P.lipFirst[CCELLS * CCELLS] = (uint16_t)P.lips.size();
    ceilingOf(P);
    for (int i = 0; i < CCELLS; i++)
        for (int k = 0; k < CCELLS; k++) soffitsOf(P, w, i, k);
    flightsOf(P, w, d);
    if (w.level == 2) {
        // Only where the partition is there with this opening in it: grand halls
        // have no partition, and an arch there would hang over open water.
        for (int axis = 0; axis < 2; ++axis)
            for (int start : { 3, 11 }) {
                auto wallAt = [&](int t) { return axis ? d.wallW[8][t] : d.wallN[t][8]; };
                if (wallAt(start - 1) != WALL_SOLID || wallAt(start + 3) != WALL_SOLID) continue;
                P.arches.push_back({ (uint8_t)axis, (uint8_t)start });
            }
    }
    return P;
}
