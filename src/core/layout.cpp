#include "layout.h"
#include "hash.h"
#include "level_rules.h"
#include <algorithm>
#include <cmath>

namespace {

// Which decorations each level carries.
struct LevelDecor {
    bool fittings;    // outlets, switches, grilles, exit signs, diffusers, sprinklers
    bool scrawl;
    bool lifts;       // lift doors in long walls
    bool spalls;      // blown concrete on walls and columns
    bool pipes;       // service pipes under the ceiling along solid walls
    bool streamers;
};
const LevelDecor DECOR[NLEVELS] = {
    //  fittings scrawl lifts  spalls pipes  streamers
    { true,  true,  false, false, false, false },   // Level 0
    { true,  true,  true,  true,  true,  false },   // Level 1
    { false, false, false, false, false, false },   // the Poolrooms
    { true,  true,  false, false, true,  false },   // the Red Halls
    { true,  true,  false, false, false, true  },   // LEVEL FUN
};

// Where a fixture sits below the ceiling or off the floor, metres.
constexpr float CONDUIT_DROP = 0.155f;    // conduit underside below the wall top
constexpr float PIPE_DROP = 0.22f;        // highest pipe centre below the ceiling
constexpr float PIPE_OFF = 0.34f;         // pipe centre off the wall's centreline
constexpr float SCRAWL_LO = 0.28f, SCRAWL_HI = 1.72f;   // along the edge
constexpr float SCRAWL_Y = 0.95f, SCRAWL_H = 0.66f;

// The first wall fitting a hash picks, rarest first, and its centre height.
bool pickWallFitting(uint32_t h, FixtureKind &kind, float &yc) {
    if (h % EXITSIGN_RATE == 0) { kind = FixtureKind::ExitSign; yc = 2.44f; return true; }
    if (h % GRILLE_RATE == 0)   { kind = FixtureKind::Grille;   yc = 2.10f; return true; }
    if (h % SWITCH_RATE == 0)   { kind = FixtureKind::Switch;   yc = 1.22f; return true; }
    if (h % OUTLET_RATE == 0) {
        kind = ((h >> 11) % OUTLET_BROKEN == 0) ? FixtureKind::BrokenOutlet : FixtureKind::Outlet;
        yc = 0.32f;
        return true;
    }
    return false;
}

Fixture fixture(FixtureKind kind, int i, int k) {
    Fixture f;
    f.kind = kind;
    f.i = (int8_t)i;
    f.k = (int8_t)k;
    return f;
}

// A rail's cap follows what you stand on beside it, whichever side is higher.
// Between two holes the storey below draws the rail.
void railOn(World &w, Opening &op, int a, int b, bool west) {
    float ex0 = a * CELL, ez0 = b * CELL;
    float ex1 = west ? ex0 : ex0 + CELL, ez1 = west ? ez0 + CELL : ez0;
    int oa = west ? a - 1 : a, ob = west ? b : b - 1;             // the cell across the edge
    float inx = west ? 0.05f : 0.0f, inz = west ? 0.0f : 0.05f;   // a step into cell (a, b)
    bool any = false, hole = false;
    int voidSide = 0;
    float base = 1e9f;
    auto side = [&](int ca, int cb, float x, float z) {
        uint8_t f = w.vflagAt(ca, cb);
        if (f & VF_HOLE) {
            hole = true;
            bool own = ca == a && cb == b;
            voidSide = west ? (own ? -1 : 1) : (own ? 1 : -1);
            return -1e9f;
        }
        any = true;
        base = std::min(base, w.floorY(ca, cb));
        return (f & VF_STAIR) ? w.stairY(x, z, true) : w.floorY(ca, cb);
    };
    auto topAt = [&](float x, float z) {
        return std::max(side(a, b, x + inx, z + inz), side(oa, ob, x - inx, z - inz));
    };
    float t0 = topAt(ex0 + (ex1 - ex0) * 0.01f, ez0 + (ez1 - ez0) * 0.01f);
    float t1 = topAt(ex0 + (ex1 - ex0) * 0.99f, ez0 + (ez1 - ez0) * 0.99f);
    op.overVoid = !any;
    op.base = base;
    op.railTop0 = t0 + RAIL_H;
    op.railTop1 = t1 + RAIL_H;
    op.underside = hole;
    op.voidSide = (int8_t)voidSide;
}

// An exit's glyph: four or five strokes between lattice points.
void glyphOn(Opening &op, uint32_t h) {
    Rng r(((uint64_t)h << 1) ^ 0x51B01ULL);
    op.glyphLen = (uint8_t)(4 + r.ri(0, 2));
    for (int n = 0; n < op.glyphLen; n++) op.glyph[n] = (uint8_t)r.ri(0, 8);
}

// The opening on one edge of cell (i, k), if its wall kind is one.
bool openingOn(World &w, Opening &op, int cx, int cz, int i, int k, bool west) {
    int gi = cx * CCELLS + i, gk = cz * CCELLS + k;
    float gx = cx * CHUNK + i * CELL, gz = cz * CHUNK + k * CELL;
    uint8_t v = west ? w.wallWVal(gi, gk) : w.wallNVal(gi, gk);
    switch (v) {
    case WALL_DOOR:   op.kind = OpeningKind::Doorway; break;
    case WALL_LOCKED: op.kind = OpeningKind::LockedDoor; break;
    case WALL_WINDOW: op.kind = OpeningKind::Window; break;
    case WALL_RAIL:   op.kind = OpeningKind::Rail; break;
    case WALL_EXIT:   op.kind = w.cursedExit(gi, gk) ? OpeningKind::CursedExit : OpeningKind::Exit; break;
    default: return false;
    }
    op.i = (uint8_t)i;
    op.k = (uint8_t)k;
    op.west = west;
    // the cell across the edge, and the next edges along the line either way
    int oi = west ? gi - 1 : gi, ok = west ? gk : gk - 1;
    auto along = [&](int d) { return west ? w.wallWVal(gi, gk + d) : w.wallNVal(gi + d, gk); };
    op.line = west ? gx : gz;
    op.e0 = west ? gz : gx;
    op.base = std::min(w.floorY(oi, ok), w.floorY(gi, gk));
    op.top = std::max(w.ceilY(oi, ok), w.ceilY(gi, gk));
    op.floorY = w.floorY(gi, gk);
    if (op.kind == OpeningKind::Window) {
        op.a0 = op.e0 + WINDOW_LO;
        op.a1 = op.e0 + WINDOW_HI;
        op.sillY = op.base + WINDOW_SILL;
        op.headY = op.base + WINDOW_HEAD;
    } else {
        op.a0 = op.e0 + DOOR_LO;
        op.a1 = op.e0 + DOOR_HI;
        op.sillY = op.base;
        op.headY = op.base + DOOR_HEAD;
    }
    if (op.kind == OpeningKind::Doorway) {
        op.joinLo = along(-1) == WALL_DOOR;
        op.joinHi = along(1) == WALL_DOOR;
    }
    if (op.kind == OpeningKind::Exit || op.kind == OpeningKind::CursedExit) {
        op.leafRoom = along(1) == WALL_SOLID;
        glyphOn(op, ih(gi, gk, w.sseed() ^ (west ? 0x51B1u : 0x51B0u)));
    }
    if (op.kind == OpeningKind::Rail) railOn(w, op, gi, gk, west);
    return true;
}

// The fixtures of one cell, in the order the mesher emits them: lift doors,
// wall spalls, wall fittings, the ceiling fitting, conduit, scrawl, a column
// spall, pipes, a valve. North edge before west throughout.
void cellFixtures(World &w, const ChunkData &d, const LevelDecor &decor, int cx, int cz, int i, int kk,
                  std::vector<Fixture> &out) {
    const unsigned seed = w.sseed();
    float gx = cx * CHUNK + i * CELL, gz = cz * CHUNK + kk * CELL;
    int gi0 = cx * CCELLS + i, gk0 = cz * CCELLS + kk;
    // The fittings and scrawl hash the cell as unsigned, and conduit divides
    // it unsigned: a negative cell's run bucket is not fdiv's.
    uint32_t gi = (uint32_t)gi0, gk = (uint32_t)gk0;
    float fyc = w.floorY(gi0, gk0), cyc = w.ceilY(gi0, gk0);
    float nb = std::min(w.floorY(gi0, gk0 - 1), fyc);
    float nt = std::max(w.ceilY(gi0, gk0 - 1), cyc);
    float wb = std::min(w.floorY(gi0 - 1, gk0), fyc);
    float wt = std::max(w.ceilY(gi0 - 1, gk0), cyc);
    uint8_t nv = w.wallNVal(gi0, gk0), wv = w.wallWVal(gi0, gk0);
    const bool flN = cellHasFloor(w, gi0, gk0), flS = cellHasFloor(w, gi0, gk0 - 1),
               flW = cellHasFloor(w, gi0 - 1, gk0);
    const bool clN = cellHasCeiling(w, gi0, gk0), clS = cellHasCeiling(w, gi0, gk0 - 1),
               clW = cellHasCeiling(w, gi0 - 1, gk0);

    if (decor.lifts && nv == WALL_SOLID && liftHash(gi0, gk0, seed) &&
        w.wallNVal(gi0 - 1, gk0) == WALL_SOLID && w.wallNVal(gi0 + 1, gk0) == WALL_SOLID) {
        float sgn = (ih(gi0, gk0, seed ^ 0xE1E8u) & 1) ? 1.0f : -1.0f;
        Fixture f = fixture(FixtureKind::LiftDoor, i, kk);
        f.pos = { gx, nb, gz + sgn * WT };
        f.normal = { 0, 0, sgn };
        out.push_back(f);
    }
    if (decor.spalls) {
        uint32_t sn = ih(gi0, gk0, seed ^ 0x5BA2u), sw = ih(gi0, gk0, seed ^ 0x5BA3u);
        if (nv == WALL_SOLID && sn % 13 == 0) {
            float sgn = (sn >> 4) & 1 ? 1.0f : -1.0f;
            Fixture f = fixture(FixtureKind::Spall, i, kk);
            f.pos = { gx + 0.5f + ((sn >> 5) & 7) * 0.14f, nb + 0.5f + ((sn >> 8) & 15) * 0.16f, gz + sgn * WT };
            f.normal = { 0, 0, sgn };
            f.w = 0.30f; f.h = 0.34f; f.seed = sn;
            out.push_back(f);
        }
        if (wv == WALL_SOLID && sw % 13 == 0) {
            float sgn = (sw >> 4) & 1 ? 1.0f : -1.0f;
            Fixture f = fixture(FixtureKind::Spall, i, kk);
            f.pos = { gx + sgn * WT, wb + 0.5f + ((sw >> 8) & 15) * 0.16f, gz + 0.5f + ((sw >> 5) & 7) * 0.14f };
            f.normal = { sgn, 0, 0 };
            f.w = 0.30f; f.h = 0.34f; f.seed = sw;
            out.push_back(f);
        }
    }
    // Wall fittings, not on a face over a hole or a flight. Bit 4 picks the face,
    // bits 7-9 the place along the wall.
    if (decor.fittings && nv == WALL_SOLID) {
        uint32_t h = ih(gi, gk, seed ^ 0x71F0u);
        Fixture f = fixture(FixtureKind::Outlet, i, kk);
        bool plus = (h & 16) != 0;
        if (pickWallFitting(h, f.kind, f.pos.y) && (plus ? flN : flS)) {
            f.pos.x = gx + 0.45f + ((h >> 7) & 7) * 0.155f;
            f.pos.z = plus ? gz + WT : gz - WT;
            f.normal = { 0, 0, plus ? 1.0f : -1.0f };
            out.push_back(f);
        }
    }
    if (decor.fittings && wv == WALL_SOLID) {
        uint32_t h = ih(gi, gk, seed ^ 0x71F9u);
        Fixture f = fixture(FixtureKind::Outlet, i, kk);
        bool plus = (h & 16) != 0;
        if (pickWallFitting(h, f.kind, f.pos.y) && (plus ? flN : flW)) {
            f.pos.x = plus ? gx + WT : gx - WT;
            f.pos.z = gz + 0.45f + ((h >> 7) & 7) * 0.155f;
            f.normal = { plus ? 1.0f : -1.0f, 0, 0 };
            out.push_back(f);
        }
    }
    // Ceiling: a diffuser in the tile grid, or a sprinkler.
    uint32_t hc = ih(gi, gk, seed ^ 0x71E3u);
    if (decor.fittings && clN && (hc % DIFFUSER_RATE == 0 || hc % SPRINK_RATE == 0)) {
        Fixture f = fixture(hc % DIFFUSER_RATE == 0 ? FixtureKind::Diffuser : FixtureKind::Sprinkler, i, kk);
        f.pos = { gx + CELL * 0.5f, cyc, gz + CELL * 0.5f };
        f.normal = { 0, -1, 0 };
        out.push_back(f);
    }
    // Conduit along the top of a wall with a ceiling both sides. Hashed per run of
    // CONDUIT_RUN cells, so a run keeps its face.
    if (nv == WALL_SOLID && clN && clS) {
        uint32_t hr = ih((int)(gi / CONDUIT_RUN), (int)gk, seed ^ 0x71C5u);
        if (hr % 7 == 0) {
            bool plus = (hr & 32) != 0;
            float y = nt - CONDUIT_DROP, z = plus ? gz + WT : gz - WT;
            Fixture f = fixture(FixtureKind::Conduit, i, kk);
            f.pos = { gx - WT, y, z };
            f.end = { gx + CELL + WT, y, z };
            f.normal = { 0, 0, plus ? 1.0f : -1.0f };
            out.push_back(f);
        }
    }
    if (wv == WALL_SOLID && clN && clW) {
        uint32_t hr = ih((int)gi, (int)(gk / CONDUIT_RUN), seed ^ 0x71CBu);
        if (hr % 7 == 0) {
            bool plus = (hr & 32) != 0;
            float y = wt - CONDUIT_DROP, x = plus ? gx + WT : gx - WT;
            Fixture f = fixture(FixtureKind::Conduit, i, kk);
            f.pos = { x, y, gz - WT };
            f.end = { x, y, gz + CELL + WT };
            f.normal = { plus ? 1.0f : -1.0f, 0, 0 };
            out.push_back(f);
        }
    }
    // Scrawl: bit 3 picks the face, bits 5+ the phrase, 9-10 the height, 12-15
    // the tilt, 17-18 the tint.
    auto scrawl = [&](uint32_t hs, float base, float lo, float face, bool west) {
        float y0 = base + SCRAWL_Y + ((hs >> 9) & 3) * 0.12f, y1 = y0 + SCRAWL_H;
        float a0 = lo + SCRAWL_LO, a1 = lo + SCRAWL_HI;
        float mid = (a0 + a1) * 0.5f;
        float sgn = (hs & 8) ? 1.0f : -1.0f;
        Fixture f = fixture(FixtureKind::Scrawl, i, kk);
        f.pos = west ? Vec3{ face, (y0 + y1) * 0.5f, mid } : Vec3{ mid, (y0 + y1) * 0.5f, face };
        f.normal = west ? Vec3{ sgn, 0, 0 } : Vec3{ 0, 0, sgn };
        f.w = (a1 - a0) * 0.5f;
        f.h = (y1 - y0) * 0.5f;
        f.angle = ((int)((hs >> 12) & 15) - 7.5f) * 0.0085f;
        f.variant = (uint8_t)((hs >> 5) % SCRAWL_PHRASES);
        f.tone = (uint8_t)((hs >> 17) & 3);
        out.push_back(f);
    };
    if (decor.scrawl && nv == WALL_SOLID) {
        uint32_t hs = ih(gi, gk, seed ^ 0x5C1Bu);
        if (hs % SCRAWL_RATE == 0 && ((hs & 8) ? flN : flS))
            scrawl(hs, nb, gx, (hs & 8) ? gz + WT : gz - WT, false);
    }
    if (decor.scrawl && wv == WALL_SOLID) {
        uint32_t hs = ih(gi, gk, seed ^ 0x5C2Du);
        if (hs % SCRAWL_RATE == 0 && ((hs & 8) ? flN : flW))
            scrawl(hs, wb, gz, (hs & 8) ? gx + WT : gx - WT, true);
    }
    // A column with its cover blown off, toward a corner, where it goes first.
    if (decor.spalls && d.pillar[i][kk]) {
        uint32_t sh = ih(gi0, gk0, seed ^ 0x5BA1u);
        if (sh % 3 == 0) {
            int face = (int)((sh >> 3) & 3);
            const Vec3 NS[4] = { {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0} };
            Vec3 n = NS[face], u = (face < 2) ? Vec3{ 1, 0, 0 } : Vec3{ 0, 0, 1 };
            float off = ((sh >> 6) & 1) ? 0.28f : -0.28f;
            Fixture f = fixture(FixtureKind::PillarSpall, i, kk);
            f.pos = { gx + 1.0f + n.x * 0.58f + u.x * off, fyc + 0.7f + ((sh >> 8) & 15) / 15.0f * 2.0f,
                      gz + 1.0f + n.z * 0.58f + u.z * off };
            f.normal = n;
            f.w = 0.24f;
            f.h = 0.30f + ((sh >> 12) & 7) * 0.03f;
            f.seed = sh;
            out.push_back(f);
        }
    }
    // Pipes follow a whole row: the run hash is per row (north walls) or column
    // (west walls). A collar every few metres.
    if (decor.pipes) {
        auto pipe = [&](uint32_t rh, bool west, int collarKey) {
            float py = cyc - PIPE_DROP - ((rh >> 5) & 3) * 0.09f;
            float side = ((rh >> 9) & 1) ? PIPE_OFF : -PIPE_OFF;
            Fixture f = fixture(FixtureKind::Pipe, i, kk);
            f.pos = west ? Vec3{ gx + side, py, gz } : Vec3{ gx, py, gz + side };
            f.end = west ? Vec3{ gx + side, py, gz + CELL } : Vec3{ gx + CELL, py, gz + side };
            f.w = 0.065f + ((rh >> 11) & 3) * 0.012f;
            f.tone = (uint8_t)((rh >> 13) & 1);
            f.variant = ((collarKey * 2654435761u) & 3) == 0;
            out.push_back(f);
        };
        if (nv == WALL_SOLID) {
            uint32_t rh = ih(gk0, 7717, seed ^ 0x9191u);
            if (rh % 4 == 0) pipe(rh, false, gi0);
        }
        if (wv == WALL_SOLID) {
            uint32_t rh = ih(gi0, 3313, seed ^ 0x9292u);
            if (rh % 4 == 0) pipe(rh, true, gk0);
        }
    }
    if (w.valveAt(gi0, gk0)) {
        Fixture f = fixture(FixtureKind::Valve, i, kk);
        f.pos = { gx + 1.0f, d.elev[i][kk] * ELEV_UNIT, gz + 1.0f };
        f.end = { f.pos.x, cyc, f.pos.z };
        out.push_back(f);
    }
}

// Crepe streamers, three to six a chunk, each sagging between two points under
// the ceiling.
void streamers(World &w, int cx, int cz, std::vector<Fixture> &out) {
    float wx = cx * CHUNK, wz = cz * CHUNK;
    Rng r(hash64(World::key(cx, cz) ^ 0xFE57AULL ^ (uint64_t)w.sseed()));
    int n = 3 + r.ri(0, 3);
    for (int s = 0; s < n; s++) {
        float ax = wx + r.f01() * CHUNK, az = wz + r.f01() * CHUNK;
        float bx = ax + (r.f01() - 0.5f) * 9, bz = az + (r.f01() - 0.5f) * 9;
        float cA = w.ceilY((int)floorf(ax / CELL), (int)floorf(az / CELL));
        float cB = w.ceilY((int)floorf(bx / CELL), (int)floorf(bz / CELL));
        float ytop = std::min(cA, cB) - 0.03f;
        Fixture f;
        f.kind = FixtureKind::Streamer;
        f.pos = { ax, ytop, az };
        f.end = { bx, ytop, bz };
        f.h = ytop - 0.52f - r.f01() * 0.35f;
        f.variant = (uint8_t)r.ri(0, 4);
        out.push_back(f);
    }
}

// The fittings whose centres fall in this chunk, in grid order.
void fittings(World &w, const ChunkData &d, int cx, int cz, std::vector<LightFitting> &out) {
    const LevelRules &rules = LEVEL_RULES[w.level];
    float wx = cx * CHUNK, wz = cz * CHUNK, ls = rules.ls;
    float so = storeyHashOffset(w.qs);
    int g0x = (int)floorf(wx / ls), g1x = (int)floorf((wx + CHUNK) / ls);
    int g0z = (int)floorf(wz / ls), g1z = (int)floorf((wz + CHUNK) / ls);
    for (int gx = g0x; gx <= g1x; gx++)
        for (int gz = g0z; gz <= g1z; gz++) {
            float lx = gx * ls + ls * 0.5f, lz = gz * ls + ls * 0.5f;
            if (lx < wx || lx >= wx + CHUNK || lz < wz || lz >= wz + CHUNK) continue;
            LightFitting f;
            f.gx = gx;
            f.gz = gz;
            f.gap = FittingGap::None;
            if (d.manila) {
                float mx = wx + MANILA_MID * CELL, mz = wz + MANILA_MID * CELL;
                if (fabsf(lx - mx) < 4.0f && fabsf(lz - mz) < 4.0f) f.gap = FittingGap::ManilaRoom;
            }
            if (f.gap == FittingGap::None && w.storeyH > 0.0f) {
                int ci = (int)floorf(lx / CELL + 0.5f), ck = (int)floorf(lz / CELL + 0.5f);   // the corner it is centred on
                if ((w.vflagAt(ci, ck) | w.vflagAt(ci - 1, ck) | w.vflagAt(ci, ck - 1) | w.vflagAt(ci - 1, ck - 1))
                    & VF_OPENUP)
                    f.gap = FittingGap::UnderOpening;
            }
            f.ceilingY = w.ceilY((int)floorf(lx / CELL), (int)floorf(lz / CELL));
            f.pos = { lx, f.ceilingY - LIGHT_DROP, lz };
            f.turned = (ih((int)floorf(lx / ls), (int)floorf(lz / ls), w.sseed() ^ 0xBA77u) & 1) != 0;
            float h = tubeHash((float)gx + so, (float)gz + so * 1.7f);
            f.dead = h < rules.dead;
            f.output = 1.0f - rules.vary * (h * 53.7f - floorf(h * 53.7f));
            f.faulty = h > 1.0f - rules.faulty;
            out.push_back(f);
        }
}

}  // namespace

bool cellHasFloor(World &w, int gi, int gk) {
    return w.storeyH <= 0.0f || !(w.vflagAt(gi, gk) & (VF_HOLE | VF_STAIR));
}

bool cellHasCeiling(World &w, int gi, int gk) {
    return w.storeyH <= 0.0f || !(w.vflagAt(gi, gk) & VF_OPENUP);
}

float tubeHash(float gx, float gz) {
    float v = sinf(gx * 127.1f + gz * 311.7f) * 43758.5453f;
    return v - floorf(v);
}

float storeyHashOffset(int s) {
    float t = (float)s * 0.6180339f;
    return (t - floorf(t)) * 97.0f;
}

ChunkLayout chunkLayout(World &w, int cx, int cz) {
    ChunkLayout L;
    L.cx = cx;
    L.cz = cz;
    L.storey = w.qs;
    ChunkData &d = w.data(cx, cz);
    const LevelDecor &decor = DECOR[w.level];
    const unsigned seed = w.sseed();
    for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
        L.propAt[i][kk] = L.openN[i][kk] = L.openW[i][kk] = -1;
        for (int west = 0; west < 2; west++) {
            Opening op;
            if (!openingOn(w, op, cx, cz, i, kk, west != 0)) continue;
            (west ? L.openW : L.openN)[i][kk] = (int16_t)L.openings.size();
            L.openings.push_back(op);
        }
        L.first[i * CCELLS + kk] = (uint16_t)L.fixtures.size();
        cellFixtures(w, d, decor, cx, cz, i, kk, L.fixtures);
        uint8_t kind = d.prop[i][kk];
        if (kind == PROP_NONE) continue;
        int gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
        float x = cx * CHUNK + i * CELL + 1.0f, z = cz * CHUNK + kk * CELL + 1.0f;
        PropPlacement p;
        p.kind = (PropKind)kind;
        p.i = (uint8_t)i;
        p.k = (uint8_t)kk;
        p.gi = gi;
        p.gk = gk;
        p.x = x;
        p.z = z;
        if (kind == PROP_VENDING) {
            float bx0, bz0, bx1, bz1;
            vendFootprint(d.propRot[i][kk], x, z, p.x, p.z, bx0, bz0, bx1, bz1);
        }
        p.floorY = d.elev[i][kk] * ELEV_UNIT;
        p.turn = d.propRot[i][kk] & 3;
        p.yaw = p.turn * PROP_TURN;
        p.againstWall = (d.propRot[i][kk] & PROP_AGAINST_WALL) != 0;
        p.hash = ih(gi, gk, seed ^ PROP_HASH_SALT);
        p.hash2 = ih(gi, gk, seed ^ 0xCAFEu);
        L.propAt[i][kk] = (int16_t)L.props.size();
        L.props.push_back(p);
    }
    L.first[CCELLS * CCELLS] = (uint16_t)L.fixtures.size();
    if (d.manila) {
        float mx = cx * CHUNK + MANILA_MID * CELL, mz = cz * CHUNK + MANILA_MID * CELL;
        Fixture f;
        f.kind = FixtureKind::ManilaRoom;
        f.pos = { mx, w.ceilY(cellOf(mx), cellOf(mz)), mz };
        f.seed = ih(cx, cz, seed ^ 0x3A11u);
        L.fixtures.push_back(f);
    }
    if (decor.streamers) streamers(w, cx, cz, L.fixtures);
    fittings(w, d, cx, cz, L.fittings);
    return L;
}

ChunkLayout::Span ChunkLayout::fixturesIn(int i, int k) const {
    const Fixture *base = fixtures.data();
    return { base + first[i * CCELLS + k], base + first[i * CCELLS + k + 1] };
}

ChunkLayout::Span ChunkLayout::chunkFixtures() const {
    const Fixture *base = fixtures.data();
    return { base + first[CCELLS * CCELLS], base + fixtures.size() };
}

const PropPlacement *ChunkLayout::propIn(int i, int k) const {
    return propAt[i][k] < 0 ? nullptr : &props[propAt[i][k]];
}

const Opening *ChunkLayout::opening(int i, int k, bool west) const {
    int16_t n = west ? openW[i][k] : openN[i][k];
    return n < 0 ? nullptr : &openings[n];
}
